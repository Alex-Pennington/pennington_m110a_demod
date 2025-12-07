#ifndef M110A_WATTERSON_H
#define M110A_WATTERSON_H

/**
 * Watterson HF Channel Simulator
 * 
 * Implements the classic two-path Rayleigh fading model with Gaussian
 * Doppler spectrum, as described in:
 * 
 * Watterson, Juroshek, Bensema - "Experimental Confirmation of an 
 * HF Channel Model", IEEE Trans. Comm., 1970
 * 
 * Key features:
 * - Two independent Rayleigh fading paths
 * - Gaussian Doppler spectrum (configurable spread 0.1-10 Hz)
 * - Differential delay between paths (0-10 ms)
 * - CCIR/ITU standard channel profiles
 */

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <random>
#include <cmath>
#include <complex>
#include <sstream>

namespace m110a {

// ============================================================================
// Phase 1: Gaussian Doppler Filter
// ============================================================================

/**
 * IIR filter that shapes white noise to have Gaussian Doppler spectrum.
 * 
 * The Gaussian PSD is: G(f) = exp(-f²/(2σ²))
 * where σ = spread_hz / 2.35 (so spread_hz is the -3dB bandwidth)
 * 
 * We use a 2nd-order lowpass IIR as approximation:
 *   H(s) = ω₀² / (s² + √2·ω₀·s + ω₀²)  [Butterworth]
 * 
 * The filter is applied to complex Gaussian noise to generate
 * Rayleigh fading tap coefficients.
 */
class GaussianDopplerFilter {
public:
    /**
     * Create Doppler filter
     * @param spread_hz  Doppler spread (-3dB bandwidth), typically 0.1-10 Hz
     * @param update_rate_hz  Rate at which filter is clocked (typically 100-1000 Hz)
     */
    GaussianDopplerFilter(float spread_hz, float update_rate_hz)
        : spread_hz_(spread_hz)
        , update_rate_(update_rate_hz) {
        design_filter();
        reset();
    }
    
    /**
     * Process one sample of complex Gaussian noise
     * @param input  Complex Gaussian noise sample (I + jQ, unit variance)
     * @return Filtered complex sample with Gaussian Doppler spectrum
     */
    complex_t process(complex_t input) {
        // Direct Form II Transposed for numerical stability
        complex_t output = b0_ * input + state1_;
        state1_ = b1_ * input - a1_ * output + state2_;
        state2_ = b2_ * input - a2_ * output;
        return output;
    }
    
    /**
     * Reset filter state
     */
    void reset() {
        state1_ = complex_t(0, 0);
        state2_ = complex_t(0, 0);
    }
    
    /**
     * Get filter coefficients for verification
     */
    void get_coefficients(float& b0, float& b1, float& b2,
                          float& a1, float& a2) const {
        b0 = b0_; b1 = b1_; b2 = b2_;
        a1 = a1_; a2 = a2_;
    }
    
    float spread_hz() const { return spread_hz_; }
    float update_rate() const { return update_rate_; }

private:
    float spread_hz_;
    float update_rate_;
    
    // IIR coefficients (normalized, a0 = 1)
    float b0_, b1_, b2_;
    float a1_, a2_;
    
    // Filter state
    complex_t state1_, state2_;
    
