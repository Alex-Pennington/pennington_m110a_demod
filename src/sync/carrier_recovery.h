#ifndef M110A_CARRIER_RECOVERY_H
#define M110A_CARRIER_RECOVERY_H

#include "common/types.h"
#include "common/constants.h"
#include "modem/symbol_mapper.h"
#include "sync/timing_recovery.h"
#include <cmath>
#include <algorithm>
#include <memory>

namespace m110a {

/**
 * Second-order loop filter for carrier recovery
 * Implements a PI controller with configurable bandwidth and damping
 */
class CarrierLoopFilter {
public:
    struct Config {
        float bandwidth;      // Loop bandwidth (normalized to symbol rate)
        float damping;        // Damping factor (0.707 = critical)
        
        Config() : bandwidth(0.02f), damping(0.707f) {}
    };
    
    explicit CarrierLoopFilter(const Config& config = Config{}) {
        configure(config);
        reset();
    }
    
    void configure(const Config& config) {
        // Compute loop gains using standard formulas
        float BnT = config.bandwidth;
        float zeta = config.damping;
        
        // Natural frequency
        float wn = 2.0f * BnT / (zeta + 1.0f / (4.0f * zeta));
        
        // Loop gains
        Kp_ = 2.0f * zeta * wn;
        Ki_ = wn * wn;
    }
    
    void reset() {
        integrator_ = 0.0f;
        freq_estimate_ = 0.0f;
    }
    
    /**
     * Filter phase error to produce frequency/phase adjustment
     * @param error Phase error in radians
     * @return Phase adjustment for NCO
     */
    float filter(float error) {
        // Proportional path (phase correction)
        float prop = Kp_ * error;
        
        // Integral path (frequency correction)
        integrator_ += Ki_ * error;
        
        // Limit integrator to prevent windup (~±50 Hz at 2400 baud)
        float max_freq = 0.1f;  // Normalized to symbol rate
        integrator_ = std::clamp(integrator_, -max_freq, max_freq);
        
        freq_estimate_ = integrator_;
        
        return prop + integrator_;
    }
    
    /**
     * Get estimated frequency offset (normalized to symbol rate)
     */
    float frequency_estimate() const { return freq_estimate_; }
    
    /**
     * Get frequency estimate in Hz
     */
    float frequency_hz(float symbol_rate = SYMBOL_RATE) const {
        return freq_estimate_ * symbol_rate;
    }

private:
    float Kp_;              // Proportional gain
    float Ki_;              // Integral gain
    float integrator_;      // Integrator state
    float freq_estimate_;   // Current frequency estimate
};

/**
 * Decision-Directed Phase Detector for 8-PSK
 * 
 * Computes phase error by comparing received symbol to nearest
 * constellation point. Works after timing recovery when symbols
 * are properly sampled.
 */
class PhaseDetector8PSK {
public:
    PhaseDetector8PSK() = default;
    
    /**
     * Compute phase error for 8-PSK symbol
     * @param symbol Received complex symbol (after timing recovery)
     * @return Phase error in radians (positive = phase leads)
     */
    float compute(complex_t symbol) {
        // Skip very small symbols (noise/transients)
        float mag = std::abs(symbol);
        if (mag < 0.1f) {
            return 0.0f;
        }
        
        // Get phase of received symbol
        float rx_phase = std::arg(symbol);
        
        // Quantize to nearest 8-PSK point (45° spacing)
        // Phase points: 0, 45, 90, 135, 180, -135, -90, -45 degrees
        float phase_step = PI / 4.0f;  // 45 degrees
        
        // Find nearest constellation point
        int sector = static_cast<int>(std::round(rx_phase / phase_step));
        float ideal_phase = sector * phase_step;
        
        // Phase error is difference between received and ideal
        float error = rx_phase - ideal_phase;
        
        // Wrap to [-pi/8, pi/8] for 8-PSK (half the sector width)
        while (error > phase_step / 2.0f) error -= phase_step;
        while (error < -phase_step / 2.0f) error += phase_step;
        
        return error;
    }
    
    /**
     * Get the decided symbol (nearest constellation point)
     * @param symbol Received symbol
     * @return Index 0-7 of nearest constellation point
     */
    int hard_decision(complex_t symbol) const {
        float phase = std::arg(symbol);
        float phase_step = PI / 4.0f;
        int sector = static_cast<int>(std::round(phase / phase_step)) & 0x7;
        return sector;
    }
};

/**
 * Complete Carrier Recovery System for 8-PSK
 * 
 * Uses decision-directed phase detection with a second-order
 * loop filter to track carrier phase and frequency offsets.
 * 
 * Input: Symbol-rate complex samples from timing recovery
 * Output: Phase-corrected symbols ready for demodulation
 */
class CarrierRecovery {
public:
    struct Config {
        float symbol_rate;
        float loop_bandwidth;     // Normalized to symbol rate
        float loop_damping;
        float initial_phase;      // Initial phase estimate (radians)
        float initial_freq;       // Initial frequency estimate (Hz)
        
        Config()
            : symbol_rate(SYMBOL_RATE)
            , loop_bandwidth(0.02f)
            , loop_damping(0.707f)
            , initial_phase(0.0f)
            , initial_freq(0.0f) {}
    };
    
    explicit CarrierRecovery(const Config& config = Config{})
        : config_(config)
        , phase_(config.initial_phase)
        , symbol_count_(0) {
        
        CarrierLoopFilter::Config lf_config;
        lf_config.bandwidth = config.loop_bandwidth;
        lf_config.damping = config.loop_damping;
        loop_filter_.configure(lf_config);
        
        // Initialize with frequency estimate if provided
        if (config.initial_freq != 0.0f) {
            // Pre-load integrator with initial frequency
            float norm_freq = config.initial_freq / config.symbol_rate;
            // This is approximate; the loop will refine it
            phase_increment_ = 2.0f * PI * norm_freq;
        } else {
            phase_increment_ = 0.0f;
        }
    }
    
