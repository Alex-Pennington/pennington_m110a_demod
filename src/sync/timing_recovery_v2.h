#ifndef M110A_TIMING_RECOVERY_V2_H
#define M110A_TIMING_RECOVERY_V2_H

/**
 * Adaptive Timing Recovery V2
 * 
 * Designed for SPS=4 after decimation from 48kHz to 9600Hz.
 * Uses Gardner TED with proper midpoint tracking.
 * 
 * Features:
 * - Farrow cubic interpolator for fractional delay
 * - Gardner TED with explicit midpoint buffer
 * - Second-order loop filter (PI controller)
 * - Adaptive bandwidth for acquisition vs tracking
 * - Symbol strobe output with interpolated samples
 */

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

namespace m110a {

/**
 * Cubic Farrow Interpolator
 * Provides fractional delay interpolation using 4 samples
 */
class FarrowInterpolatorV2 {
public:
    FarrowInterpolatorV2() { reset(); }
    
    void reset() {
        buffer_.fill(complex_t(0.0f, 0.0f));
        idx_ = 0;
    }
    
    void push(complex_t sample) {
        buffer_[idx_] = sample;
        idx_ = (idx_ + 1) & 3;
    }
    
    /**
     * Interpolate at fractional delay mu (0 <= mu < 1)
     * mu=0 returns sample at index 1 (one sample ago)
     * mu=1 approaches sample at index 2 (current)
     */
    complex_t interpolate(float mu) const {
        // Get samples: x[n-3], x[n-2], x[n-1], x[n]
        complex_t x0 = buffer_[idx_];             // oldest (n-3)
        complex_t x1 = buffer_[(idx_ + 1) & 3];   // n-2
        complex_t x2 = buffer_[(idx_ + 2) & 3];   // n-1
        complex_t x3 = buffer_[(idx_ + 3) & 3];   // newest (n)
        
        // Cubic Lagrange interpolation
        float mu2 = mu * mu;
        float mu3 = mu2 * mu;
        
        float c0 = -mu3/6.0f + mu2/2.0f - mu/3.0f;
        float c1 = mu3/2.0f - mu2 - mu/2.0f + 1.0f;
        float c2 = -mu3/2.0f + mu2/2.0f + mu;
        float c3 = mu3/6.0f - mu/6.0f;
        
        return c0*x0 + c1*x1 + c2*x2 + c3*x3;
    }
    
    // Direct sample access for midpoint calculation
    complex_t get_sample(int delay) const {
        // delay=0 is newest, delay=3 is oldest
        int i = (idx_ + 3 - delay) & 3;
        return buffer_[i];
    }

private:
    std::array<complex_t, 4> buffer_;
    int idx_;
};

/**
 * Gardner Timing Error Detector with explicit midpoint tracking
 */
class GardnerTEDv2 {
public:
    GardnerTEDv2() { reset(); }
    
    void reset() {
        prev_symbol_ = complex_t(0.0f, 0.0f);
        has_prev_ = false;
    }
    
    /**
     * Compute timing error
     * @param symbol Current symbol sample
     * @param midpoint Sample at half-symbol point
     * @return Timing error (positive = late, negative = early)
     */
    float compute(complex_t symbol, complex_t midpoint) {
        if (!has_prev_) {
            prev_symbol_ = symbol;
            has_prev_ = true;
            return 0.0f;
        }
        
        // Gardner TED: e = Re{(x[n] - x[n-1]) * conj(x[n-0.5])}
        complex_t diff = symbol - prev_symbol_;
        float error = (diff * std::conj(midpoint)).real();
        
        prev_symbol_ = symbol;
        return error;
    }

private:
    complex_t prev_symbol_;
    bool has_prev_;
};

/**
 * Second-order timing loop filter
 */
class TimingLoopFilterV2 {
public:
    struct Config {
        float bandwidth = 0.01f;    // Normalized to symbol rate
        float damping = 0.707f;     // Critical damping
    };
    
    TimingLoopFilterV2() {
        configure(Config{});
        reset();
    }
    
    explicit TimingLoopFilterV2(const Config& cfg) {
        configure(cfg);
        reset();
    }
    