    void design_filter() {
        // Design 2nd-order Butterworth lowpass via bilinear transform
        // Cutoff frequency = spread_hz (this gives approximately Gaussian shape)
        
        // Handle edge case of very low spread
        float fc = std::max(spread_hz_, 0.01f);
        
        // Pre-warp cutoff frequency for bilinear transform
        float wc = 2.0f * update_rate_ * std::tan(PI * fc / update_rate_);
        
        // Butterworth 2nd order analog prototype: H(s) = 1/(s² + √2·s + 1)
        // Scale to cutoff: H(s) = wc²/(s² + √2·wc·s + wc²)
        
        // Bilinear transform: s = 2*fs*(z-1)/(z+1)
        float k = 2.0f * update_rate_;
        float k2 = k * k;
        float wc2 = wc * wc;
        float sqrt2_wc_k = std::sqrt(2.0f) * wc * k;
        
        // Denominator: (s² + √2·wc·s + wc²) with s = k(z-1)/(z+1)
        // = k²(z-1)² + √2·wc·k(z-1)(z+1) + wc²(z+1)²
        // Expand and collect powers of z:
        float a0 = k2 + sqrt2_wc_k + wc2;
        a1_ = (2.0f * wc2 - 2.0f * k2) / a0;
        a2_ = (k2 - sqrt2_wc_k + wc2) / a0;
        
        // Numerator: wc² with same substitution
        // = wc²(z+1)² = wc²(z² + 2z + 1)
        b0_ = wc2 / a0;
        b1_ = 2.0f * wc2 / a0;
        b2_ = wc2 / a0;
        
        // Normalize for unity DC gain (optional, helps with amplitude)
        // DC gain = (b0 + b1 + b2) / (1 + a1 + a2)
        float dc_gain = (b0_ + b1_ + b2_) / (1.0f + a1_ + a2_);
        b0_ /= dc_gain;
        b1_ /= dc_gain;
        b2_ /= dc_gain;
    }
};

/**
 * Compute frequency response magnitude of the Doppler filter
 * for verification purposes.
 */
inline std::vector<float> doppler_filter_response(
    const GaussianDopplerFilter& filter,
    int num_points = 256) 
{
    float b0, b1, b2, a1, a2;
    filter.get_coefficients(b0, b1, b2, a1, a2);
    
    std::vector<float> response(num_points);
    float fs = filter.update_rate();
    
    for (int i = 0; i < num_points; i++) {
        float f = (i * fs / 2.0f) / num_points;  // 0 to fs/2
        float w = 2.0f * PI * f / fs;  // Normalized frequency
        
        // H(e^jw) = (b0 + b1*e^-jw + b2*e^-2jw) / (1 + a1*e^-jw + a2*e^-2jw)
        complex_t ejw(std::cos(w), -std::sin(w));
        complex_t ej2w(std::cos(2*w), -std::sin(2*w));
        
        complex_t num = complex_t(b0, 0) + complex_t(b1, 0) * ejw + complex_t(b2, 0) * ej2w;
        complex_t den = complex_t(1, 0) + complex_t(a1, 0) * ejw + complex_t(a2, 0) * ej2w;
        
        response[i] = std::abs(num / den);
    }
    
    return response;
}

// ============================================================================
// Phase 2: Rayleigh Fading Generator
// ============================================================================

/**
 * Generates Rayleigh fading tap coefficients with Gaussian Doppler spectrum.
 * 
 * Output is a complex coefficient where:
 * - Magnitude follows Rayleigh distribution
 * - Phase is uniformly distributed [0, 2π]
 * - Temporal correlation follows Gaussian Doppler spectrum
 * - Average power is normalized to unity (0 dB)
 */
class RayleighFadingGenerator {
public:
    /**
     * Create fading generator
     * @param spread_hz  Doppler spread in Hz
     * @param update_rate_hz  Tap update rate in Hz
     * @param seed  Random seed for reproducibility
     */
    RayleighFadingGenerator(float spread_hz, float update_rate_hz, unsigned int seed = 42)
        : filter_(spread_hz, update_rate_hz)
        , rng_(seed)
        , normal_dist_(0.0f, 1.0f)
        , power_sum_(0)
        , sample_count_(0)
        , gain_(1.0f) {
        // Run warmup to estimate power and set normalization gain
        warmup();
    }
    
    /**
     * Generate next fading tap coefficient
     * @return Complex tap coefficient with Rayleigh magnitude, unit average power
     */
    complex_t next() {
        // Generate complex Gaussian noise
        float i = normal_dist_(rng_);
        float q = normal_dist_(rng_);
        complex_t noise(i, q);
        
        // Filter to get Gaussian Doppler spectrum
        complex_t filtered = filter_.process(noise);
        
        // Apply normalization for unit average power
        return filtered * gain_;
    }
    
    /**
     * Reset generator state
     */
    void reset() {
        filter_.reset();
        // Re-run warmup after reset
        warmup();
    }
    
    float spread_hz() const { return filter_.spread_hz(); }

private:
    GaussianDopplerFilter filter_;
    std::mt19937 rng_;
    std::normal_distribution<float> normal_dist_;
    float power_sum_;
    int sample_count_;
    float gain_;
    
