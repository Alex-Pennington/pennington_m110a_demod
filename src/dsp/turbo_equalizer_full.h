/**
 * @file turbo_equalizer_full.h
 * @brief Full Turbo Equalizer with Mode-Aware SISO Integration
 * 
 * Properly handles MIL-STD-188-110A structure:
 *   - Mode-specific interleaver dimensions
 *   - Scrambler-aware soft demapping
 *   - Rate 1/2 K=7 convolutional code via BCJR
 *   - Proper extrinsic information exchange
 * 
 * Data flow per iteration:
 *   Received symbols
 *     → Soft MLSE (with priors)
 *     → Soft descramble
 *     → Soft inverse Gray
 *     → Soft Demapper (symbol → bit LLRs)
 *     → Deinterleaver
 *     → SISO Decoder (BCJR)
 *     → Extrinsic extraction
 *     → Interleaver
 *     → Soft Gray + Scramble
 *     → Soft Mapper (bit LLRs → symbol priors)
 *     → Feed back to MLSE
 */

#ifndef TURBO_EQUALIZER_FULL_H
#define TURBO_EQUALIZER_FULL_H

#include "src/dsp/mlse_adaptive.h"
#include "src/dsp/soft_demapper_turbo.h"
#include "src/dsp/soft_mapper_turbo.h"
#include "src/modem/soft_interleaver.h"
#include "src/modem/siso_viterbi.h"
#include "src/m110a/mode_config.h"

#include <vector>
#include <complex>
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>

namespace m110a {

using complex_t = std::complex<float>;

/**
 * Configuration for full turbo equalizer
 */
struct TurboFullConfig {
    // Mode configuration (determines interleaver, modulation, FEC)
    ModeId mode_id = ModeId::M2400S;
    
    // Turbo parameters
    int max_iterations = 4;
    float convergence_threshold = 0.1f;
    bool early_termination = true;
    float extrinsic_scale = 0.75f;  // Damping factor
    
    // MLSE parameters
    int channel_memory = 3;
    float noise_variance = 0.1f;
    
    // Noise variance for soft demapping
    float demapper_noise_var = 0.1f;
};

/**
 * Statistics from turbo decode
 */
struct TurboFullStats {
    int iterations_used = 0;
    std::vector<float> avg_llr_per_iter;
    bool converged = false;
    float final_ber_estimate = 0;
};

/**
 * Full Turbo Equalizer with Mode-Aware Integration
 */
class TurboEqualizerFull {
public:
    explicit TurboEqualizerFull(const TurboFullConfig& cfg = TurboFullConfig())
        : cfg_(cfg)
        , mode_cfg_(ModeDatabase::get(cfg.mode_id))
    {
        // Initialize MLSE
        AdaptiveMLSEConfig mlse_cfg;
        mlse_cfg.channel_memory = cfg.channel_memory;
        mlse_cfg.noise_variance = cfg.noise_variance;
        mlse_.reset(new AdaptiveMLSE(mlse_cfg));
        
        // Initialize SISO decoder (uses defaults from SISOConfig)
        SISOConfig siso_cfg;
        siso_.reset(new SISODecoder(siso_cfg));
        
        // Initialize interleaver with mode-specific dimensions
        // Interleaver works on BITS, so multiply cols by bits_per_symbol
        int bit_cols = mode_cfg_.interleaver.cols * mode_cfg_.bits_per_symbol;
        interleaver_.reset(new SoftInterleaver(mode_cfg_.interleaver.rows, bit_cols));
        
        // Compute scrambler sequence (160 symbols, cyclic)
        compute_scrambler_sequence();
    }
    
