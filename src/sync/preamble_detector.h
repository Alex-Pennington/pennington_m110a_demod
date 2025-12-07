#ifndef M110A_PREAMBLE_DETECTOR_H
#define M110A_PREAMBLE_DETECTOR_H

#include "common/types.h"
#include "common/constants.h"
#include "modem/scrambler.h"
#include "modem/symbol_mapper.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>

namespace m110a {

/**
 * Preamble detection state
 */
enum class SyncState {
    SEARCHING,      // Looking for preamble correlation
    CONFIRMING,     // Found peak, waiting for confirmation
    ACQUIRED,       // Sync acquired, ready to receive
    TRACKING        // Receiving data, tracking with probes
};

/**
 * Detection result returned when sync is acquired
 */
struct SyncResult {
    bool acquired;
    int sample_offset;
    float freq_offset_hz;
    float timing_offset;
    float snr_estimate;
    float correlation_peak;
    
    SyncResult() 
        : acquired(false)
        , sample_offset(0)
        , freq_offset_hz(0.0f)
        , timing_offset(0.0f)
        , snr_estimate(0.0f)
        , correlation_peak(0.0f) {}
};

/**
 * MIL-STD-188-110A Preamble Detector
 * 
 * The 110A preamble consists of repeated 0.2s segments (480 symbols each).
 * SHORT/ZERO preamble has 3 segments (0.6s), LONG has 24 segments (4.8s).
 * 
 * Detection strategy:
 * 1. Downconvert to baseband using nominal carrier frequency
 * 2. Match filter with SRRC
 * 3. Correlate against known preamble segment
 * 4. Detect repeated correlation peaks at 0.2s intervals
 * 5. Estimate frequency offset from correlation phase rotation
 * 6. Confirm sync with multiple peaks
 */
class PreambleDetector {
public:
    /**
     * Configuration for preamble detector
     */
    struct Config {
        float sample_rate;
        float carrier_freq;
        float detection_threshold;
        float confirmation_threshold;
        int required_peaks;
        int segment_symbols;
        
        Config()
            : sample_rate(SAMPLE_RATE)
            , carrier_freq(CARRIER_FREQ)
            , detection_threshold(0.4f)
            , confirmation_threshold(0.5f)
            , required_peaks(2)
            , segment_symbols(480) {}
    };
    
    explicit PreambleDetector(const Config& config = Config{});
    
    /**
     * Process a block of input samples
     * @param samples Input PCM samples
     * @return SyncResult with detection status
     */
    SyncResult process(const std::vector<sample_t>& samples);
    
    /**
     * Process single sample (for streaming)
     * @param sample Input sample
     * @return SyncResult (check acquired flag)
     */
    SyncResult process_sample(sample_t sample);
    
    /**
     * Reset detector to initial state
     */
    void reset();
    
    /**
     * Get current detection state
     */
    SyncState state() const { return state_; }
    
    /**
     * Get current correlation magnitude (for debugging/display)
     */
    float correlation_magnitude() const { return last_corr_mag_; }
    
    /**
     * Get reference preamble symbols (for external use)
     */
    const std::vector<complex_t>& reference_symbols() const { return ref_symbols_; }
    
    /**
     * Set callback for correlation output (for debugging/plotting)
     */
    using CorrCallback = std::function<void(int sample_idx, float mag, float phase)>;
    void set_correlation_callback(CorrCallback cb) { corr_callback_ = cb; }

private:
    Config config_;
    SyncState state_;
    
    // Reference preamble (one 0.2s segment, baseband symbols)
    std::vector<complex_t> ref_symbols_;
    
    // Reference preamble at sample rate (after upsampling, for correlation)
    std::vector<complex_t> ref_samples_;
    
    // DSP components
    NCO downconvert_nco_;
    std::unique_ptr<ComplexFirFilter> matched_filter_;
    std::vector<float> srrc_taps_;
    
    // Correlation state
    std::vector<complex_t> corr_buffer_;     // Circular buffer for correlation
    size_t corr_write_idx_;
    int samples_per_segment_;
    
    // Detection state
    float last_corr_mag_;
    float last_corr_phase_;
    int peak_count_;
    int samples_since_peak_;
    int first_peak_sample_;
    float accumulated_phase_;
    
    // Frequency estimation
    std::vector<float> peak_phases_;         // Phases at each detected peak
    std::vector<int> peak_positions_;        // Sample positions of peaks
    
    // Sample counter
    int total_samples_;
    
    // Callback for debugging
    CorrCallback corr_callback_;
    
    // Generate reference preamble segment
    void generate_reference();
    
    // Compute correlation at current position
    complex_t compute_correlation();
    
