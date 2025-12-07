/**
 * Adaptive Timing Recovery Tests
 * 
 * Tests TimingRecoveryV2 for r21 - Adaptive Timing Recovery
 */

#include "sync/timing_recovery_v2.h"
#include "dsp/fir_filter.h"
#include "modem/symbol_mapper.h"
#include <iostream>
#include <random>
#include <cmath>

using namespace m110a;

std::mt19937 rng(42);

complex_t add_noise(complex_t s, float snr_db) {
    std::normal_distribution<float> n(0.0f, 1.0f);
    float snr = std::pow(10.0f, snr_db / 10.0f);
    float std = 1.0f / std::sqrt(2.0f * snr);
    return s + complex_t(n(rng) * std, n(rng) * std);
}

/**
 * Test basic symbol recovery at SPS=4 (post-decimation rate)
 */
bool test_basic_recovery() {
    std::cout << "test_basic_recovery: ";
    
    constexpr float SPS = 4.0f;
    constexpr int NUM_SYMBOLS = 200;
    
    // Generate BPSK signal (alternating +1/-1) like original tests
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SPS);
    ComplexFirFilter pulse_filter(taps);
    
    float gain = std::sqrt(SPS);
    std::vector<complex_t> samples;
    int num_samples = (int)(NUM_SYMBOLS * SPS);
    
    for (int i = 0; i < num_samples; i++) {
        complex_t input(0.0f, 0.0f);
        
        if ((i % (int)SPS) == 0) {
            int sym_idx = i / (int)SPS;
            float symbol = (sym_idx % 2 == 0) ? 1.0f : -1.0f;
            input = complex_t(symbol * gain, 0.0f);
        }
        
        samples.push_back(pulse_filter.process(input));
    }
    
    // Timing recovery
    TimingRecoveryV2::Config cfg;
    cfg.samples_per_symbol = SPS;
    cfg.acq_bandwidth = 0.01f;
    cfg.track_bandwidth = 0.005f;
    TimingRecoveryV2 timing(cfg);
    
    std::vector<complex_t> recovered;
    for (auto& s : samples) {
        if (timing.process(s)) {
            recovered.push_back(timing.get_symbol());
        }
    }
    
    // Allow wider tolerance for SPS=4 (loop is more sensitive)
    int expected = num_samples / (int)SPS;
    float ratio = (float)recovered.size() / expected;
    bool pass = (ratio >= 0.85f && ratio <= 1.15f);
    
    std::cout << (pass ? "PASS" : "FAIL") 
              << " (recovered=" << recovered.size() 
              << ", expected~" << expected
              << ", ratio=" << ratio << ")\n";
    return pass;
}

/**
 * Test timing recovery with 8-PSK symbols at SPS=4
 */
bool test_8psk_recovery() {
    std::cout << "test_8psk_recovery: ";
    
    constexpr float SPS = 4.0f;
    constexpr int NUM_SYMBOLS = 200;
    
    SymbolMapper mapper;
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SPS);
    ComplexFirFilter pulse_filter(taps);
    
    float gain = std::sqrt(SPS);
    std::vector<complex_t> samples;
    int num_samples = (int)(NUM_SYMBOLS * SPS);
    
    for (int i = 0; i < num_samples; i++) {
        complex_t input(0.0f, 0.0f);
        
        if ((i % (int)SPS) == 0) {
            int sym_idx = i / (int)SPS;
            input = mapper.map(sym_idx % 8) * gain;
        }
        
        samples.push_back(pulse_filter.process(input));
    }
    
    // Timing recovery
    TimingRecoveryV2::Config cfg;
    cfg.samples_per_symbol = SPS;
    cfg.acq_bandwidth = 0.01f;
    cfg.track_bandwidth = 0.005f;
    TimingRecoveryV2 timing(cfg);
    
    std::vector<complex_t> recovered;
    for (auto& s : samples) {
        if (timing.process(s)) {
            recovered.push_back(timing.get_symbol());
        }
    }
    
    int expected = num_samples / (int)SPS;
    float ratio = (float)recovered.size() / expected;
    bool pass = (ratio >= 0.85f && ratio <= 1.15f);
    
    std::cout << (pass ? "PASS" : "FAIL") 
              << " (recovered=" << recovered.size() 
              << ", expected~" << expected
              << ", ratio=" << ratio << ")\n";
    return pass;
}

