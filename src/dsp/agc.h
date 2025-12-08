/**
 * @file agc.h
 * @brief Automatic Gain Control for constellation normalization
 * 
 * AGC normalizes signal power before equalization, ensuring:
 * - Consistent constellation size for equalizer
 * - Better soft decision scaling
 * - Stable adaptation across varying signal levels
 * 
 * From MATLAB MIL-STD-188-110A reference:
 * "The AGC ensures that the average signal power into the equalizer is 1 watt.
 *  This operation ensures that the constellation of the equalizer input signal
 *  is most closely matched to the ideal constellation."
 */

#ifndef AGC_H
#define AGC_H

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace m110a {

/**
 * AGC Configuration
 */
struct AGCConfig {
    float target_power = 1.0f;      // Target output power (1.0 = unit power)
    float attack_rate = 0.1f;       // Fast attack for increasing gain (0-1)
    float decay_rate = 0.01f;       // Slow decay for decreasing gain (0-1)
    float max_gain = 100.0f;        // Maximum gain limit
    float min_gain = 0.01f;         // Minimum gain limit
    bool enabled = true;            // Enable/disable AGC
};

/**
 * Automatic Gain Control
 * 
 * Normalizes signal power to target level using asymmetric
 * attack/decay for stable operation on fading channels.
 */
class AGC {
public:
    using complex_t = std::complex<float>;
    
    explicit AGC(const AGCConfig& config = AGCConfig())
        : config_(config)
        , gain_(1.0f)
        , power_estimate_(1.0f) {}
    
    /**
     * Reset AGC state
     */
    void reset() {
        gain_ = 1.0f;
        power_estimate_ = config_.target_power;
    }
    
    /**
     * Process a block of complex samples
     * @param samples Input samples (modified in place)
     */
    void process(std::vector<complex_t>& samples) {
        if (!config_.enabled || samples.empty()) return;
        
        // Estimate input power
        float input_power = estimate_power(samples);
        
        // Update power estimate with smoothing
        float alpha = (input_power > power_estimate_) ? 
                      config_.attack_rate : config_.decay_rate;
        power_estimate_ = alpha * input_power + (1.0f - alpha) * power_estimate_;
        
        // Calculate required gain
        float desired_gain = std::sqrt(config_.target_power / (power_estimate_ + 1e-10f));
        
        // Clamp gain to limits
        gain_ = std::clamp(desired_gain, config_.min_gain, config_.max_gain);
        
        // Apply gain
        for (auto& s : samples) {
            s *= gain_;
        }
    }
    
    /**
     * Process a block of real samples
     */
    void process(std::vector<float>& samples) {
        if (!config_.enabled || samples.empty()) return;
        
        // Estimate input power
        float input_power = 0.0f;
        for (auto s : samples) {
            input_power += s * s;
        }
        input_power /= samples.size();
        
        // Update power estimate
        float alpha = (input_power > power_estimate_) ? 
                      config_.attack_rate : config_.decay_rate;
        power_estimate_ = alpha * input_power + (1.0f - alpha) * power_estimate_;
        
        // Calculate and clamp gain
        float desired_gain = std::sqrt(config_.target_power / (power_estimate_ + 1e-10f));
        gain_ = std::clamp(desired_gain, config_.min_gain, config_.max_gain);
        
        // Apply gain
        for (auto& s : samples) {
            s *= gain_;
        }
    }
    
    /**
     * Process samples and return new vector (non-modifying)
     */
    std::vector<complex_t> process_copy(const std::vector<complex_t>& samples) {
        std::vector<complex_t> output = samples;
        process(output);
        return output;
    }
    
    /**
     * Get current gain value
     */
    float gain() const { return gain_; }
    
    /**
     * Get current power estimate
     */
    float power_estimate() const { return power_estimate_; }
    
    /**
     * Get gain in dB
     */
    float gain_db() const { return 20.0f * std::log10(gain_ + 1e-10f); }
    
    /**
     * Set gain directly (for initialization)
     */
    void set_gain(float g) { 
        gain_ = std::clamp(g, config_.min_gain, config_.max_gain); 
    }
    
    /**
     * Update configuration
     */
    void configure(const AGCConfig& config) { config_ = config; }
    
    /**
     * Static helper: normalize a block to unit power
     * Simple one-shot normalization (no state)
     */
    static void normalize(std::vector<complex_t>& samples, float target = 1.0f) {
        if (samples.empty()) return;
        
        float power = estimate_power(samples);
        if (power < 1e-10f) return;
        
        float gain = std::sqrt(target / power);
        for (auto& s : samples) {
            s *= gain;
        }
    }
    
    /**
     * Static helper: estimate power of complex samples
     */
    static float estimate_power(const std::vector<complex_t>& samples) {
        if (samples.empty()) return 0.0f;
        
        float power = 0.0f;
        for (const auto& s : samples) {
            power += std::norm(s);  // |s|^2
        }
        return power / samples.size();
    }

private:
    AGCConfig config_;
    float gain_;
    float power_estimate_;
};

/**
 * Symbol-level AGC for constellation normalization
 * 
 * Operates on demodulated symbols rather than raw samples.
 * Uses ideal constellation power as reference.
 */
class SymbolAGC {
public:
    using complex_t = std::complex<float>;
    
    /**
     * Construct with target constellation power
     * @param target_power Target power (1.0 for unit circle PSK)
     * @param alpha Smoothing factor (0.01-0.1 typical)
     */
    explicit SymbolAGC(float target_power = 1.0f, float alpha = 0.05f)
        : target_power_(target_power)
        , alpha_(alpha)
        , gain_(1.0f)
        , power_estimate_(target_power) {}
    
    /**
     * Process one symbol
     */
    complex_t process(complex_t sym) {
        // Update power estimate
        float sym_power = std::norm(sym);
        power_estimate_ = alpha_ * sym_power + (1.0f - alpha_) * power_estimate_;
        
        // Update gain
        gain_ = std::sqrt(target_power_ / (power_estimate_ + 1e-10f));
        gain_ = std::clamp(gain_, 0.1f, 10.0f);
        
        return sym * gain_;
    }
    
    /**
     * Process symbol block
     */
    void process(std::vector<complex_t>& symbols) {
        for (auto& s : symbols) {
            s = process(s);
        }
    }
    
    /**
     * Reset state
     */
    void reset() {
        gain_ = 1.0f;
        power_estimate_ = target_power_;
    }
    
    float gain() const { return gain_; }

private:
    float target_power_;
    float alpha_;
    float gain_;
    float power_estimate_;
};

} // namespace m110a

#endif // AGC_H