    /**
     * Full turbo equalization decode
     * 
     * @param received Received symbols (data only, probes removed)
     * @param preamble_rx Received preamble for channel estimation
     * @param preamble_ref Known preamble reference
     * @param scrambler_start Starting scrambler index
     * @return Decoded data bits
     */
    std::vector<uint8_t> decode(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_ref,
            int scrambler_start = 0) {
        
        stats_ = TurboFullStats();
        
        // Channel estimation from preamble
        if (!preamble_rx.empty() && !preamble_ref.empty()) {
            mlse_->estimate_channel(preamble_rx, preamble_ref);
        }
        
        const int num_symbols = received.size();
        const int bits_per_sym = mode_cfg_.bits_per_symbol;
        const int num_bits = num_symbols * bits_per_sym;
        
        // Initialize symbol priors (uniform)
        std::vector<SoftSymbol> symbol_priors(num_symbols);
        for (auto& sp : symbol_priors) {
            for (int s = 0; s < 8; s++) sp.probs[s] = 0.125f;
        }
        
        std::vector<float> prev_llrs;
        
        // ===== Turbo Iterations =====
        for (int iter = 0; iter < cfg_.max_iterations; iter++) {
            stats_.iterations_used = iter + 1;
            
            // ----- Step 1: Soft MLSE Equalization -----
            std::vector<SoftSymbol> soft_symbols;
            if (iter == 0) {
                soft_symbols = mlse_->equalize_soft(received);
            } else {
                soft_symbols = mlse_->turbo_iteration(received, symbol_priors);
            }
            
            // ----- Step 2: Soft Descramble + Inverse Gray -----
            // Apply soft descrambling: rotate probabilities
            std::vector<SoftSymbol> descrambled(num_symbols);
            for (int i = 0; i < num_symbols; i++) {
                int scr_idx = (scrambler_start + i) % 160;
                int scr = scrambler_seq_[scr_idx];
                
                // Descramble: rotate probabilities by -scr (mod 8)
                for (int s = 0; s < 8; s++) {
                    int src = (s + scr) & 7;  // Original scrambled index
                    descrambled[i].probs[s] = soft_symbols[i].probs[src];
                }
                descrambled[i].hard_decision = (soft_symbols[i].hard_decision - scr + 8) & 7;
            }
            
            // Apply soft inverse Gray mapping
            std::vector<SoftSymbol> gray_decoded(num_symbols);
            for (int i = 0; i < num_symbols; i++) {
                // Inverse Gray: map from Gray code to natural binary
                for (int s = 0; s < 8; s++) {
                    int gray_s = INV_GRAY8[s];  // Natural → Gray
                    gray_decoded[i].probs[s] = descrambled[i].probs[gray_s];
                }
            }
            
            // ----- Step 3: Soft Demapping (Symbol → Bit LLRs) -----
            std::vector<float> bit_llrs;
            bit_llrs.reserve(num_bits);
            
            for (int i = 0; i < num_symbols; i++) {
                auto llrs = demap_soft_symbol(gray_decoded[i].probs);
                for (float llr : llrs) {
                    bit_llrs.push_back(llr);
                }
            }
            
            // ----- Step 4: Deinterleave -----
            auto deinterleaved = interleaver_->deinterleave(bit_llrs);
            
            // ----- Step 5: SISO Decode -----
            // For rate 1/2 code: pair up bits as [c0, c1] for each data bit
            // MIL-STD uses systematic bits from G1 and parity from G2
            std::vector<float> channel_llrs;
            channel_llrs.reserve(deinterleaved.size());
            
            // Pass LLRs to SISO (expects [c0_0, c1_0, c0_1, c1_1, ...])
            for (size_t i = 0; i < deinterleaved.size(); i++) {
                channel_llrs.push_back(deinterleaved[i]);
            }
            
            // Get extrinsic information from decoder
            auto extrinsic = siso_->decode(channel_llrs);
            
            // Apply damping to prevent oscillation
            for (auto& e : extrinsic) {
                e *= cfg_.extrinsic_scale;
            }
            
            // Track convergence
            float avg_llr = 0;
            for (auto e : extrinsic) avg_llr += std::abs(e);
            avg_llr /= std::max(1, (int)extrinsic.size());
            stats_.avg_llr_per_iter.push_back(avg_llr);
            
            // Check convergence
            if (cfg_.early_termination && iter > 0 && !prev_llrs.empty()) {
                float change = 0;
                for (size_t i = 0; i < extrinsic.size() && i < prev_llrs.size(); i++) {
                    change += std::abs(extrinsic[i] - prev_llrs[i]);
                }
                change /= std::max(1, (int)extrinsic.size());
                if (change < cfg_.convergence_threshold) {
                    stats_.converged = true;
                    break;
                }
            }
            prev_llrs = extrinsic;
            
            // ----- Step 6: Expand extrinsic to bit positions -----
            // Each data bit produces 2 coded bits (rate 1/2)
            // Distribute extrinsic info back
            std::vector<float> expanded_ext(num_bits, 0.0f);
            for (size_t i = 0; i < extrinsic.size(); i++) {
                // Both coded bits from same data bit get same extrinsic
                if (i * 2 < expanded_ext.size()) {
                    expanded_ext[i * 2] = extrinsic[i];
                }
                if (i * 2 + 1 < expanded_ext.size()) {
                    expanded_ext[i * 2 + 1] = extrinsic[i];
                }
            }
            
            // ----- Step 7: Interleave -----
            auto interleaved_ext = interleaver_->interleave(expanded_ext);
            
            // ----- Step 8: Soft Mapping (Bit LLRs → Symbol Priors) -----
            // Also apply Gray coding and scrambling in reverse
            for (int i = 0; i < num_symbols; i++) {
                int base_idx = i * bits_per_sym;
                if (base_idx + bits_per_sym > (int)interleaved_ext.size()) break;
                
                // Extract bit LLRs for this symbol
                std::array<float, 3> llrs = {0, 0, 0};
                for (int b = 0; b < bits_per_sym && b < 3; b++) {
                    llrs[b] = interleaved_ext[base_idx + b];
                }
                
                // Map to symbol probabilities (natural binary)
                auto probs = map_llrs_to_probs(llrs);
                
                // Apply Gray coding
                std::array<float, 8> gray_probs;
                for (int s = 0; s < 8; s++) {
                    gray_probs[GRAY8[s]] = probs[s];  // Natural → Gray
                }
                
                // Apply scrambling
                int scr_idx = (scrambler_start + i) % 160;
                int scr = scrambler_seq_[scr_idx];
                
                for (int s = 0; s < 8; s++) {
                    int scrambled = (s + scr) & 7;
                    symbol_priors[i].probs[scrambled] = gray_probs[s];
                }
                
                // Normalize
                float sum = 0;
                for (int s = 0; s < 8; s++) sum += symbol_priors[i].probs[s];
                if (sum > 0) {
                    for (int s = 0; s < 8; s++) symbol_priors[i].probs[s] /= sum;
                }
            }
        }
        
        // ===== Final Hard Decision =====
        // Get final soft output and make hard decisions
        auto final_soft = mlse_->turbo_iteration(received, symbol_priors);
        
        // Descramble and decode
        std::vector<uint8_t> decoded_bits;
        for (int i = 0; i < num_symbols; i++) {
            int scr_idx = (scrambler_start + i) % 160;
            int scr = scrambler_seq_[scr_idx];
            
            // Hard decision
            int best = 0;
            float best_prob = final_soft[i].probs[0];
            for (int s = 1; s < 8; s++) {
                if (final_soft[i].probs[s] > best_prob) {
                    best_prob = final_soft[i].probs[s];
                    best = s;
                }
            }
            
            // Descramble
            int descrambled = (best - scr + 8) & 7;
            
            // Inverse Gray
            int natural = INV_GRAY8[descrambled];
            
            // Extract bits (MSB first for 8-PSK)
            for (int b = bits_per_sym - 1; b >= 0; b--) {
                decoded_bits.push_back((natural >> b) & 1);
            }
        }
        
        // Deinterleave bits
        std::vector<float> bit_llrs_final(decoded_bits.size());
        for (size_t i = 0; i < decoded_bits.size(); i++) {
            bit_llrs_final[i] = decoded_bits[i] ? -10.0f : 10.0f;
        }
        auto deint_final = interleaver_->deinterleave(bit_llrs_final);
        
        // Viterbi decode (hard decision on SISO output)
        auto final_extrinsic = siso_->decode(deint_final);
        
        // Convert to bytes
        std::vector<uint8_t> result;
        uint8_t byte = 0;
        int bit_count = 0;
        
        for (float llr : final_extrinsic) {
            int bit = (llr < 0) ? 1 : 0;
            byte = (byte << 1) | bit;
            bit_count++;
            if (bit_count == 8) {
                result.push_back(byte);
                byte = 0;
                bit_count = 0;
            }
        }
        
        return result;
    }
    
