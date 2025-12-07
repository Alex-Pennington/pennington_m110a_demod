#include "sync/preamble_detector.h"
#include "m110a/m110a_tx.h"
#include "io/pcm_file.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>

using namespace m110a;

void test_reference_generation() {
    std::cout << "=== Test: Reference Preamble Generation ===\n";
    
    PreambleDetector detector;
    
    const auto& ref = detector.reference_symbols();
    std::cout << "Reference symbols: " << ref.size() << "\n";
    std::cout << "Expected: " << 480 << " (one 0.2s segment)\n";
    assert(ref.size() == 480);
    
    // Verify all symbols on unit circle
    float max_mag = 0.0f, min_mag = 1.0f;
    for (const auto& s : ref) {
        float mag = std::abs(s);
        max_mag = std::max(max_mag, mag);
        min_mag = std::min(min_mag, mag);
    }
    std::cout << "Symbol magnitude range: [" << min_mag << ", " << max_mag << "]\n";
    assert(min_mag > 0.99f && max_mag < 1.01f);
    
    // Verify matches TX preamble
    M110A_Tx tx;
    auto tx_symbols = tx.generate_preamble_symbols(false);
    
    // TX generates 1440 symbols (3 segments), we have first 480
    bool match = true;
    for (size_t i = 0; i < ref.size() && i < tx_symbols.size(); i++) {
        if (std::abs(ref[i] - tx_symbols[i]) > 1e-5f) {
            match = false;
            std::cout << "Mismatch at " << i << ": ref=" << ref[i] 
                      << " tx=" << tx_symbols[i] << "\n";
            break;
        }
    }
    std::cout << "Matches TX preamble: " << (match ? "YES" : "NO") << "\n";
    assert(match);
    
    std::cout << "PASSED\n\n";
}

void test_clean_preamble_detection() {
    std::cout << "=== Test: Clean Preamble Detection ===\n";
    
    // Generate clean preamble
    M110A_Tx tx;
    auto samples = tx.generate_preamble(false);
    
    std::cout << "Input: " << samples.size() << " samples ("
              << samples.size() / SAMPLE_RATE << "s)\n";
    
    // Detect with appropriate thresholds
    PreambleDetector::Config config;
    config.detection_threshold = 0.4f;
    config.confirmation_threshold = 0.5f;
    
    PreambleDetector detector(config);
    
    float max_corr = 0.0f;
    int max_corr_sample = 0;
    
    SyncResult result;
    for (size_t i = 0; i < samples.size(); i++) {
        result = detector.process_sample(samples[i]);
        
        float mag = detector.correlation_magnitude();
        if (mag > max_corr) {
            max_corr = mag;
            max_corr_sample = i;
        }
        
        if (result.acquired) {
            std::cout << "Sync acquired at sample " << i << "!\n";
            break;
        }
    }
    
    std::cout << "Max correlation: " << max_corr << " at sample " << max_corr_sample << "\n";
    std::cout << "Final state: " << static_cast<int>(detector.state()) << "\n";
    
    if (result.acquired) {
        std::cout << "Sync result:\n";
        std::cout << "  Sample offset: " << result.sample_offset << "\n";
        std::cout << "  Freq offset: " << result.freq_offset_hz << " Hz\n";
        std::cout << "  Correlation peak: " << result.correlation_peak << "\n";
        std::cout << "  SNR estimate: " << result.snr_estimate << " dB\n";
    }
    
    assert(result.acquired);
    assert(std::abs(result.freq_offset_hz) < 10.0f);  // Should be near zero
    
    std::cout << "PASSED\n\n";
}

void test_frequency_offset_detection() {
    std::cout << "=== Test: Frequency Offset Detection ===\n";
    
    // For now, just verify clean signal has near-zero offset estimate
    // Full frequency offset testing requires proper channel simulation
    
    M110A_Tx tx;
    auto samples = tx.generate_preamble(false);
    
    PreambleDetector detector;
    SyncResult result;
    
    for (const auto& s : samples) {
        result = detector.process_sample(s);
        if (result.acquired) break;
    }
    
    std::cout << "Clean signal:\n";
    std::cout << "  Acquired: " << (result.acquired ? "YES" : "NO") << "\n";
    std::cout << "  Estimated freq offset: " << result.freq_offset_hz << " Hz\n";
    std::cout << "  (Should be near zero for clean signal)\n";
    
    assert(result.acquired);
    // Clean signal should have small estimated offset
    assert(std::abs(result.freq_offset_hz) < 20.0f);
    
    std::cout << "PASSED\n\n";
    std::cout << "NOTE: Full frequency offset testing with actual offset\n";
    std::cout << "      requires channel simulation (future enhancement)\n\n";
}

