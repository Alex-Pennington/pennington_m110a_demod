#ifndef M110A_FIR_FILTER_H
#define M110A_FIR_FILTER_H

#include "../common/types.h"
#include "../common/constants.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace m110a {

/**
 * Generic FIR Filter
 * 
 * Template supports both real and complex sample types.
 * Uses circular buffer for efficient sample storage.
 */
template<typename T>
class FirFilter {
public:
    /**
     * Create filter with given tap coefficients
     */
    explicit FirFilter(const std::vector<float>& taps);
    
    /**
     * Process one input sample, return filtered output
     */
    T process(T input);
    
    /**
     * Process block of samples in-place
     */
    void process_block(T* data, size_t count);
    
    /**
     * Process block, writing to output buffer
     */
    void process_block(const T* input, T* output, size_t count);
    
    /**
     * Reset filter state (clear delay line)
     */
    void reset();
    
    /**
     * Get filter length
     */
    size_t length() const { return taps_.size(); }
    
    /**
     * Get group delay in samples
     */
    size_t delay() const { return taps_.size() / 2; }
    
    /**
     * Get tap coefficients (read-only)
     */
    const std::vector<float>& taps() const { return taps_; }
    
private:
    std::vector<float> taps_;
    std::vector<T> buffer_;
    size_t write_idx_;
};

// ============================================================================
// FIR Filter Implementation
// ============================================================================

template<typename T>
FirFilter<T>::FirFilter(const std::vector<float>& taps)
    : taps_(taps)
    , buffer_(taps.size(), T{})
    , write_idx_(0) {
}

template<typename T>
T FirFilter<T>::process(T input) {
    // Write new sample to circular buffer
    buffer_[write_idx_] = input;
    
    // Convolve
    T output{};
    size_t read_idx = write_idx_;
    
    for (size_t i = 0; i < taps_.size(); i++) {
        output += taps_[i] * buffer_[read_idx];
        
        // Move backwards through circular buffer
        if (read_idx == 0) {
            read_idx = taps_.size() - 1;
        } else {
            read_idx--;
        }
    }
    
    // Advance write pointer
    write_idx_++;
    if (write_idx_ >= taps_.size()) {
        write_idx_ = 0;
    }
    
    return output;
}

template<typename T>
void FirFilter<T>::process_block(T* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        data[i] = process(data[i]);
    }
}

template<typename T>
void FirFilter<T>::process_block(const T* input, T* output, size_t count) {
    for (size_t i = 0; i < count; i++) {
        output[i] = process(input[i]);
    }
}

template<typename T>
void FirFilter<T>::reset() {
    std::fill(buffer_.begin(), buffer_.end(), T{});
    write_idx_ = 0;
}

// ============================================================================
// Filter Design Functions
// ============================================================================

/**
 * Generate Square Root Raised Cosine (SRRC) filter coefficients
 * 
 * @param alpha       Roll-off factor (0 < alpha <= 1), typically 0.35 for M110A
 * @param span        Filter span in symbols (each side)
 * @param sps         Samples per symbol
 * @return            Normalized filter coefficients
 */
inline std::vector<float> generate_srrc_taps(float alpha, int span, float sps) {
    int half_len = static_cast<int>(span * sps);
    int len = 2 * half_len + 1;
    std::vector<float> taps(len);
    
    float sum = 0.0f;
    
    for (int i = 0; i < len; i++) {
        float t = (i - half_len) / sps;  // Time in symbol periods
        float h;
        
        if (std::abs(t) < 1e-6f) {
            // t = 0: special case
            h = 1.0f - alpha + 4.0f * alpha / PI;
        }
        else if (std::abs(std::abs(t) - 1.0f / (4.0f * alpha)) < 1e-6f) {
            // t = ±1/(4α): special case
            float term1 = (1.0f + 2.0f / PI) * std::sin(PI / (4.0f * alpha));
            float term2 = (1.0f - 2.0f / PI) * std::cos(PI / (4.0f * alpha));
            h = alpha / std::sqrt(2.0f) * (term1 + term2);
        }
        else {
            // General case
            float num = std::sin(PI * t * (1.0f - alpha)) + 
                        4.0f * alpha * t * std::cos(PI * t * (1.0f + alpha));
            float den = PI * t * (1.0f - std::pow(4.0f * alpha * t, 2.0f));
            h = num / den;
        }
        
        taps[i] = h;
        sum += h * h;  // For energy normalization
    }
    
    // Normalize for unit energy
    float norm = std::sqrt(sum);
    for (auto& tap : taps) {
        tap /= norm;
    }
    
    return taps;
}

/**
 * Generate simple lowpass filter (windowed sinc)
 * 
 * @param cutoff      Cutoff frequency (normalized, 0 to 0.5)
 * @param num_taps    Number of taps (should be odd)
 * @return            Normalized filter coefficients
 */
inline std::vector<float> generate_lowpass_taps(float cutoff, int num_taps) {
    std::vector<float> taps(num_taps);
    int half = num_taps / 2;
    float sum = 0.0f;
    
    for (int i = 0; i < num_taps; i++) {
        int n = i - half;
        float h;
        
        if (n == 0) {
            h = 2.0f * cutoff;
        } else {
            h = std::sin(2.0f * PI * cutoff * n) / (PI * n);
        }
        
        // Hamming window
        float w = 0.54f - 0.46f * std::cos(2.0f * PI * i / (num_taps - 1));
        taps[i] = h * w;
        sum += taps[i];
    }
    
    // Normalize for unity DC gain
    for (auto& tap : taps) {
        tap /= sum;
    }
    
    return taps;
}

/**
 * Generate bandpass filter centered at given frequency
 * 
 * @param center      Center frequency (normalized, 0 to 0.5)
 * @param bandwidth   Bandwidth (normalized)
 * @param num_taps    Number of taps (should be odd)
 * @return            Filter coefficients
 */
inline std::vector<float> generate_bandpass_taps(float center, float bandwidth, int num_taps) {
    // Generate lowpass prototype
    auto lp = generate_lowpass_taps(bandwidth / 2.0f, num_taps);
    
    // Shift to center frequency
    int half = num_taps / 2;
    for (int i = 0; i < num_taps; i++) {
        int n = i - half;
        lp[i] *= 2.0f * std::cos(2.0f * PI * center * n);
    }
    
    return lp;
}

// Type aliases for common filter types
using RealFirFilter = FirFilter<sample_t>;
using ComplexFirFilter = FirFilter<complex_t>;

} // namespace m110a

#endif // M110A_FIR_FILTER_H
