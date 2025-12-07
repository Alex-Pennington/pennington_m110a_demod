#ifndef M110A_CHANNEL_ESTIMATOR_H
#define M110A_CHANNEL_ESTIMATOR_H

/**
 * Channel Estimator for MIL-STD-188-110A
 * 
 * Uses the 16 known probe symbols per frame for:
 *   1. Channel amplitude/phase estimation
 *   2. SNR estimation
 *   3. Fine frequency tracking
 *   4. Channel quality metrics
 * 
 * Frame structure: 32 data symbols + 16 probe symbols = 48 total
 * Probes are scrambled using SCRAMBLER_INIT_PREAMBLE
 */

#include "common/types.h"
#include "common/constants.h"
#include "modem/scrambler.h"
#include "modem/symbol_mapper.h"
#include <vector>
#include <cmath>
#include <array>
#include <numeric>
#include <algorithm>

namespace m110a {

/**
 * Channel estimate from a single probe block
 */
struct ChannelEstimate {
    complex_t gain;           // Complex channel gain (amplitude and phase)
    float amplitude;          // |gain|
    float phase_offset;       // arg(gain) in radians
    float snr_db;             // Estimated SNR in dB
    float noise_variance;     // Estimated noise variance
    float freq_offset_hz;     // Estimated frequency offset
    bool valid;               // True if estimate is reliable
    
    ChannelEstimate()
        : gain(1.0f, 0.0f)
        , amplitude(1.0f)
        , phase_offset(0.0f)
        , snr_db(30.0f)
        , noise_variance(0.001f)
        , freq_offset_hz(0.0f)
        , valid(false) {}
};

/**
 * Channel Estimator Class
 */
class ChannelEstimator {
public:
    struct Config {
        int probe_symbols;           // Number of probe symbols (16)
        float alpha;                 // Smoothing factor for estimates (0-1)
        float min_snr_threshold;     // Minimum SNR to consider valid (dB)
        float symbol_rate;           // For frequency offset calculation
        
        Config()
            : probe_symbols(PROBE_SYMBOLS_PER_FRAME)
            , alpha(0.3f)            // 30% new, 70% old
            , min_snr_threshold(5.0f)
            , symbol_rate(SYMBOL_RATE) {}
    };
    
    struct Stats {
        int frames_processed;
        float avg_snr_db;
        float avg_amplitude;
        float freq_offset_hz;
        float phase_variance;        // Phase estimate stability
        
        Stats() 
            : frames_processed(0)
            , avg_snr_db(0.0f)
            , avg_amplitude(1.0f)
            , freq_offset_hz(0.0f)
            , phase_variance(0.0f) {}
    };
    
    explicit ChannelEstimator(const Config& cfg = Config{})
        : config_(cfg)
        , stats_()
        , current_estimate_()
        , prev_phase_(0.0f)
        , phase_accumulator_(0.0f)
        , probe_count_(0) {
        
        generate_probe_reference();
    }
    
    void reset() {
        stats_ = Stats();
        current_estimate_ = ChannelEstimate();
        prev_phase_ = 0.0f;
        phase_accumulator_ = 0.0f;
        probe_count_ = 0;
    }
    
