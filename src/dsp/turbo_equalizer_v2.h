/**
 * @file turbo_equalizer_v2.h
 * @brief Mode-Aware Turbo Equalizer with Full SISO Integration
 * 
 * Properly handles:
 *   - Mode-specific interleaver sizes
 *   - Rate 1/2 convolutional code (K=7)
 *   - 8-PSK Gray mapping
 *   - Iterative soft information exchange
 * 
 * MIL-STD-188-110A Turbo Loop:
 *   Received symbols 
 *     → Soft MLSE (with priors)
 *     → Soft Demapper (8-PSK → 3 bit LLRs)
 *     → Deinterleaver (coded bits)
 *     → SISO Decoder (rate 1/2, K=7)
 *     → Interleaver (extrinsic LLRs)
 *     → Soft Mapper (bit LLRs → symbol priors)
 *     → Feed back to MLSE
 */

#ifndef TURBO_EQUALIZER_V2_H
#define TURBO_EQUALIZER_V2_H

#include "src/dsp/mlse_adaptive.h"
#include "src/dsp/soft_demapper_turbo.h"
#include "src/dsp/soft_mapper_turbo.h"
#include "src/modem/soft_interleaver.h"
#include "src/modem/siso_viterbi.h"
#include "src/m110a/mode_config.h"

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <memory>
#include <iostream>

namespace m110a {

/**
 * Turbo configuration with mode awareness
 */
struct TurboConfigV2 {
    // Iteration control
    int max_iterations = 5;
    float convergence_threshold = 0.05f;
    bool early_termination = true;
    
    // Extrinsic scaling (0.5-1.0) - prevents oscillation
    float extrinsic_scale = 0.7f;
    
    // MLSE configuration
    AdaptiveMLSEConfig mlse_cfg;
    
    // Noise variance estimate (updated from MLSE)
    float noise_variance = 0.1f;
    
    // Debug output
    bool verbose = false;
    
    TurboConfigV2() {
        mlse_cfg.channel_memory = 3;
        mlse_cfg.traceback_depth = 25;
        mlse_cfg.track_during_data = true;
        mlse_cfg.adaptation_rate = 0.005f;
        mlse_cfg.noise_variance = 0.1f;
    }
};

/**
 * Statistics from turbo equalization
 */
struct TurboStatsV2 {
    int iterations_used = 0;
    std::vector<float> avg_llr_per_iter;
    std::vector<float> max_llr_per_iter;
    bool converged = false;
    float snr_estimate_db = 0;
    int decoded_bits = 0;
    int interleaver_size = 0;
};

/**
 * Mode-Aware Turbo Equalizer
 * 
 * Properly integrates MLSE and SISO decoder with mode-specific
 * interleaver sizing.
 */
class TurboEqualizerV2 {
public:
    /**
     * Construct turbo equalizer for a specific mode
     * 
     * @param mode_id Mode identifier (sets interleaver size)
     * @param cfg Turbo configuration
     */
    TurboEqualizerV2(ModeId mode_id, const TurboConfigV2& cfg = TurboConfigV2())
        : cfg_(cfg)
        , mode_id_(mode_id)
        , mode_cfg_(ModeDatabase::get(mode_id))
        , mlse_(cfg.mlse_cfg)
    {
        // Get interleaver parameters from mode
        int rows = mode_cfg_.interleaver.rows;
        int cols = mode_cfg_.interleaver.cols;
        
        // Interleaver operates on coded bits
        // For 8-PSK with rate 1/2: 3 bits/symbol, all coded
        interleaver_bits_ = rows * cols;
        
        // Create soft interleaver
        interleaver_ = std::make_unique<SoftInterleaver>(rows, cols);
        
        // Calculate derived parameters
        // Rate 1/2 code: interleaver_bits / 2 = info bits
        // 8-PSK: interleaver_bits / 3 = symbols
        info_bits_per_block_ = interleaver_bits_ / 2;
        symbols_per_block_ = interleaver_bits_ / 3;
        
        if (cfg_.verbose) {
            std::cout << "TurboEqualizerV2 for " << mode_cfg_.name << ":\n"
                      << "  Interleaver: " << rows << "x" << cols 
                      << " = " << interleaver_bits_ << " bits\n"
                      << "  Info bits/block: " << info_bits_per_block_ << "\n"
                      << "  Symbols/block: " << symbols_per_block_ << "\n";
        }
    }
    
