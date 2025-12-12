// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_sample_source.cpp
 * @brief Unit tests for SampleSource interface and implementations
 * 
 * Tests:
 * - IQSource format conversions (int16 planar, int16 interleaved, float planar, float interleaved)
 * - IQSource decimation (2 MSPS → 48 kHz)
 * - AudioSource real-to-complex conversion
 * - SampleSource interface polymorphism
 */

#include "api/sample_source.h"
#include "api/audio_source.h"
#include "api/iq_source.h"
#include <iostream>
#include <cmath>
#include <cassert>

using namespace m110a;

// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) std::cout << "Testing " << name << "... "
#define PASS() do { std::cout << "PASS" << std::endl; tests_passed++; } while(0)
#define FAIL(msg) do { std::cout << "FAIL: " << msg << std::endl; tests_failed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/**
 * Test IQSource int16 planar format conversion
 */
void test_iq_source_int16_planar() {
    TEST("IQSource int16 planar conversion");
    
    // Create source with 48kHz input (1:1, no decimation)
    IQSource source(48000.0, IQSource::Format::INT16_PLANAR, 48000.0);
    
    // Test data: simple I/Q samples
    const int16_t xi[] = {16384, 0, -16384, 0};
    const int16_t xq[] = {0, 16384, 0, -16384};
    
    source.push_samples_planar(xi, xq, 4);
    
    ASSERT(source.has_data(), "Source should have data");
    ASSERT(source.samples_available() == 4, "Should have 4 samples");
    
    std::complex<float> output[4];
    size_t read = source.read(output, 4);
    
    ASSERT(read == 4, "Should read 4 samples");
    
    // Check values (16384/32768 = 0.5)
    float expected_val = 16384.0f / 32768.0f;
    ASSERT(std::abs(output[0].real() - expected_val) < 0.001f, "Sample 0 I incorrect");
    ASSERT(std::abs(output[0].imag() - 0.0f) < 0.001f, "Sample 0 Q incorrect");
    ASSERT(std::abs(output[1].real() - 0.0f) < 0.001f, "Sample 1 I incorrect");
    ASSERT(std::abs(output[1].imag() - expected_val) < 0.001f, "Sample 1 Q incorrect");
    
    PASS();
}

/**
 * Test IQSource int16 interleaved format conversion
 */
void test_iq_source_int16_interleaved() {
    TEST("IQSource int16 interleaved conversion");
    
    IQSource source(48000.0, IQSource::Format::INT16_INTERLEAVED, 48000.0);
    
    // Interleaved: I0, Q0, I1, Q1, ...
    const int16_t iq[] = {16384, 0, 0, 16384, -16384, 0, 0, -16384};
    
    source.push_samples_interleaved(iq, 4);  // 4 pairs
    
    ASSERT(source.samples_available() == 4, "Should have 4 samples");
    
    std::complex<float> output[4];
    size_t read = source.read(output, 4);
    
    ASSERT(read == 4, "Should read 4 samples");
    
    float expected_val = 16384.0f / 32768.0f;
    ASSERT(std::abs(output[0].real() - expected_val) < 0.001f, "Sample 0 I incorrect");
    ASSERT(std::abs(output[0].imag() - 0.0f) < 0.001f, "Sample 0 Q incorrect");
    
    PASS();
}

/**
 * Test IQSource float planar format conversion
 */
void test_iq_source_float32_planar() {
    TEST("IQSource float32 planar conversion");
    
    IQSource source(48000.0, IQSource::Format::FLOAT32_PLANAR, 48000.0);
    
    const float xi[] = {0.5f, 0.0f, -0.5f, 0.0f};
    const float xq[] = {0.0f, 0.5f, 0.0f, -0.5f};
    
    source.push_samples_planar(xi, xq, 4);
    
    ASSERT(source.samples_available() == 4, "Should have 4 samples");
    
    std::complex<float> output[4];
    size_t read = source.read(output, 4);
    
    ASSERT(read == 4, "Should read 4 samples");
    ASSERT(std::abs(output[0].real() - 0.5f) < 0.001f, "Sample 0 I incorrect");
    ASSERT(std::abs(output[0].imag() - 0.0f) < 0.001f, "Sample 0 Q incorrect");
    
    PASS();
}

