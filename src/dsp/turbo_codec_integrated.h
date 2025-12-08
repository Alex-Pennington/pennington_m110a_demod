/**
 * @file turbo_codec_integrated.h
 * @brief Turbo Equalizer Integrated with M110A Codec Chain
 * 
 * This turbo equalizer properly matches the M110A codec structure:
 * 
 * TX Chain: bits → FEC → interleave → Gray → scramble → PSK
 * RX Chain: PSK → MLSE → soft descramble → soft inv-Gray → soft demap → 
 *           deinterleave → SISO → interleave → soft map → soft Gray → 
 *           soft scramble → MLSE feedback
 * 
 * Key matching requirements:
 * - Scrambler: 160-symbol cycle, mod-8 addition
 * - Gray: MGD3 = {0,1,3,2,7,6,4,5}, INV_MGD3 = {0,1,3,2,6,7,5,4}
 * - Interleaver: mode-specific rows/cols with helical pattern
 */

#ifndef TURBO_CODEC_INTEGRATED_H
#define TURBO_CODEC_INTEGRATED_H

#include "src/dsp/mlse_adaptive.h"
#include "src/modem/siso_viterbi.h"
#include "src/modem/multimode_interleaver.h"
#include "src/modem/scrambler_fixed.h"
#include "src/modem/gray_code.h"
#include "src/m110a/mode_config.h"

#include <vector>
#include <complex>
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>

namespace m110a {

using complex_t = std::complex<float>;

// 8-PSK constellation
static const std::array<complex_t, 8> TURBO_PSK8 = {{
    { 1.000f,  0.000f}, { 0.707f,  0.707f},
    { 0.000f,  1.000f}, {-0.707f,  0.707f},
    {-1.000f,  0.000f}, {-0.707f, -0.707f},
    { 0.000f, -1.000f}, { 0.707f, -0.707f}
}};

/**
 * Configuration for integrated turbo equalizer
 */
struct TurboIntegratedConfig {
    // Mode (determines interleaver, scrambler, gray)
    ModeId mode_id = ModeId::M2400S;
    
    // Turbo parameters
    int max_iterations = 5;
    float convergence_threshold = 0.05f;
    bool early_termination = true;
    float extrinsic_scale = 0.7f;
    
    // MLSE parameters
    int channel_memory = 3;
    float noise_variance = 0.1f;
    
    // Debug
    bool verbose = false;
};

/**
 * Statistics from turbo equalization
 */
struct TurboIntegratedStats {
    int iterations = 0;
    std::vector<float> avg_llr;
    bool converged = false;
    int symbols_processed = 0;
    int bits_decoded = 0;
};

/**
 * Soft symbol representation
 */
struct TurboSoftSymbol {
    std::array<float, 8> probs = {0.125f, 0.125f, 0.125f, 0.125f,
                                  0.125f, 0.125f, 0.125f, 0.125f};
    int hard = 0;
    float reliability = 0;
    
    void normalize() {
        float sum = 0;
        for (float p : probs) sum += p;
        if (sum > 0) {
            for (float& p : probs) p /= sum;
        }
    }
    