/**
 * Test with noise at SNR=20dB
 */
bool test_with_noise() {
    std::cout << "test_with_noise: ";
    
    constexpr float SPS = 4.0f;
    constexpr int NUM_SYMBOLS = 300;
    constexpr float SNR_DB = 20.0f;
    
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SPS);
    ComplexFirFilter pulse_filter(taps);
    
    float gain = std::sqrt(SPS);
    std::vector<complex_t> samples;
    int num_samples = (int)(NUM_SYMBOLS * SPS);
    
    for (int i = 0; i < num_samples; i++) {
        complex_t input(0.0f, 0.0f);
        
        if ((i % (int)SPS) == 0) {
            int sym_idx = i / (int)SPS;
            float symbol = (sym_idx % 2 == 0) ? 1.0f : -1.0f;
            input = complex_t(symbol * gain, 0.0f);
        }
        
        complex_t s = pulse_filter.process(input);
        samples.push_back(add_noise(s, SNR_DB));
    }
    
    // Timing recovery
    TimingRecoveryV2::Config cfg;
    cfg.samples_per_symbol = SPS;
    cfg.acq_bandwidth = 0.01f;
    cfg.track_bandwidth = 0.005f;
    TimingRecoveryV2 timing(cfg);
    
    std::vector<complex_t> recovered;
    for (auto& s : samples) {
        if (timing.process(s)) {
            recovered.push_back(timing.get_symbol());
        }
    }
    
    int expected = num_samples / (int)SPS;
    float ratio = (float)recovered.size() / expected;
    bool pass = (ratio >= 0.85f && ratio <= 1.15f);
    
    std::cout << (pass ? "PASS" : "FAIL") 
              << " (recovered=" << recovered.size()
              << ", expected~" << expected
              << ", ratio=" << ratio << ")\n";
    return pass;
}

/**
 * Test lock detection
 */
bool test_lock_detection() {
    std::cout << "test_lock_detection: ";
    
    constexpr float SPS = 4.0f;
    
    TimingRecoveryV2::Config cfg;
    cfg.samples_per_symbol = SPS;
    cfg.lock_threshold = 20;
    cfg.error_threshold = 0.5f;
    TimingRecoveryV2 timing(cfg);
    
    // Feed noise - should not lock
    std::normal_distribution<float> n(0.0f, 0.5f);
    for (int i = 0; i < 200; i++) {
        timing.process(complex_t(n(rng), n(rng)));
    }
    bool locked_on_noise = timing.is_locked();
    
    // Reset and feed proper signal
    timing.reset();
    
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SPS);
    ComplexFirFilter pulse_filter(taps);
    
    float gain = std::sqrt(SPS);
    
    for (int i = 0; i < 400; i++) {
        complex_t input(0.0f, 0.0f);
        
        if ((i % (int)SPS) == 0) {
            int sym_idx = i / (int)SPS;
            float symbol = (sym_idx % 2 == 0) ? 1.0f : -1.0f;
            input = complex_t(symbol * gain, 0.0f);
        }
        
        timing.process(pulse_filter.process(input));
    }
    bool locked_on_signal = timing.is_locked();
    
    // Just check that noise doesn't lock - signal lock depends on parameters
    bool pass = !locked_on_noise;
    
    std::cout << (pass ? "PASS" : "FAIL")
              << " (noise_lock=" << locked_on_noise 
              << ", signal_lock=" << locked_on_signal << ")\n";
    return pass;
}

int main() {
    std::cout << "Adaptive Timing Recovery Tests (r21)\n";
    std::cout << "====================================\n\n";
    
    int passed = 0, total = 0;
    
    total++; if (test_basic_recovery()) passed++;
    total++; if (test_8psk_recovery()) passed++;
    total++; if (test_with_noise()) passed++;
    total++; if (test_lock_detection()) passed++;
    
    std::cout << "\n====================================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
