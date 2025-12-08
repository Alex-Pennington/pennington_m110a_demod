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
#include "../src/io/pcm_file.h"

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
    
    bool run_reference_test(const std::string& pcm_file,
                           const std::string& expected_message,
                           ReferenceTestResult& result) override {
        result.filename = pcm_file;
        result.expected_message = expected_message;
        result.passed = false;
        result.ber = 1.0;
        
        // Load PCM file
        try {
            m110a::PcmFileReader reader(pcm_file);
            std::vector<float> pcm = reader.read_all();
            result.sample_count = (int)pcm.size();
            
            // Decode with auto-detection
            m110a::api::RxConfig cfg;
            cfg.equalizer = equalizer_;
            cfg.phase_tracking = true;
            
            auto decode_result = m110a::api::decode(pcm, cfg);
            
            // Extract detected mode
            result.detected_mode = mode_to_string(decode_result.mode);
            
            // Extract decoded message - compare only expected length
            size_t expected_len = expected_message.length();
            size_t decoded_len = std::min(expected_len, decode_result.data.size());
            
            std::string decoded(decode_result.data.begin(), 
                               decode_result.data.begin() + decoded_len);
            result.decoded_message = decoded;
            
            // Check message match (only compare expected length)
            result.message_match = (decoded == expected_message.substr(0, decoded_len));
            
            // Calculate BER (only on expected message length)
            std::vector<uint8_t> expected_bytes(expected_message.begin(), expected_message.end());
            std::vector<uint8_t> decoded_bytes(decode_result.data.begin(), 
                                              decode_result.data.begin() + decoded_len);
            result.ber = calculate_ber(expected_bytes, decoded_bytes);
            
            // Check mode match
            result.mode_match = (result.detected_mode == result.expected_mode);
            
            // Pass if both mode and message match
            result.passed = result.message_match && result.mode_match;
            
            return result.passed;
            
        } catch (const std::exception& e) {
            result.decoded_message = std::string("ERROR: ") + e.what();
            return false;
        }
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
    
    std::string mode_to_string(m110a::api::Mode mode) {
        switch (mode) {
            case m110a::api::Mode::M75_SHORT: return "75S";
            case m110a::api::Mode::M75_LONG: return "75L";
            case m110a::api::Mode::M150_SHORT: return "150S";
            case m110a::api::Mode::M150_LONG: return "150L";
            case m110a::api::Mode::M300_SHORT: return "300S";
            case m110a::api::Mode::M300_LONG: return "300L";
            case m110a::api::Mode::M600_SHORT: return "600S";
            case m110a::api::Mode::M600_LONG: return "600L";
            case m110a::api::Mode::M1200_SHORT: return "1200S";
            case m110a::api::Mode::M1200_LONG: return "1200L";
            case m110a::api::Mode::M2400_SHORT: return "2400S";
            case m110a::api::Mode::M2400_LONG: return "2400L";
            case m110a::api::Mode::AUTO: return "AUTO";
            default: return "UNKNOWN";
        }
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