    void configure(const Config& cfg) {
        float BnT = cfg.bandwidth;
        float zeta = cfg.damping;
        float denom = 1.0f + 2.0f*zeta*BnT + BnT*BnT;
        
        Kp_ = (4.0f * zeta * BnT) / denom;
        Ki_ = (4.0f * BnT * BnT) / denom;
    }
    
    void reset() {
        integrator_ = 0.0f;
    }
    
    float filter(float error) {
        float prop = Kp_ * error;
        integrator_ += Ki_ * error;
        integrator_ = std::clamp(integrator_, -0.5f, 0.5f);
        return prop + integrator_;
    }
    
    float integrator() const { return integrator_; }
    void set_bandwidth(float bw) {
        Config cfg;
        cfg.bandwidth = bw;
        configure(cfg);
    }

private:
    float Kp_, Ki_;
    float integrator_;
};

/**
 * Adaptive Timing Recovery V2
 * 
 * Complete timing recovery with:
 * - Farrow interpolator for fractional timing
 * - Gardner TED with proper midpoint
 * - Adaptive loop bandwidth
 * - Timing lock detection
 */
class TimingRecoveryV2 {
public:
    struct Config {
        float samples_per_symbol = 4.0f;
        float acq_bandwidth = 0.005f;   // Conservative acquisition bandwidth
        float track_bandwidth = 0.002f; // Very narrow tracking bandwidth
        float damping = 0.707f;
        int lock_threshold = 50;        // Symbols to declare lock
        float error_threshold = 0.3f;   // Error threshold for lock
    };
    
    struct Stats {
        int samples_processed = 0;
        int symbols_output = 0;
        float timing_error_avg = 0.0f;
        float mu = 0.0f;
        bool locked = false;
    };
    
    TimingRecoveryV2() : TimingRecoveryV2(Config{}) {}
    
    explicit TimingRecoveryV2(const Config& cfg)
        : config_(cfg)
        , sps_(cfg.samples_per_symbol)
        , mu_(0.0f)
        , strobe_(false)
        , locked_(false)
        , lock_count_(0)
        , error_sum_(0.0f)
        , error_count_(0) {
        
        // Verify SPS is suitable for Gardner TED
        if (sps_ > 8.0f) {
            // Warning: Gardner TED may not work well at high SPS
        }
        
        // Configure loop filter for acquisition
        TimingLoopFilterV2::Config lf_cfg;
        lf_cfg.bandwidth = cfg.acq_bandwidth;
        lf_cfg.damping = cfg.damping;
        loop_filter_.configure(lf_cfg);
        
        // Initialize midpoint buffer
        midpoint_buffer_.resize(static_cast<int>(sps_) + 2, complex_t(0,0));
        mid_idx_ = 0;
        
        reset();
    }
    
    void reset() {
        interpolator_.reset();
        ted_.reset();
        loop_filter_.reset();
        
        mu_ = 0.0f;
        strobe_ = false;
        locked_ = false;
        lock_count_ = 0;
        error_sum_ = 0.0f;
        error_count_ = 0;
        stats_ = Stats{};
        
        std::fill(midpoint_buffer_.begin(), midpoint_buffer_.end(), complex_t(0,0));
        mid_idx_ = 0;
    }
    
    /**
     * Process one input sample
     * @return true if a symbol is ready (call get_symbol())
     */
    bool process(complex_t sample) {
        stats_.samples_processed++;
        strobe_ = false;
        
        // Push to interpolator
        interpolator_.push(sample);
        
        // Store in midpoint buffer for explicit midpoint lookup
        midpoint_buffer_[mid_idx_] = sample;
        mid_idx_ = (mid_idx_ + 1) % midpoint_buffer_.size();
        
        // Advance timing NCO
        mu_ += 1.0f / sps_;
        
        if (mu_ >= 1.0f) {
            mu_ -= 1.0f;
            
            // Interpolate symbol at current timing phase
            last_symbol_ = interpolator_.interpolate(mu_);
            
            // Get midpoint sample (SPS/2 samples ago)
            int mid_delay = static_cast<int>(sps_ / 2.0f + 0.5f);
            int mid_i = (mid_idx_ - mid_delay + midpoint_buffer_.size()) % midpoint_buffer_.size();
            complex_t midpoint = midpoint_buffer_[mid_i];
            
            // Compute timing error
            float error = ted_.compute(last_symbol_, midpoint);
            
            // Update error statistics
            error_sum_ += std::abs(error);
            error_count_++;
            
            // Filter and apply correction
            float adjustment = loop_filter_.filter(error);
            mu_ += adjustment;
            
            // Wrap mu
            while (mu_ < 0.0f) mu_ += 1.0f;
            while (mu_ >= 1.0f) mu_ -= 1.0f;
            
            // Check for lock
            update_lock_status(error);
            
            strobe_ = true;
            stats_.symbols_output++;
            stats_.mu = mu_;
        }
        
        return strobe_;
    }
    
