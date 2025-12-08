/**
 * @file direct_backend.h
 * @brief Direct API Backend for Test Framework
 * 
 * Implements ITestBackend using direct modem API calls.
 * Applies channel impairments locally (AWGN, multipath, freq offset).
 */

#ifndef DIRECT_BACKEND_H
#define DIRECT_BACKEND_H

#include "test_framework.h"
#include "../api/modem.h"

#include <random>
#include <cmath>

namespace test_framework {

class DirectBackend : public ITestBackend {
public:
    DirectBackend(unsigned int seed = 42) : rng_(seed), seed_(seed), connected_(false) {}
    
    bool connect() override {
        connected_ = true;
        return true;
    }
    
    void disconnect() override {
        connected_ = false;
    }
    
    bool is_connected() override {
        return connected_;
    }
    
    bool set_equalizer(const std::string& eq_type) override {
        if (eq_type == "NONE") equalizer_ = m110a::api::Equalizer::NONE;
        else if (eq_type == "DFE") equalizer_ = m110a::api::Equalizer::DFE;
        else if (eq_type == "DFE_RLS") equalizer_ = m110a::api::Equalizer::DFE_RLS;
        else if (eq_type == "MLSE_L2") equalizer_ = m110a::api::Equalizer::MLSE_L2;
        else if (eq_type == "MLSE_L3") equalizer_ = m110a::api::Equalizer::MLSE_L3;
        else if (eq_type == "MLSE_ADAPTIVE") equalizer_ = m110a::api::Equalizer::MLSE_ADAPTIVE;
        else if (eq_type == "TURBO") equalizer_ = m110a::api::Equalizer::TURBO;
        else return false;
        eq_type_ = eq_type;
        return true;
    }
    
    bool run_test(const ModeInfo& mode, 
                  const ChannelCondition& channel,
                  const std::vector<uint8_t>& test_data,
                  double& ber_out) override {
        ber_out = 1.0;
        
        // Map mode command to API mode
        m110a::api::Mode api_mode = parse_mode(mode.cmd);
        if (api_mode == m110a::api::Mode::AUTO) {
            return false;  // AUTO means parse failed
        }
        
        // Encode
        auto pcm_result = m110a::api::encode(test_data, api_mode);
        if (!pcm_result) {
            return false;
        }
        
        std::vector<float> pcm = pcm_result.value();
        
        // Apply channel impairments
        apply_channel(pcm, channel);
        
        // Decode
        m110a::api::RxConfig cfg;
        cfg.equalizer = equalizer_;
        cfg.phase_tracking = true;
        
        auto result = m110a::api::decode(pcm, cfg);
        
        // Calculate BER
        ber_out = calculate_ber(test_data, result.data);
        
        return ber_out <= channel.expected_ber_threshold;
    }
    
    std::string backend_name() const override {
        return "Direct API";
    }
    
    void reset_state() override {
        rng_.seed(seed_);  // Reset to consistent state
    }
    
    // Clone for parallel execution - each thread gets its own backend with unique RNG seed
    std::unique_ptr<ITestBackend> clone() const override {
        static std::atomic<unsigned int> clone_counter{1000};
        auto backend = std::make_unique<DirectBackend>(clone_counter++);
        backend->equalizer_ = equalizer_;
        backend->eq_type_ = eq_type_;
        backend->connected_ = true;
        return backend;
    }

private:
    std::mt19937 rng_;
    unsigned int seed_;
    bool connected_;
    m110a::api::Equalizer equalizer_ = m110a::api::Equalizer::DFE;
    std::string eq_type_ = "DFE";
    
    m110a::api::Mode parse_mode(const std::string& cmd) {
        if (cmd == "75S") return m110a::api::Mode::M75_SHORT;
        if (cmd == "75L") return m110a::api::Mode::M75_LONG;
        if (cmd == "150S") return m110a::api::Mode::M150_SHORT;
        if (cmd == "150L") return m110a::api::Mode::M150_LONG;
        if (cmd == "300S") return m110a::api::Mode::M300_SHORT;
        if (cmd == "300L") return m110a::api::Mode::M300_LONG;
        if (cmd == "600S") return m110a::api::Mode::M600_SHORT;
        if (cmd == "600L") return m110a::api::Mode::M600_LONG;
        if (cmd == "1200S") return m110a::api::Mode::M1200_SHORT;
        if (cmd == "1200L") return m110a::api::Mode::M1200_LONG;
        if (cmd == "2400S") return m110a::api::Mode::M2400_SHORT;
        if (cmd == "2400L") return m110a::api::Mode::M2400_LONG;
        return m110a::api::Mode::AUTO;  // AUTO means parse failed
    }
    
    void apply_channel(std::vector<float>& samples, const ChannelCondition& channel) {
        // Apply multipath first (if specified)
        if (channel.multipath_delay_samples > 0) {
            apply_multipath(samples, channel.multipath_delay_samples, channel.multipath_gain);
        }
        
        // Apply frequency offset (if specified)
        if (std::abs(channel.freq_offset_hz) > 0.01f) {
            apply_freq_offset(samples, channel.freq_offset_hz);
        }
        
        // Apply AWGN last (if specified)
        if (channel.snr_db < 99.0f) {
            apply_awgn(samples, channel.snr_db);
        }
    }
    
    void apply_awgn(std::vector<float>& samples, float snr_db) {
        float signal_power = 0.0f;
        for (float s : samples) signal_power += s * s;
        signal_power /= samples.size();
        
        float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
        float noise_std = std::sqrt(noise_power);
        
        std::normal_distribution<float> noise(0.0f, noise_std);
        for (float& s : samples) s += noise(rng_);
    }
    
    void apply_multipath(std::vector<float>& samples, int delay_samples, float echo_gain) {
        std::vector<float> output(samples.size(), 0.0f);
        for (size_t i = 0; i < samples.size(); i++) {
            output[i] = samples[i];
            if (i >= static_cast<size_t>(delay_samples)) {
                output[i] += echo_gain * samples[i - delay_samples];
            }
        }
        samples = output;
    }
    
    void apply_freq_offset(std::vector<float>& samples, float offset_hz, float sample_rate = 48000.0f) {
        float phase = 0.0f;
        float phase_inc = 2.0f * 3.14159265f * offset_hz / sample_rate;
        for (float& s : samples) {
            s *= std::cos(phase);
            phase += phase_inc;
            if (phase > 6.28318f) phase -= 6.28318f;
        }
    }
};

} // namespace test_framework

#endif // DIRECT_BACKEND_H