    /**
     * Get statistics from last decode
     */
    const TurboFullStats& stats() const { return stats_; }
    
    /**
     * Access to MLSE for channel estimation
     */
    AdaptiveMLSE& mlse() { return *mlse_; }

private:
    TurboFullConfig cfg_;
    ModeConfig mode_cfg_;
    
    std::unique_ptr<AdaptiveMLSE> mlse_;
    std::unique_ptr<SISODecoder> siso_;
    std::unique_ptr<SoftInterleaver> interleaver_;
    
    TurboFullStats stats_;
    
    // Scrambler sequence (160 values, 0-7)
    std::array<int, 160> scrambler_seq_;
    
    // Gray code tables
    static constexpr int GRAY8[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    static constexpr int INV_GRAY8[8] = {0, 1, 3, 2, 7, 6, 4, 5};  // Self-inverse for this mapping
    
    /**
     * Compute scrambler sequence
     */
    void compute_scrambler_sequence() {
        // MIL-STD-188-110A data scrambler: 9-bit LFSR
        // Polynomial: x^9 + x^4 + 1
        // Init: all 1s
        uint16_t sr = 0x1FF;  // 9 bits all 1
        
        for (int i = 0; i < 160; i++) {
            // Output is bits [8:6] = top 3 bits (value 0-7)
            scrambler_seq_[i] = (sr >> 6) & 7;
            
            // Feedback: bit 8 XOR bit 3
            int feedback = ((sr >> 8) ^ (sr >> 3)) & 1;
            sr = ((sr << 1) | feedback) & 0x1FF;
        }
    }
    
    /**
     * Soft demapping: symbol probabilities → bit LLRs
     */
    std::array<float, 3> demap_soft_symbol(const std::array<float, 8>& probs) {
        std::array<float, 3> llrs;
        
        // For 8-PSK with natural binary mapping:
        // Symbol s has bits: b2 = s>>2, b1 = (s>>1)&1, b0 = s&1
        
        for (int bit_idx = 0; bit_idx < 3; bit_idx++) {
            float p0 = 0, p1 = 0;
            int mask = 1 << (2 - bit_idx);  // b2, b1, b0
            
            for (int s = 0; s < 8; s++) {
                if ((s & mask) == 0) {
                    p0 += probs[s];
                } else {
                    p1 += probs[s];
                }
            }
            
            // LLR = log(P(b=0) / P(b=1))
            const float eps = 1e-10f;
            llrs[bit_idx] = std::log((p0 + eps) / (p1 + eps));
            
            // Clip to reasonable range
            llrs[bit_idx] = std::max(-20.0f, std::min(20.0f, llrs[bit_idx]));
        }
        
        return llrs;
    }
    
    /**
     * Soft mapping: bit LLRs → symbol probabilities
     */
    std::array<float, 8> map_llrs_to_probs(const std::array<float, 3>& llrs) {
        std::array<float, 8> probs;
        
        // P(b=0) = 1 / (1 + exp(-LLR))
        // P(b=1) = 1 / (1 + exp(+LLR))
        
        std::array<float, 3> p0, p1;
        for (int i = 0; i < 3; i++) {
            float clamped = std::max(-20.0f, std::min(20.0f, llrs[i]));
            p0[i] = 1.0f / (1.0f + std::exp(-clamped));
            p1[i] = 1.0f - p0[i];
        }
        
        // P(s) = product of bit probabilities
        float sum = 0;
        for (int s = 0; s < 8; s++) {
            int b2 = (s >> 2) & 1;
            int b1 = (s >> 1) & 1;
            int b0 = s & 1;
            
            probs[s] = (b2 ? p1[0] : p0[0]) *
                       (b1 ? p1[1] : p0[1]) *
                       (b0 ? p1[2] : p0[2]);
            sum += probs[s];
        }
        
        // Normalize
        if (sum > 0) {
            for (int s = 0; s < 8; s++) probs[s] /= sum;
        }
        
        return probs;
    }
};

// Static member definitions
constexpr int TurboEqualizerFull::GRAY8[8];
constexpr int TurboEqualizerFull::INV_GRAY8[8];

} // namespace m110a

#endif // TURBO_EQUALIZER_FULL_H
