/**
 * @file soft_mapper.h
 * @brief Soft 8-PSK Mapper for Turbo Equalization
 * 
 * Converts bit LLRs from decoder back to symbol probabilities
 * for use as a priori information in the next equalizer iteration.
 * 
 * This is the inverse of the soft demapper.
 */

#ifndef SOFT_MAPPER_TURBO_H
#define SOFT_MAPPER_TURBO_H

#include <array>
#include <vector>
#include <cmath>
#include <complex>
#include "src/dsp/soft_demapper_turbo.h"

namespace m110a {

class Soft8PSKMapper {
public:
    /**
     * Convert 3 bit LLRs to 8 symbol probabilities
     * 
     * P(s) = P(b2) * P(b1) * P(b0) where bits determined by Gray map
     * P(b=0) = 1 / (1 + exp(-LLR))
     * P(b=1) = 1 / (1 + exp(+LLR))
     * 
     * @param bit_llrs LLRs for (b2, b1, b0), positive = more likely 0
     * @return Probabilities for symbols 0-7 (sum to 1)
     */
    std::array<float, 8> map(const std::array<float, 3>& bit_llrs) {
        std::array<float, 8> probs;
        
        // Convert LLRs to bit probabilities
        std::array<float, 3> p0;  // P(bit = 0)
        std::array<float, 3> p1;  // P(bit = 1)
        
        for (int b = 0; b < 3; b++) {
            // Clip LLR to prevent overflow
            float llr = std::max(-20.0f, std::min(20.0f, bit_llrs[b]));
            
            // P(b=0) = sigmoid(LLR) = 1/(1 + exp(-LLR))
            // P(b=1) = 1 - P(b=0) = 1/(1 + exp(+LLR))
            float exp_neg = std::exp(-llr);
            float exp_pos = std::exp(llr);
            
            p0[b] = 1.0f / (1.0f + exp_neg);
            p1[b] = 1.0f / (1.0f + exp_pos);
        }
        
        // Compute symbol probabilities
        float sum = 0;
        for (int s = 0; s < 8; s++) {
            // P(s) = product of bit probabilities
            float p = 1.0f;
            for (int b = 0; b < 3; b++) {
                int bit_val = PSK8_TURBO_GRAY_MAP[s][b];
                p *= (bit_val == 0) ? p0[b] : p1[b];
            }
            probs[s] = p;
            sum += p;
        }
        
        // Normalize (should already sum to ~1, but ensure numerical stability)
        if (sum > 0) {
            for (int s = 0; s < 8; s++) {
                probs[s] /= sum;
            }
        }
        
        return probs;
    }
    
    /**
     * Batch mapping of bit LLR sequence to symbol probabilities
     * 
     * @param bit_llrs LLRs for all bits (3 per symbol)
     * @return Symbol probabilities for each symbol time
     */
    std::vector<std::array<float, 8>> map_sequence(const std::vector<float>& bit_llrs) {
        std::vector<std::array<float, 8>> result;
        result.reserve(bit_llrs.size() / 3);
        
        for (size_t i = 0; i + 2 < bit_llrs.size(); i += 3) {
            std::array<float, 3> llrs = {bit_llrs[i], bit_llrs[i+1], bit_llrs[i+2]};
            result.push_back(map(llrs));
        }
        
        return result;
    }
    
    /**
     * Convert bit LLRs to expected symbol (soft symbol)
     * Returns weighted average of constellation points
     * 
     * @param bit_llrs LLRs for (b2, b1, b0)
     * @return Expected complex symbol
     */
    std::complex<float> map_to_symbol(const std::array<float, 3>& bit_llrs) {
        auto probs = map(bit_llrs);
        
        // 8-PSK constellation
        static const std::array<std::complex<float>, 8> PSK8 = {{
            std::complex<float>( 1.000f,  0.000f),
            std::complex<float>( 0.707f,  0.707f),
            std::complex<float>( 0.000f,  1.000f),
            std::complex<float>(-0.707f,  0.707f),
            std::complex<float>(-1.000f,  0.000f),
            std::complex<float>(-0.707f, -0.707f),
            std::complex<float>( 0.000f, -1.000f),
            std::complex<float>( 0.707f, -0.707f)
        }};
        
        std::complex<float> expected(0, 0);
        for (int s = 0; s < 8; s++) {
            expected += probs[s] * PSK8[s];
        }
        
        return expected;
    }
};

} // namespace m110a

#endif // SOFT_MAPPER_TURBO_H