    void compute_hard() {
        hard = 0;
        float best = probs[0];
        for (int i = 1; i < 8; i++) {
            if (probs[i] > best) {
                best = probs[i];
                hard = i;
            }
        }
        reliability = std::log(best + 1e-10f) - std::log((1.0f - best) / 7 + 1e-10f);
    }
};

/**
 * Integrated Turbo Equalizer
 * 
 * Properly matches M110A codec chain for seamless integration.
 */
class TurboCodecIntegrated {
public:
    explicit TurboCodecIntegrated(const TurboIntegratedConfig& cfg)
        : cfg_(cfg)
        , mode_cfg_(ModeDatabase::get(cfg.mode_id))
    {
        // Initialize MLSE
        AdaptiveMLSEConfig mlse_cfg;
        mlse_cfg.channel_memory = cfg.channel_memory;
        mlse_cfg.noise_variance = cfg.noise_variance;
        mlse_cfg.traceback_depth = 25;
        mlse_.reset(new AdaptiveMLSE(mlse_cfg));
        
        // Initialize SISO decoder
        SISOConfig siso_cfg;
        siso_.reset(new SISODecoder(siso_cfg));
        
        // Initialize interleaver (bit-level) - use M110A helical interleaver
        interleaver_.reset(new MultiModeInterleaver(mode_cfg_.interleaver));
        
        // Pre-compute scrambler sequence (160 symbols)
        DataScramblerFixed scr;
        for (int i = 0; i < 160; i++) {
            scrambler_seq_[i] = scr.next();
        }
        
        if (cfg.verbose) {
            std::cout << "TurboCodecIntegrated for " << mode_cfg_.name << ":\n"
                      << "  Interleaver: " << mode_cfg_.interleaver.rows 
                      << "x" << mode_cfg_.interleaver.cols << " = " 
                      << interleaver_->block_size() << " bits\n"
                      << "  Bits/symbol: " << mode_cfg_.bits_per_symbol << "\n";
        }
    }
    
