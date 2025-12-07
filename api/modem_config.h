/**
 * @file modem_config.h
 * @brief Configuration structures for M110A Modem API
 */

#ifndef M110A_API_MODEM_CONFIG_H
#define M110A_API_MODEM_CONFIG_H

#include "modem_types.h"

namespace m110a {
namespace api {

// ============================================================
// TX Configuration
// ============================================================

/**
 * Transmitter configuration
 */
struct TxConfig {
    /// Operating mode (required for TX)
    Mode mode = Mode::M2400_SHORT;
    
    /// Output sample rate in Hz
    float sample_rate = SAMPLE_RATE_DEFAULT;
    
    /// Carrier frequency in Hz
    float carrier_freq = CARRIER_FREQ_DEFAULT;
    
    /// Output amplitude (0.0 to 1.0)
    float amplitude = 0.8f;
    
    /// Add preamble to transmission
    bool include_preamble = true;
    
    /// Add EOM (End of Message) marker
    bool include_eom = true;
    
    /// Use RRC pulse shaping (improves spectrum, requires RX matched filter)
    bool use_pulse_shaping = false;
    
    /**
     * Validate configuration
     * @return OK if valid, error code otherwise
     */
    Result<void> validate() const {
        if (mode == Mode::AUTO) {
            return Error(ErrorCode::INVALID_MODE, "AUTO mode not valid for TX");
        }
        if (sample_rate != 8000.0f && sample_rate != 48000.0f) {
            return Error(ErrorCode::INVALID_SAMPLE_RATE, 
                        "Sample rate must be 8000 or 48000 Hz");
        }
        if (carrier_freq < 500.0f || carrier_freq > 3000.0f) {
            return Error(ErrorCode::INVALID_CARRIER_FREQ,
                        "Carrier frequency must be 500-3000 Hz");
        }
        if (amplitude < 0.0f || amplitude > 1.0f) {
            return Error(ErrorCode::INVALID_CONFIG, "Amplitude must be 0.0-1.0");
        }
        return Result<void>();
    }
    
    /**
     * Create default config for a mode
     */
    static TxConfig for_mode(Mode m) {
        TxConfig cfg;
        cfg.mode = m;
        return cfg;
    }
};

// ============================================================
// RX Configuration  
// ============================================================

/**
 * Receiver configuration
 */
struct RxConfig {
    /// Operating mode (AUTO for auto-detection)
    Mode mode = Mode::AUTO;
    
    /// Input sample rate in Hz
    float sample_rate = SAMPLE_RATE_DEFAULT;
    
    /// Expected carrier frequency in Hz
    float carrier_freq = CARRIER_FREQ_DEFAULT;
    
    /// Carrier frequency search range (+/- Hz)
    float freq_search_range = 100.0f;
    
    /// Equalizer algorithm
    Equalizer equalizer = Equalizer::DFE;
    
    /// Use Normalized LMS (NLMS) for DFE adaptation
    /// NLMS normalizes step size by input power for faster convergence
    /// on time-varying channels. Recommended for fading conditions.
    bool use_nlms = false;
    
    /// Enable adaptive phase tracking (decision-directed PLL)
    bool phase_tracking = true;
    
    /// Enable automatic gain control
    bool agc_enabled = true;
    
    /// Minimum SNR to attempt decode (dB)
    float min_snr_db = 3.0f;
    
    /// Maximum time to wait for signal (seconds, 0 = no timeout)
    float timeout_seconds = 0.0f;
    
    /**
     * Validate configuration
     */
    Result<void> validate() const {
        if (sample_rate != 8000.0f && sample_rate != 48000.0f) {
            return Error(ErrorCode::INVALID_SAMPLE_RATE,
                        "Sample rate must be 8000 or 48000 Hz");
        }
        if (carrier_freq < 500.0f || carrier_freq > 3000.0f) {
            return Error(ErrorCode::INVALID_CARRIER_FREQ,
                        "Carrier frequency must be 500-3000 Hz");
        }
        return Result<void>();
    }
    
    /**
     * Create default config
     */
    static RxConfig defaults() {
        return RxConfig();
    }
    
    /**
     * Create config for specific mode (no auto-detect)
     */
    static RxConfig for_mode(Mode m) {
        RxConfig cfg;
        cfg.mode = m;
        return cfg;
    }
};

// ============================================================
// Builder Pattern for Complex Configuration
// ============================================================

/**
 * Fluent builder for TxConfig
 */
class TxConfigBuilder {
public:
    TxConfigBuilder& mode(Mode m) { cfg_.mode = m; return *this; }
    TxConfigBuilder& sample_rate(float sr) { cfg_.sample_rate = sr; return *this; }
    TxConfigBuilder& carrier_freq(float cf) { cfg_.carrier_freq = cf; return *this; }
    TxConfigBuilder& amplitude(float a) { cfg_.amplitude = a; return *this; }
    TxConfigBuilder& with_preamble(bool p = true) { cfg_.include_preamble = p; return *this; }
    TxConfigBuilder& with_eom(bool e = true) { cfg_.include_eom = e; return *this; }
    
    Result<TxConfig> build() {
        auto result = cfg_.validate();
        if (!result.ok()) return result.error();
        return cfg_;
    }
    
private:
    TxConfig cfg_;
};

/**
 * Fluent builder for RxConfig
 */
class RxConfigBuilder {
public:
    RxConfigBuilder& mode(Mode m) { cfg_.mode = m; return *this; }
    RxConfigBuilder& sample_rate(float sr) { cfg_.sample_rate = sr; return *this; }
    RxConfigBuilder& carrier_freq(float cf) { cfg_.carrier_freq = cf; return *this; }
    RxConfigBuilder& freq_search(float range) { cfg_.freq_search_range = range; return *this; }
    RxConfigBuilder& equalizer(Equalizer eq) { cfg_.equalizer = eq; return *this; }
    RxConfigBuilder& agc(bool enabled) { cfg_.agc_enabled = enabled; return *this; }
    RxConfigBuilder& min_snr(float db) { cfg_.min_snr_db = db; return *this; }
    RxConfigBuilder& timeout(float seconds) { cfg_.timeout_seconds = seconds; return *this; }
    
    Result<RxConfig> build() {
        auto result = cfg_.validate();
        if (!result.ok()) return result.error();
        return cfg_;
    }
    
private:
    RxConfig cfg_;
};

} // namespace api
} // namespace m110a

#endif // M110A_API_MODEM_CONFIG_H
