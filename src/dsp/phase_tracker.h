#ifndef M110A_PHASE_TRACKER_H
#define M110A_PHASE_TRACKER_H

/**
 * Adaptive Phase Tracker
 * 
 * Tracks and corrects slow phase drift during data transmission using
 * decision-directed feedback. Works with both probe-aided and blind modes.
 * 
 * Algorithm: 2nd-order PLL with proportional-integral control
 * 
 * Phase error: φ_err = arg(received * conj(expected))
 * Update: θ += α * φ_err + β * ∫φ_err
 */

#include "common/types.h"
#include <vector>
#include <array>
#include <cmath>

namespace m110a {

struct PhaseTrackerConfig {
    float alpha = 0.05f;      // Proportional gain (phase tracking bandwidth)
    float beta = 0.002f;      // Integral gain (frequency offset tracking)
    float max_freq_hz = 10.0f; // Maximum frequency offset to track
    float symbol_rate = 2400.0f;
    
    // Decision-directed parameters
    bool decision_directed = true;  // Use hard decisions for tracking
    float dd_threshold = 0.7f;      // Confidence threshold for DD updates
};

class PhaseTracker {
public:
    explicit PhaseTracker(const PhaseTrackerConfig& config = PhaseTrackerConfig())
        : config_(config)
        , phase_(0.0f)
        , freq_(0.0f)
        , max_freq_rad_(2.0f * PI * config.max_freq_hz / config.symbol_rate) {
        
        // Build 8-PSK constellation for decision-directed mode
        for (int i = 0; i < 8; i++) {
            float angle = i * PI / 4.0f;
            constellation_[i] = complex_t(std::cos(angle), std::sin(angle));
        }
    }
    
    /**
     * Process symbols with phase tracking
     * Returns phase-corrected symbols
     */
    std::vector<complex_t> process(const std::vector<complex_t>& symbols) {
        std::vector<complex_t> output;
        output.reserve(symbols.size());
        
        for (const auto& sym : symbols) {
            output.push_back(process_symbol(sym));
        }
        
        return output;
    }
    
    /**
     * Process single symbol with phase tracking
     */
    complex_t process_symbol(complex_t received) {
        // Apply current phase correction
        complex_t correction = std::polar(1.0f, -phase_);
        complex_t corrected = received * correction;
        
        if (config_.decision_directed) {
            // Find closest constellation point
            int best_idx = 0;
            float best_dist = std::norm(corrected - constellation_[0]);
            for (int i = 1; i < 8; i++) {
                float dist = std::norm(corrected - constellation_[i]);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = i;
                }
            }
            
            // Only update if decision is confident
            float symbol_mag = std::abs(corrected);
            if (symbol_mag > 0.1f && best_dist < config_.dd_threshold) {
                // Phase error = angle between corrected and expected
                complex_t expected = constellation_[best_idx];
                complex_t error_phasor = corrected * std::conj(expected);
                float phase_error = std::atan2(error_phasor.imag(), error_phasor.real());
                
                // 2nd order PLL update
                freq_ += config_.beta * phase_error;
                freq_ = std::clamp(freq_, -max_freq_rad_, max_freq_rad_);
                phase_ += config_.alpha * phase_error + freq_;
                
                // Wrap phase
                while (phase_ > PI) phase_ -= 2.0f * PI;
                while (phase_ < -PI) phase_ += 2.0f * PI;
            }
        }
        
        return corrected;
    }
    
    /**
     * Train on known symbols (probes)
     */
    void train(const std::vector<complex_t>& received, 
               const std::vector<complex_t>& expected) {
        for (size_t i = 0; i < std::min(received.size(), expected.size()); i++) {
            // Apply current phase correction
            complex_t correction = std::polar(1.0f, -phase_);
            complex_t corrected = received[i] * correction;
            
            // Phase error from known symbol
            complex_t error_phasor = corrected * std::conj(expected[i]);
            float phase_error = std::atan2(error_phasor.imag(), error_phasor.real());
            
            // Update with higher confidence on known symbols
            freq_ += config_.beta * 2.0f * phase_error;
            freq_ = std::clamp(freq_, -max_freq_rad_, max_freq_rad_);
            phase_ += config_.alpha * 2.0f * phase_error + freq_;
            
            while (phase_ > PI) phase_ -= 2.0f * PI;
            while (phase_ < -PI) phase_ += 2.0f * PI;
        }
    }
    
    /**
     * Set initial phase estimate
     */
    void set_phase(float phase) { 
        phase_ = phase; 
        while (phase_ > PI) phase_ -= 2.0f * PI;
        while (phase_ < -PI) phase_ += 2.0f * PI;
    }
    
    /**
     * Set initial frequency estimate (Hz)
     */
    void set_frequency(float freq_hz) {
        freq_ = 2.0f * PI * freq_hz / config_.symbol_rate;
        freq_ = std::clamp(freq_, -max_freq_rad_, max_freq_rad_);
    }
    
    /**
     * Get current phase estimate (radians)
     */
    float get_phase() const { return phase_; }
    
    /**
     * Get current frequency offset estimate (Hz)
     */
    float get_frequency() const { 
        return freq_ * config_.symbol_rate / (2.0f * PI); 
    }
    
    /**
     * Reset tracker state
     */
    void reset() {
        phase_ = 0.0f;
        freq_ = 0.0f;
    }

private:
    PhaseTrackerConfig config_;
    float phase_;           // Current phase estimate (radians)
    float freq_;            // Current frequency offset (radians/symbol)
    float max_freq_rad_;    // Maximum frequency in radians/symbol
    std::array<complex_t, 8> constellation_;
};

/**
 * Combined Phase Tracker + Equalizer wrapper
 * Applies phase tracking before equalization for best results
 */
class PhaseTrackedEqualizer {
public:
    struct Config {
        PhaseTrackerConfig phase_config;
        bool enable_phase_tracking;
        
        Config() : phase_config(), enable_phase_tracking(true) {}
    };
    
    explicit PhaseTrackedEqualizer(const Config& config)
        : config_(config)
        , tracker_(config.phase_config) {}
    
    PhaseTrackedEqualizer() : PhaseTrackedEqualizer(Config()) {}
    
    /**
     * Process frame: track phase on probes, correct data symbols
     */
    std::vector<complex_t> process_frame(
            const std::vector<complex_t>& data_symbols,
            const std::vector<complex_t>& probe_symbols,
            const std::vector<complex_t>& probe_reference) {
        
        if (!config_.enable_phase_tracking) {
            return data_symbols;
        }
        
        // Train on probes first
        tracker_.train(probe_symbols, probe_reference);
        
        // Apply phase correction to data
        return tracker_.process(data_symbols);
    }
    
    /**
     * Get current frequency offset estimate
     */
    float get_frequency_offset() const { return tracker_.get_frequency(); }
    
    /**
     * Reset state
     */
    void reset() { tracker_.reset(); }

private:
    Config config_;
    PhaseTracker tracker_;
};

} // namespace m110a

#endif // M110A_PHASE_TRACKER_H
