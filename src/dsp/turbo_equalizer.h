/**
 * @file turbo_equalizer.h
 * @brief Full Turbo Equalizer for MIL-STD-188-110A
 * 
 * Iteratively exchanges soft information between:
 *   1. Adaptive MLSE Equalizer (ISI mitigation)
 *   2. SISO Viterbi Decoder (error correction)
 * 
 * Data flow per iteration:
 *   Received symbols 
 *     → Soft MLSE (with priors)
 *     → Soft Demapper (symbol → bit LLRs)
 *     → Deinterleaver
 *     → SISO Decoder
 *     → Interleaver  
 *     → Soft Mapper (bit LLRs → symbol priors)
 *     → Feed back to MLSE
 * 
 * Typical improvement: 2-3x BER reduction per iteration
 */

#ifndef TURBO_EQUALIZER_H
#define TURBO_EQUALIZER_H

#include "src/dsp/mlse_adaptive.h"
#include "src/dsp/soft_demapper_turbo.h"
#include "src/dsp/soft_mapper_turbo.h"
#include "src/modem/soft_interleaver.h"
#include "src/modem/siso_viterbi.h"

#include <vector>
#include <complex>

namespace m110a {

struct TurboConfig {
    int max_iterations = 4;           // Typically 3-5 sufficient
    float convergence_threshold = 0.1f; // Stop if avg LLR change < threshold
    bool early_termination = true;    // Allow stopping before max_iterations
    
    // Sub-component configs
    AdaptiveMLSEConfig mlse_cfg;
    SISOConfig siso_cfg;
    SoftDemapperConfig demapper_cfg;
    
    // Interleaver (set based on mode)
    int interleaver_rows = 40;
    int interleaver_cols = 72;
    
    // Damping factor for extrinsic info (0.5-1.0)
    // Prevents oscillation in turbo loop
    float extrinsic_scale = 0.75f;
    
    TurboConfig() {
        mlse_cfg.channel_memory = 2;
        mlse_cfg.traceback_depth = 20;
        mlse_cfg.track_during_data = false;
        mlse_cfg.adaptation_rate = 0.01f;
        mlse_cfg.noise_variance = 0.1f;
    }
};

struct TurboStats {
    int iterations_used = 0;
    std::vector<float> avg_llr_magnitude;  // Per iteration
    bool converged = false;
    float final_avg_llr = 0;
};

class TurboEqualizer {
public:
    explicit TurboEqualizer(const TurboConfig& cfg = TurboConfig())
        : cfg_(cfg)
        , mlse_(cfg.mlse_cfg)
        , siso_(cfg.siso_cfg)
        , demapper_(cfg.demapper_cfg)
        , interleaver_(cfg.interleaver_rows, cfg.interleaver_cols * 3)  // ×3 for bits
    {}
    
