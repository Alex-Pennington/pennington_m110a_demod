#ifndef M110A_TIMING_RECOVERY_H
#define M110A_TIMING_RECOVERY_H

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

namespace m110a {

/**
 * Polynomial interpolator for fractional sample delay
 * Uses Farrow structure with cubic interpolation
 */
class FarrowInterpolator {
public:
    FarrowInterpolator() {
        reset();
    }
    
    void reset() {
        buffer_.fill(complex_t(0.0f, 0.0f));
        write_idx_ = 0;
    }
    
    /**
     * Push a new sample into the interpolator
     */
    void push(complex_t sample) {
        buffer_[write_idx_] = sample;
        write_idx_ = (write_idx_ + 1) & 3;  // Circular buffer of 4
    }
    
    /**
     * Get interpolated sample at fractional delay mu (0 <= mu < 1)
     * mu = 0 returns the oldest sample, mu = 1 approaches the newest
     */
    complex_t interpolate(float mu) const {
        // Get samples in order: x[n-3], x[n-2], x[n-1], x[n]
        int idx = write_idx_;
        complex_t x0 = buffer_[idx];           // oldest (n-3)
        complex_t x1 = buffer_[(idx + 1) & 3]; // n-2
        complex_t x2 = buffer_[(idx + 2) & 3]; // n-1
        complex_t x3 = buffer_[(idx + 3) & 3]; // newest (n)
        
        // Cubic Lagrange interpolation coefficients
        // This interpolates between x1 and x2 with mu in [0,1)
        float mu2 = mu * mu;
        float mu3 = mu2 * mu;
        
        // Farrow coefficients for cubic interpolation
        float c0 = -1.0f/6.0f * mu3 + 1.0f/2.0f * mu2 - 1.0f/3.0f * mu;
        float c1 = 1.0f/2.0f * mu3 - mu2 - 1.0f/2.0f * mu + 1.0f;
        float c2 = -1.0f/2.0f * mu3 + 1.0f/2.0f * mu2 + mu;
        float c3 = 1.0f/6.0f * mu3 - 1.0f/6.0f * mu;
        
        return c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3;
    }

private:
    std::array<complex_t, 4> buffer_;
    int write_idx_;
};

/**
 * Gardner Timing Error Detector
 * 
 * The Gardner TED computes timing error from:
 *   e[n] = Re{ (x[n] - x[n-1]) * conj(x[n-0.5]) }
 * 
 * Where x[n] and x[n-1] are symbol samples and x[n-0.5] is
 * the midpoint sample between them.
 * 
 * Requires 2 samples per symbol to work optimally.
 */
class GardnerTED {
public:
    GardnerTED() {
        reset();
    }
    
    void reset() {
        prev_symbol_ = complex_t(0.0f, 0.0f);
        midpoint_ = complex_t(0.0f, 0.0f);
        has_prev_ = false;
    }
    
    /**
     * Compute timing error given current symbol and midpoint samples
     * @param symbol Current symbol sample
     * @param midpoint Sample at half-symbol point between prev and current
     * @return Timing error (positive = sample late, negative = sample early)
     */
    float compute(complex_t symbol, complex_t midpoint) {
        if (!has_prev_) {
            prev_symbol_ = symbol;
            midpoint_ = midpoint;
            has_prev_ = true;
            return 0.0f;
        }
        
        // Gardner TED: e = Re{ (x[n] - x[n-1]) * conj(x[n-0.5]) }
        complex_t diff = symbol - prev_symbol_;
        complex_t error_complex = diff * std::conj(midpoint);
        float error = error_complex.real();
        
        // Store for next iteration
        prev_symbol_ = symbol;
        midpoint_ = midpoint;
        
        return error;
    }

private:
    complex_t prev_symbol_;
    complex_t midpoint_;
    bool has_prev_;
};

/**
 * Second-order loop filter for timing recovery
 * Implements a PI (proportional-integral) controller
 */
class TimingLoopFilter {
public:
    struct Config {
        float bandwidth;      // Loop bandwidth (normalized to symbol rate)
        float damping;        // Damping factor (typically 0.707 for critical)
        
