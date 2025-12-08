/**
 * @file channel_sim.h
 * @brief Channel Simulation API for M110A Modem Testing
 * 
 * Provides realistic HF channel impairments for testing the modem
 * under various conditions: AWGN, multipath, frequency offset,
 * fading, and combined channel models.
 * 
 * These functions are useful for:
 * - Automated testing and validation
 * - BER performance characterization
 * - Equalizer algorithm comparison
 * - Interactive testing via server interface
 * 
 * @author Phoenix Nest LLC
 * @version 1.1.0
 */

#ifndef M110A_API_CHANNEL_SIM_H
#define M110A_API_CHANNEL_SIM_H

#include "modem_types.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <complex>

namespace m110a {
namespace api {
namespace channel {

// ============================================================
// Constants
// ============================================================

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;

// ============================================================
// AWGN (Additive White Gaussian Noise)
// ============================================================

/**
 * Add AWGN noise to samples at specified SNR
 * 
 * Models thermal noise in the receiver. The noise is white
 * (flat spectrum) and Gaussian distributed.
 * 
 * @param samples Audio samples (modified in place)
 * @param snr_db Target signal-to-noise ratio in dB
 * @param rng Random number generator
 * 
 * Theory:
 *   SNR = P_signal / P_noise
 *   P_noise = P_signal / 10^(SNR_dB/10)
 *   noise_std = sqrt(P_noise)
 */
inline void add_awgn(Samples& samples, float snr_db, std::mt19937& rng) {
    if (samples.empty()) return;
    
    // Calculate signal power
    float signal_power = 0.0f;
    for (const auto& s : samples) {
        signal_power += s * s;
    }
    signal_power /= static_cast<float>(samples.size());
    
    // Calculate noise power from SNR
    float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
    float noise_std = std::sqrt(noise_power);
    
    // Add Gaussian noise
    std::normal_distribution<float> noise(0.0f, noise_std);
    for (auto& s : samples) {
        s += noise(rng);
    }
}

/**
 * Add AWGN with automatic seeding
 * 
 * @param samples Audio samples (modified in place)
 * @param snr_db Target signal-to-noise ratio in dB
 */
inline void add_awgn(Samples& samples, float snr_db) {
    std::random_device rd;
    std::mt19937 rng(rd());
    add_awgn(samples, snr_db, rng);
}

/**
 * Add AWGN with fixed seed (for reproducible tests)
 * 
 * @param samples Audio samples (modified in place)
 * @param snr_db Target signal-to-noise ratio in dB
 * @param seed Random seed for reproducibility
 */
inline void add_awgn_seeded(Samples& samples, float snr_db, uint32_t seed) {
    std::mt19937 rng(seed);
    add_awgn(samples, snr_db, rng);
}

// ============================================================
// Static Multipath (Single Echo)
// ============================================================

/**
 * Add static multipath (single delayed echo)
 * 
 * Models a single reflection arriving after the direct path.
 * Common in HF groundwave + ionospheric reflection.
 * 
 * @param samples Audio samples (modified in place)
 * @param delay_samples Delay of echo in samples
 * @param echo_gain Gain of echo (0.0-1.0, typically 0.3-0.7)
 * 
 * Output model:
 *   y[n] = x[n] + echo_gain * x[n - delay]
 * 
 * At 48kHz sample rate:
 *   - 1ms delay = 48 samples
 *   - 2ms delay = 96 samples
 *   - Symbol period (2400 baud) = 20 samples
 */
inline void add_multipath(Samples& samples, int delay_samples, float echo_gain) {
    if (samples.empty() || delay_samples <= 0) return;
    
    Samples output(samples.size(), 0.0f);
    
    for (size_t i = 0; i < samples.size(); i++) {
        output[i] = samples[i];
        if (i >= static_cast<size_t>(delay_samples)) {
            output[i] += echo_gain * samples[i - delay_samples];
        }
    }
    
    samples = std::move(output);
}

/**
 * Add multipath with delay in milliseconds
 * 
 * @param samples Audio samples (modified in place)
 * @param delay_ms Delay of echo in milliseconds
 * @param echo_gain Gain of echo (0.0-1.0)
 * @param sample_rate Sample rate in Hz (default: 48000)
 */
inline void add_multipath_ms(Samples& samples, float delay_ms, 
                              float echo_gain, float sample_rate = 48000.0f) {
    int delay_samples = static_cast<int>(delay_ms * sample_rate / 1000.0f);
    add_multipath(samples, delay_samples, echo_gain);
}

// ============================================================
// Two-Path Multipath (More Realistic)
// ============================================================

/**
 * Add two-path multipath channel
 * 
 * Models two reflection paths, common in HF skywave propagation
 * where signals arrive via different ionospheric layers.
 * 
 * @param samples Audio samples (modified in place)
 * @param delay1_samples First echo delay in samples
 * @param gain1 First echo gain
 * @param delay2_samples Second echo delay in samples
 * @param gain2 Second echo gain
 * 
 * Output model:
 *   y[n] = x[n] + gain1*x[n-delay1] + gain2*x[n-delay2]
 */
inline void add_two_path(Samples& samples, 
                          int delay1_samples, float gain1,
                          int delay2_samples, float gain2) {
    if (samples.empty()) return;
    
    Samples output(samples.size(), 0.0f);
    
    for (size_t i = 0; i < samples.size(); i++) {
        output[i] = samples[i];
        if (i >= static_cast<size_t>(delay1_samples)) {
            output[i] += gain1 * samples[i - delay1_samples];
        }
        if (i >= static_cast<size_t>(delay2_samples)) {
            output[i] += gain2 * samples[i - delay2_samples];
        }
    }
    
    samples = std::move(output);
}

// ============================================================
// Frequency Offset
// ============================================================

/**
 * Add frequency offset (carrier drift)
 * 
 * Models frequency error between TX and RX oscillators,
 * or Doppler shift from ionospheric motion.
 * 
 * @param samples Audio samples (modified in place)
 * @param offset_hz Frequency offset in Hz (can be negative)
 * @param sample_rate Sample rate in Hz (default: 48000)
 * 
 * Implementation: Uses Hilbert transform to create analytic signal,
 * applies frequency shift, then takes real part.
 * 
 * Typical HF values:
 *   - Crystal oscillator drift: ±1-5 Hz
 *   - Ionospheric Doppler: ±0.1-2 Hz
 *   - Combined worst case: ±10 Hz
 */
inline void add_freq_offset(Samples& samples, float offset_hz, 
                             float sample_rate = 48000.0f) {
    if (samples.empty() || offset_hz == 0.0f) return;
    
    // For proper frequency shift of a real signal:
    // 1. Create analytic signal: x_a(t) = x(t) + j*H{x(t)} where H is Hilbert transform
    // 2. Frequency shift: y_a(t) = x_a(t) * e^(j*2*pi*f*t)
    // 3. Take real part: y(t) = Re{y_a(t)}
    //
    // Simplified approach: Use FIR Hilbert transformer
    // For small frequency offsets relative to bandwidth, we can approximate
    // the Hilbert transform using a simple 90-degree phase shift filter
    
    const int HILBERT_LEN = 31;  // FIR Hilbert transformer length (must be odd)
    std::vector<float> hilbert_coeffs(HILBERT_LEN);
    int half = HILBERT_LEN / 2;
    
    // Design Hilbert transformer coefficients
    for (int i = 0; i < HILBERT_LEN; i++) {
        int n = i - half;
        if (n == 0) {
            hilbert_coeffs[i] = 0.0f;
        } else if (n % 2 == 0) {
            hilbert_coeffs[i] = 0.0f;  // Even indices are zero
        } else {
            // Odd indices: 2/(pi*n) with Hamming window
            float window = 0.54f - 0.46f * std::cos(TWO_PI * i / (HILBERT_LEN - 1));
            hilbert_coeffs[i] = window * 2.0f / (M_PI * n);
        }
    }
    
    // Compute Hilbert transform (imaginary part of analytic signal)
    std::vector<float> hilbert_out(samples.size(), 0.0f);
    for (size_t i = half; i < samples.size() - half; i++) {
        float sum = 0.0f;
        for (int j = 0; j < HILBERT_LEN; j++) {
            sum += samples[i - half + j] * hilbert_coeffs[j];
        }
        hilbert_out[i] = sum;
    }
    
    // Apply frequency shift: y = Re{(x + j*H{x}) * e^(j*2*pi*f*t)}
    //                         = x*cos(wt) - H{x}*sin(wt)
    float phase = 0.0f;
    float phase_inc = TWO_PI * offset_hz / sample_rate;
    
    for (size_t i = 0; i < samples.size(); i++) {
        float cos_p = std::cos(phase);
        float sin_p = std::sin(phase);
        samples[i] = samples[i] * cos_p - hilbert_out[i] * sin_p;
        phase += phase_inc;
        if (phase > TWO_PI) phase -= TWO_PI;
        if (phase < -TWO_PI) phase += TWO_PI;
    }
}

/**
 * Add frequency offset with initial phase
 * 
 * @param samples Audio samples (modified in place)
 * @param offset_hz Frequency offset in Hz
 * @param initial_phase_rad Starting phase in radians
 * @param sample_rate Sample rate in Hz
 */
inline void add_freq_offset_phased(Samples& samples, float offset_hz,
                                    float initial_phase_rad,
                                    float sample_rate = 48000.0f) {
    if (samples.empty()) return;
    
    float phase = initial_phase_rad;
    float phase_inc = TWO_PI * offset_hz / sample_rate;
    
    for (auto& s : samples) {
        s *= std::cos(phase);
        phase += phase_inc;
        if (phase > TWO_PI) phase -= TWO_PI;
        if (phase < -TWO_PI) phase += TWO_PI;
    }
}

// ============================================================
// Phase Noise
// ============================================================

/**
 * Add phase noise (jitter on carrier)
 * 
 * Models oscillator instability and phase jitter.
 * 
 * @param samples Audio samples (modified in place)
 * @param noise_std_rad Standard deviation of phase noise in radians
 * @param rng Random number generator
 * 
 * Typical values:
 *   - Good oscillator: 0.01-0.05 rad
 *   - Poor oscillator: 0.1-0.2 rad
 */
inline void add_phase_noise(Samples& samples, float noise_std_rad, 
                             std::mt19937& rng) {
    if (samples.empty()) return;
    
    std::normal_distribution<float> phase_noise(0.0f, noise_std_rad);
    
    for (auto& s : samples) {
        float phase_error = phase_noise(rng);
        s *= std::cos(phase_error);
    }
}

// ============================================================
// Rayleigh Fading (Time-Varying Channel)
// ============================================================

/**
 * Add Rayleigh fading
 * 
 * Models rapid amplitude fluctuations from ionospheric
 * scintillation and multipath interference.
 * 
 * @param samples Audio samples (modified in place)
 * @param doppler_hz Doppler spread in Hz (fading rate)
 * @param sample_rate Sample rate in Hz
 * @param rng Random number generator
 * 
 * Doppler spread values:
 *   - Slow fading (quiet ionosphere): 0.1-0.5 Hz
 *   - Moderate fading: 0.5-2 Hz
 *   - Fast fading (disturbed): 2-5 Hz
 */
inline void add_rayleigh_fading(Samples& samples, float doppler_hz,
                                 float sample_rate, std::mt19937& rng) {
    if (samples.empty() || doppler_hz <= 0.0f) return;
    
    // Generate complex Gaussian process with Jakes spectrum
    size_t n = samples.size();
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    
    // Number of oscillators for Jakes model
    constexpr int N_OSC = 8;
    
    std::vector<float> fading_envelope(n);
    
    for (size_t i = 0; i < n; i++) {
        float real_sum = 0.0f;
        float imag_sum = 0.0f;
        
        for (int k = 0; k < N_OSC; k++) {
            float alpha = PI * (k + 0.5f) / N_OSC;
            float w = TWO_PI * doppler_hz * std::cos(alpha);
            real_sum += std::cos(w * i / sample_rate);
            imag_sum += std::sin(w * i / sample_rate);
        }
        
        real_sum /= std::sqrt(static_cast<float>(N_OSC));
        imag_sum /= std::sqrt(static_cast<float>(N_OSC));
        
        // Rayleigh envelope
        fading_envelope[i] = std::sqrt(real_sum * real_sum + imag_sum * imag_sum);
    }
    
    // Apply fading
    for (size_t i = 0; i < n; i++) {
        samples[i] *= fading_envelope[i];
    }
}

// ============================================================
// Combined Channel Models
// ============================================================

/**
 * Channel model configuration
 */
struct ChannelConfig {
    // AWGN
    bool awgn_enabled = false;
    float snr_db = 30.0f;
    