    /**
     * Full turbo equalization
     * 
     * @param received Channel output symbols (after matched filter)
     * @param preamble_rx Received preamble for channel estimation
     * @param preamble_tx Known preamble symbols
     * @return Decoded bits
     */
    std::vector<uint8_t> decode(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_tx) {
        
        stats_ = TurboStats();
        
        // Initial channel estimate from preamble
        mlse_.estimate_channel(preamble_rx, preamble_tx);
        
        // Initialize priors to zero (no information)
        std::vector<float> symbol_priors_flat(received.size() * 8, 0.125f);
        
        std::vector<float> prev_llrs;
        
        for (int iter = 0; iter < cfg_.max_iterations; iter++) {
            stats_.iterations_used = iter + 1;
            
            // ========== Step 1: Soft MLSE Equalization ==========
            // Convert flat priors to per-symbol arrays
            std::vector<SoftSymbol> mlse_priors(received.size());
            for (size_t i = 0; i < received.size(); i++) {
                for (int s = 0; s < 8; s++) {
                    mlse_priors[i].probs[s] = symbol_priors_flat[i * 8 + s];
                }
            }
            
            // Run MLSE with priors
            auto soft_symbols = (iter == 0) 
                ? mlse_.equalize_soft(received)
                : mlse_.turbo_iteration(received, mlse_priors);
            
            // ========== Step 2: Soft Demapping ==========
            // Convert symbol probabilities to bit LLRs
            std::vector<float> bit_llrs;
            bit_llrs.reserve(soft_symbols.size() * 3);
            
            for (const auto& sym : soft_symbols) {
                auto llr = demapper_.demap_probs(sym.probs);
                bit_llrs.push_back(llr[0]);  // b2
                bit_llrs.push_back(llr[1]);  // b1
                bit_llrs.push_back(llr[2]);  // b0
            }
            
            // ========== Step 3: Deinterleave ==========
            auto deinterleaved = interleaver_.deinterleave(bit_llrs);
            
            // ========== Step 4: SISO Decode ==========
            // Need to pair up bits for rate 1/2 code
            // Assume systematic: data bit goes through both G1 and G2
            // channel_llr format: [c0_0, c1_0, c0_1, c1_1, ...]
            
            // For 8-PSK, 3 bits/symbol, rate 1/2 code means:
            // 3 encoded bits per symbol = 1.5 info bits per symbol
            // This is mode-dependent, simplified here
            
            std::vector<float> channel_for_siso;
            // Simple approach: use LLRs directly as channel observations
            // Real implementation would need proper code-symbol mapping
            for (size_t i = 0; i + 1 < deinterleaved.size(); i += 2) {
                channel_for_siso.push_back(deinterleaved[i]);
                channel_for_siso.push_back(deinterleaved[i + 1]);
            }
            
            // Get extrinsic from decoder
            auto extrinsic = siso_.decode(channel_for_siso);
            
            // Apply damping
            for (auto& e : extrinsic) {
                e *= cfg_.extrinsic_scale;
            }
            
            // ========== Step 5: Interleave extrinsic ==========
            // Expand back to 3 bits per symbol
            std::vector<float> expanded_ext(bit_llrs.size(), 0.0f);
            for (size_t i = 0; i < extrinsic.size() && i * 2 + 1 < expanded_ext.size(); i++) {
                // Distribute decoder info back to bit positions
                expanded_ext[i * 2] = extrinsic[i];
                expanded_ext[i * 2 + 1] = extrinsic[i];
            }
            
            auto interleaved_ext = interleaver_.interleave(expanded_ext);
            
            // ========== Step 6: Soft Mapping → Symbol Priors ==========
            symbol_priors_flat.clear();
            for (size_t i = 0; i + 2 < interleaved_ext.size(); i += 3) {
                std::array<float, 3> llrs = {
                    interleaved_ext[i],
                    interleaved_ext[i + 1],
                    interleaved_ext[i + 2]
                };
                auto probs = mapper_.map(llrs);
                for (int s = 0; s < 8; s++) {
                    symbol_priors_flat.push_back(probs[s]);
                }
            }
            
            // Pad if needed
            while (symbol_priors_flat.size() < received.size() * 8) {
                symbol_priors_flat.push_back(0.125f);
            }
            
            // ========== Check convergence ==========
            float avg_llr = 0;
            for (const auto& e : extrinsic) {
                avg_llr += std::abs(e);
            }
            avg_llr /= std::max(1, (int)extrinsic.size());
            stats_.avg_llr_magnitude.push_back(avg_llr);
            
            if (cfg_.early_termination && !prev_llrs.empty()) {
                float change = 0;
                int count = std::min(prev_llrs.size(), extrinsic.size());
                for (int i = 0; i < count; i++) {
                    change += std::abs(extrinsic[i] - prev_llrs[i]);
                }
                change /= std::max(1, count);
                
                if (change < cfg_.convergence_threshold) {
                    stats_.converged = true;
                    break;
                }
            }
            
            prev_llrs = extrinsic;
        }
        
        // Final hard decisions
        stats_.final_avg_llr = stats_.avg_llr_magnitude.empty() ? 0 
                              : stats_.avg_llr_magnitude.back();
        
        // Decode with APP (not just extrinsic)
        std::vector<float> channel_final;
        auto deint_final = interleaver_.deinterleave(
            demapper_.demap_sequence(received));
        for (size_t i = 0; i + 1 < deint_final.size(); i += 2) {
            channel_final.push_back(deint_final[i]);
            channel_final.push_back(deint_final[i + 1]);
        }
        
        auto app = siso_.decode_app(channel_final, prev_llrs);
        return siso_.hard_decide(app);
    }
    
    /**
     * Simplified decode without full turbo (single MLSE + decode)
     */
    std::vector<uint8_t> decode_simple(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_tx) {
        
        // Channel estimation
        mlse_.estimate_channel(preamble_rx, preamble_tx);
        
        // Single MLSE pass
        auto soft = mlse_.equalize_soft(received);
        
        // Demapping
        std::vector<float> llrs;
        for (const auto& sym : soft) {
            auto bits = demapper_.demap_probs(sym.probs);
            llrs.push_back(bits[0]);
            llrs.push_back(bits[1]);
            llrs.push_back(bits[2]);
        }
        
        // Deinterleave
        auto deint = interleaver_.deinterleave(llrs);
        
        // Decode
        std::vector<float> channel;
        for (size_t i = 0; i + 1 < deint.size(); i += 2) {
            channel.push_back(deint[i]);
            channel.push_back(deint[i + 1]);
        }
        
        auto ext = siso_.decode(channel);
        return siso_.hard_decide(ext);
    }
    
    /**
     * Get statistics from last decode
     */
    const TurboStats& stats() const { return stats_; }
    
    /**
     * Access MLSE for channel info
     */
    AdaptiveMLSE& mlse() { return mlse_; }

private:
    TurboConfig cfg_;
    
    AdaptiveMLSE mlse_;
    SISODecoder siso_;
    Soft8PSKDemapper demapper_;
    Soft8PSKMapper mapper_;
    SoftInterleaver interleaver_;
    
    TurboStats stats_;
};

} // namespace m110a

#endif // TURBO_EQUALIZER_H
