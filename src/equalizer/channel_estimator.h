#ifndef M110A_CHANNEL_ESTIMATOR_H
#define M110A_CHANNEL_ESTIMATOR_H

/**
 * Channel Impulse Response (CIR) Estimator
 * 
 * Estimates the multipath channel from known training symbols.
 * Used by both DFE (for pre-training) and MLSE (for trellis).
 * 
 * Algorithm: Least Squares estimation
 *   Model: r[n] = sum_k h[k] * s[n-k] + noise
 *   Solution: h = (S^H * S + lambda*I)^(-1) * S^H * r
 * 
 * Where:
 *   r[n] = received symbols
 *   s[n] = transmitted (known) symbols  
 *   h[k] = channel taps to estimate
 *   lambda = regularization for noise robustness
 */

#include "common/types.h"
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace m110a {

/**
 * Channel Estimator Configuration
 */
struct ChannelEstimatorConfig {
    int num_taps = 5;              // Number of channel taps to estimate
    float regularization = 0.001f; // Tikhonov regularization (noise robustness)
    bool normalize = true;         // Normalize so |h[0]| = 1
};

/**
 * Channel estimation result
 */
struct ChannelEstimate {
    std::vector<complex_t> taps;   // Estimated channel taps
    float delay_spread = 0.0f;     // Delay spread in symbols
    int main_tap_index = 0;        // Index of main (strongest) tap
    float estimation_error = 0.0f; // RMS error on training data
    bool valid = false;            // True if estimation succeeded
};

/**
 * Channel Estimator Class
 */
class ChannelEstimator {
public:
    explicit ChannelEstimator(const ChannelEstimatorConfig& config = ChannelEstimatorConfig())
        : config_(config) {}
    
    /**
     * Estimate channel from known symbol pairs
     * 
     * @param received   Received symbols (after matched filter)
     * @param expected   Known transmitted symbols
     * @return Channel estimate with taps and diagnostics
     */
    ChannelEstimate estimate(const std::vector<complex_t>& received,
                             const std::vector<complex_t>& expected) {
        ChannelEstimate result;
        int L = config_.num_taps;
        int N = static_cast<int>(std::min(received.size(), expected.size()));
        
        // Need at least L symbols for estimation
        if (N < L + 10) {  // Require 10 extra for robustness
            result.valid = false;
            result.taps.assign(L, complex_t(0, 0));
            result.taps[0] = complex_t(1, 0);  // Default to identity
            return result;
        }
        
        // Build and solve normal equations: (S^H * S + lambda*I) * h = S^H * r
        std::vector<std::vector<complex_t>> SHS(L, std::vector<complex_t>(L, complex_t(0,0)));
        std::vector<complex_t> SHr(L, complex_t(0,0));
        
        // Accumulate matrices
        for (int n = L - 1; n < N; n++) {
            // Row of S: [s[n], s[n-1], ..., s[n-L+1]]
            for (int i = 0; i < L; i++) {
                complex_t si = expected[n - i];
                
                // Accumulate S^H * r
                SHr[i] += std::conj(si) * received[n];
                
                // Accumulate S^H * S
                for (int j = 0; j < L; j++) {
                    complex_t sj = expected[n - j];
                    SHS[i][j] += std::conj(si) * sj;
                }
            }
        }
        
        // Add regularization (Tikhonov)
        float lambda = config_.regularization * (N - L + 1);
        for (int i = 0; i < L; i++) {
            SHS[i][i] += complex_t(lambda, 0);
        }
        
        // Solve using Gaussian elimination with partial pivoting
        result.taps = solve_linear_system(SHS, SHr);
        
        if (result.taps.empty()) {
            // Solver failed
            result.valid = false;
            result.taps.assign(L, complex_t(0, 0));
            result.taps[0] = complex_t(1, 0);
            return result;
        }
        
        // Find main tap (strongest)
        float max_mag = 0.0f;
        for (int i = 0; i < L; i++) {
            float mag = std::abs(result.taps[i]);
            if (mag > max_mag) {
                max_mag = mag;
                result.main_tap_index = i;
            }
        }
        
        // Normalize if requested
        if (config_.normalize && max_mag > 0.001f) {
            complex_t scale = result.taps[result.main_tap_index];
            float scale_mag = std::abs(scale);
            for (auto& t : result.taps) {
                t /= scale_mag;  // Normalize magnitude only (preserve relative phase)
            }
        }
        
        // Compute delay spread (RMS width of channel power)
        result.delay_spread = compute_delay_spread(result.taps);
        
        // Compute estimation error on training data
        result.estimation_error = compute_error(received, expected, result.taps);
        
        result.valid = true;
        return result;
    }
    
    /**
     * Generate probe training symbols for preamble
     * Uses MIL-STD-188-110A preamble structure
     * 
     * @param num_symbols Number of symbols to generate (max 288 for common)
     * @return Expected symbol values
     */
    static std::vector<complex_t> generate_preamble_reference(int num_symbols);
    
private:
    ChannelEstimatorConfig config_;
    
    /**
     * Solve complex linear system Ax = b using Gaussian elimination
     */
    std::vector<complex_t> solve_linear_system(
        std::vector<std::vector<complex_t>>& A,
        std::vector<complex_t>& b) {
        
        int n = static_cast<int>(A.size());
        std::vector<complex_t> x(n);
        
        // Augmented matrix [A | b]
        std::vector<std::vector<complex_t>> aug(n, std::vector<complex_t>(n + 1));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                aug[i][j] = A[i][j];
            }
            aug[i][n] = b[i];
        }
        
        // Forward elimination with partial pivoting
        for (int col = 0; col < n; col++) {
            // Find pivot
            int pivot_row = col;
            float pivot_mag = std::abs(aug[col][col]);
            for (int row = col + 1; row < n; row++) {
                float mag = std::abs(aug[row][col]);
                if (mag > pivot_mag) {
                    pivot_mag = mag;
                    pivot_row = row;
                }
            }
            
            // Check for singular
            if (pivot_mag < 1e-10f) {
                return {};  // Singular matrix
            }
            
            // Swap rows
            if (pivot_row != col) {
                std::swap(aug[col], aug[pivot_row]);
            }
            
            // Eliminate
            for (int row = col + 1; row < n; row++) {
                complex_t factor = aug[row][col] / aug[col][col];
                for (int j = col; j <= n; j++) {
                    aug[row][j] -= factor * aug[col][j];
                }
            }
        }
        
        // Back substitution
        for (int i = n - 1; i >= 0; i--) {
            complex_t sum = aug[i][n];
            for (int j = i + 1; j < n; j++) {
                sum -= aug[i][j] * x[j];
            }
            x[i] = sum / aug[i][i];
        }
        
        return x;
    }
    
    /**
     * Compute delay spread (RMS width of power delay profile)
     */
    float compute_delay_spread(const std::vector<complex_t>& taps) {
        float total_power = 0.0f;
        float mean_delay = 0.0f;
        
        for (size_t i = 0; i < taps.size(); i++) {
            float power = std::norm(taps[i]);
            total_power += power;
            mean_delay += i * power;
        }
        
        if (total_power < 1e-10f) return 0.0f;
        mean_delay /= total_power;
        
        // RMS spread
        float rms = 0.0f;
        for (size_t i = 0; i < taps.size(); i++) {
            float power = std::norm(taps[i]);
            float diff = static_cast<float>(i) - mean_delay;
            rms += diff * diff * power;
        }
        rms = std::sqrt(rms / total_power);
        
        return rms;
    }
    
    /**
     * Compute RMS error between received and reconstructed signal
     */
    float compute_error(const std::vector<complex_t>& received,
                        const std::vector<complex_t>& expected,
                        const std::vector<complex_t>& taps) {
        int L = static_cast<int>(taps.size());
        int N = static_cast<int>(std::min(received.size(), expected.size()));
        
        float mse = 0.0f;
        int count = 0;
        
        for (int n = L - 1; n < N; n++) {
            // Compute expected received: sum_k h[k] * s[n-k]
            complex_t expected_rx(0, 0);
            for (int k = 0; k < L; k++) {
                expected_rx += taps[k] * expected[n - k];
            }
            
            complex_t error = received[n] - expected_rx;
            mse += std::norm(error);
            count++;
        }
        
        return (count > 0) ? std::sqrt(mse / count) : 0.0f;
    }
};