    void reset() {
        phase_ = config_.initial_phase;
        phase_increment_ = 0.0f;
        loop_filter_.reset();
        symbol_count_ = 0;
    }
    
    /**
     * Process one symbol through carrier recovery
     * @param symbol Input symbol from timing recovery
     * @return Phase-corrected symbol
     */
    complex_t process(complex_t symbol) {
        // Rotate symbol by current phase estimate (derotate)
        complex_t corrected = symbol * std::polar(1.0f, -phase_);
        
        // Compute phase error using decision-directed detector
        float error = phase_detector_.compute(corrected);
        
        // Filter error to get phase/frequency adjustment
        float adjustment = loop_filter_.filter(error);
        
        // Update phase accumulator
        phase_ += adjustment;
        
        // Keep phase wrapped to [-pi, pi]
        while (phase_ > PI) phase_ -= 2.0f * PI;
        while (phase_ < -PI) phase_ += 2.0f * PI;
        
        symbol_count_++;
        
        return corrected;
    }
    
    /**
     * Process a block of symbols
     * @param symbols Input symbols
     * @param corrected Output corrected symbols (appended)
     * @return Number of symbols processed
     */
    int process_block(const std::vector<complex_t>& symbols,
                      std::vector<complex_t>& corrected) {
        for (const auto& s : symbols) {
            corrected.push_back(process(s));
        }
        return symbols.size();
    }
    
    /**
     * Get current phase estimate (radians)
     */
    float phase() const { return phase_; }
    
    /**
     * Get current frequency offset estimate (Hz)
     */
    float frequency_offset() const {
        return loop_filter_.frequency_hz(config_.symbol_rate);
    }
    
    /**
     * Get normalized frequency estimate
     */
    float frequency_normalized() const {
        return loop_filter_.frequency_estimate();
    }
    
    /**
     * Get symbol count
     */
    int symbol_count() const { return symbol_count_; }
    
    /**
     * Set phase directly (for acquisition)
     */
    void set_phase(float phase) { phase_ = phase; }
    
    /**
     * Check if loop is locked (low phase variance)
     * This is a simple heuristic based on frequency estimate stability
     */
    bool is_locked() const {
        // Consider locked if frequency estimate is small and stable
        return std::abs(loop_filter_.frequency_estimate()) < 0.05f 
               && symbol_count_ > 100;
    }

private:
    Config config_;
    
    PhaseDetector8PSK phase_detector_;
    CarrierLoopFilter loop_filter_;
    
    float phase_;              // Current phase estimate (radians)
    float phase_increment_;    // Phase increment per symbol
    int symbol_count_;
};

/**
 * Combined Timing and Carrier Recovery
 * 
 * Convenience class that chains timing recovery and carrier recovery
 * for complete symbol synchronization.
 */
class SymbolSynchronizer {
public:
    struct Config {
        // Timing recovery settings
        float timing_bandwidth;
        float timing_damping;
        float samples_per_symbol;  // SPS for timing recovery (default 20 for 48kHz)
        
        // Carrier recovery settings  
        float carrier_bandwidth;
        float carrier_damping;
        
        // Initial estimates (from preamble detector)
        float initial_freq_offset;
        
        Config()
            : timing_bandwidth(0.01f)
            , timing_damping(0.707f)
            , samples_per_symbol(20.0f)  // 48kHz / 2400 baud
            , carrier_bandwidth(0.02f)
            , carrier_damping(0.707f)
            , initial_freq_offset(0.0f) {}
    };
    
    explicit SymbolSynchronizer(const Config& config = Config{}) {
        // Configure timing recovery
        TimingRecovery::Config tr_config;
        tr_config.samples_per_symbol = config.samples_per_symbol;
        tr_config.loop_bandwidth = config.timing_bandwidth;
        tr_config.loop_damping = config.timing_damping;
        timing_ = std::make_unique<TimingRecovery>(tr_config);
        
        // Configure carrier recovery
        CarrierRecovery::Config cr_config;
        cr_config.loop_bandwidth = config.carrier_bandwidth;
        cr_config.loop_damping = config.carrier_damping;
        cr_config.initial_freq = config.initial_freq_offset;
        carrier_ = std::make_unique<CarrierRecovery>(cr_config);
    }
    
    void reset() {
        timing_->reset();
        carrier_->reset();
    }
    
    /**
     * Process baseband samples to produce synchronized symbols
     * @param samples Input baseband samples
     * @param symbols Output synchronized symbols
     * @return Number of symbols produced
     */
    int process(const std::vector<complex_t>& samples,
                std::vector<complex_t>& symbols) {
        int count = 0;
        
        for (const auto& s : samples) {
            if (timing_->process(s)) {
                // Got a symbol from timing recovery
                complex_t timed = timing_->get_symbol();
                
                // Pass through carrier recovery
                complex_t synced = carrier_->process(timed);
                symbols.push_back(synced);
                count++;
            }
        }
        
        return count;
    }
    
    // Access to individual components
    TimingRecovery& timing() { return *timing_; }
    CarrierRecovery& carrier() { return *carrier_; }
    
    const TimingRecovery& timing() const { return *timing_; }
    const CarrierRecovery& carrier() const { return *carrier_; }

private:
    std::unique_ptr<TimingRecovery> timing_;
    std::unique_ptr<CarrierRecovery> carrier_;
};

} // namespace m110a

#endif // M110A_CARRIER_RECOVERY_H