    /**
     * Process a probe block and update channel estimate
     * @param received Received probe symbols (16 symbols)
     * @param frame_number Frame number for probe scrambler state (0-based)
     * @return Channel estimate for this probe block
     */
    ChannelEstimate process_probes(const std::vector<complex_t>& received,
                                    int frame_number = -1) {
        
        if (received.size() < static_cast<size_t>(config_.probe_symbols)) {
            return ChannelEstimate();  // Invalid
        }
        
        // Get probe reference (may vary by frame if scrambler advances)
        const auto& ref = (frame_number >= 0) 
            ? get_probe_reference(frame_number) 
            : probe_ref_;
        
        // Compute channel estimate via correlation
        // H = sum(rx * conj(ref)) / sum(|ref|^2)
        complex_t correlation(0.0f, 0.0f);
        float ref_power = 0.0f;
        
        for (int i = 0; i < config_.probe_symbols; i++) {
            correlation += received[i] * std::conj(ref[i]);
            ref_power += std::norm(ref[i]);
        }
        
        complex_t channel_gain = correlation / ref_power;
        
        // Compute error/noise after channel compensation
        float signal_power = 0.0f;
        float error_power = 0.0f;
        
        for (int i = 0; i < config_.probe_symbols; i++) {
            complex_t compensated = received[i] / channel_gain;
            complex_t error = compensated - ref[i];
            
            signal_power += std::norm(ref[i]);
            error_power += std::norm(error);
        }
        
        // Estimate SNR
        float noise_var = error_power / config_.probe_symbols;
        float snr_linear = (signal_power / config_.probe_symbols) / std::max(noise_var, 1e-10f);
        float snr_db = 10.0f * std::log10(snr_linear);
        
        // Extract phase and track frequency offset
        float current_phase = std::arg(channel_gain);
        float phase_delta = current_phase - prev_phase_;
        
        // Unwrap phase
        while (phase_delta > PI) phase_delta -= 2.0f * PI;
        while (phase_delta < -PI) phase_delta += 2.0f * PI;
        
        // Frequency offset: phase change per frame → Hz
        // Each frame is (DATA_SYMBOLS + PROBE_SYMBOLS) / SYMBOL_RATE seconds
        float frame_duration = FRAME_SYMBOLS / config_.symbol_rate;
        float freq_offset = (probe_count_ > 0) ? 
            (phase_delta / (2.0f * PI)) / frame_duration : 0.0f;
        
        prev_phase_ = current_phase;
        
        // Build estimate
        ChannelEstimate est;
        est.gain = channel_gain;
        est.amplitude = std::abs(channel_gain);
        est.phase_offset = current_phase;
        est.snr_db = snr_db;
        est.noise_variance = noise_var;
        est.freq_offset_hz = freq_offset;
        est.valid = (snr_db >= config_.min_snr_threshold);
        
        // Update smoothed estimates
        if (probe_count_ == 0) {
            current_estimate_ = est;
        } else {
            float a = config_.alpha;
            current_estimate_.gain = a * est.gain + (1.0f - a) * current_estimate_.gain;
            current_estimate_.amplitude = std::abs(current_estimate_.gain);
            current_estimate_.phase_offset = std::arg(current_estimate_.gain);
            current_estimate_.snr_db = a * est.snr_db + (1.0f - a) * current_estimate_.snr_db;
            current_estimate_.noise_variance = a * est.noise_variance + 
                                               (1.0f - a) * current_estimate_.noise_variance;
            current_estimate_.freq_offset_hz = a * est.freq_offset_hz + 
                                               (1.0f - a) * current_estimate_.freq_offset_hz;
            current_estimate_.valid = est.valid;
        }
        
        // Update stats
        probe_count_++;
        stats_.frames_processed++;
        stats_.avg_snr_db = current_estimate_.snr_db;
        stats_.avg_amplitude = current_estimate_.amplitude;
        stats_.freq_offset_hz = current_estimate_.freq_offset_hz;
        
        return est;
    }
    
    /**
     * Apply channel compensation to a symbol
     * @param symbol Received symbol
     * @return Compensated symbol
     */
    complex_t compensate(complex_t symbol) const {
        if (std::abs(current_estimate_.gain) < 0.01f) {
            return symbol;
        }
        return symbol / current_estimate_.gain;
    }
    
    /**
     * Apply channel compensation to a block of symbols
     */
    std::vector<complex_t> compensate_block(const std::vector<complex_t>& symbols) const {
        std::vector<complex_t> out;
        out.reserve(symbols.size());
        
        for (const auto& s : symbols) {
            out.push_back(compensate(s));
        }
        return out;
    }
    
    /**
     * Get soft bit scaling based on SNR
     * Higher SNR → more confident soft bits
     */
    float get_soft_scale() const {
        // Scale based on SNR (higher SNR = more confident)
        float snr_linear = std::pow(10.0f, current_estimate_.snr_db / 10.0f);
        return std::sqrt(snr_linear);
    }
    
    /**
     * Estimate channel quality for adaptation decisions
     * @return Quality metric 0-1 (1 = excellent)
     */
    float channel_quality() const {
        if (!current_estimate_.valid) return 0.0f;
        
        // Combine SNR and amplitude stability
        float snr_factor = std::min(1.0f, current_estimate_.snr_db / 20.0f);
        float amp_factor = (current_estimate_.amplitude > 0.5f && 
                           current_estimate_.amplitude < 2.0f) ? 1.0f : 0.5f;
        
        return snr_factor * amp_factor;
    }
    
    // Accessors
    const ChannelEstimate& estimate() const { return current_estimate_; }
    const Stats& stats() const { return stats_; }
    const std::vector<complex_t>& probe_reference() const { return probe_ref_; }
    