    // Multipath
    bool multipath_enabled = false;
    int multipath_delay_samples = 48;   // 1ms at 48kHz
    float multipath_gain = 0.5f;        // -6dB echo
    
    // Frequency offset
    bool freq_offset_enabled = false;
    float freq_offset_hz = 0.0f;
    
    // Phase noise
    bool phase_noise_enabled = false;
    float phase_noise_std_rad = 0.05f;
    
    // Rayleigh fading
    bool fading_enabled = false;
    float fading_doppler_hz = 1.0f;
    
    // Sample rate
    float sample_rate = 48000.0f;
    
    // Random seed (0 = random)
    uint32_t seed = 0;
};

/**
 * Apply complete channel model
 * 
 * Applies all enabled impairments in the correct order:
 * 1. Frequency offset (before multipath for realism)
 * 2. Multipath
 * 3. Fading
 * 4. Phase noise
 * 5. AWGN (always last)
 * 
 * @param samples Audio samples (modified in place)
 * @param config Channel configuration
 */
inline void apply_channel(Samples& samples, const ChannelConfig& config) {
    std::mt19937 rng;
    if (config.seed == 0) {
        std::random_device rd;
        rng.seed(rd());
    } else {
        rng.seed(config.seed);
    }
    
    // 1. Frequency offset
    if (config.freq_offset_enabled) {
        add_freq_offset(samples, config.freq_offset_hz, config.sample_rate);
    }
    
    // 2. Multipath
    if (config.multipath_enabled) {
        add_multipath(samples, config.multipath_delay_samples, config.multipath_gain);
    }
    
    // 3. Fading
    if (config.fading_enabled) {
        add_rayleigh_fading(samples, config.fading_doppler_hz, config.sample_rate, rng);
    }
    
    // 4. Phase noise
    if (config.phase_noise_enabled) {
        add_phase_noise(samples, config.phase_noise_std_rad, rng);
    }
    
    // 5. AWGN (always last)
    if (config.awgn_enabled) {
        add_awgn(samples, config.snr_db, rng);
    }
}

// ============================================================
// Preset Channel Models
// ============================================================

/**
 * Good HF channel (daytime, short path)
 * - High SNR
 * - Minimal multipath
 * - Low Doppler
 */
inline ChannelConfig channel_good_hf() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 25.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 24;  // 0.5ms
    cfg.multipath_gain = 0.3f;
    return cfg;
}

/**
 * Moderate HF channel (typical conditions)
 * - Medium SNR
 * - Significant multipath
 * - Moderate fading
 */
inline ChannelConfig channel_moderate_hf() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 18.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 48;  // 1ms
    cfg.multipath_gain = 0.5f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 1.0f;
    return cfg;
}

/**
 * Poor HF channel (disturbed ionosphere)
 * - Low SNR
 * - Strong multipath
 * - Fast fading
 * - Frequency drift
 */
inline ChannelConfig channel_poor_hf() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 12.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 96;  // 2ms
    cfg.multipath_gain = 0.7f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 3.0f;
    cfg.freq_offset_enabled = true;
    cfg.freq_offset_hz = 5.0f;
    return cfg;
}

