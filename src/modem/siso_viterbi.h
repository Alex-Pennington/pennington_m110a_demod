/**
#include <cstdint>
 * @file siso_viterbi.h
 * @brief Soft-Input Soft-Output Decoder for Turbo Equalization
 * 
 * Implements the BCJR (Bahl-Cocke-Jelinek-Raviv) algorithm for
 * soft decoding of the MIL-STD-188-110A convolutional code.
 * 
 * Code parameters:
 *   K = 7 (constraint length)
 *   Rate = 1/2
 *   G1 = 0171 (octal) = 121 (decimal) = 1111001 (binary)
 *   G2 = 0133 (octal) = 91 (decimal)  = 1011011 (binary)
 * 
 * BCJR produces LLRs that can be decomposed into:
 *   L_out = L_apriori + L_channel + L_extrinsic
 * 
 * For turbo equalization, we extract L_extrinsic to feed back.
 * 
 * Reference: BCJR, "Optimal Decoding of Linear Codes", IEEE Trans IT, 1974
 */

#ifndef SISO_VITERBI_H
#define SISO_VITERBI_H

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace m110a {

struct SISOConfig {
    // MIL-STD-188-110A code
    int constraint_length = 7;    // K
    int poly_g1 = 0133;           // Octal: feedforward poly 1 (matches VITERBI_G1)
    int poly_g2 = 0171;           // Octal: feedforward poly 2 (matches VITERBI_G2)
    
    bool use_max_log = true;      // Max-log-MAP approximation (faster)
    float llr_clip = 50.0f;       // Clip extreme LLRs
    
    // Trellis has 2^(K-1) = 64 states
    int num_states() const { return 1 << (constraint_length - 1); }
};

class SISODecoder {
public:
    explicit SISODecoder(const SISOConfig& cfg = SISOConfig()) 
        : cfg_(cfg)
        , num_states_(cfg.num_states()) {
        build_trellis();
    }
    
    /**
     * BCJR decode: soft-in, soft-out
     * 
     * @param channel_llr LLRs from channel (2 per info bit: systematic + parity)
     *                    Format: [c0_0, c1_0, c0_1, c1_1, ...] 
     *                    where c0 = G1 output, c1 = G2 output
     * @param apriori_llr A priori LLRs for info bits (from equalizer feedback)
     *                    If empty, assumes uniform prior (all zeros)
     * @return Extrinsic LLRs for info bits (to feed back to equalizer)
     */
    std::vector<float> decode(const std::vector<float>& channel_llr,
                              const std::vector<float>& apriori_llr = {}) {
        
        int num_bits = channel_llr.size() / 2;
        if (num_bits == 0) return {};
        
        // Allocate alpha (forward) and beta (backward) metrics
        // alpha[t][s] = log P(state s at time t | past observations)
        std::vector<std::vector<float>> alpha(num_bits + 1, 
            std::vector<float>(num_states_, -1e30f));
        std::vector<std::vector<float>> beta(num_bits + 1,
            std::vector<float>(num_states_, -1e30f));
        
        // Initialize: start in state 0
        alpha[0][0] = 0.0f;
        
        // Initialize: end can be any state (or state 0 for terminated)
        for (int s = 0; s < num_states_; s++) {
            beta[num_bits][s] = 0.0f;  // Assume unterminated
        }
        
        // ========== Forward pass (compute alpha) ==========
        for (int t = 0; t < num_bits; t++) {
            // Get channel LLRs for this bit
            float lc0 = channel_llr[2 * t];      // G1 output LLR
            float lc1 = channel_llr[2 * t + 1];  // G2 output LLR
            
            // Get a priori for this info bit
            float la = (t < (int)apriori_llr.size()) ? apriori_llr[t] : 0.0f;
            
            for (int next_s = 0; next_s < num_states_; next_s++) {
                float max_metric = -1e30f;
                
                // For each possible previous state
                for (int prev_s = 0; prev_s < num_states_; prev_s++) {
                    for (int input = 0; input < 2; input++) {
                        if (next_state_[prev_s][input] != next_s) continue;
                        
                        // Branch metric
                        float gamma = compute_gamma(prev_s, input, lc0, lc1, la);
                        float metric = alpha[t][prev_s] + gamma;
                        
                        if (cfg_.use_max_log) {
                            max_metric = std::max(max_metric, metric);
                        } else {
                            // Log-sum-exp
                            if (max_metric < -1e29f) {
                                max_metric = metric;
                            } else {
                                max_metric = log_add(max_metric, metric);
                            }
                        }
                    }
                }
                
                alpha[t + 1][next_s] = max_metric;
            }
            
            // Normalize to prevent underflow
            normalize(alpha[t + 1]);
        }
        
        // ========== Backward pass (compute beta) ==========
        for (int t = num_bits - 1; t >= 0; t--) {
            float lc0 = channel_llr[2 * t];
            float lc1 = channel_llr[2 * t + 1];
            float la = (t < (int)apriori_llr.size()) ? apriori_llr[t] : 0.0f;
            
            for (int curr_s = 0; curr_s < num_states_; curr_s++) {
                float max_metric = -1e30f;
                
                for (int input = 0; input < 2; input++) {
                    int next_s = next_state_[curr_s][input];
                    float gamma = compute_gamma(curr_s, input, lc0, lc1, la);
                    float metric = beta[t + 1][next_s] + gamma;
                    
                    if (cfg_.use_max_log) {
                        max_metric = std::max(max_metric, metric);
                    } else {
                        if (max_metric < -1e29f) {
                            max_metric = metric;
                        } else {
                            max_metric = log_add(max_metric, metric);
                        }
                    }
                }
                
                beta[t][curr_s] = max_metric;
            }
            
            normalize(beta[t]);
        }
        
        // ========== Compute LLRs ==========
        std::vector<float> llr_out(num_bits);
        
        for (int t = 0; t < num_bits; t++) {
            float lc0 = channel_llr[2 * t];
            float lc1 = channel_llr[2 * t + 1];
            float la = (t < (int)apriori_llr.size()) ? apriori_llr[t] : 0.0f;
            
            float sum_0 = -1e30f;  // Log P(input=0 | all observations)
            float sum_1 = -1e30f;  // Log P(input=1 | all observations)
            
            for (int curr_s = 0; curr_s < num_states_; curr_s++) {
                for (int input = 0; input < 2; input++) {
                    int next_s = next_state_[curr_s][input];
                    float gamma = compute_gamma(curr_s, input, lc0, lc1, la);
                    float metric = alpha[t][curr_s] + gamma + beta[t + 1][next_s];
                    
                    if (input == 0) {
                        sum_0 = cfg_.use_max_log ? std::max(sum_0, metric) 
                                                 : log_add(sum_0, metric);
                    } else {
                        sum_1 = cfg_.use_max_log ? std::max(sum_1, metric)
                                                 : log_add(sum_1, metric);
                    }
                }
            }
            
            // LLR = log P(b=0) - log P(b=1)
            float llr = sum_0 - sum_1;
            llr = std::max(-cfg_.llr_clip, std::min(cfg_.llr_clip, llr));
            llr_out[t] = llr;
        }
        
        // ========== Extract extrinsic information ==========
        // L_extrinsic = L_out - L_apriori - L_channel_systematic
        // For non-systematic code, we just subtract apriori
        std::vector<float> extrinsic(num_bits);
        for (int t = 0; t < num_bits; t++) {
            float la = (t < (int)apriori_llr.size()) ? apriori_llr[t] : 0.0f;
            extrinsic[t] = llr_out[t] - la;
        }
        
        return extrinsic;
    }
    
    /**
     * Hard decision from LLRs
     */
    std::vector<uint8_t> hard_decide(const std::vector<float>& llrs) {
        std::vector<uint8_t> bits;
        bits.reserve(llrs.size());
        for (float l : llrs) {
            bits.push_back(l >= 0 ? 0 : 1);
        }
        return bits;
    }
    
    /**
     * Get full APP (a posteriori probability) LLRs
     * Use for final decoding (not for turbo feedback)
     */
    std::vector<float> decode_app(const std::vector<float>& channel_llr,
                                  const std::vector<float>& apriori_llr = {}) {
        // Run BCJR but return full LLR (not extrinsic)
        auto extrinsic = decode(channel_llr, apriori_llr);
        
        // Add back apriori to get APP
        std::vector<float> app(extrinsic.size());
        for (size_t i = 0; i < app.size(); i++) {
            float la = (i < apriori_llr.size()) ? apriori_llr[i] : 0.0f;
            app[i] = extrinsic[i] + la;
        }
        return app;
    }

private:
    SISOConfig cfg_;
    int num_states_;
    
    // Trellis structure
    std::vector<std::array<int, 2>> next_state_;    // [state][input] → next state
    std::vector<std::array<int, 2>> output_c0_;     // [state][input] → G1 output (0 or 1)
    std::vector<std::array<int, 2>> output_c1_;     // [state][input] → G2 output (0 or 1)
    
    void build_trellis() {
        next_state_.resize(num_states_);
        output_c0_.resize(num_states_);
        output_c1_.resize(num_states_);
        
        // K=7 encoder: 7-bit register, state = bits [6:1]
        // After shift: new_state = (old_state >> 1) | (input << (K-2))
        // Full register for output computation: (input << (K-1)) | old_state
        //   BUT we need to match MS-DMT convention: input at bit 6
        
        for (int state = 0; state < num_states_; state++) {
            for (int input = 0; input < 2; input++) {
                // MS-DMT: state >> 1, then input enters at bit 6
                // next_state = (state >> 1) | (input << (K-2))
                next_state_[state][input] = (state >> 1) | (input << (cfg_.constraint_length - 2));
                
                // For output computation, form full 7-bit register
                // The full register is [input, state[5:1], state[0]] but the way 
                // MS-DMT stores state, state[5:0] are the 6 bits after the input.
                // Actually: state = bits [5:0], input goes to bit 6
                int full_reg = (input << (cfg_.constraint_length - 1)) | state;
                
                // Compute outputs using generator polynomials
                output_c0_[state][input] = parity(full_reg & cfg_.poly_g1);
                output_c1_[state][input] = parity(full_reg & cfg_.poly_g2);
            }
        }
    }
    
    static int parity(int x) {
        int p = 0;
        while (x) {
            p ^= (x & 1);
            x >>= 1;
        }
        return p;
    }
    
    /**
     * Compute branch metric (gamma) in log domain
     * 
     * gamma(s, s', u) = log P(u) + log P(c|u)
     *                 = (la/2) * (1 - 2*u) + sum of channel contributions
     */
    float compute_gamma(int state, int input, float lc0, float lc1, float la) const {
        int c0 = output_c0_[state][input];
        int c1 = output_c1_[state][input];
        
        // For BPSK: P(r|c) ∝ exp(r * c) where c ∈ {-1, +1}
        // LLR = log(P(c=0)/P(c=1)) = log(P(r|c=0)/P(r|c=1))
        // So contribution is (lc/2) * (1 - 2*c) where c is encoded bit
        
        float gamma = 0.0f;
        
        // A priori contribution for input bit
        // la > 0 means input=0 more likely
        gamma += (la / 2.0f) * (1 - 2 * input);
        
        // Channel contribution for c0
        gamma += (lc0 / 2.0f) * (1 - 2 * c0);
        
        // Channel contribution for c1
        gamma += (lc1 / 2.0f) * (1 - 2 * c1);
        
        return gamma;
    }
    
    /**
     * Log-sum-exp: log(exp(a) + exp(b))
     */
    static float log_add(float a, float b) {
        if (a > b) {
            return a + std::log1p(std::exp(b - a));
        } else {
            return b + std::log1p(std::exp(a - b));
        }
    }
    
    /**
     * Normalize log probabilities (subtract max to prevent overflow)
     */
    void normalize(std::vector<float>& v) const {
        float max_val = *std::max_element(v.begin(), v.end());
        if (max_val > -1e20f) {
            for (auto& x : v) x -= max_val;
        }
    }
    
public:
    /**
     * Soft re-encode info bit LLRs to coded bit LLRs
     * 
     * For turbo equalization feedback, we need to convert extrinsic
     * information on info bits back to extrinsic on coded bits.
     * 
     * This uses a forward-only soft encoder approximation.
     * For each info bit with LLR L_u:
     *   - Compute P(u=0) = 1/(1+exp(-L_u))
     *   - For each possible state s with probability P(s):
     *     - If u=0: output c0=output_c0_[s][0], c1=output_c1_[s][0]
     *     - If u=1: output c0=output_c0_[s][1], c1=output_c1_[s][1]
     *   - L_c0 = log(P(c0=0)/P(c0=1))
     * 
     * Simplified version: assume uniform state distribution and use 
     * the correlation between info and coded bits.
     * 
     * @param info_llr Extrinsic LLRs for info bits
     * @return LLRs for coded bits [c0_0, c1_0, c0_1, c1_1, ...]
     */
    std::vector<float> soft_encode(const std::vector<float>& info_llr) {
        int num_bits = info_llr.size();
        std::vector<float> coded_llr(num_bits * 2, 0.0f);
        
        // Forward pass with soft state probabilities
        std::vector<float> state_prob(num_states_, 0.0f);
        state_prob[0] = 1.0f;  // Start in state 0
        
        for (int t = 0; t < num_bits; t++) {
            float L_u = info_llr[t];
            float p0 = 1.0f / (1.0f + std::exp(-L_u));  // P(u=0)
            float p1 = 1.0f - p0;                        // P(u=1)
            
            // Accumulate probabilities for c0 and c1
            float p_c0_is_0 = 0.0f, p_c0_is_1 = 0.0f;
            float p_c1_is_0 = 0.0f, p_c1_is_1 = 0.0f;
            
            std::vector<float> next_state_prob(num_states_, 0.0f);
            
            for (int s = 0; s < num_states_; s++) {
                if (state_prob[s] < 1e-10f) continue;
                
                // Transition with input=0
                {
                    float prob = state_prob[s] * p0;
                    int c0 = output_c0_[s][0];
                    int c1 = output_c1_[s][0];
                    int ns = next_state_[s][0];
                    
                    if (c0 == 0) p_c0_is_0 += prob;
                    else p_c0_is_1 += prob;
                    
                    if (c1 == 0) p_c1_is_0 += prob;
                    else p_c1_is_1 += prob;
                    
                    next_state_prob[ns] += prob;
                }
                
                // Transition with input=1
                {
                    float prob = state_prob[s] * p1;
                    int c0 = output_c0_[s][1];
                    int c1 = output_c1_[s][1];
                    int ns = next_state_[s][1];
                    
                    if (c0 == 0) p_c0_is_0 += prob;
                    else p_c0_is_1 += prob;
                    
                    if (c1 == 0) p_c1_is_0 += prob;
                    else p_c1_is_1 += prob;
                    
                    next_state_prob[ns] += prob;
                }
            }
            
            // Compute LLRs for coded bits
            coded_llr[2*t] = std::log((p_c0_is_0 + 1e-10f) / (p_c0_is_1 + 1e-10f));
            coded_llr[2*t + 1] = std::log((p_c1_is_0 + 1e-10f) / (p_c1_is_1 + 1e-10f));
            
            // Clip
            coded_llr[2*t] = std::max(-cfg_.llr_clip, std::min(cfg_.llr_clip, coded_llr[2*t]));
            coded_llr[2*t + 1] = std::max(-cfg_.llr_clip, std::min(cfg_.llr_clip, coded_llr[2*t + 1]));
            
            // Update state probabilities
            state_prob = next_state_prob;
            
            // Normalize state probabilities
            float sum = 0;
            for (float p : state_prob) sum += p;
            if (sum > 0) {
                for (float& p : state_prob) p /= sum;
            }
        }
        
        return coded_llr;
    }
};

} // namespace m110a

#endif // SISO_VITERBI_H
