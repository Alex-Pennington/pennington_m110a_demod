// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file iq_source.h
 * @brief I/Q input source - accepts complex samples from SDR or file
 * 
 * Handles format conversion, decimation, and resampling from SDR sample
 * rates (e.g., 2 MSPS) to modem rate (48 kHz).
 * 
 * Key insight: Everything downstream of "Complex Baseband 48kHz" stays
 * exactly the same. We're only adding an alternative front-end that
 * accepts complex samples directly (no Hilbert transform reconstruction).
 */

#ifndef M110A_API_IQ_SOURCE_H
#define M110A_API_IQ_SOURCE_H

#include "sample_source.h"
#include <vector>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <mutex>

namespace m110a {

/**
 * I/Q input source - accepts complex samples from SDR or file.
 * Handles format conversion, decimation, and resampling to 48kHz.
 */
class IQSource : public SampleSource {
public:
    /**
     * Input format specification
     */
    enum class Format {
        INT16_PLANAR,       ///< Separate int16_t I and Q arrays (phoenix_sdr native)
        INT16_INTERLEAVED,  ///< Interleaved int16_t I,Q,I,Q,... (.iqr files)
        FLOAT32_PLANAR,     ///< Separate float I and Q arrays
        FLOAT32_INTERLEAVED ///< Interleaved float I,Q,I,Q,... (GNU Radio)
    };
    
    /**
     * Construct I/Q source with specified input parameters.
     * 
     * @param input_rate_hz   Input sample rate (e.g., 2000000 for 2 MSPS)
     * @param format          Sample format
     * @param output_rate_hz  Target output rate (default 48000)
     */
    IQSource(double input_rate_hz, Format format, double output_rate_hz = 48000.0)
        : input_rate_hz_(input_rate_hz)
        , output_rate_hz_(output_rate_hz)
        , format_(format)
        , read_pos_(0)
        , write_pos_(0) {
        
        // Calculate decimation factor
        // For 2 MSPS → 48 kHz, ratio is 41.667
        // We'll use multi-stage decimation for efficiency
        setup_decimation();
    }
    
    /**
     * Push raw I/Q samples into the source (planar int16 format).
     * Called from SDR callback or file reader.
     * 
     * @param xi    I (real) samples
     * @param xq    Q (imaginary) samples  
     * @param count Number of samples
     */
    void push_samples_planar(const int16_t* xi, const int16_t* xq, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Convert to complex<float> and normalize to ±1.0
        constexpr float scale = 1.0f / 32768.0f;
        
        std::vector<std::complex<float>> input(count);
        for (size_t i = 0; i < count; ++i) {
            input[i] = std::complex<float>(xi[i] * scale, xq[i] * scale);
        }
        
        process_and_decimate(input);
    }
    
    /**
     * Push raw I/Q samples into the source (planar float format).
     * 
     * @param xi    I (real) samples
     * @param xq    Q (imaginary) samples  
     * @param count Number of samples
     */
    void push_samples_planar(const float* xi, const float* xq, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::complex<float>> input(count);
        for (size_t i = 0; i < count; ++i) {
            input[i] = std::complex<float>(xi[i], xq[i]);
        }
        
        process_and_decimate(input);
    }
    
    /**
     * Push raw I/Q samples (interleaved int16 format).
     * 
     * @param iq    Interleaved I,Q,I,Q,... samples
     * @param count Number of sample PAIRS
     */
    void push_samples_interleaved(const int16_t* iq, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        constexpr float scale = 1.0f / 32768.0f;
        
        std::vector<std::complex<float>> input(count);
        for (size_t i = 0; i < count; ++i) {
            input[i] = std::complex<float>(iq[2*i] * scale, iq[2*i + 1] * scale);
        }
        
        process_and_decimate(input);
    }
    
    /**
     * Push raw I/Q samples (interleaved float format).
     * 
     * @param iq    Interleaved I,Q,I,Q,... samples
     * @param count Number of sample PAIRS
     */
    void push_samples_interleaved(const float* iq, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::complex<float>> input(count);
        for (size_t i = 0; i < count; ++i) {
            input[i] = std::complex<float>(iq[2*i], iq[2*i + 1]);
        }
        
        process_and_decimate(input);
    }
    
    /**
     * Push pre-converted complex samples directly.
     * 
     * @param samples Complex samples at input rate
     * @param count Number of samples
     */
    void push_samples_complex(const std::complex<float>* samples, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::complex<float>> input(samples, samples + count);
        process_and_decimate(input);
    }
    
    /**
     * Read complex baseband samples at output rate (48kHz).
     */
    size_t read(std::complex<float>* out, size_t count) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t available = output_buffer_.size() - read_pos_;
        size_t to_read = std::min(count, available);
        
        if (to_read > 0) {
            std::copy(output_buffer_.begin() + read_pos_,
                      output_buffer_.begin() + read_pos_ + to_read,
                      out);
            read_pos_ += to_read;
        }
        