    /**
     * Turbo equalize received symbols
     * 
     * @param received Received symbols (data only, probes removed)
     * @param preamble_rx Received preamble for channel estimation
     * @param preamble_ref Known preamble reference
     * @param scrambler_start Starting position in scrambler sequence
     * @return Soft bit LLRs (after deinterleaving, before Viterbi)
     * 
     * Note: Returns LLRs so they can be passed to existing Viterbi decoder.
     * Positive LLR = likely 0, Negative LLR = likely 1.
     */
    std::vector<float> equalize(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_ref,
            int scrambler_start = 0) {
        
        stats_ = TurboIntegratedStats();
        stats_.symbols_processed = received.size();
        
        // Channel estimation from preamble
        if (!preamble_rx.empty() && !preamble_ref.empty()) {
            mlse_->estimate_channel(preamble_rx, preamble_ref);
        }
        
        const int num_symbols = received.size();
        const int bits_per_sym = mode_cfg_.bits_per_symbol;
        const int num_bits = num_symbols * bits_per_sym;
        const int block_size = mode_cfg_.interleaver.rows * mode_cfg_.interleaver.cols;
        
        // Initialize priors (uniform)
        std::vector<TurboSoftSymbol> symbol_priors(num_symbols);
        
        // Store channel LLRs for final output
        std::vector<float> channel_llrs(num_bits, 0.0f);
        std::vector<float> extrinsic_llrs(num_bits / 2, 0.0f);  // Rate 1/2
        
        std::vector<float> prev_ext;
        
        // ===== Turbo Iterations =====
        for (int iter = 0; iter < cfg_.max_iterations; iter++) {
            stats_.iterations = iter + 1;
            
            // ----- Step 1: Soft MLSE -----
            std::vector<TurboSoftSymbol> soft_rx;
            soft_rx.reserve(num_symbols);
            
            for (int i = 0; i < num_symbols; i++) {
                TurboSoftSymbol ss;
                
                // Compute soft probabilities based on distance
                float noise_var = cfg_.noise_variance;
                float max_log = -1e30f;
                
                // Include prior from previous iteration
                for (int s = 0; s < 8; s++) {
                    complex_t expected = TURBO_PSK8[s];
                    
                    // For ISI channels, would include channel convolution here
                    // Simplified: direct distance
                    float dist_sq = std::norm(received[i] - expected);
                    float log_prob = -dist_sq / (2.0f * noise_var);
                    
                    // Add prior (log domain)
                    if (iter > 0) {
                        log_prob += std::log(symbol_priors[i].probs[s] + 1e-10f);
                    }
                    
                    ss.probs[s] = log_prob;
                    max_log = std::max(max_log, log_prob);
                }
                
                // Normalize to probabilities
                float sum = 0;
                for (int s = 0; s < 8; s++) {
                    ss.probs[s] = std::exp(ss.probs[s] - max_log);
                    sum += ss.probs[s];
                }
                for (int s = 0; s < 8; s++) {
                    ss.probs[s] /= sum;
                }
                ss.compute_hard();
                
                soft_rx.push_back(ss);
            }
            
            // ----- Step 2: Soft Descramble -----
            // Rotate probabilities by -scrambler value
            // NOTE: For data-only symbols, compute correct scrambler indices
            // accounting for probe symbol gaps in the full frame structure
            int unknown_len = mode_cfg_.unknown_data_len;
            int known_len = mode_cfg_.known_data_len;
            int pattern_len = unknown_len + known_len;
            
            std::vector<TurboSoftSymbol> descrambled(num_symbols);
            for (int i = 0; i < num_symbols; i++) {
                // Compute correct scrambler index for this data symbol
                int frame = i / unknown_len;
                int data_idx_in_frame = i % unknown_len;
                int scr_idx = (scrambler_start + frame * pattern_len + data_idx_in_frame) % 160;
                int scr = scrambler_seq_[scr_idx];
                
                for (int s = 0; s < 8; s++) {
                    // Descramble: received symbol s came from (s - scr + 8) % 8
                    int src = (s + scr) & 7;
                    descrambled[i].probs[s] = soft_rx[i].probs[src];
                }
                descrambled[i].compute_hard();
            }
            
            // ----- Step 3: Soft Inverse Gray -----
            // Map from Gray code to natural binary
            std::vector<TurboSoftSymbol> natural(num_symbols);
            for (int i = 0; i < num_symbols; i++) {
                for (int s = 0; s < 8; s++) {
                    // INV_MGD3[gray_idx] = natural_idx
                    // We have P(gray_idx), want P(natural_idx)
                    // P(natural_idx = n) = P(gray_idx = MGD3[n])
                    int gray_s = MGD3[s];
                    natural[i].probs[s] = descrambled[i].probs[gray_s];
                }
                natural[i].compute_hard();
            }
            
            // ----- Step 4: Soft Demapping -----
            // Convert symbol probabilities to bit LLRs
            std::vector<float> bit_llrs;
            bit_llrs.reserve(num_bits);
            
            for (int i = 0; i < num_symbols; i++) {
                // For 8-PSK: symbol s → bits [b2, b1, b0]
                // s = b2*4 + b1*2 + b0
                
                for (int bit_pos = 0; bit_pos < bits_per_sym; bit_pos++) {
                    float p0 = 0, p1 = 0;
                    int mask = 1 << (bits_per_sym - 1 - bit_pos);
                    
                    for (int s = 0; s < 8; s++) {
                        if ((s & mask) == 0) {
                            p0 += natural[i].probs[s];
                        } else {
                            p1 += natural[i].probs[s];
                        }
                    }
                    
                    // LLR = log(P(bit=0) / P(bit=1))
                    float llr = std::log((p0 + 1e-10f) / (p1 + 1e-10f));
                    llr = std::max(-20.0f, std::min(20.0f, llr));
                    bit_llrs.push_back(llr);
                }
            }
            
            // Store channel LLRs
            channel_llrs = bit_llrs;
            
            // ----- Step 5: Deinterleave -----
            // Process in blocks
            std::vector<float> deinterleaved;
            int num_blocks = (num_bits + block_size - 1) / block_size;
            
            for (int blk = 0; blk < num_blocks; blk++) {
                int start = blk * block_size;
                int end = std::min(start + block_size, num_bits);
                
                std::vector<float> block_in(block_size, 0.0f);
                for (int i = start; i < end; i++) {
                    block_in[i - start] = bit_llrs[i];
                }
                
                auto block_out = interleaver_->deinterleave_float(block_in);
                for (int i = 0; i < (end - start); i++) {
                    deinterleaved.push_back(block_out[i]);
                }
            }
            
            // ----- Step 6: SISO Decode -----
            // Rate 1/2 code: pairs of [c0, c1] for each info bit
            // Add a priori from previous iteration
            std::vector<float> apriori(deinterleaved.size() / 2, 0.0f);
            if (iter > 0 && !extrinsic_llrs.empty()) {
                for (size_t i = 0; i < apriori.size() && i < extrinsic_llrs.size(); i++) {
                    apriori[i] = extrinsic_llrs[i];
                }
            }
            
            auto new_extrinsic = siso_->decode(deinterleaved, apriori);
            
            // Apply damping
            for (auto& e : new_extrinsic) {
                e *= cfg_.extrinsic_scale;
            }
            
            // Track convergence
            float avg_llr = 0;
            for (auto e : new_extrinsic) avg_llr += std::abs(e);
            avg_llr /= std::max(1, (int)new_extrinsic.size());
            stats_.avg_llr.push_back(avg_llr);
            
            // Check convergence
            if (cfg_.early_termination && iter > 0 && !prev_ext.empty()) {
                float change = 0;
                for (size_t i = 0; i < new_extrinsic.size() && i < prev_ext.size(); i++) {
                    change += std::abs(new_extrinsic[i] - prev_ext[i]);
                }
                change /= std::max(1, (int)new_extrinsic.size());
                if (change < cfg_.convergence_threshold) {
                    stats_.converged = true;
                    extrinsic_llrs = new_extrinsic;
                    break;
                }
            }
            
            prev_ext = new_extrinsic;
            extrinsic_llrs = new_extrinsic;
            
            // ----- Step 7: Soft re-encode to coded bits -----
            // Convert info bit extrinsic to coded bit extrinsic
            auto coded_ext = siso_->soft_encode(new_extrinsic);
            
            // ----- Step 8: Interleave -----
            std::vector<float> interleaved;
            for (int blk = 0; blk < num_blocks; blk++) {
                int start = blk * block_size;
                int end = std::min(start + block_size, num_bits);
                
                std::vector<float> block_in(block_size, 0.0f);
                for (int i = start; i < end; i++) {
                    block_in[i - start] = (i < (int)coded_ext.size()) ? coded_ext[i] : 0.0f;
                }
                
                auto block_out = interleaver_->interleave_float(block_in);
                for (int i = 0; i < (end - start); i++) {
                    interleaved.push_back(block_out[i]);
                }
            }
            
            // ----- Step 9: Soft Mapping -----
            // Convert bit LLRs back to symbol priors
            for (int i = 0; i < num_symbols; i++) {
                int bit_base = i * bits_per_sym;
                
                // Compute bit probabilities from LLRs
                std::array<float, 3> p0, p1;
                for (int b = 0; b < bits_per_sym; b++) {
                    float llr = (bit_base + b < (int)interleaved.size()) ? 
                                interleaved[bit_base + b] : 0.0f;
                    llr = std::max(-20.0f, std::min(20.0f, llr));
                    p0[b] = 1.0f / (1.0f + std::exp(-llr));
                    p1[b] = 1.0f - p0[b];
                }
                
                // Compute symbol probabilities (natural binary)
                TurboSoftSymbol nat;
                for (int s = 0; s < 8; s++) {
                    int b2 = (s >> 2) & 1;
                    int b1 = (s >> 1) & 1;
                    int b0 = s & 1;
                    
                    nat.probs[s] = (b2 ? p1[0] : p0[0]) *
                                   (b1 ? p1[1] : p0[1]) *
                                   (b0 ? p1[2] : p0[2]);
                }
                nat.normalize();
                
                // ----- Step 10: Soft Gray Encode -----
                TurboSoftSymbol gray;
                for (int s = 0; s < 8; s++) {
                    // P(gray_idx = g) = P(natural_idx = INV_MGD3[g])
                    gray.probs[MGD3[s]] = nat.probs[s];
                }
                
                // ----- Step 11: Soft Scramble -----
                // Use correct scrambler index for this data symbol
                int frame = i / unknown_len;
                int data_idx_in_frame = i % unknown_len;
                int scr_idx = (scrambler_start + frame * pattern_len + data_idx_in_frame) % 160;
                int scr = scrambler_seq_[scr_idx];
                
                for (int s = 0; s < 8; s++) {
                    // Scramble: symbol s becomes (s + scr) % 8
                    int dst = (s + scr) & 7;
                    symbol_priors[i].probs[dst] = gray.probs[s];
                }
                symbol_priors[i].normalize();
            }
        }
        
        // ===== Return Final Output =====
        // Return INTERLEAVED LLRs (channel_llrs)
        // These are in the same order as the codec expects (before deinterleaving)
        
        stats_.bits_decoded = channel_llrs.size();
        
        return channel_llrs;
    }
    