    /**
     * Process block of samples
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
    
    // Accessors
    complex_t get_symbol() const { return last_symbol_; }
    bool has_symbol() const { return strobe_; }
    float mu() const { return mu_; }
    bool is_locked() const { return locked_; }
    const Stats& stats() const { return stats_; }
    
    // Manual timing adjustment
    void set_mu(float mu) { mu_ = mu; }
    void adjust_mu(float delta) { 
        mu_ += delta;
        while (mu_ < 0.0f) mu_ += 1.0f;
        while (mu_ >= 1.0f) mu_ -= 1.0f;
    }

private:
    Config config_;
    float sps_;
    
    FarrowInterpolatorV2 interpolator_;
    GardnerTEDv2 ted_;
    TimingLoopFilterV2 loop_filter_;
    
    // Midpoint buffer for explicit midpoint lookup
    std::vector<complex_t> midpoint_buffer_;
    size_t mid_idx_;
    
    float mu_;
    bool strobe_;
    complex_t last_symbol_;
    
    // Lock detection
    bool locked_;
    int lock_count_;
    float error_sum_;
    int error_count_;
    
    Stats stats_;
    
    void update_lock_status(float error) {
        // Check if error is small enough
        if (std::abs(error) < config_.error_threshold) {
            lock_count_++;
            if (lock_count_ >= config_.lock_threshold && !locked_) {
                locked_ = true;
                stats_.locked = true;
                
                // Switch to tracking bandwidth
                loop_filter_.set_bandwidth(config_.track_bandwidth);
            }
        } else {
            lock_count_ = std::max(0, lock_count_ - 2);
            if (lock_count_ == 0 && locked_) {
                locked_ = false;
                stats_.locked = false;
                
                // Switch back to acquisition bandwidth
                loop_filter_.set_bandwidth(config_.acq_bandwidth);
            }
        }
        
        // Update average error
        if (error_count_ > 0) {
            stats_.timing_error_avg = error_sum_ / error_count_;
        }
        
        // Reset counters periodically
        if (error_count_ >= 100) {
            error_sum_ /= 2;
            error_count_ /= 2;
        }
    }
};

/**
 * Mueller & Müller TED - Alternative for high SPS
 * 
 * Only needs consecutive symbols, no midpoint required.
 * Works at any SPS.
 */
class MuellerMullerTED {
public:
    MuellerMullerTED() { reset(); }
    
    void reset() {
        prev_symbol_ = complex_t(0.0f, 0.0f);
        prev_decision_ = complex_t(1.0f, 0.0f);
        has_prev_ = false;
    }
    
    /**
     * Compute timing error using M&M algorithm
     * e = Re{d[n-1]*conj(x[n]) - d[n]*conj(x[n-1])}
     */
    float compute(complex_t symbol, complex_t decision) {
        if (!has_prev_) {
            prev_symbol_ = symbol;
            prev_decision_ = decision;
            has_prev_ = true;
            return 0.0f;
        }
        
        // M&M TED
        complex_t term1 = prev_decision_ * std::conj(symbol);
        complex_t term2 = decision * std::conj(prev_symbol_);
        float error = (term1 - term2).real();
        
        prev_symbol_ = symbol;
        prev_decision_ = decision;
        
        return error;
    }

private:
    complex_t prev_symbol_;
    complex_t prev_decision_;
    bool has_prev_;
};

} // namespace m110a

#endif // M110A_TIMING_RECOVERY_V2_H