        // Compact buffer when fully read
        if (read_pos_ == output_buffer_.size()) {
            output_buffer_.clear();
            read_pos_ = 0;
        }
        
        return to_read;
    }
    
    double sample_rate() const override {
        return output_rate_hz_;
    }
    
    bool has_data() const override {
        return read_pos_ < output_buffer_.size();
    }
    
    const char* source_type() const override {
        return "iq";
    }
    
    void reset() override {
        std::lock_guard<std::mutex> lock(mutex_);
        output_buffer_.clear();
        read_pos_ = 0;
        
        // Reset decimation filter states
        for (auto& state : stage_states_) {
            std::fill(state.begin(), state.end(), std::complex<float>(0, 0));
        }
        for (auto& idx : stage_write_idx_) {
            idx = 0;
        }
        for (auto& cnt : stage_count_) {
            cnt = 0;
        }
        resample_phase_ = 0.0f;
    }
    
    /**
     * Get current center frequency (for display/logging).
     */
    double center_frequency() const { return center_freq_hz_; }
    
    /**
     * Get current bandwidth (for display/logging).
     */
    double bandwidth() const { return bandwidth_hz_; }
    
    /**
     * Set metadata from SDR (optional, for logging).
     */
    void set_metadata(double center_freq_hz, double bandwidth_hz) {
        center_freq_hz_ = center_freq_hz;
        bandwidth_hz_ = bandwidth_hz;
    }
    
    /**
     * Get input sample rate.
     */
    double input_rate() const { return input_rate_hz_; }
    
    /**
     * Get format.
     */
    Format format() const { return format_; }
    
    /**
     * Get number of samples available to read.
     */
    size_t samples_available() const {
        return output_buffer_.size() - read_pos_;
    }

private:
    // Configuration
    double input_rate_hz_;
    double output_rate_hz_;
    Format format_;
    double center_freq_hz_ = 0.0;
    double bandwidth_hz_ = 0.0;
    
    // Multi-stage decimation
    struct DecimStage {
        int factor;                         // Decimation factor for this stage
        std::vector<float> coeffs;          // FIR filter coefficients
    };
    std::vector<DecimStage> stages_;
    std::vector<std::vector<std::complex<float>>> stage_states_;  // Filter delay lines
    std::vector<size_t> stage_write_idx_;   // Circular buffer write indices
    std::vector<int> stage_count_;          // Decimation counters
    
    // Final resampling (for non-integer ratios)
    float resample_ratio_ = 1.0f;
    float resample_phase_ = 0.0f;
    std::complex<float> prev_sample_ = {0, 0};
    
    // Output buffer
    std::vector<std::complex<float>> output_buffer_;
    size_t read_pos_;
    size_t write_pos_;
    
    mutable std::mutex mutex_;
    
    /**
     * Setup multi-stage decimation based on input/output rates.
     * 
     * For 2 MSPS → 48 kHz (ratio 41.667):
     *   Stage 1: 2,000,000 → 250,000 Hz  (decimate by 8)
     *   Stage 2:   250,000 →  50,000 Hz  (decimate by 5)
     *   Stage 3:    50,000 →  48,000 Hz  (polyphase resample 48/50)
     * 
     * For other rates, we calculate appropriate factors.
     */
    void setup_decimation() {
        stages_.clear();
        stage_states_.clear();
        stage_write_idx_.clear();
        stage_count_.clear();
        
        double current_rate = input_rate_hz_;
        
        // Calculate total decimation needed
        double total_ratio = input_rate_hz_ / output_rate_hz_;
        
        if (total_ratio <= 1.0) {
            // No decimation needed (or interpolation required - not supported)
            resample_ratio_ = 1.0f;
            return;
        }
        
        // Try to find good integer factors
        // Common SDR rates: 2 MSPS, 1 MSPS, 500 kHz, 250 kHz
        
        // Stage 1: Large decimation (factor 8 if possible)
        if (total_ratio >= 8.0) {
            add_decim_stage(8, current_rate);
            current_rate /= 8.0;
        } else if (total_ratio >= 4.0) {
            add_decim_stage(4, current_rate);
            current_rate /= 4.0;
        } else if (total_ratio >= 2.0) {
            add_decim_stage(2, current_rate);
            current_rate /= 2.0;
        }
        
        // Stage 2: Medium decimation
        double remaining_ratio = current_rate / output_rate_hz_;
        if (remaining_ratio >= 5.0) {
            add_decim_stage(5, current_rate);
            current_rate /= 5.0;
        } else if (remaining_ratio >= 4.0) {
            add_decim_stage(4, current_rate);
            current_rate /= 4.0;
        } else if (remaining_ratio >= 2.0) {
            add_decim_stage(2, current_rate);
            current_rate /= 2.0;
        }
        
        // Stage 3: Final decimation if needed
        remaining_ratio = current_rate / output_rate_hz_;
        if (remaining_ratio >= 2.0) {
            int factor = static_cast<int>(remaining_ratio);
            if (factor >= 2) {
                add_decim_stage(factor, current_rate);
                current_rate /= factor;
            }
        }
        
        // Final resampling for non-integer remainder
        resample_ratio_ = static_cast<float>(current_rate / output_rate_hz_);
    }
    