        Config() : bandwidth(0.01f), damping(0.707f) {}
    };
    
    explicit TimingLoopFilter(const Config& config = Config{}) {
        configure(config);
        reset();
    }
    
    void configure(const Config& config) {
        // Compute loop gains from bandwidth and damping
        // Using Gardner's formula for second-order loop
        float BnT = config.bandwidth;  // Normalized bandwidth
        float zeta = config.damping;
        
        float denom = 1.0f + 2.0f * zeta * BnT + BnT * BnT;
        
        // Proportional gain
        Kp_ = (4.0f * zeta * BnT) / denom;
        
        // Integral gain
        Ki_ = (4.0f * BnT * BnT) / denom;
    }
    
    void reset() {
        integrator_ = 0.0f;
    }
    
    /**
     * Filter timing error to produce timing adjustment
     * @param error Raw timing error from TED
     * @return Filtered timing adjustment
     */
    float filter(float error) {
        // Proportional path
        float prop = Kp_ * error;
        
        // Integral path
        integrator_ += Ki_ * error;
        
        // Limit integrator to prevent windup
        integrator_ = std::clamp(integrator_, -0.5f, 0.5f);
        
        return prop + integrator_;
    }
    
    float integrator() const { return integrator_; }

private:
    float Kp_;          // Proportional gain
    float Ki_;          // Integral gain
    float integrator_;  // Integrator state
};

/**
 * Complete Timing Recovery System
 * 
 * Combines interpolator, Gardner TED, and loop filter to provide
 * symbol-rate output from input samples.
 * 
 * IMPORTANT: For Gardner TED to work correctly, samples_per_symbol must be
 * small enough that the midpoint sample (SPS/2 samples back) is within
 * the 4-sample interpolator window. This means SPS <= 6 works reliably.
 * For higher SPS, decimate first!
 */
class TimingRecovery {
public:
    struct Config {
        float samples_per_symbol;
        float loop_bandwidth;
        float damping;
        
        // Legacy fields
        float sample_rate;
        float symbol_rate;
        float loop_damping;
        
        Config() 
            : samples_per_symbol(20.0f)  // Default to 48kHz/2400 baud for backward compatibility
            , loop_bandwidth(0.01f)
            , damping(0.707f)
            , sample_rate(48000.0f)
            , symbol_rate(2400.0f)
            , loop_damping(0.707f) {}
    };
    
    explicit TimingRecovery(const Config& config = Config{})
        : config_(config)
        , samples_per_symbol_(config.samples_per_symbol > 0 ? config.samples_per_symbol 
                              : config.sample_rate / config.symbol_rate)
        , mu_(0.0f)
        , strobe_(false)
        , sample_count_(0)
        , symbol_count_(0) {
        
        // Warn if SPS is too high for Gardner TED
        if (samples_per_symbol_ > 6.0f) {
            // High SPS - Gardner midpoint may be unreliable
            // Consider decimating before timing recovery
        }
        
        TimingLoopFilter::Config lf_config;
        lf_config.bandwidth = config.loop_bandwidth;
        lf_config.damping = config.damping > 0 ? config.damping : config.loop_damping;
        loop_filter_.configure(lf_config);
        
        reset();
    }
    
    void reset() {
        interpolator_.reset();
        ted_.reset();
        loop_filter_.reset();
        mu_ = 0.0f;
        strobe_ = false;
        sample_count_ = 0;
        symbol_count_ = 0;
        last_symbol_ = complex_t(0.0f, 0.0f);
        last_midpoint_ = complex_t(0.0f, 0.0f);
        sample_history_.fill(complex_t(0.0f, 0.0f));
        sample_history_idx_ = 0;
    }
    