    /**
     * Full turbo equalization with mode-aware processing
     * 
     * @param received Channel output symbols
     * @param preamble_rx Received preamble for channel estimation
     * @param preamble_tx Known preamble symbols
     * @return Decoded info bits
     */
    std::vector<uint8_t> decode(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_tx) {
        
        stats_ = TurboStatsV2();
        stats_.interleaver_size = interleaver_bits_;
        
        // Initial channel estimate
        if (!preamble_rx.empty() && !preamble_tx.empty()) {
            mlse_.estimate_channel(preamble_rx, preamble_tx);
        }
        
        // Estimate noise variance from MLSE
        float noise_var = cfg_.noise_variance;
        
        // Number of full interleaver blocks
        int num_symbols = static_cast<int>(received.size());
        int num_blocks = std::max(1, (num_symbols * 3) / interleaver_bits_);
        
        // If data is much smaller than interleaver, skip turbo iterations
        // (padding would introduce errors)
        bool skip_turbo = (num_symbols * 3) < (interleaver_bits_ / 2);
        if (skip_turbo && cfg_.verbose) {
            std::cout << "Data too small for turbo (" << num_symbols << " symbols), using single pass\n";
        }
        
        if (cfg_.verbose && !skip_turbo) {
            std::cout << "Processing " << num_symbols << " symbols in " 
                      << num_blocks << " block(s)\n";
        }
        
        // Initialize a priori LLRs to zero (no prior info)
        std::vector<float> apriori_llrs(num_symbols * 3, 0.0f);
        
        // Store previous extrinsic for convergence check
        std::vector<float> prev_extrinsic;
        
        // Limit iterations when data too small
        int actual_max_iter = skip_turbo ? 1 : cfg_.max_iterations;
        
        // Turbo iterations
        for (int iter = 0; iter < actual_max_iter; iter++) {
            stats_.iterations_used = iter + 1;
            
            // ===== Step 1: Soft MLSE Equalization =====
            std::vector<SoftSymbol> soft_symbols;
            
            if (iter == 0) {
                // First iteration: no priors
                soft_symbols = mlse_.equalize_soft(received);
            } else {
                // Subsequent iterations: use priors from decoder
                std::vector<SoftSymbol> priors(num_symbols);
                
                for (int i = 0; i < num_symbols; i++) {
                    int bit_idx = i * 3;
                    if (bit_idx + 2 < static_cast<int>(apriori_llrs.size())) {
                        std::array<float, 3> llrs = {
                            apriori_llrs[bit_idx],
                            apriori_llrs[bit_idx + 1],
                            apriori_llrs[bit_idx + 2]
                        };
                        auto probs = mapper_.map(llrs);
                        for (int s = 0; s < 8; s++) {
                            priors[i].probs[s] = probs[s];
                        }
                    } else {
                        // Uniform prior
                        for (int s = 0; s < 8; s++) {
                            priors[i].probs[s] = 0.125f;
                        }
                    }
                }
                
                soft_symbols = mlse_.turbo_iteration(received, priors);
            }
            
            // ===== Step 2: Soft Demapping (symbol → bit LLRs) =====
            std::vector<float> channel_llrs;
            channel_llrs.reserve(num_symbols * 3);
            
            for (const auto& sym : soft_symbols) {
                auto bits = demapper_.demap_probs(sym.probs);
                channel_llrs.push_back(bits[0]);  // b2
                channel_llrs.push_back(bits[1]);  // b1
                channel_llrs.push_back(bits[2]);  // b0
            }
            
            // Save for final decode and for get_hard_symbols()
            channel_llrs_ = channel_llrs;
            last_soft_symbols_ = soft_symbols;
            
            // ===== Step 3: Process each interleaver block =====
            std::vector<float> extrinsic_all;
            extrinsic_all.reserve(channel_llrs.size());
            
            for (int block = 0; block < num_blocks; block++) {
                int start_bit = block * interleaver_bits_;
                int end_bit = std::min(start_bit + interleaver_bits_, 
                                       static_cast<int>(channel_llrs.size()));
                int block_bits = end_bit - start_bit;
                
                // Extract block
                std::vector<float> block_llrs(
                    channel_llrs.begin() + start_bit,
                    channel_llrs.begin() + end_bit);
                
                // Pad to full block if needed
                while (static_cast<int>(block_llrs.size()) < interleaver_bits_) {
                    block_llrs.push_back(0.0f);
                }
                
                // Deinterleave
                auto deint_llrs = interleaver_->deinterleave(block_llrs);
                
                // ===== Step 4: SISO Decode =====
                // Rate 1/2 code: pair up bits [c0, c1] for each info bit
                std::vector<float> coded_pairs;
                for (size_t i = 0; i + 1 < deint_llrs.size(); i += 2) {
                    coded_pairs.push_back(deint_llrs[i]);
                    coded_pairs.push_back(deint_llrs[i + 1]);
                }
                
                // Get apriori for this block (if available)
                std::vector<float> apriori_block;
                if (iter > 0 && block * info_bits_per_block_ < static_cast<int>(prev_extrinsic.size())) {
                    int start = block * info_bits_per_block_;
                    int end = std::min(start + info_bits_per_block_, 
                                       static_cast<int>(prev_extrinsic.size()));
                    apriori_block.assign(prev_extrinsic.begin() + start,
                                        prev_extrinsic.begin() + end);
                }
                
                // SISO decode
                auto extrinsic = siso_.decode(coded_pairs, apriori_block);
                
                // Apply damping
                for (auto& e : extrinsic) {
                    e *= cfg_.extrinsic_scale;
                }
                
                // ===== Step 5: Interleave extrinsic back =====
                // Expand extrinsic from info bits to coded bits
                // Each info bit produces 2 coded bits
                std::vector<float> ext_coded(interleaver_bits_, 0.0f);
                for (size_t i = 0; i < extrinsic.size() && i * 2 + 1 < ext_coded.size(); i++) {
                    // Extrinsic for info bit applies to both coded bits
                    ext_coded[i * 2] = extrinsic[i];
                    ext_coded[i * 2 + 1] = extrinsic[i];
                }
                
                // Interleave
                auto int_ext = interleaver_->interleave(ext_coded);
                
                // Store for feedback
                for (int i = 0; i < block_bits; i++) {
                    extrinsic_all.push_back(int_ext[i]);
                }
            }
            
            // ===== Step 6: Update a priori for next iteration =====
            apriori_llrs = extrinsic_all;
            
            // Compute statistics
            float avg_llr = 0, max_llr = 0;
            for (const auto& e : extrinsic_all) {
                float abs_e = std::abs(e);
                avg_llr += abs_e;
                max_llr = std::max(max_llr, abs_e);
            }
            avg_llr /= std::max(1, static_cast<int>(extrinsic_all.size()));
            
            stats_.avg_llr_per_iter.push_back(avg_llr);
            stats_.max_llr_per_iter.push_back(max_llr);
            
            if (cfg_.verbose) {
                std::cout << "  Iter " << (iter + 1) << ": avg_LLR=" << avg_llr 
                          << ", max_LLR=" << max_llr << "\n";
            }
            
            // Check convergence
            if (cfg_.early_termination && !prev_extrinsic.empty()) {
                float change = 0;
                int count = std::min(prev_extrinsic.size(), extrinsic_all.size());
                for (int i = 0; i < count; i++) {
                    change += std::abs(extrinsic_all[i] - prev_extrinsic[i]);
                }
                change /= std::max(1, count);
                
                if (change < cfg_.convergence_threshold) {
                    stats_.converged = true;
                    if (cfg_.verbose) {
                        std::cout << "  Converged at iteration " << (iter + 1) << "\n";
                    }
                    break;
                }
            }
            
            // Store for next iteration
            prev_extrinsic.clear();
            for (int block = 0; block < num_blocks; block++) {
                int start = block * interleaver_bits_;
                int end = std::min(start + interleaver_bits_, 
                                   static_cast<int>(extrinsic_all.size()));
                
                // Deinterleave this block's extrinsic
                std::vector<float> block_ext(
                    extrinsic_all.begin() + start,
                    extrinsic_all.begin() + end);
                while (static_cast<int>(block_ext.size()) < interleaver_bits_) {
                    block_ext.push_back(0.0f);
                }
                
                auto deint_ext = interleaver_->deinterleave(block_ext);
                
                // Convert to info bit extrinsic
                for (size_t i = 0; i + 1 < deint_ext.size(); i += 2) {
                    prev_extrinsic.push_back((deint_ext[i] + deint_ext[i + 1]) / 2.0f);
                }
            }
        }
        
        // ===== Final Decoding =====
        // Use APP (a posteriori probability) = channel + extrinsic
        std::vector<uint8_t> decoded_bits;
        
        for (int block = 0; block < num_blocks; block++) {
            int start_bit = block * interleaver_bits_;
            int end_bit = std::min(start_bit + interleaver_bits_, 
                                   static_cast<int>(apriori_llrs.size()));
            
            // Get final channel LLRs
            std::vector<float> final_llrs;
            for (int i = start_bit; i < end_bit; i++) {
                if (i < static_cast<int>(channel_llrs_.size())) {
                    final_llrs.push_back(channel_llrs_[i] + apriori_llrs[i]);
                } else {
                    final_llrs.push_back(apriori_llrs[i]);
                }
            }
            while (static_cast<int>(final_llrs.size()) < interleaver_bits_) {
                final_llrs.push_back(0.0f);
            }
            
            // Deinterleave
            auto deint = interleaver_->deinterleave(final_llrs);
            
            // Pair up for decoder
            std::vector<float> coded_pairs;
            for (size_t i = 0; i + 1 < deint.size(); i += 2) {
                coded_pairs.push_back(deint[i]);
                coded_pairs.push_back(deint[i + 1]);
            }
            
            // Final decode with APP
            auto block_decoded = siso_.hard_decide(
                siso_.decode_app(coded_pairs, prev_extrinsic));
            
            for (auto b : block_decoded) {
                decoded_bits.push_back(b);
            }
        }
        
        stats_.decoded_bits = decoded_bits.size();
        
        // Estimate SNR from LLR magnitudes
        float final_avg = stats_.avg_llr_per_iter.empty() ? 0 
                         : stats_.avg_llr_per_iter.back();
        stats_.snr_estimate_db = 10.0f * std::log10(final_avg * final_avg / 4.0f + 1e-10f);
        
        return decoded_bits;
    }
    