    /**
     * Add a decimation stage.
     */
    void add_decim_stage(int factor, double input_rate) {
        DecimStage stage;
        stage.factor = factor;
        
        // Design lowpass filter for this stage
        // Cutoff at output_nyquist / input_rate
        int num_taps = 63;  // Reasonable filter length
        float cutoff = 0.8f / factor;  // 80% of Nyquist
        
        stage.coeffs = generate_lowpass_taps(num_taps, cutoff);
        
        stages_.push_back(stage);
        
        // Initialize filter state
        stage_states_.push_back(std::vector<std::complex<float>>(num_taps, {0, 0}));
        stage_write_idx_.push_back(0);
        stage_count_.push_back(0);
    }
    
    /**
     * Generate lowpass filter taps.
     */
    static std::vector<float> generate_lowpass_taps(int num_taps, float cutoff) {
        std::vector<float> taps(num_taps);
        int M = num_taps - 1;
        float sum = 0.0f;
        
        for (int n = 0; n <= M; n++) {
            float x = n - M / 2.0f;
            float sinc = (std::abs(x) < 1e-6f) ? 1.0f : 
                         std::sin(static_cast<float>(M_PI) * cutoff * x) / (static_cast<float>(M_PI) * x);
            
            // Hamming window
            float window = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * n / M);
            taps[n] = sinc * window * cutoff;
            sum += taps[n];
        }
        
        // Normalize
        for (auto& t : taps) t /= sum;
        
        return taps;
    }
    
    /**
     * Process input samples through decimation stages.
     */
    void process_and_decimate(const std::vector<std::complex<float>>& input) {
        if (stages_.empty()) {
            // No decimation needed, just copy to output
            output_buffer_.insert(output_buffer_.end(), input.begin(), input.end());
            return;
        }
        
        // Process through each decimation stage
        std::vector<std::complex<float>> current = input;
        
        for (size_t stage_idx = 0; stage_idx < stages_.size(); ++stage_idx) {
            current = decimate_stage(current, stage_idx);
        }
        
        // Apply final resampling if needed
        if (std::abs(resample_ratio_ - 1.0f) > 0.001f) {
            current = resample_final(current);
        }
        
        // Add to output buffer
        output_buffer_.insert(output_buffer_.end(), current.begin(), current.end());
    }
    
    /**
     * Apply one decimation stage.
     */
    std::vector<std::complex<float>> decimate_stage(
            const std::vector<std::complex<float>>& input,
            size_t stage_idx) {
        
        const auto& stage = stages_[stage_idx];
        auto& state = stage_states_[stage_idx];
        auto& write_idx = stage_write_idx_[stage_idx];
        auto& count = stage_count_[stage_idx];
        
        std::vector<std::complex<float>> output;
        output.reserve(input.size() / stage.factor + 1);
        
        for (const auto& sample : input) {
            // Write to circular buffer
            state[write_idx] = sample;
            
            // Increment count
            if (++count >= stage.factor) {
                count = 0;
                
                // Compute FIR output
                std::complex<float> sum(0, 0);
                size_t read_idx = write_idx;
                
                for (size_t i = 0; i < stage.coeffs.size(); ++i) {
                    sum += stage.coeffs[i] * state[read_idx];
                    if (read_idx == 0) {
                        read_idx = state.size() - 1;
                    } else {
                        --read_idx;
                    }
                }
                
                output.push_back(sum);
            }
            
            // Advance write pointer
            if (++write_idx >= state.size()) {
                write_idx = 0;
            }
        }
        
        return output;
    }
    
    /**
     * Apply final linear interpolation resampling for non-integer ratios.
     */
    std::vector<std::complex<float>> resample_final(
            const std::vector<std::complex<float>>& input) {
        
        std::vector<std::complex<float>> output;
        
        for (const auto& sample : input) {
            // Advance phase by resample ratio
            resample_phase_ += resample_ratio_;
            
            // Output samples while phase >= 1
            while (resample_phase_ >= 1.0f) {
                // Calculate interpolation fraction BEFORE decrement
                // frac represents how far between prev_sample_ and sample we are
                float frac = 1.0f - (resample_phase_ - 1.0f);
                resample_phase_ -= 1.0f;
                
                // Linear interpolation between prev and current
                std::complex<float> interpolated = 
                    prev_sample_ * frac + sample * (1.0f - frac);
                output.push_back(interpolated);
            }
            
            prev_sample_ = sample;
        }
        
        return output;
    }
};

} // namespace m110a

#endif // M110A_API_IQ_SOURCE_H
