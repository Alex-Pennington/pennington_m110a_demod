#ifndef M110A_FREQ_SEARCH_DETECTOR_H
#define M110A_FREQ_SEARCH_DETECTOR_H

/**
 * Frequency-Searching Preamble Detector
 * 
 * Wraps the standard PreambleDetector to add frequency search capability.
 * Tries multiple frequency hypotheses to find the best match, enabling
 * acquisition with frequency offsets up to ±50 Hz (configurable).
 * 
 * Usage:
 *   FreqSearchDetector detector(config);
 *   auto result = detector.detect(rf_samples);
 *   if (result.acquired) {
 *       // Use result.freq_offset_hz to compensate RX NCO
 *   }
 */

#include "preamble_detector.h"
#include <vector>

namespace m110a {

struct FreqSearchResult {
    bool acquired;
    float freq_offset_hz;      // Total estimated frequency offset from nominal
    float timing_offset;       // Timing phase (0.0 to 1.0)
    float correlation_peak;    // Best correlation magnitude
    int sample_offset;         // Sample index of first correlation peak
    float snr_estimate;        // SNR estimate from correlation
    
    FreqSearchResult()
        : acquired(false)
        , freq_offset_hz(0.0f)
        , timing_offset(0.0f)
        , correlation_peak(0.0f)
        , sample_offset(0)
        , snr_estimate(0.0f) {}
};

class FreqSearchDetector {
public:
    struct Config {
        float sample_rate;
        float carrier_freq;          // Nominal carrier frequency
        float freq_search_range;     // Search ± this many Hz
        float freq_step;             // Step size for frequency search
        float detection_threshold;
        float confirmation_threshold;
        int required_peaks;
        int segment_symbols;
        
        Config()
            : sample_rate(SAMPLE_RATE)
            , carrier_freq(CARRIER_FREQ)
            , freq_search_range(50.0f)   // ±50 Hz default
            , freq_step(5.0f)            // 5 Hz steps
            , detection_threshold(0.3f)
            , confirmation_threshold(0.3f)
            , required_peaks(2)
            , segment_symbols(480) {}
    };
    
    explicit FreqSearchDetector(const Config& config = Config{})
        : config_(config) {}
    
    /**
     * Detect preamble with frequency search
     * @param samples Input RF samples
     * @return FreqSearchResult with detection status and frequency estimate
     */
    FreqSearchResult detect(const std::vector<float>& samples) {
        FreqSearchResult best;
        best.correlation_peak = 0.0f;
        
        // Try frequency hypotheses from negative to positive
        for (float freq_offset = -config_.freq_search_range;
             freq_offset <= config_.freq_search_range;
             freq_offset += config_.freq_step) {
            
            // Configure preamble detector for this frequency hypothesis
            PreambleDetector::Config det_config;
            det_config.sample_rate = config_.sample_rate;
            det_config.carrier_freq = config_.carrier_freq + freq_offset;
            det_config.detection_threshold = config_.detection_threshold;
            det_config.confirmation_threshold = config_.confirmation_threshold;
            det_config.required_peaks = config_.required_peaks;
            det_config.segment_symbols = config_.segment_symbols;
            
            PreambleDetector detector(det_config);
            SyncResult sync;
            float max_corr_this_freq = 0.0f;
            SyncResult best_sync_this_freq;
            
            // Process all samples, track best correlation for this frequency
            for (float s : samples) {
                sync = detector.process_sample(s);
                
                // Track peak correlation at this frequency
                float corr = detector.correlation_magnitude();
                if (corr > max_corr_this_freq) {
                    max_corr_this_freq = corr;
                }
                
                if (sync.acquired) {
                    best_sync_this_freq = sync;
                    break;  // Got sync at this frequency
                }
            }
            
            // If this frequency acquired AND has better correlation than previous best
            if (best_sync_this_freq.acquired && max_corr_this_freq > best.correlation_peak) {
                best.acquired = true;
                // The frequency offset is just the search offset
                // (don't add sync.freq_offset_hz as it's often noisy)
                best.freq_offset_hz = freq_offset;
                best.timing_offset = best_sync_this_freq.timing_offset;
                best.correlation_peak = max_corr_this_freq;
                best.sample_offset = best_sync_this_freq.sample_offset;
                best.snr_estimate = best_sync_this_freq.snr_estimate;
            }
        }
        
        return best;
    }
    
    /**
     * Get the frequency search range
     */
    float search_range() const { return config_.freq_search_range; }
    
    /**
     * Get the frequency step size
     */
    float freq_step() const { return config_.freq_step; }

private:
    Config config_;
};

} // namespace m110a

#endif // M110A_FREQ_SEARCH_DETECTOR_H