    /**
     * Simplified single-pass decode (no turbo iterations)
     */
    std::vector<uint8_t> decode_single_pass(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_tx) {
        
        // Channel estimate
        if (!preamble_rx.empty() && !preamble_tx.empty()) {
            mlse_.estimate_channel(preamble_rx, preamble_tx);
        }
        
        // Single MLSE pass
        auto soft = mlse_.equalize_soft(received);
        
        // Demap
        std::vector<float> llrs;
        for (const auto& sym : soft) {
            auto bits = demapper_.demap_probs(sym.probs);
            llrs.push_back(bits[0]);
            llrs.push_back(bits[1]);
            llrs.push_back(bits[2]);
        }
        
        // Process blocks
        int num_blocks = std::max(1, static_cast<int>(llrs.size()) / interleaver_bits_);
        std::vector<uint8_t> decoded;
        
        for (int block = 0; block < num_blocks; block++) {
            int start = block * interleaver_bits_;
            int end = std::min(start + interleaver_bits_, static_cast<int>(llrs.size()));
            
            std::vector<float> block_llrs(llrs.begin() + start, llrs.begin() + end);
            while (static_cast<int>(block_llrs.size()) < interleaver_bits_) {
                block_llrs.push_back(0.0f);
            }
            
            auto deint = interleaver_->deinterleave(block_llrs);
            
            std::vector<float> coded;
            for (size_t i = 0; i + 1 < deint.size(); i += 2) {
                coded.push_back(deint[i]);
                coded.push_back(deint[i + 1]);
            }
            
            auto ext = siso_.decode(coded);
            auto bits = siso_.hard_decide(ext);
            
            for (auto b : bits) decoded.push_back(b);
        }
        
        return decoded;
    }
    
