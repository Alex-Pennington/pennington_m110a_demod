/**
 * Channel estimator unit tests
 */

#include "channel/channel_estimator.h"
#include "modem/symbol_mapper.h"
#include <iostream>
#include <random>
#include <cmath>

using namespace m110a;

std::mt19937 rng(42);
std::normal_distribution<float> noise_dist(0.0f, 1.0f);

complex_t add_noise(complex_t s, float snr_db) {
    float snr_linear = std::pow(10.0f, snr_db / 10.0f);
    float noise_std = 1.0f / std::sqrt(2.0f * snr_linear);
    return s + complex_t(noise_dist(rng) * noise_std, noise_dist(rng) * noise_std);
}

bool test_channel_estimation() {
    std::cout << "test_channel_estimation: ";
    
    ChannelEstimator est;
    const auto& ref = est.probe_reference();
    
    // Apply channel: gain=0.8, phase=30°
    complex_t channel = std::polar(0.8f, 30.0f * PI / 180.0f);
    std::vector<complex_t> rx;
    for (const auto& s : ref) {
        rx.push_back(s * channel);
    }
    
    auto result = est.process_probes(rx, 0);
    
    // Check estimates
    float amp_err = std::abs(result.amplitude - 0.8f);
    float phase_err = std::abs(result.phase_offset - 30.0f * PI / 180.0f);
    
    bool pass = (amp_err < 0.01f && phase_err < 0.01f);
    std::cout << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_snr_estimation() {
    std::cout << "test_snr_estimation: ";
    
    ChannelEstimator est;
    const auto& ref = est.probe_reference();
    
    float test_snr = 20.0f;
    float total_error = 0.0f;
    int trials = 10;
    
    for (int t = 0; t < trials; t++) {
        est.reset();
        std::vector<complex_t> rx;
        for (const auto& s : ref) {
            rx.push_back(add_noise(s, test_snr));
        }
        auto result = est.process_probes(rx, 0);
        total_error += std::abs(result.snr_db - test_snr);
    }
    
    float avg_error = total_error / trials;
    bool pass = (avg_error < 2.0f);  // Within 2 dB
    std::cout << (pass ? "PASS" : "FAIL") << " (avg error: " << avg_error << " dB)\n";
    return pass;
}

bool test_channel_compensation() {
    std::cout << "test_channel_compensation: ";
    
    ChannelEstimator est;
    const auto& ref = est.probe_reference();
    
    // Apply channel
    complex_t channel = std::polar(0.7f, 45.0f * PI / 180.0f);
    std::vector<complex_t> rx;
    for (const auto& s : ref) {
        rx.push_back(add_noise(s * channel, 25.0f));
    }
    
    est.process_probes(rx, 0);
    
    // Test compensation on data
    SymbolMapper mapper;
    std::vector<complex_t> data, rx_data;
    for (int i = 0; i < 32; i++) {
        complex_t s = mapper.map(i % 8);
        data.push_back(s);
        rx_data.push_back(add_noise(s * channel, 25.0f));
    }
    
    auto compensated = est.compensate_block(rx_data);
    
    float mse = 0.0f;
    for (size_t i = 0; i < data.size(); i++) {
        mse += std::norm(compensated[i] - data[i]);
    }
    mse /= data.size();
    
    bool pass = (mse < 0.1f);
    std::cout << (pass ? "PASS" : "FAIL") << " (MSE: " << mse << ")\n";
    return pass;
}

bool test_channel_tracker() {
    std::cout << "test_channel_tracker: ";
    
    ChannelTracker tracker;
    SymbolMapper mapper;
    
    // Build a frame
    std::vector<complex_t> frame;
    for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
        frame.push_back(mapper.map(i % 8));
    }
    const auto& ref = tracker.estimator().probe_reference();
    for (const auto& p : ref) {
        frame.push_back(p);
    }
    
    // Apply channel
    complex_t channel = std::polar(0.9f, 20.0f * PI / 180.0f);
    std::vector<complex_t> rx_frame;
    for (const auto& s : frame) {
        rx_frame.push_back(add_noise(s * channel, 25.0f));
    }
    
    // Process
    std::vector<complex_t> compensated;
    bool ok = tracker.process_frame(rx_frame, compensated);
    
    if (!ok || compensated.size() != DATA_SYMBOLS_PER_FRAME) {
        std::cout << "FAIL (process_frame error)\n";
        return false;
    }
    
    float mse = 0.0f;
    for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
        mse += std::norm(compensated[i] - frame[i]);
    }
    mse /= DATA_SYMBOLS_PER_FRAME;
    
    bool pass = (mse < 0.2f);
    std::cout << (pass ? "PASS" : "FAIL") << " (MSE: " << mse << ")\n";
    return pass;
}

int main() {
    std::cout << "Channel Estimator Tests\n";
    std::cout << "=======================\n\n";
    
    int passed = 0;
    int total = 0;
    
    total++; if (test_channel_estimation()) passed++;
    total++; if (test_snr_estimation()) passed++;
    total++; if (test_channel_compensation()) passed++;
    total++; if (test_channel_tracker()) passed++;
    
    std::cout << "\n=======================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