void test_noisy_detection() {
    std::cout << "=== Test: Noisy Preamble Detection ===\n";
    
    // Generate clean preamble
    M110A_Tx tx;
    auto samples = tx.generate_preamble(false);
    
    // Add noise at 15 dB SNR
    float snr_db = 15.0f;
    
    // Compute signal power
    float signal_power = 0.0f;
    for (auto s : samples) {
        signal_power += s * s;
    }
    signal_power /= samples.size();
    
    // Calculate noise power
    float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
    float noise_std = std::sqrt(noise_power);
    
    // Simple linear congruential generator
    uint32_t seed = 12345;
    auto rand_uniform = [&seed]() -> float {
        seed = seed * 1664525 + 1013904223;  // Numerical Recipes LCG
        return static_cast<float>(seed) / 4294967296.0f;  // [0, 1)
    };
    
    // Add Gaussian noise using Box-Muller
    std::vector<sample_t> noisy_samples(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float u1 = rand_uniform();
        float u2 = rand_uniform();
        // Avoid log(0)
        if (u1 < 1e-10f) u1 = 1e-10f;
        float noise = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * PI * u2);
        noisy_samples[i] = samples[i] + noise * noise_std;
    }
    
    std::cout << "SNR: " << snr_db << " dB\n";
    std::cout << "Noise std: " << noise_std << "\n";
    
    // Detect with relaxed thresholds for noisy signal
    PreambleDetector::Config config;
    config.detection_threshold = 0.3f;
    config.confirmation_threshold = 0.4f;
    
    PreambleDetector detector(config);
    SyncResult result;
    
    for (const auto& s : noisy_samples) {
        result = detector.process_sample(s);
        if (result.acquired) break;
    }
    
    std::cout << "Detection result:\n";
    std::cout << "  Acquired: " << (result.acquired ? "YES" : "NO") << "\n";
    if (result.acquired) {
        std::cout << "  Correlation peak: " << result.correlation_peak << "\n";
        std::cout << "  SNR estimate: " << result.snr_estimate << " dB\n";
    }
    
    assert(result.acquired);
    
    std::cout << "PASSED\n\n";
}

void test_long_preamble() {
    std::cout << "=== Test: Long Preamble Detection ===\n";
    
    // Generate LONG preamble (4.8s)
    M110A_Tx tx;
    auto samples = tx.generate_preamble(true);
    
    std::cout << "Input: " << samples.size() << " samples ("
              << samples.size() / SAMPLE_RATE << "s)\n";
    
    // Detect (should find it early, doesn't need full 4.8s)
    PreambleDetector::Config config;
    config.detection_threshold = 0.4f;
    config.confirmation_threshold = 0.5f;
    
    PreambleDetector detector(config);
    SyncResult result;
    int detect_sample = 0;
    
    for (size_t i = 0; i < samples.size(); i++) {
        result = detector.process_sample(samples[i]);
        if (result.acquired) {
            detect_sample = i;
            break;
        }
    }
    
    std::cout << "Detected at sample " << detect_sample 
              << " (" << detect_sample / SAMPLE_RATE << "s)\n";
    
    assert(result.acquired);
    // Should detect within first 1.5 seconds (need time for 2 correlation peaks)
    assert(detect_sample < 1.5f * SAMPLE_RATE);
    
    std::cout << "PASSED\n\n";
}

void test_from_pcm_file() {
    std::cout << "=== Test: Detection from PCM File ===\n";
    
    std::string filename = "test/vectors/clean/preamble_short.pcm";
    
    try {
        PcmFileReader reader(filename);
        auto samples = reader.read_all();
        
        std::cout << "Read: " << samples.size() << " samples from " << filename << "\n";
        
        PreambleDetector::Config config;
        config.detection_threshold = 0.4f;
        config.confirmation_threshold = 0.5f;
        
        PreambleDetector detector(config);
        SyncResult result;
        
        for (const auto& s : samples) {
            result = detector.process_sample(s);
            if (result.acquired) break;
        }
        
        std::cout << "Detection result:\n";
        std::cout << "  Acquired: " << (result.acquired ? "YES" : "NO") << "\n";
        std::cout << "  Freq offset: " << result.freq_offset_hz << " Hz\n";
        std::cout << "  Correlation: " << result.correlation_peak << "\n";
        
        assert(result.acquired);
        std::cout << "PASSED\n\n";
        
    } catch (const std::exception& e) {
        std::cout << "Could not open file (run generate_test_signals first): " 
                  << e.what() << "\n";
        std::cout << "SKIPPED\n\n";
    }
}

void test_correlation_profile() {
    std::cout << "=== Test: Correlation Profile ===\n";
    
    M110A_Tx tx;
    auto samples = tx.generate_preamble(false);
    
    PreambleDetector detector;
    
    // Collect correlation values
    std::vector<float> correlations;
    std::vector<int> peak_indices;
    float threshold = 0.4f;
    
    float prev_corr = 0.0f;
    for (size_t i = 0; i < samples.size(); i++) {
        detector.process_sample(samples[i]);
        float corr = detector.correlation_magnitude();
        correlations.push_back(corr);
        
        // Detect local peaks above threshold
        if (i > 0 && prev_corr > threshold && prev_corr > corr && 
            prev_corr > correlations[i-2]) {
            peak_indices.push_back(i - 1);
        }
        prev_corr = corr;
    }
    
    std::cout << "Found " << peak_indices.size() << " correlation peaks\n";
    std::cout << "Peak positions:\n";
    
    int expected_spacing = static_cast<int>(480 * SAMPLE_RATE / SYMBOL_RATE);
    std::cout << "Expected spacing: " << expected_spacing << " samples (0.2s)\n\n";
    
    for (size_t i = 0; i < peak_indices.size(); i++) {
        std::cout << "  Peak " << i << " at sample " << peak_indices[i]
                  << " (corr=" << correlations[peak_indices[i]] << ")";
        if (i > 0) {
            int spacing = peak_indices[i] - peak_indices[i-1];
            std::cout << " spacing=" << spacing;
        }
        std::cout << "\n";
    }
    
    // Should have 3 peaks for 3 preamble segments
    // (but correlation builds up, so may detect at end of each segment)
    assert(peak_indices.size() >= 2);
    
    std::cout << "PASSED\n\n";
}

int main() {
    std::cout << "M110A Preamble Detector Tests\n";
    std::cout << "=============================\n\n";
    
    test_reference_generation();
    test_clean_preamble_detection();
    test_correlation_profile();
    test_frequency_offset_detection();
    test_noisy_detection();
    test_long_preamble();
    test_from_pcm_file();
    
    std::cout << "All preamble detector tests passed!\n";
    return 0;
}