    void warmup() {
        // Generate samples to estimate average power
        const int warmup_samples = 1000;
        float power_acc = 0;
        
        for (int i = 0; i < warmup_samples; i++) {
            float re = normal_dist_(rng_);
            float im = normal_dist_(rng_);
            complex_t noise(re, im);
            complex_t out = filter_.process(noise);
            power_acc += std::norm(out);
        }
        
        float avg_power = power_acc / warmup_samples;
        
        // Set gain for unit average power
        // E[|gain * x|²] = gain² * E[|x|²] = 1
        // gain = 1 / sqrt(avg_power)
        if (avg_power > 1e-10f) {
            gain_ = 1.0f / std::sqrt(avg_power);
        } else {
            gain_ = 1.0f;
        }
    }
};

// ============================================================================
// Phase 3: Watterson Channel
// ============================================================================

/**
 * Watterson HF Channel Simulator
 * 
 * Two-path Rayleigh fading channel with independent fading on each path.
 * Each path has:
 * - Configurable delay
 * - Configurable gain
 * - Independent Rayleigh fading with Gaussian Doppler spectrum
 */
class WattersonChannel {
public:
    struct Config {
        float sample_rate;         // RF sample rate (e.g., 48000 Hz)
        float doppler_spread_hz;   // Doppler spread (0.1-10 Hz typical)
        float delay_ms;            // Differential delay of path 2 (0-10 ms)
        float path1_gain_db;       // Path 1 gain (usually 0 dB)
        float path2_gain_db;       // Path 2 gain (-6 to 0 dB typical)
        float tap_update_rate_hz;  // Rate to update fading taps (100-1000 Hz)
        unsigned int seed;         // Random seed
        
        Config()
            : sample_rate(48000.0f)
            , doppler_spread_hz(1.0f)
            , delay_ms(1.0f)
            , path1_gain_db(0.0f)
            , path2_gain_db(0.0f)
            , tap_update_rate_hz(100.0f)
            , seed(42) {}
    };
    
    explicit WattersonChannel(const Config& config)
        : config_(config)
        , path1_fading_(config.doppler_spread_hz, config.tap_update_rate_hz, config.seed)
        , path2_fading_(config.doppler_spread_hz, config.tap_update_rate_hz, config.seed + 12345)
        , samples_per_tap_update_(static_cast<int>(config.sample_rate / config.tap_update_rate_hz))
        , sample_counter_(0)
        , current_tap1_(1, 0)
        , current_tap2_(1, 0) {
        
        // Convert gains from dB to linear
        path1_gain_ = std::pow(10.0f, config.path1_gain_db / 20.0f);
        path2_gain_ = std::pow(10.0f, config.path2_gain_db / 20.0f);
        
        // Calculate delay in samples
        delay_samples_ = static_cast<int>(config.delay_ms * config.sample_rate / 1000.0f);
        
        // Initialize delay line (real values for RF)
        delay_line_real_.resize(delay_samples_ + 1, 0.0f);
        delay_write_idx_ = 0;
        
        // Initialize taps
        update_taps();
    }
    
    /**
     * Process RF samples through channel
     * @param input Real RF samples
     * @return Faded RF samples
     */
    std::vector<float> process(const std::vector<float>& input) {
        std::vector<float> output(input.size());
        
        for (size_t i = 0; i < input.size(); i++) {
            output[i] = process_sample(input[i]);
        }
        
        return output;
    }
    
    /**
     * Process single RF sample
     * 
     * For real RF signals, we apply magnitude fading only.
     * Phase variations from Rayleigh fading are slow enough
     * (Doppler spread << carrier frequency) that carrier recovery
     * can track them.
     */
    float process_sample(float input) {
        // Update fading taps periodically
        if (sample_counter_++ >= samples_per_tap_update_) {
            sample_counter_ = 0;
            update_taps();
        }
        
        // Path 1: Direct path with magnitude fading
        float path1_out = input * std::abs(current_tap1_) * path1_gain_;
        
        // Path 2: Delayed path with independent magnitude fading
        // Write to delay line (store real value)
        delay_line_real_[delay_write_idx_] = input;
        
        // Read from delay line
        int read_idx = (delay_write_idx_ - delay_samples_ + delay_line_real_.size()) % delay_line_real_.size();
        float delayed = delay_line_real_[read_idx];
        float path2_out = delayed * std::abs(current_tap2_) * path2_gain_;
        
        // Advance delay line
        delay_write_idx_ = (delay_write_idx_ + 1) % delay_line_real_.size();
        
        // Sum paths
        return path1_out + path2_out;
    }
    