    // Estimate frequency offset from peak phases
    float estimate_frequency_offset();
    
    // Get samples per symbol
    float samples_per_symbol() const {
        return config_.sample_rate / SYMBOL_RATE;
    }
};

// ============================================================================
// Implementation
// ============================================================================

inline PreambleDetector::PreambleDetector(const Config& config)
    : config_(config)
    , state_(SyncState::SEARCHING)
    , downconvert_nco_(config.sample_rate, -config.carrier_freq)  // Negative for downconversion
    , corr_write_idx_(0)
    , last_corr_mag_(0.0f)
    , last_corr_phase_(0.0f)
    , peak_count_(0)
    , samples_since_peak_(0)
    , first_peak_sample_(0)
    , accumulated_phase_(0.0f)
    , total_samples_(0) {
    
    // Generate SRRC matched filter
    srrc_taps_ = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, samples_per_symbol());
    matched_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps_);
    
    // Calculate samples per segment
    samples_per_segment_ = static_cast<int>(config_.segment_symbols * samples_per_symbol());
    
    // Generate reference preamble
    generate_reference();
    
    // Allocate correlation buffer (one segment worth)
    corr_buffer_.resize(ref_samples_.size(), complex_t(0.0f, 0.0f));
}

inline void PreambleDetector::generate_reference() {
    // Generate one preamble segment (480 symbols)
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    ref_symbols_.clear();
    ref_symbols_.reserve(config_.segment_symbols);
    
    for (int i = 0; i < config_.segment_symbols; i++) {
        uint8_t tribit = scr.next_tribit();
        ref_symbols_.push_back(mapper.map(tribit));
    }
    
    // Upsample to sample rate for correlation
    // We use a simplified approach: place symbols at integer sample positions
    int sps = static_cast<int>(samples_per_symbol() + 0.5f);
    ref_samples_.clear();
    ref_samples_.reserve(config_.segment_symbols * sps);
    
    // Create upsampled reference (symbol, then zeros)
    ComplexFirFilter ref_filter(srrc_taps_);
    float gain = std::sqrt(static_cast<float>(sps));
    
    for (const auto& sym : ref_symbols_) {
        ref_samples_.push_back(ref_filter.process(sym * gain));
        for (int i = 1; i < sps; i++) {
            ref_samples_.push_back(ref_filter.process(complex_t(0.0f, 0.0f)));
        }
    }
    
    // Normalize reference for unit energy
    float energy = 0.0f;
    for (const auto& s : ref_samples_) {
        energy += std::norm(s);
    }
    float norm = std::sqrt(energy);
    if (norm > 0.0f) {
        for (auto& s : ref_samples_) {
            s /= norm;
        }
    }
}

inline void PreambleDetector::reset() {
    state_ = SyncState::SEARCHING;
    downconvert_nco_.reset();
    matched_filter_->reset();
    
    std::fill(corr_buffer_.begin(), corr_buffer_.end(), complex_t(0.0f, 0.0f));
    corr_write_idx_ = 0;
    
    last_corr_mag_ = 0.0f;
    last_corr_phase_ = 0.0f;
    peak_count_ = 0;
    samples_since_peak_ = 0;
    first_peak_sample_ = 0;
    accumulated_phase_ = 0.0f;
    total_samples_ = 0;
    
    peak_phases_.clear();
    peak_positions_.clear();
}

inline complex_t PreambleDetector::compute_correlation() {
    // Cross-correlate buffer with reference
    complex_t sum(0.0f, 0.0f);
    
    size_t buf_idx = corr_write_idx_;
    for (size_t i = 0; i < ref_samples_.size(); i++) {
        // Read from circular buffer (oldest first)
        sum += corr_buffer_[buf_idx] * std::conj(ref_samples_[i]);
        
        buf_idx++;
        if (buf_idx >= corr_buffer_.size()) {
            buf_idx = 0;
        }
    }
    
    return sum;
}

inline float PreambleDetector::estimate_frequency_offset() {
    if (peak_positions_.size() < 2) {
        return 0.0f;
    }
    
    // Linear regression of phase vs sample position
    // Frequency offset = d(phase)/d(sample) * sample_rate / (2*pi)
    
    float sum_x = 0.0f, sum_y = 0.0f, sum_xx = 0.0f, sum_xy = 0.0f;
    int n = static_cast<int>(peak_positions_.size());
    
    // Unwrap phases first
    std::vector<float> unwrapped_phases = peak_phases_;
    for (size_t i = 1; i < unwrapped_phases.size(); i++) {
        float diff = unwrapped_phases[i] - unwrapped_phases[i-1];
        while (diff > PI) diff -= 2.0f * PI;
        while (diff < -PI) diff += 2.0f * PI;
        unwrapped_phases[i] = unwrapped_phases[i-1] + diff;
    }
    
    for (int i = 0; i < n; i++) {
        float x = static_cast<float>(peak_positions_[i]);
        float y = unwrapped_phases[i];
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }
    
    float denom = n * sum_xx - sum_x * sum_x;
    if (std::abs(denom) < 1e-10f) {
        return 0.0f;
    }
    
    float slope = (n * sum_xy - sum_x * sum_y) / denom;
    
    // Convert phase slope to frequency
    return slope * config_.sample_rate / (2.0f * PI);
}

