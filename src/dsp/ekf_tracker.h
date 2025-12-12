#ifndef M110A_EKF_TRACKER_H
#define M110A_EKF_TRACKER_H

/**
 * Extended Kalman Filter for Carrier Phase/Frequency Tracking
 * 
 * Implements optimal tracking of phase and frequency during data
 * transmission. Designed for MIL-STD-188-110A probe-aided tracking.
 * 
 * "Currently the best for tracking CFO during data transmission.
 * Presents a large estimation range while approaching the Cramer-Rao
 * Bound closely with quite reduced complexity. Handles time-varying
 * channels (Doppler from ionospheric propagation)."
 * 
 * State vector: x = [phase, frequency]
 * 
 * State transition model:
 *   phase(k+1) = phase(k) + freq(k) × T + w_phase
 *   freq(k+1) = freq(k) + w_freq
 * 
 * Measurement model (using probes):
 *   z(k) = measured_phase - expected_phase + v
 * 
 * The EKF handles:
 * - Residual frequency offset from acquisition
 * - Slow ionospheric Doppler drift (~5 Hz/s max on HF)
 * - Phase noise from oscillators
 * - Optimal weighting based on SNR
 * 
 * Operation modes:
 * 1. Probe-aided: Update on known probe symbols (most accurate)
 * 2. Decision-directed: Update on hard decisions (continuous)
 * 3. Hybrid: Probe-aided with DD between probes
 */

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace m110a {

/**
 * EKF Tracker Configuration
 */
struct EKFTrackerConfig {
    float symbol_rate = 2400.0f;
    
    // Process noise (tune for expected dynamics)
    // Higher = faster tracking, more noise
    // Lower = smoother, slower response
    float q_phase = 0.001f;         // Phase process noise variance (rad²)
    float q_freq = 0.0001f;         // Frequency process noise variance (rad²/sample²)
    
    // Measurement noise (from SNR estimate)
    float r_phase = 0.1f;           // Phase measurement noise variance (rad²)
    
    // Initial uncertainty
    float p_phase_init = 1.0f;      // Initial phase uncertainty (rad²)
    float p_freq_init = 0.01f;      // Initial frequency uncertainty (rad²/sample²)
    
    // Tracking limits
    float max_freq_hz = 50.0f;      // Maximum trackable frequency (Hz)
    float max_freq_rate_hz_s = 10.0f; // Maximum frequency rate of change (Hz/s)
    
    // Decision-directed settings
    bool enable_dd = true;          // Enable decision-directed tracking
    float dd_confidence_threshold = 0.7f;  // Min confidence for DD update
    float dd_weight = 0.3f;         // Weight for DD updates vs probe updates
};

/**
 * Extended Kalman Filter Phase/Frequency Tracker
 */
class EKFTracker {
public:
    explicit EKFTracker(const EKFTrackerConfig& cfg = EKFTrackerConfig())
        : config_(cfg)
        , symbol_period_(1.0f / cfg.symbol_rate) {
        
        reset();
        
        // Build 8-PSK constellation for decision-directed mode
        for (int i = 0; i < 8; i++) {
            float angle = i * PI / 4.0f;
            constellation_[i] = complex_t(std::cos(angle), std::sin(angle));
        }
        
        // Convert frequency limits to radians/sample
        max_freq_rad_ = 2.0f * PI * cfg.max_freq_hz / cfg.symbol_rate;
    }
    
    /**
     * Reset tracker state
     */
    void reset() {
        // State: [phase, freq]
        phase_ = 0.0f;
        freq_ = 0.0f;
        
        // Covariance matrix P = [[p_phase, 0], [0, p_freq]]
        P_[0][0] = config_.p_phase_init;
        P_[0][1] = 0.0f;
        P_[1][0] = 0.0f;
        P_[1][1] = config_.p_freq_init;
        
        symbols_processed_ = 0;
    }
    
    /**
     * Initialize with known frequency offset (from acquisition)
     * 
     * @param freq_hz Frequency offset in Hz
     * @param uncertainty_hz Uncertainty in Hz (affects initial covariance)
     */
    void initialize(float freq_hz, float uncertainty_hz = 1.0f) {
        reset();
        freq_ = 2.0f * PI * freq_hz / config_.symbol_rate;  // Convert to rad/sample
        
        // Set initial frequency uncertainty
        float uncertainty_rad = 2.0f * PI * uncertainty_hz / config_.symbol_rate;
        P_[1][1] = uncertainty_rad * uncertainty_rad;
    }
    
    /**
     * Process a single symbol with phase/frequency tracking
     * 
     * @param received Received symbol (complex)
     * @return Phase-corrected symbol
     */
    complex_t process(complex_t received) {
        // Predict step (advance phase by frequency estimate)
        predict();
        
        // Apply current phase correction
        complex_t correction = std::polar(1.0f, -phase_);
        complex_t corrected = received * correction;
        
        // Decision-directed update (if enabled)
        if (config_.enable_dd) {
            update_dd(corrected);
        }
        
        symbols_processed_++;
        return corrected;
    }
    
    /**
     * Process symbols with phase tracking (batch)
     * 
     * @param symbols Input symbols
     * @return Phase-corrected symbols
     */
    std::vector<complex_t> process(const std::vector<complex_t>& symbols) {
        std::vector<complex_t> output;
        output.reserve(symbols.size());
        
        for (const auto& sym : symbols) {
            output.push_back(process(sym));
        }
        
        return output;
    }
    
    /**
     * Update tracker with known probe symbol
     * 
     * This is the primary update mechanism - uses known probe
     * symbols for accurate phase measurement.
     * 
     * @param received Received probe symbol
     * @param expected Expected probe symbol (reference)
     */
    void update_probe(complex_t received, complex_t expected) {
        // Apply current correction
        complex_t correction = std::polar(1.0f, -phase_);
        complex_t corrected = received * correction;
        
        // Measure phase error
        complex_t error_phasor = corrected * std::conj(expected);
        float phase_error = std::atan2(error_phasor.imag(), error_phasor.real());
        
        // Kalman update with full measurement weight
        kalman_update(phase_error, config_.r_phase);
    }
    
    /**
     * Update with multiple probe symbols
     * 
     * @param received Vector of received probe symbols
     * @param expected Vector of expected probe symbols
     */
    void update_probes(const std::vector<complex_t>& received,
                       const std::vector<complex_t>& expected) {
        size_t n = std::min(received.size(), expected.size());
        
        for (size_t i = 0; i < n; i++) {
            update_probe(received[i], expected[i]);
            predict();  // Advance state between probes
        }
    }
    
    /**
     * Train on known sequence (like preamble)
     * 
     * More aggressive update for initial convergence.
     * 
     * @param received Received symbols
     * @param expected Expected symbols
     */
    void train(const std::vector<complex_t>& received,
               const std::vector<complex_t>& expected) {
        size_t n = std::min(received.size(), expected.size());
        
        // Use reduced measurement noise for training (more trust in known symbols)
        float training_r = config_.r_phase * 0.5f;
        
        for (size_t i = 0; i < n; i++) {
            predict();
            
            complex_t correction = std::polar(1.0f, -phase_);
            complex_t corrected = received[i] * correction;
            
            complex_t error_phasor = corrected * std::conj(expected[i]);
            float phase_error = std::atan2(error_phasor.imag(), error_phasor.real());
            
            kalman_update(phase_error, training_r);
        }
    }
    
    /**
     * Get current phase estimate
     */
    float phase() const { return phase_; }
    
    /**
     * Get current frequency estimate (Hz)
     */
    float frequency_hz() const {
        return freq_ * config_.symbol_rate / (2.0f * PI);
    }
    
    /**
     * Get frequency estimate (radians/sample)
     */
    float frequency_rad() const { return freq_; }
    
    /**
     * Set measurement noise based on SNR
     * 
     * @param snr_db Signal-to-noise ratio in dB
     */
    void set_snr(float snr_db) {
        // Phase noise variance ≈ 1/(2×SNR) for PSK
        float snr_linear = std::pow(10.0f, snr_db / 10.0f);
        config_.r_phase = 1.0f / (2.0f * snr_linear);
        
        // Clamp to reasonable range
        config_.r_phase = std::clamp(config_.r_phase, 0.001f, 1.0f);
    }
    
    /**
     * Get phase uncertainty (standard deviation in radians)
     */
    float phase_uncertainty() const {
        return std::sqrt(P_[0][0]);
    }
    
    /**
     * Get frequency uncertainty (standard deviation in Hz)
     */
    float frequency_uncertainty_hz() const {
        return std::sqrt(P_[1][1]) * config_.symbol_rate / (2.0f * PI);
    }

private:
    EKFTrackerConfig config_;
    float symbol_period_;
    
    // State
    float phase_;           // Current phase estimate (radians)
    float freq_;            // Current frequency estimate (radians/sample)
    float P_[2][2];         // State covariance matrix
    
    // Derived
    float max_freq_rad_;
    int symbols_processed_;
    std::array<complex_t, 8> constellation_;
    
    /**
     * EKF Predict Step
     * 
     * State transition:
     *   phase(k+1) = phase(k) + freq(k)
     *   freq(k+1) = freq(k)
     * 
     * Covariance:
     *   P(k+1) = F × P(k) × F' + Q
     * 
     * Where F = [[1, 1], [0, 1]]
     */
    void predict() {
        // State prediction
        phase_ += freq_;
        // freq_ stays the same (random walk model)
        
        // Wrap phase to [-π, π]
        while (phase_ > PI) phase_ -= 2.0f * PI;
        while (phase_ < -PI) phase_ += 2.0f * PI;
        
        // Covariance prediction: P = F × P × F' + Q
        // F = [[1, 1], [0, 1]]
        // 
        // P_new[0][0] = P[0][0] + P[0][1] + P[1][0] + P[1][1] + Q_phase
        // P_new[0][1] = P[0][1] + P[1][1]
        // P_new[1][0] = P[1][0] + P[1][1]
        // P_new[1][1] = P[1][1] + Q_freq
        
        float p00 = P_[0][0] + P_[0][1] + P_[1][0] + P_[1][1] + config_.q_phase;
        float p01 = P_[0][1] + P_[1][1];
        float p10 = P_[1][0] + P_[1][1];
        float p11 = P_[1][1] + config_.q_freq;
        
        P_[0][0] = p00;
        P_[0][1] = p01;
        P_[1][0] = p10;
        P_[1][1] = p11;
    }
    
    /**
     * EKF Update Step
     * 
     * Measurement: z = phase_error
     * H = [1, 0] (we only measure phase directly)
     * 
     * Kalman gain: K = P × H' × (H × P × H' + R)^(-1)
     * State update: x = x + K × (z - H × x)
     * Covariance update: P = (I - K × H) × P
     * 
     * @param phase_error Measured phase error (radians)
     * @param r Measurement noise variance
     */
    void kalman_update(float phase_error, float r) {
        // Innovation covariance: S = H × P × H' + R = P[0][0] + R
        float S = P_[0][0] + r;
        
        // Kalman gain: K = P × H' / S
        // K = [[P[0][0]/S], [P[1][0]/S]]
        float K0 = P_[0][0] / S;
        float K1 = P_[1][0] / S;
        
        // State update
        phase_ += K0 * phase_error;
        freq_ += K1 * phase_error;
        
        // Clamp frequency to valid range
        freq_ = std::clamp(freq_, -max_freq_rad_, max_freq_rad_);
        
        // Wrap phase
        while (phase_ > PI) phase_ -= 2.0f * PI;
        while (phase_ < -PI) phase_ += 2.0f * PI;
        
        // Covariance update: P = (I - K × H) × P
        // (I - K × H) = [[1-K0, 0], [-K1, 1]]
        // P_new = [[1-K0, 0], [-K1, 1]] × P
        
        float p00 = (1.0f - K0) * P_[0][0];
        float p01 = (1.0f - K0) * P_[0][1];
        float p10 = -K1 * P_[0][0] + P_[1][0];
        float p11 = -K1 * P_[0][1] + P_[1][1];
        
        P_[0][0] = p00;
        P_[0][1] = p01;
        P_[1][0] = p10;
        P_[1][1] = p11;
    }
    
    /**
     * Decision-directed update
     * 
     * Uses hard decision on received symbol to estimate phase error.
     * Less accurate than probe-based but provides continuous tracking.
     */
    void update_dd(complex_t corrected) {
        // Find nearest constellation point
        int best_idx = 0;
        float best_dist = std::norm(corrected - constellation_[0]);
        
        for (int i = 1; i < 8; i++) {
            float dist = std::norm(corrected - constellation_[i]);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = i;
            }
        }
        
        // Confidence based on distance to decision boundary
        // For 8-PSK, distance between adjacent points is √(2 - √2) ≈ 0.765
        float symbol_mag = std::abs(corrected);
        float normalized_dist = std::sqrt(best_dist) / symbol_mag;
        float confidence = 1.0f - normalized_dist / 0.4f;  // 0.4 = half decision boundary
        confidence = std::clamp(confidence, 0.0f, 1.0f);
        
        // Only update if confident
        if (confidence < config_.dd_confidence_threshold) {
            return;
        }
        
        // Phase error
        complex_t expected = constellation_[best_idx];
        complex_t error_phasor = corrected * std::conj(expected);
        float phase_error = std::atan2(error_phasor.imag(), error_phasor.real());
        
        // Update with increased noise (less trust than probes)
        float dd_r = config_.r_phase / (config_.dd_weight * confidence);
        kalman_update(phase_error, dd_r);
    }
};

} // namespace m110a

#endif // M110A_EKF_TRACKER_H