/**
 * Test IQSource float32 interleaved format conversion
 */
void test_iq_source_float32_interleaved() {
    TEST("IQSource float32 interleaved conversion");
    
    IQSource source(48000.0, IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    const float iq[] = {0.5f, 0.0f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, -0.5f};
    
    source.push_samples_interleaved(iq, 4);
    
    ASSERT(source.samples_available() == 4, "Should have 4 samples");
    
    std::complex<float> output[4];
    size_t read = source.read(output, 4);
    
    ASSERT(read == 4, "Should read 4 samples");
    ASSERT(std::abs(output[0].real() - 0.5f) < 0.001f, "Sample 0 I incorrect");
    
    PASS();
}

/**
 * Test IQSource decimation from 2 MSPS to 48 kHz
 */
void test_iq_source_decimation() {
    TEST("IQSource decimation (2 MSPS -> 48 kHz)");
    
    IQSource source(2000000.0, IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Generate a test tone: 1 kHz sine wave at 2 MSPS
    // Should produce approximately (2M / 48k) = 41.67x fewer output samples
    const size_t input_samples = 200000;  // 0.1 seconds of input
    std::vector<float> iq_data;
    iq_data.reserve(input_samples * 2);
    
    const float freq = 1000.0f;  // 1 kHz tone
    const float sample_rate = 2000000.0f;
    
    for (size_t i = 0; i < input_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float phase = 2.0f * static_cast<float>(M_PI) * freq * t;
        iq_data.push_back(std::cos(phase));  // I
        iq_data.push_back(std::sin(phase));  // Q
    }
    
    source.push_samples_interleaved(iq_data.data(), input_samples);
    
    // Expected output: approximately 200000 / 41.67 ≈ 4800 samples
    size_t available = source.samples_available();
    
    // Allow 20% tolerance for filter transients
    size_t expected_min = static_cast<size_t>(input_samples * 48000.0 / 2000000.0 * 0.8);
    size_t expected_max = static_cast<size_t>(input_samples * 48000.0 / 2000000.0 * 1.2);
    
    ASSERT(available >= expected_min && available <= expected_max,
           "Decimation ratio incorrect");
    
    // Read output and verify it looks like a 1 kHz tone
    std::vector<std::complex<float>> output(available);
    size_t read = source.read(output.data(), available);
    
    ASSERT(read == available, "Should read all available samples");
    
    // Check that output has roughly correct frequency content
    // (simple check: verify oscillation period is approximately 48 samples for 1 kHz at 48 kHz)
    // Skip first 100 samples for filter settling
    if (read > 200) {
        // Find zero crossings on real part
        int zero_crossings = 0;
        for (size_t i = 101; i < read - 1; ++i) {
            if ((output[i].real() >= 0 && output[i+1].real() < 0) ||
                (output[i].real() < 0 && output[i+1].real() >= 0)) {
                zero_crossings++;
            }
        }
        
        // Expected: ~2 zero crossings per cycle, ~100 cycles in 4800 samples
        // So roughly 200 zero crossings
        ASSERT(zero_crossings > 50, "Output doesn't look like a sine wave");
    }
    
    PASS();
}

/**
 * Test AudioSource basic functionality
 */
void test_audio_source_basic() {
    TEST("AudioSource basic conversion");
    
    // Create a simple audio tone
    std::vector<float> audio(4800);  // 0.1 sec at 48 kHz
    const float freq = 1800.0f;  // Carrier frequency
    
    for (size_t i = 0; i < audio.size(); ++i) {
        float t = static_cast<float>(i) / 48000.0f;
        audio[i] = std::cos(2.0f * static_cast<float>(M_PI) * freq * t);
    }
    
    AudioSource source(audio);
    source.set_carrier_freq(1800.0);
    
    ASSERT(source.has_data(), "Source should have data");
    ASSERT(source.total_samples() == 4800, "Should have 4800 samples");
    ASSERT(source.source_type() == std::string("audio"), "Source type should be 'audio'");
    
    std::vector<std::complex<float>> output(4800);
    size_t read = source.read(output.data(), 4800);
    
    ASSERT(read == 4800, "Should read all samples");
    ASSERT(!source.has_data(), "Should be empty after reading all");
    
    // After downconversion at carrier frequency, the output contains both
    // DC (difference) and 2x carrier (sum) terms. Without a lowpass filter,
    // individual sample magnitudes oscillate. Check mean magnitude instead.
    float sum_mag = 0;
    for (size_t i = 100; i < 4700; ++i) {
        sum_mag += std::abs(output[i]);
    }
    float mean_mag = sum_mag / 4600.0f;
    
    // Mean magnitude should be approximately 0.5-0.7 for a unit amplitude input
    ASSERT(mean_mag > 0.3f && mean_mag < 1.0f, "Mean output magnitude out of range");
    
    PASS();
}

/**
 * Test AudioSource PCM input
 */
void test_audio_source_pcm() {
    TEST("AudioSource PCM input");
    
    // Create PCM samples
    int16_t pcm[100];
    for (int i = 0; i < 100; ++i) {
        pcm[i] = static_cast<int16_t>(16384 * std::sin(2.0 * M_PI * i / 48.0));
    }
    
    AudioSource source(pcm, 100);
    
    ASSERT(source.total_samples() == 100, "Should have 100 samples");
    
    PASS();
}

/**
 * Test SampleSource polymorphism
 */
void test_sample_source_polymorphism() {
    TEST("SampleSource polymorphism");
    
    // Create both types through base pointer
    std::vector<float> audio = {0.1f, 0.2f, 0.3f, 0.4f};
    
    SampleSource* audio_ptr = new AudioSource(audio);
    SampleSource* iq_ptr = new IQSource(48000.0, IQSource::Format::FLOAT32_PLANAR, 48000.0);
    
    ASSERT(std::string(audio_ptr->source_type()) == "audio", "Audio type incorrect");
    ASSERT(std::string(iq_ptr->source_type()) == "iq", "IQ type incorrect");
    ASSERT(audio_ptr->sample_rate() == 48000.0, "Audio sample rate incorrect");
    ASSERT(iq_ptr->sample_rate() == 48000.0, "IQ sample rate incorrect");
    
    delete audio_ptr;
    delete iq_ptr;
    
    PASS();
}

/**
 * Test IQSource reset functionality
 */
void test_iq_source_reset() {
    TEST("IQSource reset");
    
    IQSource source(48000.0, IQSource::Format::FLOAT32_PLANAR, 48000.0);
    
    const float xi[] = {0.5f, 0.5f};
    const float xq[] = {0.5f, 0.5f};
    source.push_samples_planar(xi, xq, 2);
    
    ASSERT(source.samples_available() == 2, "Should have samples before reset");
    
    source.reset();
    
    ASSERT(source.samples_available() == 0, "Should be empty after reset");
    ASSERT(!source.has_data(), "Should have no data after reset");
    
    PASS();
}

/**
 * Test IQSource metadata
 */
void test_iq_source_metadata() {
    TEST("IQSource metadata");
    
    IQSource source(2000000.0, IQSource::Format::INT16_PLANAR, 48000.0);
    
    source.set_metadata(14070000.0, 200000.0);
    
    ASSERT(source.center_frequency() == 14070000.0, "Center frequency incorrect");
    ASSERT(source.bandwidth() == 200000.0, "Bandwidth incorrect");
    ASSERT(source.input_rate() == 2000000.0, "Input rate incorrect");
    
    PASS();
}

int main() {
    std::cout << "=== SampleSource Unit Tests ===" << std::endl;
    std::cout << std::endl;
    
    // Format conversion tests
    test_iq_source_int16_planar();
    test_iq_source_int16_interleaved();
    test_iq_source_float32_planar();
    test_iq_source_float32_interleaved();
    
    // Decimation test
    test_iq_source_decimation();
    
    // AudioSource tests
    test_audio_source_basic();
    test_audio_source_pcm();
    
    // Interface tests
    test_sample_source_polymorphism();
    test_iq_source_reset();
    test_iq_source_metadata();
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
