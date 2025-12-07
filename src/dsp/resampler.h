#ifndef M110A_RESAMPLER_H
#define M110A_RESAMPLER_H

/**
 * Simple Integer Resampler
 * 
 * Provides decimation and interpolation for sample rate conversion.
 * Uses simple FIR anti-aliasing filter.
 */

#include "common/types.h"
#include "dsp/fir_filter.h"
#include <vector>
#include <cmath>

namespace m110a {

/**
 * Generate lowpass filter taps for resampling
 * Cutoff at fs_out/2 to prevent aliasing
 */
inline std::vector<float> generate_lowpass_taps(int num_taps, float cutoff_ratio) {
    std::vector<float> taps(num_taps);
    int M = num_taps - 1;
    float sum = 0.0f;
    
    for (int n = 0; n <= M; n++) {
        float x = n - M / 2.0f;
        float sinc = (std::abs(x) < 1e-6f) ? 1.0f : std::sin(PI * cutoff_ratio * x) / (PI * x);
        
        // Hamming window
        float window = 0.54f - 0.46f * std::cos(2.0f * PI * n / M);
        taps[n] = sinc * window * cutoff_ratio;
        sum += taps[n];
    }
    
    // Normalize
    for (auto& t : taps) t /= sum;
    
    return taps;
}

/**
 * Decimator - reduces sample rate by integer factor
 */
class Decimator {
public:
    /**
     * @param factor Decimation factor (e.g., 5 for 48000→9600)
     * @param filter_taps Number of anti-aliasing filter taps
     */
    Decimator(int factor, int filter_taps = 63)
        : factor_(factor), count_(0) {
        // Anti-aliasing filter: cutoff at 1/factor of input Nyquist
        float cutoff = 1.0f / factor_;
        auto taps = generate_lowpass_taps(filter_taps, cutoff);
        filter_ = std::make_unique<FirFilter<float>>(taps);
    }
    
    /**
     * Process input samples, return decimated output
     */
    std::vector<float> process(const std::vector<float>& input) {
        std::vector<float> output;
        output.reserve(input.size() / factor_ + 1);
        
        for (float s : input) {
            float filtered = filter_->process(s);
            if (++count_ >= factor_) {
                count_ = 0;
                output.push_back(filtered);
            }
        }
        return output;
    }
    
    /**
     * Process single sample, returns true when output available
     */
    bool process_sample(float in, float& out) {
        float filtered = filter_->process(in);
        if (++count_ >= factor_) {
            count_ = 0;
            out = filtered;
            return true;
        }
        return false;
    }
    
    void reset() {
        filter_->reset();
        count_ = 0;
    }
    
    int factor() const { return factor_; }

private:
    int factor_;
    int count_;
    std::unique_ptr<FirFilter<float>> filter_;
};

/**
 * Interpolator - increases sample rate by integer factor
 */
class Interpolator {
public:
    /**
     * @param factor Interpolation factor (e.g., 5 for 9600→48000)
     * @param filter_taps Number of anti-imaging filter taps
     */
    Interpolator(int factor, int filter_taps = 63)
        : factor_(factor) {
        // Anti-imaging filter
        float cutoff = 1.0f / factor_;
        auto taps = generate_lowpass_taps(filter_taps, cutoff);
        // Scale by factor to compensate for zero-stuffing
        for (auto& t : taps) t *= factor_;
        filter_ = std::make_unique<FirFilter<float>>(taps);
    }
    
    /**
     * Process input samples, return interpolated output
     */
    std::vector<float> process(const std::vector<float>& input) {
        std::vector<float> output;
        output.reserve(input.size() * factor_);
        
        for (float s : input) {
            // Insert sample followed by zeros
            output.push_back(filter_->process(s));
            for (int i = 1; i < factor_; i++) {
                output.push_back(filter_->process(0.0f));
            }
        }
        return output;
    }
    
    void reset() {
        filter_->reset();
    }
    
    int factor() const { return factor_; }

private:
    int factor_;
    std::unique_ptr<FirFilter<float>> filter_;
};

/**
 * Rational Resampler - P/Q rate conversion
 * Output rate = Input rate * P / Q
 */
class RationalResampler {
public:
    /**
     * @param up_factor Interpolation factor P
     * @param down_factor Decimation factor Q
     */
    RationalResampler(int up_factor, int down_factor, int filter_taps = 127)
        : up_(up_factor), down_(down_factor), phase_(0) {
        // Combined polyphase filter
        float cutoff = std::min(1.0f / up_factor, 1.0f / down_factor);
        auto taps = generate_lowpass_taps(filter_taps * up_factor, cutoff);
        for (auto& t : taps) t *= up_factor;
        filter_ = std::make_unique<FirFilter<float>>(taps);
    }
    
    std::vector<float> process(const std::vector<float>& input) {
        std::vector<float> output;
        
        for (float s : input) {
            // Interpolate
            for (int i = 0; i < up_; i++) {
                float filtered = filter_->process(i == 0 ? s : 0.0f);
                // Decimate
                if (++phase_ >= down_) {
                    phase_ = 0;
                    output.push_back(filtered);
                }
            }
        }
        return output;
    }
    
    void reset() {
        filter_->reset();
        phase_ = 0;
    }

private:
    int up_, down_, phase_;
    std::unique_ptr<FirFilter<float>> filter_;
};

} // namespace m110a

#endif // M110A_RESAMPLER_H
