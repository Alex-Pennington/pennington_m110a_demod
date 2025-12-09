/**
 * Debug test for poor_hf channel failures
 * Tests individual impairments and combinations
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>
#include "api/modem.h"

void apply_awgn(std::vector<float>& samples, float snr_db) {
    static std::mt19937 rng(12345);
    float signal_power = 0.0f;
    for (float s : samples) signal_power += s * s;
    signal_power /= samples.size();
    
    float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
    float noise_std = std::sqrt(noise_power);
    
    std::normal_distribution<float> noise(0.0f, noise_std);
    for (float& s : samples) s += noise(rng);
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

double calculate_ber(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    if (tx.empty() || rx.empty()) return 1.0;
    
    size_t compare_len = std::min(tx.size(), rx.size());
    int bit_errors = 0;
    int total_bits = compare_len * 8;
    
    for (size_t i = 0; i < compare_len; i++) {
        uint8_t diff = tx[i] ^ rx[i];
        for (int b = 0; b < 8; b++) {
            if (diff & (1 << b)) bit_errors++;
        }
    }
    
    return static_cast<double>(bit_errors) / total_bits;
}

void test_channel(const char* name, std::vector<float> pcm, 
                  float snr, int mp_delay, float freq_offset, float freq_search_range = 10.0f) {
    // Apply impairments
    if (mp_delay > 0) {
        apply_multipath(pcm, mp_delay, 0.5f);
    }
    if (std::abs(freq_offset) > 0.01f) {
        apply_freq_offset(pcm, freq_offset);
    }
    if (snr < 99.0f) {
        apply_awgn(pcm, snr);
    }
    
    // Decode
    m110a::api::RxConfig cfg;
    cfg.equalizer = m110a::api::Equalizer::DFE;
    cfg.phase_tracking = true;
    cfg.freq_search_range = freq_search_range;
    
    auto result = m110a::api::decode(pcm, cfg);
    
    std::cout << name << " (AFC±" << freq_search_range << "Hz): ";
    if (result.success) {
        double ber = calculate_ber(std::vector<uint8_t>{0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69, 0x63, 0x6B, 0x20, 0x62, 0x72, 0x6F, 0x77, 0x6E, 0x20}, result.data);
        std::cout << "Decoded " << result.data.size() << " bytes, BER=" 
                  << std::fixed << std::setprecision(4) << ber 
                  << ", FreqOff=" << result.freq_offset_hz << "Hz\n";
    } else {
        std::cout << "FAILED (no decode)\n";
    }
}

int main() {
    std::cout << "=== Poor HF Channel Debug Test ===\n\n";
    
    // Create test message
    std::vector<uint8_t> test_data = {
        0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69, 0x63,  // "The quic"
        0x6B, 0x20, 0x62, 0x72, 0x6F, 0x77, 0x6E, 0x20   // "k brown "
    };
    
    // Encode
    auto pcm_result = m110a::api::encode(test_data, m110a::api::Mode::M1200_SHORT);
    if (!pcm_result) {
        std::cerr << "Encode failed!\n";
        return 1;
    }
    
    std::vector<float> pcm_clean = pcm_result.value();
    std::cout << "Encoded " << test_data.size() << " bytes to " 
              << pcm_clean.size() << " samples\n\n";
    
    // Test individual impairments
    test_channel("Clean", pcm_clean, 100.0f, 0, 0.0f);
    test_channel("SNR 15dB only", pcm_clean, 15.0f, 0, 0.0f);
    test_channel("MP 48samp only", pcm_clean, 100.0f, 48, 0.0f);
    test_channel("Freq 3Hz (default AFC)", pcm_clean, 100.0f, 0, 3.0f);
    test_channel("Freq 3Hz (wider AFC)", pcm_clean, 100.0f, 0, 3.0f, 20.0f);
    
    std::cout << "\n";
    
    // Test combinations
    test_channel("SNR 15dB + MP 48", pcm_clean, 15.0f, 48, 0.0f);
    test_channel("SNR 15dB + Freq 3Hz", pcm_clean, 15.0f, 0, 3.0f);
    test_channel("MP 48 + Freq 3Hz", pcm_clean, 100.0f, 48, 3.0f);
    
    std::cout << "\n";
    
    // Test full poor_hf
    test_channel("Poor HF (all 3)", pcm_clean, 15.0f, 48, 3.0f);
    test_channel("Poor HF (wider AFC)", pcm_clean, 15.0f, 48, 3.0f, 20.0f);
    
    return 0;
}