    /**
     * Get equalized symbols from last turbo decode
     * If decode() was called, returns symbols from final turbo iteration
     * Otherwise runs single MLSE pass
     */
    std::vector<int> get_hard_symbols(const std::vector<complex_t>& received) {
        // Use saved symbols from turbo iterations if available
        if (!last_soft_symbols_.empty()) {
            std::vector<int> symbols;
            for (const auto& s : last_soft_symbols_) {
                symbols.push_back(s.hard_decision);
            }
            return symbols;
        }
        
        // Fallback: single MLSE pass
        auto soft = mlse_.equalize_soft(received);
        std::vector<int> symbols;
        for (const auto& s : soft) {
            symbols.push_back(s.hard_decision);
        }
        return symbols;
    }
    
    /**
     * Access MLSE for channel info
     */
    AdaptiveMLSE& mlse() { return mlse_; }
    
    /**
     * Get last decode statistics
     */
    const TurboStatsV2& stats() const { return stats_; }
    
    /**
     * Get mode configuration
     */
    const ModeConfig& mode_config() const { return mode_cfg_; }

private:
    TurboConfigV2 cfg_;
    ModeId mode_id_;
    ModeConfig mode_cfg_;
    
    AdaptiveMLSE mlse_;
    SISODecoder siso_;
    Soft8PSKDemapper demapper_;
    Soft8PSKMapper mapper_;
    std::unique_ptr<SoftInterleaver> interleaver_;
    
    int interleaver_bits_ = 0;
    int info_bits_per_block_ = 0;
    int symbols_per_block_ = 0;
    
    std::vector<float> channel_llrs_;  // Saved for final decode
    std::vector<SoftSymbol> last_soft_symbols_;  // Final turbo iteration symbols
    TurboStatsV2 stats_;
};

/**
 * Factory function to create turbo equalizer from mode name
 */
inline std::unique_ptr<TurboEqualizerV2> create_turbo_equalizer(
        const std::string& mode_name,
        const TurboConfigV2& cfg = TurboConfigV2()) {
    ModeId id = mode_from_string(mode_name);
    return std::make_unique<TurboEqualizerV2>(id, cfg);
}

} // namespace m110a

#endif // TURBO_EQUALIZER_V2_H
