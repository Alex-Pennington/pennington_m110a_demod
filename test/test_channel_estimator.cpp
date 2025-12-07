/**
 * Test Channel Estimator
 * 
 * Verifies the channel estimation algorithm works correctly
 * on synthetic multipath channels before integrating with modem.
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include "src/equalizer/channel_estimator.h"
#include <iostream>
#include <iomanip>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace m110a;

// Test configuration
constexpr float PASS_THRESHOLD = 0.1f;  // 10% error tolerance

/**
 * Print channel taps
 */
void print_channel(const std::string& name, const std::vector<complex_t>& taps) {
    std::cout << name << ": [";
    for (size_t i = 0; i < taps.size(); i++) {
        std::cout << std::fixed << std::setprecision(3) 
                  << "(" << taps[i].real() << ", " << taps[i].imag() << ")";
        if (i < taps.size() - 1) std::cout << ", ";
    }
    std::cout << "]\n";
}

/**
 * Apply channel to symbols
 */
std::vector<complex_t> apply_channel(const std::vector<complex_t>& symbols,
                                      const std::vector<complex_t>& channel) {
    std::vector<complex_t> output(symbols.size(), complex_t(0, 0));
    
    for (size_t n = 0; n < symbols.size(); n++) {
        for (size_t k = 0; k < channel.size() && n >= k; k++) {
            output[n] += channel[k] * symbols[n - k];
        }
    }
    
    return output;
}

/**
 * Add AWGN noise
 */
void add_noise(std::vector<complex_t>& symbols, float snr_db) {
    float signal_power = 0.0f;
    for (const auto& s : symbols) {
        signal_power += std::norm(s);
    }
    signal_power /= symbols.size();
    
    float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
    float noise_std = std::sqrt(noise_power / 2.0f);
    
    std::mt19937 rng(12345);
    std::normal_distribution<float> dist(0.0f, noise_std);
    
    for (auto& s : symbols) {
        s += complex_t(dist(rng), dist(rng));
    }
}

/**
 * Compute relative error between estimated and true channel
 */
float compute_relative_error(const std::vector<complex_t>& estimated,
                             const std::vector<complex_t>& truth) {
    float error = 0.0f;
    float norm = 0.0f;
    
    size_t n = std::min(estimated.size(), truth.size());
    for (size_t i = 0; i < n; i++) {
        error += std::norm(estimated[i] - truth[i]);
        norm += std::norm(truth[i]);
    }
    
    return (norm > 0) ? std::sqrt(error / norm) : 0.0f;
}

/**
 * Test 1: Identity channel (no multipath)
 */
bool test_identity_channel() {
    std::cout << "Test 1: Identity Channel\n";
    std::cout << "========================\n";
    
    // True channel: h = [1, 0, 0, 0, 0]
    std::vector<complex_t> true_channel = {
        complex_t(1, 0), complex_t(0, 0), complex_t(0, 0),
        complex_t(0, 0), complex_t(0, 0)
    };
    
    // Generate training symbols (use preamble)
    auto tx_symbols = ChannelEstimator::generate_preamble_reference(128);
    
    // Apply channel
    auto rx_symbols = apply_channel(tx_symbols, true_channel);
    
    // Add noise (30 dB SNR)
    add_noise(rx_symbols, 30.0f);
    
    // Estimate
    ChannelEstimatorConfig cfg;
    cfg.num_taps = 5;
    cfg.normalize = true;
    
    ChannelEstimator estimator(cfg);
    auto result = estimator.estimate(rx_symbols, tx_symbols);
    
    print_channel("True    ", true_channel);
    print_channel("Estimated", result.taps);
    
    float rel_error = compute_relative_error(result.taps, true_channel);
    std::cout << "Relative error: " << std::fixed << std::setprecision(3) 
              << rel_error * 100.0f << "%\n";
    std::cout << "Main tap index: " << result.main_tap_index << "\n";
    std::cout << "Delay spread: " << result.delay_spread << " symbols\n";
    
    bool pass = (rel_error < PASS_THRESHOLD) && (result.main_tap_index == 0);
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    return pass;
}

/**
 * Test 2: Two-path channel (1 symbol delay)
 */
bool test_two_path_channel() {
    std::cout << "Test 2: Two-Path Channel (1 symbol delay)\n";
    std::cout << "==========================================\n";
    
    // True channel: h = [1, 0.5] (main + 50% echo at 1 symbol)
    std::vector<complex_t> true_channel = {
        complex_t(1, 0), complex_t(0.5f, 0), complex_t(0, 0),
        complex_t(0, 0), complex_t(0, 0)
    };
    
    auto tx_symbols = ChannelEstimator::generate_preamble_reference(128);
    auto rx_symbols = apply_channel(tx_symbols, true_channel);
    add_noise(rx_symbols, 30.0f);
    
    ChannelEstimatorConfig cfg;
    cfg.num_taps = 5;
    cfg.normalize = true;
    
    ChannelEstimator estimator(cfg);
    auto result = estimator.estimate(rx_symbols, tx_symbols);
    
    print_channel("True    ", true_channel);
    print_channel("Estimated", result.taps);
    
    float rel_error = compute_relative_error(result.taps, true_channel);
    std::cout << "Relative error: " << std::fixed << std::setprecision(3) 
              << rel_error * 100.0f << "%\n";
    std::cout << "Delay spread: " << result.delay_spread << " symbols\n";
    
    bool pass = (rel_error < PASS_THRESHOLD);
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    return pass;
}