/**
 * CCIR Good channel (ITU-R F.520)
 * - 0.5ms delay spread
 * - 0.1 Hz Doppler
 */
inline ChannelConfig channel_ccir_good() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 20.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 24;  // 0.5ms
    cfg.multipath_gain = 0.5f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 0.1f;
    return cfg;
}

/**
 * CCIR Moderate channel (ITU-R F.520)
 * - 1ms delay spread
 * - 0.5 Hz Doppler
 */
inline ChannelConfig channel_ccir_moderate() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 15.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 48;  // 1ms
    cfg.multipath_gain = 0.5f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 0.5f;
    return cfg;
}

/**
 * CCIR Poor channel (ITU-R F.520)
 * - 2ms delay spread  
 * - 1 Hz Doppler
 */
inline ChannelConfig channel_ccir_poor() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 10.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 96;  // 2ms
    cfg.multipath_gain = 0.5f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 1.0f;
    return cfg;
}

// ============================================================
// Analysis Functions
// ============================================================

/**
 * Calculate Bit Error Rate (BER)
 * 
 * Compares transmitted and received data byte-by-byte.
 * 
 * @param tx_data Original transmitted data
 * @param rx_data Received/decoded data
 * @return Bit error rate (0.0 = perfect, 1.0 = all errors)
 */