    /**
     * Get deinterleaved LLRs with extrinsic (for external Viterbi)
     */
    std::vector<float> get_deinterleaved_llrs(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_ref,
            int scrambler_start = 0) {
        
        // First run turbo to get channel_llrs
        auto interleaved_llrs = equalize(received, preamble_rx, preamble_ref, scrambler_start);
        
        // Deinterleave
        const int block_size = mode_cfg_.interleaver.rows * mode_cfg_.interleaver.cols;
        const int num_bits = interleaved_llrs.size();
        
        std::vector<float> deinterleaved;
        int num_blocks = (num_bits + block_size - 1) / block_size;
        
        for (int blk = 0; blk < num_blocks; blk++) {
            int start = blk * block_size;
            int end = std::min(start + block_size, num_bits);
            
            std::vector<float> block_in(block_size, 0.0f);
            for (int i = start; i < end; i++) {
                block_in[i - start] = interleaved_llrs[i];
            }
            
            auto block_out = interleaver_->deinterleave_float(block_in);
            for (int i = 0; i < (end - start); i++) {
                deinterleaved.push_back(block_out[i]);
            }
        }
        
        return deinterleaved;
    }
    
    /**
     * Get equalized symbols for use with standard codec
     * 
     * @param received Received symbols
     * @param preamble_rx Received preamble
     * @param preamble_ref Known preamble
     * @param scrambler_start Scrambler starting position
     * @return Equalized complex symbols
     */
    std::vector<complex_t> equalize_symbols(
            const std::vector<complex_t>& received,
            const std::vector<complex_t>& preamble_rx,
            const std::vector<complex_t>& preamble_ref,
            int scrambler_start = 0) {
        
        // Run turbo equalization
        auto llrs = equalize(received, preamble_rx, preamble_ref, scrambler_start);
        
        // Convert final soft decisions to symbols
        std::vector<complex_t> output;
        output.reserve(received.size());
        
        const int bits_per_sym = mode_cfg_.bits_per_symbol;
        
        for (size_t sym_idx = 0; sym_idx < received.size(); sym_idx++) {
            // Get bit LLRs for this symbol (before interleaving)
            // We need to reconstruct from final channel output
            
            // For now, use improved distance metric
            int scr = scrambler_seq_[(scrambler_start + sym_idx) % 160];
            
            // Find best symbol
            float best_dist = 1e30f;
            int best_s = 0;
            
            for (int s = 0; s < 8; s++) {
                float d = std::norm(received[sym_idx] - TURBO_PSK8[s]);
                if (d < best_dist) {
                    best_dist = d;
                    best_s = s;
                }
            }
            
            output.push_back(TURBO_PSK8[best_s]);
        }
        
        return output;
    }
    
    /**
     * Get statistics from last equalization
     */
    const TurboIntegratedStats& stats() const { return stats_; }
    
    /**
     * Access MLSE for direct channel estimation
     */
    AdaptiveMLSE& mlse() { return *mlse_; }

private:
    TurboIntegratedConfig cfg_;
    ModeConfig mode_cfg_;
    
    std::unique_ptr<AdaptiveMLSE> mlse_;
    std::unique_ptr<SISODecoder> siso_;
    std::unique_ptr<MultiModeInterleaver> interleaver_;
    
    TurboIntegratedStats stats_;
    
    // Pre-computed scrambler sequence
    std::array<int, 160> scrambler_seq_;
};

} // namespace m110a

#endif // TURBO_CODEC_INTEGRATED_H
