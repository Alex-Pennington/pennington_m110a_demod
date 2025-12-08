/**
 * @file soft_demapper.h
 * @brief SNR-weighted soft demapper for 8-PSK
 * 
 * Computes proper Log-Likelihood Ratios (LLRs) based on:
 * - Actual Euclidean distance to constellation points
 * - Estimated channel SNR
 * - Gray-coded bit mapping
 */

#ifndef SOFT_DEMAPPER_H
#define SOFT_DEMAPPER_H

#include <vector>
#include <complex>
#include <cmath>
#include <array>
#include <algorithm>

namespace m110a {

using complex_t = std::complex<float>;
using soft_bit_t = int8_t;

/**
 * Gray code mapping tables
 */
namespace gray {
    // tribit (0-7) -> Gray code (constellation index)
    constexpr int MGD3[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    // constellation index -> tribit
    constexpr int INV_MGD3[8] = {0, 1, 3, 2, 6, 7, 5, 4};
}

/**
 * 8-PSK constellation points
 */
static const std::array<complex_t, 8> PSK8_CONSTELLATION = {{
    complex_t( 1.000f,  0.000f),  // 0: 0°
    complex_t( 0.707f,  0.707f),  // 1: 45°
    complex_t( 0.000f,  1.000f),  // 2: 90°
    complex_t(-0.707f,  0.707f),  // 3: 135°
    complex_t(-1.000f,  0.000f),  // 4: 180°
    complex_t(-0.707f, -0.707f),  // 5: 225°
    complex_t( 0.000f, -1.000f),  // 6: 270°
    complex_t( 0.707f, -0.707f)   // 7: 315°
}};

/**
 * Bit mapping for 8-PSK Gray code
 * constellation_idx -> which tribits have bit=1
 * 
 * Constellation  Gray   Tribit   Bit2 Bit1 Bit0
 *     0           0       0       0    0    0
 *     1           1       1       0    0    1
 *     2           3       2       0    1    0
 *     3           2       3       0    1    1
 *     4           7       6       1    1    0
 *     5           6       7       1    1    1
 *     6           4       4       1    0    0
 *     7           5       5       1    0    1
 */

/**
 * SNR-weighted soft demapper for 8-PSK
 */
class SNRWeightedDemapper8PSK {
public:
    /**
     * Set channel SNR for LLR calculation
     * @param snr_db Signal-to-noise ratio in dB
     */
    void set_snr(float snr_db) {
        snr_linear_ = std::pow(10.0f, snr_db / 10.0f);
        // Noise variance = 1/SNR (assuming unit signal power)
        sigma2_ = 1.0f / (snr_linear_ + 1e-10f);
        // Scale factor for int8 quantization
        // LLR range typically ±10, map to ±127
        scale_ = 127.0f / 10.0f;
    }
    
    /**
     * Set noise variance directly (alternative to SNR)
     * @param sigma2 Noise variance estimate
     */
    void set_noise_variance(float sigma2) {
        sigma2_ = sigma2;
        snr_linear_ = 1.0f / (sigma2 + 1e-10f);
        scale_ = 127.0f / 10.0f;
    }
    
    /**
     * Compute soft bits for a single 8-PSK symbol
     * Uses max-log-MAP approximation: LLR ≈ (d_min1² - d_min0²) / (2σ²)
     * 
     * @param sym Received symbol (after descrambling rotation)
     * @return 3 soft bits (MSB first: bit2, bit1, bit0)
     */
    std::array<soft_bit_t, 3> demap(complex_t sym) const {
        // Compute squared distances to all constellation points
        std::array<float, 8> dist2;
        for (int i = 0; i < 8; i++) {
            complex_t diff = sym - PSK8_CONSTELLATION[i];
            dist2[i] = std::norm(diff);
        }
        
        std::array<soft_bit_t, 3> soft;
        
        // For each bit position, find min distance where bit=0 and bit=1
        for (int bit = 0; bit < 3; bit++) {
            int bit_mask = 1 << (2 - bit);  // MSB first: bit2, bit1, bit0
            
            float min_dist0 = 1e30f;  // Min distance where bit=0
            float min_dist1 = 1e30f;  // Min distance where bit=1
            
            for (int i = 0; i < 8; i++) {
                int tribit = gray::INV_MGD3[i];  // Gray decode
                bool bit_val = (tribit & bit_mask) != 0;
                
                if (bit_val) {
                    min_dist1 = std::min(min_dist1, dist2[i]);
                } else {
                    min_dist0 = std::min(min_dist0, dist2[i]);
                }
            }
            
            // LLR = (d²_min1 - d²_min0) / (2σ²)
            // Positive LLR → bit more likely 0
            // Negative LLR → bit more likely 1
            float llr = (min_dist1 - min_dist0) / (2.0f * sigma2_);
            
            // Clamp and quantize to int8
            llr = std::clamp(llr * scale_, -127.0f, 127.0f);
            soft[bit] = static_cast<soft_bit_t>(llr);
        }
        
        return soft;
    }
    