inline double calculate_ber(const std::vector<uint8_t>& tx_data,
                            const std::vector<uint8_t>& rx_data) {
    if (tx_data.empty() || rx_data.empty()) return 1.0;
    
    size_t min_len = std::min(tx_data.size(), rx_data.size());
    int bit_errors = 0;
    int total_bits = 0;
    
    for (size_t i = 0; i < min_len; i++) {
        uint8_t diff = tx_data[i] ^ rx_data[i];
        // Count set bits (errors)
        while (diff) {
            bit_errors += diff & 1;
            diff >>= 1;
        }
        total_bits += 8;
    }
    
    // Account for length mismatch as errors
    if (tx_data.size() != rx_data.size()) {
        size_t len_diff = std::max(tx_data.size(), rx_data.size()) - min_len;
        bit_errors += static_cast<int>(len_diff * 8);
        total_bits += static_cast<int>(len_diff * 8);
    }
    
    return static_cast<double>(bit_errors) / total_bits;
}

/**
 * Calculate Symbol Error Rate (SER)
 * 
 * For 8-PSK (3 bits/symbol), QPSK (2 bits/symbol), BPSK (1 bit/symbol)
 * 
 * @param tx_data Original transmitted data
 * @param rx_data Received/decoded data
 * @param bits_per_symbol Bits per symbol (1=BPSK, 2=QPSK, 3=8PSK)
 * @return Symbol error rate
 */