// Implementation of preamble reference generation
inline std::vector<complex_t> ChannelEstimator::generate_preamble_reference(int num_symbols) {
    // Use the MS-DMT preamble structure
    // The common segment uses p_c_seq to select different D patterns
    // This matches the structure in msdmt_preamble.h
    
    // 8-PSK constellation (0° at symbol 0) - MUST match msdmt_preamble.h!
    static const float psk8_i[8] = {
        1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f, 0.0f, 0.707107f
    };
    static const float psk8_q[8] = {
        0.0f, 0.707107f, 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f
    };
    
    // PSK symbol patterns (Walsh-like, 8x8) - from msdmt_preamble.h
    static const uint8_t psymbol[8][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},  // D0
        {0, 4, 0, 4, 0, 4, 0, 4},  // D1
        {0, 0, 4, 4, 0, 0, 4, 4},  // D2
        {0, 4, 4, 0, 0, 4, 4, 0},  // D3
        {0, 0, 0, 0, 4, 4, 4, 4},  // D4
        {0, 4, 0, 4, 4, 0, 4, 0},  // D5
        {0, 0, 4, 4, 4, 4, 0, 0},  // D6
        {0, 4, 4, 0, 4, 0, 0, 4}   // D7
    };
    
    // Common preamble sequence: which D value to use for each of 9 blocks
    static const uint8_t p_c_seq[9] = {0, 1, 3, 0, 1, 3, 1, 2, 0};
    
    // Preamble scrambler (32 values, repeating)
    static const uint8_t pscramble[32] = {
        7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
        5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
    };
    
    std::vector<complex_t> ref;
    ref.reserve(num_symbols);
    
    int scram_idx = 0;
    int sym_count = 0;
    
    // Generate 9 blocks of 32 symbols (288 total for common)
    for (int block = 0; block < 9 && sym_count < num_symbols; block++) {
        uint8_t d_val = p_c_seq[block];  // Get the D pattern for this block
        for (int i = 0; i < 32 && sym_count < num_symbols; i++) {
            uint8_t base = psymbol[d_val][i % 8];  // Use correct D pattern
            uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
            ref.push_back(complex_t(psk8_i[scrambled], psk8_q[scrambled]));
            scram_idx++;
            sym_count++;
        }
    }
    
    return ref;
}

} // namespace m110a

#endif // M110A_CHANNEL_ESTIMATOR_H