inline SyncResult PreambleDetector::process_sample(sample_t sample) {
    SyncResult result;
    
    // Downconvert to baseband
    complex_t baseband = downconvert_nco_.mix(sample);
    
    // Match filter
    complex_t filtered = matched_filter_->process(baseband);
    
    // Add to correlation buffer
    corr_buffer_[corr_write_idx_] = filtered;
    corr_write_idx_++;
    if (corr_write_idx_ >= corr_buffer_.size()) {
        corr_write_idx_ = 0;
    }
    
    // Compute correlation
    complex_t corr = compute_correlation();
    float mag = std::abs(corr);
    float phase = std::arg(corr);
    
    // Normalize by buffer energy for stable threshold
    float buffer_energy = 0.0f;
    for (const auto& s : corr_buffer_) {
        buffer_energy += std::norm(s);
    }
    float norm_mag = (buffer_energy > 0.0f) ? mag / std::sqrt(buffer_energy) : 0.0f;
    
    last_corr_mag_ = norm_mag;
    last_corr_phase_ = phase;
    
    // Callback for debugging
    if (corr_callback_) {
        corr_callback_(total_samples_, norm_mag, phase);
    }
    
    samples_since_peak_++;
    total_samples_++;
    
    // State machine
    switch (state_) {
        case SyncState::SEARCHING:
            if (norm_mag > config_.detection_threshold) {
                // Potential peak found
                state_ = SyncState::CONFIRMING;
                peak_count_ = 1;
                first_peak_sample_ = total_samples_;
                samples_since_peak_ = 0;
                accumulated_phase_ = phase;
                
                peak_phases_.clear();
                peak_positions_.clear();
                peak_phases_.push_back(phase);
                peak_positions_.push_back(total_samples_);
            }
            break;
            
        case SyncState::CONFIRMING: {
            // Look for next peak at expected position (0.2s later)
            int expected_spacing = samples_per_segment_;
            int tolerance = expected_spacing / 10;  // 10% tolerance
            
            if (samples_since_peak_ >= expected_spacing - tolerance &&
                samples_since_peak_ <= expected_spacing + tolerance) {
                
                if (norm_mag > config_.confirmation_threshold) {
                    // Found another peak at expected position
                    peak_count_++;
                    samples_since_peak_ = 0;
                    
                    peak_phases_.push_back(phase);
                    peak_positions_.push_back(total_samples_);
                    
                    if (peak_count_ >= config_.required_peaks) {
                        // Sync acquired!
                        state_ = SyncState::ACQUIRED;
                        
                        result.acquired = true;
                        result.sample_offset = first_peak_sample_;
                        result.freq_offset_hz = estimate_frequency_offset();
                        result.correlation_peak = norm_mag;
                        
                        // Calculate timing phase from peak position
                        // The peak position modulo SPS gives the optimal sample phase
                        float sps = samples_per_symbol();
                        int peak_mod = first_peak_sample_ % static_cast<int>(std::ceil(sps));
                        result.timing_offset = static_cast<float>(peak_mod) / sps;
                        
                        // Rough SNR estimate from correlation
                        result.snr_estimate = 10.0f * std::log10(norm_mag / (1.0f - norm_mag + 0.01f));
                    }
                }
            }
            else if (samples_since_peak_ > expected_spacing + tolerance) {
                // Missed expected peak, reset
                state_ = SyncState::SEARCHING;
                peak_count_ = 0;
                peak_phases_.clear();
                peak_positions_.clear();
            }
            break;
        }
            
        case SyncState::ACQUIRED:
        case SyncState::TRACKING:
            // Already synced
            result.acquired = true;
            break;
    }
    
    return result;
}

inline SyncResult PreambleDetector::process(const std::vector<sample_t>& samples) {
    SyncResult result;
    
    for (const auto& s : samples) {
        result = process_sample(s);
        if (result.acquired && state_ == SyncState::ACQUIRED) {
            // Just acquired - return immediately with position info
            return result;
        }
    }
    
    return result;
}

} // namespace m110a

#endif // M110A_PREAMBLE_DETECTOR_H