inline double calculate_ser(const std::vector<uint8_t>& tx_data,
                            const std::vector<uint8_t>& rx_data,
                            int bits_per_symbol = 3) {
    double ber = calculate_ber(tx_data, rx_data);
    // Approximate SER from BER for independent bit errors
    // SER ≈ 1 - (1-BER)^bits_per_symbol
    return 1.0 - std::pow(1.0 - ber, bits_per_symbol);
}

/**
 * Estimate signal power
 * 
 * @param samples Audio samples
 * @return Signal power (mean squared amplitude)
 */
inline float estimate_signal_power(const Samples& samples) {
    if (samples.empty()) return 0.0f;
    
    float power = 0.0f;
    for (const auto& s : samples) {
        power += s * s;
    }
    return power / static_cast<float>(samples.size());
}

/**
 * Estimate SNR from noisy signal
 * 
 * Uses noise floor estimation from signal statistics.
 * Assumes signal has higher variance than noise.
 * 
 * @param samples Noisy audio samples
 * @return Estimated SNR in dB
 */
inline float estimate_snr(const Samples& samples) {
    if (samples.size() < 100) return 0.0f;
    
    // Simple SNR estimation using peak-to-average ratio
    float sum = 0.0f;
    float sum_sq = 0.0f;
    float peak = 0.0f;
    
    for (const auto& s : samples) {
        float abs_s = std::abs(s);
        sum += abs_s;
        sum_sq += s * s;
        if (abs_s > peak) peak = abs_s;
    }
    
    float n = static_cast<float>(samples.size());
    float mean = sum / n;
    float variance = sum_sq / n - mean * mean;
    
    if (variance <= 0.0f) return 40.0f;  // Very clean signal
    
    // Estimate SNR from peak-to-rms ratio
    float rms = std::sqrt(sum_sq / n);
    if (rms <= 0.0f) return 0.0f;
    
    float par = peak / rms;  // Peak-to-average ratio
    
    // Empirical mapping (rough estimate)
    float snr_db = 20.0f * std::log10(par) - 3.0f;
    
    return std::max(0.0f, std::min(40.0f, snr_db));
}

} // namespace channel
} // namespace api
} // namespace m110a

#endif // M110A_API_CHANNEL_SIM_H
