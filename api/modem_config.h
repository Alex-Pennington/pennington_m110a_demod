/**
 * @file modem_config.h
 * @brief Configuration structures for M110A Modem API
 */

#ifndef M110A_API_MODEM_CONFIG_H
#define M110A_API_MODEM_CONFIG_H

#include "modem_types.h"

namespace m110a {
namespace api {

/// The TX and RX run a whole number of samples per 2400 Bd symbol; of the rates
/// they were written for, only 48 kHz gives one (8 kHz gives 3.33, and the symbol
/// clock would run at 8000 / 3 = 2667 Bd).
inline constexpr const char* SAMPLE_RATE_RULE =
    "Sample rate must be 48000 Hz (a whole number of samples per 2400 Bd symbol)";

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
    
    /// Output amplitude (0.0 to 1.0): the carrier peak of a unit 8PSK symbol, so the
    /// signal RMS is amplitude / sqrt(2) with or without pulse shaping. RRC pulses
    /// peak higher than that (about 1.5 x amplitude).
    /// Default 0.32 matches MS-DMT reference output levels (~7000 RMS)
    float amplitude = 0.32f;
    
    /// Add preamble to transmission
    bool include_preamble = true;
    
    /// End the message with the EOM 4B65A5B2 and the 144-bit flush (false: no EOM)
    bool include_eom = true;
    
    /// Root-raised-cosine pulses (alpha 0.35), matched to the receiver's SRRC filter.
    /// false sends rectangular pulses, which leave ISI after any matched receiver and
    /// cost 4800S about 12 dB (docs/TX_CHAIN.md, Measured performance)
    bool use_pulse_shaping = true;
    
    /// Add calibration burst for Brain Modem compatibility
    /// Brain TX starts with: 2 symbols burst + 8 symbols silence + ramp into preamble
    /// This allows AGC settling and carrier lock for receivers
    bool include_leading_symbols = true;
    
    /// Soft ramp-up duration in milliseconds (0 = disabled)
    /// Note: Brain TX uses calibration burst instead of soft ramp, so default is 0
    float soft_ramp_ms = 0.0f;
    
    /**
     * Validate configuration
     * @return OK if valid, error code otherwise
     */
    Result<void> validate() const {
        if (mode == Mode::AUTO) {
            return Error(ErrorCode::INVALID_MODE, "AUTO mode not valid for TX");
        }
        if (sample_rate != 48000.0f) {
            return Error(ErrorCode::INVALID_SAMPLE_RATE, SAMPLE_RATE_RULE);
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
    // AFC search range (Hz) - searches from -range to +range
    // Default: ±10 Hz with two-stage AFC (FFT coarse + preamble fine)
    // Note: Preamble-only AFC limited to ~±2 Hz (see use_fft_coarse_afc)
    float freq_search_range = 10.0f;
    
    /// Enable two-stage AFC (coarse + preamble fine)
    // When true: coarse estimate from the common preamble's block-to-block
    //            phase step (sync/preamble_acq.h, unambiguous over ±37.5 Hz),
    //            then preamble search ±fine_search_range around it
    // When false: Uses legacy preamble-only AFC (±2 Hz practical limit)
    bool use_fft_coarse_afc = true;
    
    /// Largest coarse AFC estimate accepted (Hz)
    // Only used when use_fft_coarse_afc = true. Default ±30 Hz: margin
    // beyond the ±10 Hz of MIL-STD-188-110A and an SDR's clock error
    // (1 ppm is 7 Hz at 7 MHz).
    float coarse_search_range = 30.0f;
    
    /// Fine AFC search range (Hz) - preamble correlation around coarse estimate
    // Only used when use_fft_coarse_afc = true
    // Default: ±2.5 Hz (preamble correlation works reliably within this range)
    float fine_search_range = 2.5f;
    
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
    
    // === RX Improvement Options (v1.1) ===
    
    /// Use SNR-weighted soft demapper for better Viterbi input
    /// Computes proper LLRs based on estimated channel SNR
    /// Default: true (proven improvement in testing)
    bool use_snr_weighted_demapper = true;
    
    /// Use probe-based SNR estimation for soft demapper
    /// If false, assumes fixed SNR of 20 dB
    bool estimate_snr_from_probes = true;
    
    /// Fixed SNR assumption when not estimating (dB)
    /// Only used when estimate_snr_from_probes = false
    float assumed_snr_db = 20.0f;
    
    /**
     * Validate configuration
     */
    Result<void> validate() const {
        if (sample_rate != 48000.0f) {
            return Error(ErrorCode::INVALID_SAMPLE_RATE, SAMPLE_RATE_RULE);
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
    TxConfigBuilder& with_leading_symbols(bool l = true) { cfg_.include_leading_symbols = l; return *this; }
    TxConfigBuilder& soft_ramp(float ms) { cfg_.soft_ramp_ms = ms; return *this; }
    
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
