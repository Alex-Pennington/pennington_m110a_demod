/**
 * @file soft_demapper.h
 * @brief Soft 8-PSK Demapper for Turbo Equalization
 * 
 * Converts received symbols or symbol probabilities to bit LLRs.
 * Supports a priori information feedback from decoder.
 * 
 * 8-PSK Gray Mapping (MIL-STD-188-110A):
 *   Symbol  Angle    Bits (b2,b1,b0)
 *     0      0°        0  0  0
 *     1     45°        0  0  1
 *     2     90°        0  1  1
 *     3    135°        0  1  0
 *     4    180°        1  1  0
 *     5    225°        1  1  1
 *     6    270°        1  0  1
 *     7    315°        1  0  0
 * 
 * Reference: Tosato & Bisaglia, "Simplified Soft-Output Demapper for 
 *            Binary Interleaved COFDM with Application to HIPERLAN/2"
 */

#ifndef SOFT_DEMAPPER_TURBO_H
#define SOFT_DEMAPPER_TURBO_H

#include <array>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace m110a {

using complex_t = std::complex<float>;

// 8-PSK constellation points
static const std::array<complex_t, 8> PSK8_TURBO_CONSTELLATION = {{
    complex_t( 1.000f,  0.000f),  // 0: 0°
    complex_t( 0.707f,  0.707f),  // 1: 45°
    complex_t( 0.000f,  1.000f),  // 2: 90°
    complex_t(-0.707f,  0.707f),  // 3: 135°
    complex_t(-1.000f,  0.000f),  // 4: 180°
    complex_t(-0.707f, -0.707f),  // 5: 225°
    complex_t( 0.000f, -1.000f),  // 6: 270°
    complex_t( 0.707f, -0.707f)   // 7: 315°
}};

// Gray mapping: symbol index → 3 bits (b2, b1, b0)
// This maps symbol to bits such that adjacent symbols differ by 1 bit
static const int PSK8_TURBO_GRAY_MAP[8][3] = {
    {0, 0, 0},  // symbol 0 → 000
    {0, 0, 1},  // symbol 1 → 001
    {0, 1, 1},  // symbol 2 → 011
    {0, 1, 0},  // symbol 3 → 010
    {1, 1, 0},  // symbol 4 → 110
    {1, 1, 1},  // symbol 5 → 111
    {1, 0, 1},  // symbol 6 → 101
    {1, 0, 0}   // symbol 7 → 100
};

// Reverse mapping: for each bit position, which symbols have bit=0 and bit=1
// Index order matches GRAY_MAP: [0]=b2, [1]=b1, [2]=b0
static const int BIT_TO_SYMBOLS_TURBO[3][2][4] = {
    // b2: symbols with b2=0: {0,1,2,3}, b2=1: {4,5,6,7}
    {{0, 1, 2, 3}, {4, 5, 6, 7}},
    // b1: symbols with b1=0: {0,1,6,7}, b1=1: {2,3,4,5}
    {{0, 1, 6, 7}, {2, 3, 4, 5}},
    // b0: symbols with b0=0: {0,3,4,7}, b0=1: {1,2,5,6}
    {{0, 3, 4, 7}, {1, 2, 5, 6}}
};

struct SoftDemapperConfig {
    float noise_variance = 0.1f;     // σ² for AWGN
    bool use_max_log = true;         // Use max-log approximation (faster)
    float llr_clip = 20.0f;          // Clip LLRs to prevent overflow
};

class Soft8PSKDemapper {
public:
    explicit Soft8PSKDemapper(const SoftDemapperConfig& cfg = SoftDemapperConfig())
        : cfg_(cfg) {}
    