    /**
     * Reset channel state
     */
    void reset() {
        path1_fading_.reset();
        path2_fading_.reset();
        std::fill(delay_line_real_.begin(), delay_line_real_.end(), 0.0f);
        delay_write_idx_ = 0;
        sample_counter_ = 0;
        update_taps();
    }
    
    /**
     * Get current tap values (for monitoring)
     */
    void get_taps(complex_t& tap1, complex_t& tap2) const {
        tap1 = current_tap1_;
        tap2 = current_tap2_;
    }
    
    /**
     * Get channel description
     */
    std::string description() const {
        std::ostringstream oss;
        oss << "Watterson Channel:\n"
            << "  Doppler spread: " << config_.doppler_spread_hz << " Hz\n"
            << "  Differential delay: " << config_.delay_ms << " ms (" << delay_samples_ << " samples)\n"
            << "  Path 1 gain: " << config_.path1_gain_db << " dB\n"
            << "  Path 2 gain: " << config_.path2_gain_db << " dB\n";
        return oss.str();
    }
    
    const Config& config() const { return config_; }

private:
    Config config_;
    
    RayleighFadingGenerator path1_fading_;
    RayleighFadingGenerator path2_fading_;
    
    float path1_gain_;
    float path2_gain_;
    int delay_samples_;
    
    std::vector<float> delay_line_real_;
    int delay_write_idx_;
    
    int samples_per_tap_update_;
    int sample_counter_;
    
    complex_t current_tap1_;
    complex_t current_tap2_;
    
    void update_taps() {
        current_tap1_ = path1_fading_.next();
        current_tap2_ = path2_fading_.next();
    }
};

// ============================================================================
// Phase 4: Standard Profiles
// ============================================================================

/**
 * Standard HF channel profiles per CCIR/ITU recommendations
 */
struct ChannelProfile {
    const char* name;
    float doppler_spread_hz;
    float delay_ms;
    float path1_gain_db;
    float path2_gain_db;
};

// CCIR standard profiles
constexpr ChannelProfile CCIR_GOOD       = {"CCIR Good",       0.5f, 0.5f, 0.0f, -3.0f};
constexpr ChannelProfile CCIR_MODERATE   = {"CCIR Moderate",   1.0f, 1.0f, 0.0f,  0.0f};
constexpr ChannelProfile CCIR_POOR       = {"CCIR Poor",       2.0f, 2.0f, 0.0f,  0.0f};
constexpr ChannelProfile CCIR_FLUTTER    = {"CCIR Flutter",   10.0f, 0.5f, 0.0f,  0.0f};

// Additional profiles
constexpr ChannelProfile MID_LAT_DISTURBED  = {"Mid-lat Disturbed",  1.0f, 2.0f, 0.0f, 0.0f};
constexpr ChannelProfile HIGH_LAT_DISTURBED = {"High-lat Disturbed", 5.0f, 3.0f, 0.0f, 0.0f};

/**
 * Helper to create WattersonChannel::Config from a profile
 */
inline WattersonChannel::Config make_channel_config(
    const ChannelProfile& profile,
    float sample_rate = 48000.0f,
    unsigned int seed = 42) 
{
    WattersonChannel::Config cfg;
    cfg.sample_rate = sample_rate;
    cfg.doppler_spread_hz = profile.doppler_spread_hz;
    cfg.delay_ms = profile.delay_ms;
    cfg.path1_gain_db = profile.path1_gain_db;
    cfg.path2_gain_db = profile.path2_gain_db;
    cfg.seed = seed;
    return cfg;
}

} // namespace m110a

#endif // M110A_WATTERSON_H