    /**
     * Get probe reference for a specific frame
     * The scrambler state advances each frame
     */
    std::vector<complex_t> get_probe_reference(int frame_number) const {
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
        SymbolMapper mapper;
        
        // Advance scrambler to correct state for this frame
        for (int f = 0; f < frame_number; f++) {
            for (int i = 0; i < config_.probe_symbols; i++) {
                scr.next_tribit();
            }
        }
        
        // Generate probe symbols for this frame
        std::vector<complex_t> ref;
        ref.reserve(config_.probe_symbols);
        for (int i = 0; i < config_.probe_symbols; i++) {
            ref.push_back(mapper.map(scr.next_tribit()));
        }
        
        return ref;
    }

private:
    Config config_;
    Stats stats_;
    ChannelEstimate current_estimate_;
    
    std::vector<complex_t> probe_ref_;   // Base probe reference (frame 0)
    
    float prev_phase_;
    float phase_accumulator_;
    int probe_count_;
    
    void generate_probe_reference() {
        probe_ref_ = get_probe_reference(0);
    }
};

/**
 * Per-symbol channel tracker using probe interpolation
 * 
 * Interpolates channel estimates between probe blocks to provide
 * per-symbol compensation for time-varying channels.
 */
class ChannelTracker {
public:
    struct Config {
        int data_symbols;      // Symbols between probes (32)
        int probe_symbols;     // Probe symbols per frame (16)
        float interp_alpha;    // Interpolation smoothing
        
        Config()
            : data_symbols(DATA_SYMBOLS_PER_FRAME)
            , probe_symbols(PROBE_SYMBOLS_PER_FRAME)
            , interp_alpha(0.5f) {}
    };
    
    explicit ChannelTracker(const Config& cfg = Config{})
        : config_(cfg)
        , estimator_()
        , prev_gain_(1.0f, 0.0f)
        , next_gain_(1.0f, 0.0f)
        , symbol_in_frame_(0)
        , frame_count_(0) {}
    
    void reset() {
        estimator_.reset();
        prev_gain_ = complex_t(1.0f, 0.0f);
        next_gain_ = complex_t(1.0f, 0.0f);
        symbol_in_frame_ = 0;
        frame_count_ = 0;
    }
    
    /**
     * Process incoming symbol (data or probe)
     * @param symbol Received symbol
     * @param is_probe True if this is a probe symbol
     * @param probe_ref Reference symbol (for probes)
     * @return Compensated symbol (for data) or original (for probes)
     */
    complex_t process(complex_t symbol, bool is_probe = false,
                      complex_t probe_ref = complex_t(0, 0)) {
        
        if (is_probe) {
            // Accumulate probe symbols
            probe_buffer_.push_back(symbol);
            
            if (static_cast<int>(probe_buffer_.size()) >= config_.probe_symbols) {
                // Process complete probe block
                auto est = estimator_.process_probes(probe_buffer_, frame_count_);
                
                // Update interpolation endpoints
                prev_gain_ = next_gain_;
                next_gain_ = est.gain;
                
                probe_buffer_.clear();
                symbol_in_frame_ = 0;
                frame_count_++;
            }
            
            return symbol;
        } else {
            // Data symbol - interpolate channel and compensate
            float t = static_cast<float>(symbol_in_frame_) / config_.data_symbols;
            complex_t interp_gain = prev_gain_ * (1.0f - t) + next_gain_ * t;
            
            symbol_in_frame_++;
            
            if (std::abs(interp_gain) > 0.01f) {
                return symbol / interp_gain;
            }
            return symbol;
        }
    }
    
    /**
     * Process a complete frame (32 data + 16 probe)
     * @param frame All 48 symbols
     * @param data_out Compensated data symbols (32)
     * @return True if successful
     */
    bool process_frame(const std::vector<complex_t>& frame,
                       std::vector<complex_t>& data_out) {
        
        if (frame.size() < FRAME_SYMBOLS) return false;
        
        // First process probes to update estimate
        std::vector<complex_t> probes(
            frame.begin() + config_.data_symbols,
            frame.begin() + config_.data_symbols + config_.probe_symbols);
        
        auto est = estimator_.process_probes(probes, frame_count_);
        
        // Update interpolation
        prev_gain_ = next_gain_;
        next_gain_ = est.gain;
        frame_count_++;
        
        // Now compensate data symbols with interpolation
        for (int i = 0; i < config_.data_symbols; i++) {
            float t = static_cast<float>(i) / config_.data_symbols;
            complex_t interp_gain = prev_gain_ * (1.0f - t) + next_gain_ * t;
            
            complex_t compensated = frame[i];
            if (std::abs(interp_gain) > 0.01f) {
                compensated = frame[i] / interp_gain;
            }
            data_out.push_back(compensated);
        }
        
        return true;
    }
    