    /**
     * Process one input sample
     * @param sample Complex baseband sample
     * @return true if a symbol was output (call get_symbol() to retrieve)
     */
    bool process(complex_t sample) {
        interpolator_.push(sample);
        sample_count_++;
        strobe_ = false;
        
        // Store sample for midpoint calculation
        // We need to track samples to find midpoint between symbols
        sample_history_[sample_history_idx_] = sample;
        sample_history_idx_ = (sample_history_idx_ + 1) % HISTORY_SIZE;
        
        // Timing NCO: advance by 1/sps each sample
        // When it wraps (crosses 1.0), we have a symbol strobe
        mu_ += 1.0f / samples_per_symbol_;
        
        if (mu_ >= 1.0f) {
            mu_ -= 1.0f;
            
            // Interpolate symbol sample (at the crossing point)
            last_symbol_ = interpolator_.interpolate(mu_);
            
            // For Gardner TED, we need the midpoint sample between this symbol and the previous one
            // The midpoint was SPS/2 samples ago
            // Look up the sample from history that was closest to the midpoint
            int midpoint_age = static_cast<int>(samples_per_symbol_ / 2.0f + 0.5f);
            int mid_idx = (sample_history_idx_ - midpoint_age + HISTORY_SIZE) % HISTORY_SIZE;
            last_midpoint_ = sample_history_[mid_idx];
            
            // Compute timing error
            float error = ted_.compute(last_symbol_, last_midpoint_);
            
            // Filter error and adjust timing
            float adjustment = loop_filter_.filter(error);
            
            // Apply adjustment to NCO frequency (affects when next symbol strobe occurs)
            // Positive error = late = need to speed up = add to mu
            mu_ += adjustment;
            
            // Keep mu in valid range
            while (mu_ < 0.0f) mu_ += 1.0f;
            while (mu_ >= 1.0f) mu_ -= 1.0f;
            
            strobe_ = true;
            symbol_count_++;
        }
        
        return strobe_;
    }
    
    /**
     * Process a block of samples
     * @param samples Input sample block
     * @param symbols Output symbol vector (appended to)
     * @return Number of symbols produced
     */
    int process_block(const std::vector<complex_t>& samples, 
                      std::vector<complex_t>& symbols) {
        int count = 0;
        for (const auto& s : samples) {
            if (process(s)) {
                symbols.push_back(last_symbol_);
                count++;
            }
        }
        return count;
    }
    
    /**
     * Get the last output symbol (valid after process() returns true)
     */
    complex_t get_symbol() const { return last_symbol_; }
    
    /**
     * Get current timing phase (mu)
     */
    float mu() const { return mu_; }
    
    /**
     * Get timing loop integrator (frequency offset estimate)
     */
    float frequency_offset() const { 
        return loop_filter_.integrator() * config_.symbol_rate; 
    }
    
    /**
     * Check if last process() produced a symbol
     */
    bool has_symbol() const { return strobe_; }
    
    /**
     * Get symbol count
     */
    int symbol_count() const { return symbol_count_; }
    
    /**
     * Get samples per symbol
     */
    float samples_per_symbol() const { return samples_per_symbol_; }
    
    /**
     * Manually set timing phase (for initial sync)
     */
    void set_mu(float mu) { mu_ = mu; }

private:
    Config config_;
    float samples_per_symbol_;
    
    FarrowInterpolator interpolator_;
    GardnerTED ted_;
    TimingLoopFilter loop_filter_;
    
    float mu_;              // Fractional timing phase [0, 1)
    bool strobe_;           // Symbol strobe flag
    int sample_count_;      // Total samples processed
    int symbol_count_;      // Total symbols output
    
    complex_t last_symbol_;
    complex_t last_midpoint_;
    
    // Sample history for midpoint lookup (needed for Gardner TED)
    static constexpr int HISTORY_SIZE = 32;  // Enough for SPS up to 60
    std::array<complex_t, HISTORY_SIZE> sample_history_;
    int sample_history_idx_ = 0;
};

} // namespace m110a

#endif // M110A_TIMING_RECOVERY_H
