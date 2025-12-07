#ifndef M110A_MULTIPATH_H
#define M110A_MULTIPATH_H

/**
 * Multipath Channel Models for HF Simulation
 * 
 * Implements realistic HF channel models including:
 * - Two-ray model (ground + sky wave)
 * - ITU-R HF channel models (good, moderate, poor)
 * - CCIR 520-2 models
 * 
 * Operates at sample rate on RF signal.
 */

#include "common/types.h"
#include <vector>
#include <random>
#include <cmath>

namespace m110a {

/**
 * Multipath tap specification
 */
struct ChannelTap {
    float delay_ms;      // Delay in milliseconds
    float amplitude;     // Linear amplitude (relative to main path)
    float phase_deg;     // Phase in degrees
    float doppler_hz;    // Doppler shift (for fading)
    
    ChannelTap(float d = 0, float a = 1.0f, float p = 0, float dop = 0)
        : delay_ms(d), amplitude(a), phase_deg(p), doppler_hz(dop) {}
};

/**
 * RF Multipath Channel
 * 
 * Applies multipath distortion at sample rate.
 */
class MultipathRFChannel {
public:
    struct Config {
        float sample_rate;
        std::vector<ChannelTap> taps;
        float noise_power_db;     // Noise power relative to signal (negative = less noise)
        bool fading_enabled;
        
        Config() 
            : sample_rate(9600.0f)
            , noise_power_db(-100.0f)  // Effectively no noise by default
            , fading_enabled(false) {}
    };
    
    explicit MultipathRFChannel(const Config& config, unsigned int seed = 42)
        : config_(config)
        , rng_(seed)
        , sample_count_(0) {
        
        setup_delay_lines();
    }
    
    /**
     * Process a block of RF samples through the channel
     */
    std::vector<float> process(const std::vector<float>& input) {
        std::vector<float> output(input.size(), 0.0f);
        
        for (size_t i = 0; i < input.size(); i++) {
            output[i] = process_sample(input[i]);
        }
        
        return output;
    }
    
    /**
     * Process single sample
     */
    float process_sample(float input) {
        float output = 0.0f;
        
        for (size_t t = 0; t < config_.taps.size(); t++) {
            // Push input into this tap's delay line
            delay_lines_[t][write_indices_[t]] = input;
            
            // Read from delay position
            int read_idx = (write_indices_[t] - delay_samples_[t] + delay_lines_[t].size()) 
                          % delay_lines_[t].size();
            
            // Apply amplitude and phase
            float amp = config_.taps[t].amplitude;
            float phase = config_.taps[t].phase_deg * PI / 180.0f;
            
            // Apply Doppler if fading enabled
            if (config_.fading_enabled && config_.taps[t].doppler_hz != 0) {
                float doppler_phase = 2.0f * PI * config_.taps[t].doppler_hz * 
                                     sample_count_ / config_.sample_rate;
                phase += doppler_phase;
            }
            
            // For real signal, phase affects the delayed signal differently
            // Simplified: apply phase rotation to envelope
            float delayed = delay_lines_[t][read_idx];
            output += amp * delayed * std::cos(phase);
            
            // Advance write index
            write_indices_[t] = (write_indices_[t] + 1) % delay_lines_[t].size();
        }
        
        // Add noise
        if (config_.noise_power_db > -90.0f) {
            float noise_std = std::pow(10.0f, config_.noise_power_db / 20.0f);
            std::normal_distribution<float> dist(0.0f, noise_std);
            output += dist(rng_);
        }
        
        sample_count_++;
        return output;
    }
    
    void reset() {
        for (auto& dl : delay_lines_) {
            std::fill(dl.begin(), dl.end(), 0.0f);
        }
        for (auto& idx : write_indices_) {
            idx = 0;
        }
        sample_count_ = 0;
    }
    
    /**
     * Get channel description
     */
    std::string description() const {
        std::string desc = "Multipath channel with " + 
                          std::to_string(config_.taps.size()) + " taps:\n";
        for (size_t i = 0; i < config_.taps.size(); i++) {
            desc += "  Tap " + std::to_string(i) + 
                   ": delay=" + std::to_string(config_.taps[i].delay_ms) + "ms" +
                   ", amp=" + std::to_string(config_.taps[i].amplitude) +
                   ", phase=" + std::to_string(config_.taps[i].phase_deg) + "deg\n";
        }
        return desc;
    }
    
    // Preset channel configurations
    static Config two_ray_mild() {
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),     // Direct path
            ChannelTap(1.0f, 0.5f, 90.0f)     // Reflected path, 1ms delay
        };
        return cfg;
    }
    
    static Config two_ray_moderate() {
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),      // Direct path
            ChannelTap(2.0f, 0.7f, 120.0f)     // Reflected path, 2ms delay
        };
        return cfg;
    }
    
    static Config two_ray_severe() {
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),      // Direct path
            ChannelTap(3.0f, 0.9f, 180.0f)     // Nearly equal delayed path
        };
        return cfg;
    }
    
    static Config itu_good() {
        // ITU-R F.520 "Good" conditions
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),
            ChannelTap(0.5f, 0.2f, 45.0f)
        };
        return cfg;
    }
    
    static Config itu_moderate() {
        // ITU-R F.520 "Moderate" conditions
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),
            ChannelTap(1.0f, 0.5f, 90.0f),
            ChannelTap(2.0f, 0.25f, 180.0f)
        };
        return cfg;
    }
    
    static Config itu_poor() {
        // ITU-R F.520 "Poor" conditions - severe multipath
        Config cfg;
        cfg.taps = {
            ChannelTap(0.0f, 1.0f, 0.0f),
            ChannelTap(2.0f, 0.7f, 120.0f),
            ChannelTap(4.0f, 0.5f, 240.0f)
        };
        return cfg;
    }

private:
    Config config_;
    std::mt19937 rng_;
    
    std::vector<std::vector<float>> delay_lines_;
    std::vector<int> write_indices_;
    std::vector<int> delay_samples_;
    
    size_t sample_count_;
    
    void setup_delay_lines() {
        delay_lines_.clear();
        write_indices_.clear();
        delay_samples_.clear();
        
        for (const auto& tap : config_.taps) {
            // Calculate delay in samples
            int samples = static_cast<int>(tap.delay_ms * config_.sample_rate / 1000.0f);
            delay_samples_.push_back(samples);
            
            // Create delay line (need at least max_delay + 1 samples)
            int size = samples + 100;  // Extra buffer
            delay_lines_.push_back(std::vector<float>(size, 0.0f));
            write_indices_.push_back(0);
        }
    }
};

} // namespace m110a

#endif // M110A_MULTIPATH_H