/**
 * Test 3: Two-path with phase (complex echo)
 */
bool test_complex_echo() {
    std::cout << "Test 3: Two-Path with Phase (complex echo)\n";
    std::cout << "===========================================\n";
    
    // True channel: h = [1, 0.5*e^(j*pi/4)] (echo with 45° phase shift)
    float angle = M_PI / 4.0f;
    std::vector<complex_t> true_channel = {
        complex_t(1, 0), 
        complex_t(0.5f * std::cos(angle), 0.5f * std::sin(angle)),
        complex_t(0, 0), complex_t(0, 0), complex_t(0, 0)
    };
    
    auto tx_symbols = ChannelEstimator::generate_preamble_reference(128);
    auto rx_symbols = apply_channel(tx_symbols, true_channel);
    add_noise(rx_symbols, 30.0f);
    
    ChannelEstimatorConfig cfg;
    cfg.num_taps = 5;
    cfg.normalize = true;
    
    ChannelEstimator estimator(cfg);
    auto result = estimator.estimate(rx_symbols, tx_symbols);
    
    print_channel("True    ", true_channel);
    print_channel("Estimated", result.taps);
    
    float rel_error = compute_relative_error(result.taps, true_channel);
    std::cout << "Relative error: " << std::fixed << std::setprecision(3) 
              << rel_error * 100.0f << "%\n";
    std::cout << "Delay spread: " << result.delay_spread << " symbols\n";
    
    bool pass = (rel_error < PASS_THRESHOLD);
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    return pass;
}

/**
 * Test 4: Watterson-like 3-path channel
 */
bool test_watterson_like() {
    std::cout << "Test 4: Watterson-like 3-Path Channel\n";
    std::cout << "======================================\n";
    
    // Simulate 3-path: delays at 0, 1, 2 symbols with complex gains
    std::vector<complex_t> true_channel = {
        complex_t(1.0f, 0.0f),      // Main path
        complex_t(0.0f, 0.0f),      // No path at 1 symbol
        complex_t(0.3f, 0.2f),      // Echo at 2 symbols (attenuated, phase shifted)
        complex_t(0.0f, 0.0f),
        complex_t(0.1f, -0.05f)     // Weak echo at 4 symbols
    };
    
    auto tx_symbols = ChannelEstimator::generate_preamble_reference(200);
    auto rx_symbols = apply_channel(tx_symbols, true_channel);
    add_noise(rx_symbols, 25.0f);  // Lower SNR for realistic fading
    
    ChannelEstimatorConfig cfg;
    cfg.num_taps = 5;
    cfg.normalize = true;
    
    ChannelEstimator estimator(cfg);
    auto result = estimator.estimate(rx_symbols, tx_symbols);
    
    print_channel("True    ", true_channel);
    print_channel("Estimated", result.taps);
    
    float rel_error = compute_relative_error(result.taps, true_channel);
    std::cout << "Relative error: " << std::fixed << std::setprecision(3) 
              << rel_error * 100.0f << "%\n";
    std::cout << "Delay spread: " << result.delay_spread << " symbols\n";
    std::cout << "Estimation error: " << result.estimation_error << "\n";
    
    bool pass = (rel_error < PASS_THRESHOLD * 2);  // Relax for complex channel
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    return pass;
}

/**
 * Test 5: Verify preamble reference generation
 */
bool test_preamble_reference() {
    std::cout << "Test 5: Preamble Reference Generation\n";
    std::cout << "======================================\n";
    
    auto ref = ChannelEstimator::generate_preamble_reference(288);
    
    std::cout << "Generated " << ref.size() << " preamble symbols\n";
    
    // Check that all symbols are on unit circle (8-PSK)
    bool all_unit = true;
    for (size_t i = 0; i < ref.size(); i++) {
        float mag = std::abs(ref[i]);
        if (std::abs(mag - 1.0f) > 0.01f) {
            std::cout << "  Symbol " << i << " has magnitude " << mag << "\n";
            all_unit = false;
        }
    }
    
    // Print first 16 symbols
    std::cout << "First 16 symbols:\n";
    for (int i = 0; i < 16; i++) {
        float angle = std::atan2(ref[i].imag(), ref[i].real()) * 180.0f / M_PI;
        std::cout << "  [" << i << "] (" << std::fixed << std::setprecision(3)
                  << ref[i].real() << ", " << ref[i].imag() << ") = " 
                  << std::setprecision(1) << angle << "°\n";
    }
    
    bool pass = all_unit && (ref.size() == 288);
    std::cout << "All symbols on unit circle: " << (all_unit ? "YES" : "NO") << "\n";
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    return pass;
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "   Channel Estimator Test Suite\n";
    std::cout << "==============================================\n\n";
    
    int passed = 0;
    int total = 5;
    
    if (test_identity_channel()) passed++;
    if (test_two_path_channel()) passed++;
    if (test_complex_echo()) passed++;
    if (test_watterson_like()) passed++;
    if (test_preamble_reference()) passed++;
    
    std::cout << "==============================================\n";
    std::cout << "   Summary: " << passed << "/" << total << " tests passed\n";
    std::cout << "==============================================\n";
    
    return (passed == total) ? 0 : 1;
}