    /**
     * Demap a vector of symbols with descrambling
     * 
     * @param symbols Received symbols (scrambled)
     * @param scrambler_values Scrambler outputs (0-7) for each symbol
     * @return Soft bits (3 per symbol)
     */
    std::vector<soft_bit_t> demap_sequence(
            const std::vector<complex_t>& symbols,
            const std::vector<int>& scrambler_values) const {
        
        std::vector<soft_bit_t> soft;
        soft.reserve(symbols.size() * 3);
        
        for (size_t i = 0; i < symbols.size(); i++) {
            // Descramble by rotating
            int scr = scrambler_values[i] & 7;
            float angle = -scr * (M_PI / 4.0f);
            complex_t rot(std::cos(angle), std::sin(angle));
            complex_t descrambled = symbols[i] * rot;
            
            // Demap
            auto bits = demap(descrambled);
            soft.push_back(bits[0]);
            soft.push_back(bits[1]);
            soft.push_back(bits[2]);
        }
        
        return soft;
    }
    
    /**
     * Estimate SNR from probe symbols
     * Uses known probe values to estimate noise variance
     * 
     * @param probes Received probe symbols
     * @param expected Expected probe values
     * @return Estimated SNR in dB
     */
    static float estimate_snr(const std::vector<complex_t>& probes,
                              const std::vector<complex_t>& expected) {
        if (probes.empty() || probes.size() != expected.size()) {
            return 20.0f;  // Default assumption
        }
        
        // Compute signal power and error power
        float signal_power = 0.0f;
        float error_power = 0.0f;
        
        for (size_t i = 0; i < probes.size(); i++) {
            signal_power += std::norm(expected[i]);
            complex_t error = probes[i] - expected[i];
            error_power += std::norm(error);
        }
        
        signal_power /= probes.size();
        error_power /= probes.size();
        
        float snr_linear = signal_power / (error_power + 1e-10f);
        return 10.0f * std::log10(snr_linear);
    }
    
private:
    float snr_linear_ = 100.0f;  // 20 dB default
    float sigma2_ = 0.01f;       // Noise variance
    float scale_ = 12.7f;        // Quantization scale
};

/**
 * QPSK soft demapper with SNR weighting
 */
class SNRWeightedDemapperQPSK {
public:
    void set_snr(float snr_db) {
        snr_linear_ = std::pow(10.0f, snr_db / 10.0f);
        sigma2_ = 1.0f / (snr_linear_ + 1e-10f);
        scale_ = 127.0f / 10.0f;
    }
    
    std::array<soft_bit_t, 2> demap(complex_t sym) const {
        // QPSK constellation: 0°, 90°, 180°, 270°
        static const std::array<complex_t, 4> QPSK = {{
            complex_t( 1.0f,  0.0f),  // 0: 0°   -> 00
            complex_t( 0.0f,  1.0f),  // 1: 90°  -> 01
            complex_t(-1.0f,  0.0f),  // 2: 180° -> 11
            complex_t( 0.0f, -1.0f)   // 3: 270° -> 10
        }};
        
        // Gray code: constellation -> dibit
        static const int GRAY[4] = {0, 1, 3, 2};
        
        std::array<float, 4> dist2;
        for (int i = 0; i < 4; i++) {
            dist2[i] = std::norm(sym - QPSK[i]);
        }
        
        std::array<soft_bit_t, 2> soft;
        
        for (int bit = 0; bit < 2; bit++) {
            int bit_mask = 1 << (1 - bit);
            
            float min_dist0 = 1e30f;
            float min_dist1 = 1e30f;
            
            for (int i = 0; i < 4; i++) {
                int dibit = GRAY[i];
                bool bit_val = (dibit & bit_mask) != 0;
                
                if (bit_val) {
                    min_dist1 = std::min(min_dist1, dist2[i]);
                } else {
                    min_dist0 = std::min(min_dist0, dist2[i]);
                }
            }
            
            float llr = (min_dist1 - min_dist0) / (2.0f * sigma2_);
            llr = std::clamp(llr * scale_, -127.0f, 127.0f);
            soft[bit] = static_cast<soft_bit_t>(llr);
        }
        
        return soft;
    }
    
private:
    float snr_linear_ = 100.0f;
    float sigma2_ = 0.01f;
    float scale_ = 12.7f;
};

/**
 * BPSK soft demapper with SNR weighting
 */
class SNRWeightedDemapperBPSK {
public:
    void set_snr(float snr_db) {
        snr_linear_ = std::pow(10.0f, snr_db / 10.0f);
        sigma2_ = 1.0f / (snr_linear_ + 1e-10f);
        scale_ = 127.0f / 10.0f;
    }
    
    soft_bit_t demap(complex_t sym) const {
        // BPSK: +1 = bit 0, -1 = bit 1
        // LLR = 2 * Re(sym) / σ²
        float llr = 2.0f * sym.real() / sigma2_;
        llr = std::clamp(llr * scale_, -127.0f, 127.0f);
        return static_cast<soft_bit_t>(llr);
    }
    
private:
    float snr_linear_ = 100.0f;
    float sigma2_ = 0.01f;
    float scale_ = 12.7f;
};

} // namespace m110a

#endif // SOFT_DEMAPPER_H