    // Accessors
    const ChannelEstimator& estimator() const { return estimator_; }
    const ChannelEstimate& estimate() const { return estimator_.estimate(); }
    int frame_count() const { return frame_count_; }

private:
    Config config_;
    ChannelEstimator estimator_;
    
    complex_t prev_gain_;
    complex_t next_gain_;
    int symbol_in_frame_;
    int frame_count_;
    
    std::vector<complex_t> probe_buffer_;
};

/**
 * Fine frequency tracker using probe symbols
 * 
 * Measures phase rotation between consecutive probe blocks to
 * estimate and correct residual frequency offset.
 */
class ProbeFrequencyTracker {
public:
    struct Config {
        float loop_bw;         // Loop bandwidth (normalized)
        float damping;         // Damping factor
        float symbol_rate;     // For Hz conversion
        int frame_symbols;     // Symbols per frame
        
        Config()
            : loop_bw(0.005f)
            , damping(0.707f)
            , symbol_rate(SYMBOL_RATE)
            , frame_symbols(FRAME_SYMBOLS) {}
    };
    
    explicit ProbeFrequencyTracker(const Config& cfg = Config{})
        : config_(cfg)
        , phase_accumulator_(0.0f)
        , freq_estimate_(0.0f)
        , integrator_(0.0f)
        , prev_probe_phase_(0.0f)
        , frame_count_(0) {
        
        // Compute loop gains
        float BnT = cfg.loop_bw;
        float zeta = cfg.damping;
        float denom = 1.0f + 2.0f * zeta * BnT + BnT * BnT;
        Kp_ = (4.0f * zeta * BnT) / denom;
        Ki_ = (4.0f * BnT * BnT) / denom;
    }
    
    void reset() {
        phase_accumulator_ = 0.0f;
        freq_estimate_ = 0.0f;
        integrator_ = 0.0f;
        prev_probe_phase_ = 0.0f;
        frame_count_ = 0;
    }
    
    /**
     * Update frequency estimate from probe block
     * @param probe_correlation Result of correlating received probes with reference
     */
    void update_from_probes(complex_t probe_correlation) {
        float current_phase = std::arg(probe_correlation);
        
        if (frame_count_ > 0) {
            // Phase error (difference from expected)
            float phase_error = current_phase - prev_probe_phase_;
            
            // Unwrap
            while (phase_error > PI) phase_error -= 2.0f * PI;
            while (phase_error < -PI) phase_error += 2.0f * PI;
            
            // PI controller
            float prop = Kp_ * phase_error;
            integrator_ += Ki_ * phase_error;
            integrator_ = std::clamp(integrator_, -0.1f, 0.1f);
            
            freq_estimate_ = prop + integrator_;
        }
        
        prev_probe_phase_ = current_phase;
        frame_count_++;
    }
    
    /**
     * Apply frequency correction to a symbol
     * Call this for each symbol between probe updates
     */
    complex_t correct(complex_t symbol) {
        complex_t correction = std::polar(1.0f, -phase_accumulator_);
        phase_accumulator_ += freq_estimate_;
        
        // Wrap accumulator
        while (phase_accumulator_ > PI) phase_accumulator_ -= 2.0f * PI;
        while (phase_accumulator_ < -PI) phase_accumulator_ += 2.0f * PI;
        
        return symbol * correction;
    }
    
    /**
     * Get estimated frequency offset in Hz
     */
    float frequency_offset_hz() const {
        // freq_estimate_ is phase change per symbol
        return (freq_estimate_ / (2.0f * PI)) * config_.symbol_rate;
    }
    
    float phase_accumulator() const { return phase_accumulator_; }
    int frame_count() const { return frame_count_; }

private:
    Config config_;
    float phase_accumulator_;
    float freq_estimate_;      // Radians per symbol
    float integrator_;
    float prev_probe_phase_;
    int frame_count_;
    
    float Kp_, Ki_;            // Loop gains
};

} // namespace m110a

#endif // M110A_CHANNEL_ESTIMATOR_H