    /**
     * Demap single received symbol to 3 bit LLRs
     * 
     * LLR(b) = log(P(b=0|r)) - log(P(b=1|r))
     *        = log(Σ P(r|s) for s where b=0) - log(Σ P(r|s) for s where b=1)
     * 
     * For AWGN: P(r|s) ∝ exp(-|r-s|²/σ²)
     * 
     * @param received Received complex symbol
     * @param apriori A priori LLRs for 3 bits (from decoder), or empty
     * @return LLRs for bits (b2, b1, b0), positive = more likely 0
     */
    std::array<float, 3> demap(complex_t received, 
                               const std::array<float, 3>& apriori = {0, 0, 0}) {
        std::array<float, 3> llr;
        
        // Compute distances to all constellation points
        std::array<float, 8> dist_sq;
        for (int s = 0; s < 8; s++) {
            complex_t diff = received - PSK8_TURBO_CONSTELLATION[s];
            dist_sq[s] = std::norm(diff);  // |r - s|²
        }
        
        // For each bit position
        for (int b = 0; b < 3; b++) {
            if (cfg_.use_max_log) {
                // Max-log approximation: use minimum distance only
                float min_dist_0 = 1e30f;
                float min_dist_1 = 1e30f;
                
                for (int i = 0; i < 4; i++) {
                    int s0 = BIT_TO_SYMBOLS_TURBO[b][0][i];  // Symbols with bit b = 0
                    int s1 = BIT_TO_SYMBOLS_TURBO[b][1][i];  // Symbols with bit b = 1
                    
                    // Include a priori from other bits
                    float metric_0 = dist_sq[s0];
                    float metric_1 = dist_sq[s1];
                    
                    // Add a priori contribution for other bits
                    for (int ob = 0; ob < 3; ob++) {
                        if (ob != b) {
                            int bit_val_0 = PSK8_TURBO_GRAY_MAP[s0][ob];
                            int bit_val_1 = PSK8_TURBO_GRAY_MAP[s1][ob];
                            // A priori: positive LLR means bit=0 more likely
                            // If bit=1, add -apriori (penalty for unlikely)
                            metric_0 += bit_val_0 ? -apriori[ob] * cfg_.noise_variance : 0;
                            metric_1 += bit_val_1 ? -apriori[ob] * cfg_.noise_variance : 0;
                        }
                    }
                    
                    min_dist_0 = std::min(min_dist_0, metric_0);
                    min_dist_1 = std::min(min_dist_1, metric_1);
                }
                
                // LLR = (min_dist_1 - min_dist_0) / σ²
                llr[b] = (min_dist_1 - min_dist_0) / cfg_.noise_variance;
            } else {
                // Full log-sum-exp (more accurate, slower)
                float sum_0 = 0, sum_1 = 0;
                
                for (int i = 0; i < 4; i++) {
                    int s0 = BIT_TO_SYMBOLS_TURBO[b][0][i];
                    int s1 = BIT_TO_SYMBOLS_TURBO[b][1][i];
                    
                    float exp_0 = std::exp(-dist_sq[s0] / cfg_.noise_variance);
                    float exp_1 = std::exp(-dist_sq[s1] / cfg_.noise_variance);
                    
                    // Include a priori
                    for (int ob = 0; ob < 3; ob++) {
                        if (ob != b) {
                            float p0 = 1.0f / (1.0f + std::exp(-apriori[ob]));
                            int bit_val_0 = PSK8_TURBO_GRAY_MAP[s0][ob];
                            int bit_val_1 = PSK8_TURBO_GRAY_MAP[s1][ob];
                            exp_0 *= bit_val_0 ? (1 - p0) : p0;
                            exp_1 *= bit_val_1 ? (1 - p0) : p0;
                        }
                    }
                    
                    sum_0 += exp_0;
                    sum_1 += exp_1;
                }
                
                llr[b] = std::log(sum_0 + 1e-30f) - std::log(sum_1 + 1e-30f);
            }
            
            // Clip to prevent overflow
            llr[b] = std::max(-cfg_.llr_clip, std::min(cfg_.llr_clip, llr[b]));
        }
        
        return llr;
    }
    
    /**
     * Demap from symbol probabilities (from soft MLSE output)
     * 
     * @param symbol_probs P(s=0), P(s=1), ..., P(s=7)
     * @return LLRs for 3 bits
     */
    std::array<float, 3> demap_probs(const std::array<float, 8>& symbol_probs) {
        std::array<float, 3> llr;
        
        for (int b = 0; b < 3; b++) {
            float sum_0 = 0, sum_1 = 0;
            
            for (int i = 0; i < 4; i++) {
                sum_0 += symbol_probs[BIT_TO_SYMBOLS_TURBO[b][0][i]];
                sum_1 += symbol_probs[BIT_TO_SYMBOLS_TURBO[b][1][i]];
            }
            
            llr[b] = std::log(sum_0 + 1e-30f) - std::log(sum_1 + 1e-30f);
            llr[b] = std::max(-cfg_.llr_clip, std::min(cfg_.llr_clip, llr[b]));
        }
        
        return llr;
    }
    
    /**
     * Batch demapping of symbol sequence
     * 
     * @param received Received symbols
     * @param apriori A priori LLRs (3 per symbol), or empty
     * @return LLRs for all bits (3 * received.size())
     */
    std::vector<float> demap_sequence(
            const std::vector<complex_t>& received,
            const std::vector<float>& apriori = {}) {
        
        std::vector<float> llrs;
        llrs.reserve(received.size() * 3);
        
        for (size_t i = 0; i < received.size(); i++) {
            std::array<float, 3> ap = {0, 0, 0};
            if (apriori.size() >= (i + 1) * 3) {
                ap[0] = apriori[i * 3 + 0];
                ap[1] = apriori[i * 3 + 1];
                ap[2] = apriori[i * 3 + 2];
            }
            
            auto bits = demap(received[i], ap);
            llrs.push_back(bits[0]);  // b2
            llrs.push_back(bits[1]);  // b1
            llrs.push_back(bits[2]);  // b0
        }
        
        return llrs;
    }
    
    /**
     * Batch demapping from symbol probability sequence
     */
    std::vector<float> demap_probs_sequence(
            const std::vector<std::array<float, 8>>& symbol_probs) {
        
        std::vector<float> llrs;
        llrs.reserve(symbol_probs.size() * 3);
        
        for (const auto& probs : symbol_probs) {
            auto bits = demap_probs(probs);
            llrs.push_back(bits[0]);
            llrs.push_back(bits[1]);
            llrs.push_back(bits[2]);
        }
        
        return llrs;
    }
    
    /**
     * Update noise variance estimate (can be called adaptively)
     */
    void set_noise_variance(float var) {
        cfg_.noise_variance = std::max(0.001f, var);
    }
    
    float noise_variance() const { return cfg_.noise_variance; }

private:
    SoftDemapperConfig cfg_;
};

} // namespace m110a

#endif // SOFT_DEMAPPER_TURBO_H
