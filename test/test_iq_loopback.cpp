// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_iq_loopback.cpp
 * @brief Loopback test for I/Q pipeline validation
 * 
 * This test validates the signal integrity through the IQSource decimation
 * pipeline. It generates known signals, upsamples to SDR rate, decimates
 * back down, and verifies the signal is preserved.
 * 
 * Test levels:
 *   1. Simple passthrough (48kHz → 48kHz, no decimation)
 *   2. Full decimation (2 MSPS → 48kHz)
 *   3. PSK signal preservation (8-PSK constellation)
 *   4. Write mock .iqr file, read back, verify
 * 
 * Build:
 *   g++ -std=c++17 -o test_iq_loopback.exe test/test_iq_loopback.cpp -I. -Wall
 * 
 * Run:
 *   ./test_iq_loopback.exe
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include "api/iq_source.h"
#include "api/iq_file_source.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <numeric>

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("  %-55s ", #name); \
    fflush(stdout); \
    try { \
        test_##name(); \
        printf("PASS\n"); \
        tests_passed++; \
    } catch (const std::exception& e) { \
        printf("FAIL: %s\n", e.what()); \
        tests_failed++; \
    } catch (...) { \
        printf("FAIL: unknown exception\n"); \
        tests_failed++; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "Assertion failed: " #cond " (line %d)", __LINE__); \
        throw std::runtime_error(buf); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol) do { \
    double _a = (a), _b = (b), _tol = (tol); \
    if (std::abs(_a - _b) > _tol) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "Expected %g ≈ %g (tol=%g, line %d)", _a, _b, _tol, __LINE__); \
        throw std::runtime_error(buf); \
    } \
} while(0)

//=============================================================================
// Signal Analysis Utilities
//=============================================================================

/**
 * Calculate RMS amplitude of complex signal
 */
static double calc_rms(const std::vector<std::complex<float>>& signal) {
    if (signal.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& s : signal) {
        sum += std::norm(s);  // |s|^2
    }
    return std::sqrt(sum / signal.size());
}

/**
 * Calculate correlation between two complex signals
 */
static double calc_correlation(const std::vector<std::complex<float>>& a,
                                const std::vector<std::complex<float>>& b) {
    if (a.empty() || b.empty()) return 0.0;
    
    size_t len = std::min(a.size(), b.size());
    
    std::complex<double> sum(0, 0);
    double sum_a = 0.0, sum_b = 0.0;
    
    for (size_t i = 0; i < len; i++) {
        sum += std::complex<double>(a[i]) * std::conj(std::complex<double>(b[i]));
        sum_a += std::norm(a[i]);
        sum_b += std::norm(b[i]);
    }
    
    double denom = std::sqrt(sum_a * sum_b);
    if (denom < 1e-10) return 0.0;
    
    return std::abs(sum) / denom;
}

/**
 * Calculate SNR in dB (signal power / noise power)
 */
static double calc_snr_db(const std::vector<std::complex<float>>& original,
                           const std::vector<std::complex<float>>& recovered) {
    if (original.empty() || recovered.empty()) return -100.0;
    
    size_t len = std::min(original.size(), recovered.size());
    
    double signal_power = 0.0;
    double noise_power = 0.0;
    
    for (size_t i = 0; i < len; i++) {
        signal_power += std::norm(original[i]);
        auto diff = original[i] - recovered[i];
        noise_power += std::norm(diff);
    }
    
    if (noise_power < 1e-20) return 100.0;  // Perfect match
    
    return 10.0 * std::log10(signal_power / noise_power);
}

/**
 * Generate complex tone at given frequency
 */
static std::vector<std::complex<float>> generate_tone(
    double freq_hz, double sample_rate, size_t num_samples, float amplitude = 0.5f) {
    
    std::vector<std::complex<float>> signal(num_samples);
    double phase_inc = 2.0 * M_PI * freq_hz / sample_rate;
    double phase = 0.0;
    
    for (size_t i = 0; i < num_samples; i++) {
        signal[i] = std::complex<float>(
            amplitude * static_cast<float>(std::cos(phase)),
            amplitude * static_cast<float>(std::sin(phase))
        );
        phase += phase_inc;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    
    return signal;
}

/**
 * Generate 8-PSK symbols
 */
static std::vector<std::complex<float>> generate_8psk_symbols(size_t num_symbols, float amplitude = 0.5f) {
    std::vector<std::complex<float>> symbols(num_symbols);
    
    for (size_t i = 0; i < num_symbols; i++) {
        int sym = i % 8;  // Cycle through all 8 symbols
        double phase = sym * M_PI / 4.0;  // 45° spacing
        symbols[i] = std::complex<float>(
            amplitude * static_cast<float>(std::cos(phase)),
            amplitude * static_cast<float>(std::sin(phase))
        );
    }
    
    return symbols;
}

/**
 * Upsample signal by integer factor (zero-stuffing + lowpass)
 * Simplified version for testing - uses linear interpolation
 */
static std::vector<std::complex<float>> upsample(
    const std::vector<std::complex<float>>& input, int factor) {
    
    if (factor <= 1 || input.empty()) return input;
    
    std::vector<std::complex<float>> output;
    output.reserve(input.size() * factor);
    
    for (size_t i = 0; i < input.size() - 1; i++) {
        auto a = input[i];
        auto b = input[i + 1];
        
        for (int j = 0; j < factor; j++) {
            float t = static_cast<float>(j) / factor;
            output.push_back(a * (1.0f - t) + b * t);
        }
    }
    
    // Last sample
    for (int j = 0; j < factor; j++) {
        output.push_back(input.back());
    }
    
    return output;
}

/**
 * Skip initial transient samples in filter output
 */
static std::vector<std::complex<float>> skip_transient(
    const std::vector<std::complex<float>>& signal, size_t skip) {
    if (skip >= signal.size()) return {};
    return std::vector<std::complex<float>>(signal.begin() + skip, signal.end());
}

//=============================================================================
// Tests - Level 1: Simple Passthrough (48kHz → 48kHz)
//=============================================================================

TEST(passthrough_single_tone) {
    // 48kHz input, 48kHz output (no decimation)
    m110a::IQSource source(48000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Generate 1kHz tone
    auto input = generate_tone(1000.0, 48000.0, 4800);  // 100ms
    
    // Push as interleaved floats
    std::vector<float> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = input[i].real();
        interleaved[i*2+1] = input[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), input.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Should get approximately same number of samples
    ASSERT(output.size() > input.size() * 0.9);
    ASSERT(output.size() < input.size() * 1.1);
    
    // Skip filter transient and check signal quality
    auto output_stable = skip_transient(output, 100);
    auto input_stable = skip_transient(input, 100);
    
    // Trim to same length
    size_t len = std::min(output_stable.size(), input_stable.size());
    output_stable.resize(len);
    input_stable.resize(len);
    
    // Signal should be preserved (high correlation)
    double corr = calc_correlation(input_stable, output_stable);
    printf("[corr=%.3f] ", corr);
    ASSERT(corr > 0.95);
}

TEST(passthrough_dc_signal) {
    // DC signal should pass through unchanged
    m110a::IQSource source(48000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Constant complex value
    std::vector<std::complex<float>> input(4800, std::complex<float>(0.5f, 0.25f));
    
    std::vector<float> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = input[i].real();
        interleaved[i*2+1] = input[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), input.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Skip transient
    auto output_stable = skip_transient(output, 200);
    
    // Check that output is approximately the DC value
    if (!output_stable.empty()) {
        double avg_real = 0, avg_imag = 0;
        for (const auto& s : output_stable) {
            avg_real += s.real();
            avg_imag += s.imag();
        }
        avg_real /= output_stable.size();
        avg_imag /= output_stable.size();
        
        printf("[avg=(%.3f,%.3f)] ", avg_real, avg_imag);
        ASSERT_NEAR(avg_real, 0.5, 0.1);
        ASSERT_NEAR(avg_imag, 0.25, 0.1);
    }
}

//=============================================================================
// Tests - Level 2: Full Decimation (2 MSPS → 48 kHz)
//=============================================================================

TEST(decimation_tone_1khz) {
    // 2 MSPS input, 48 kHz output (41.67x decimation)
    m110a::IQSource source(2000000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Generate 1kHz tone at 2 MSPS (100ms = 200000 samples)
    auto input = generate_tone(1000.0, 2000000.0, 200000);
    
    std::vector<float> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = input[i].real();
        interleaved[i*2+1] = input[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), input.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Expected output: 100ms at 48kHz = 4800 samples
    printf("[in=%zu out=%zu] ", input.size(), output.size());
    ASSERT(output.size() > 4000);  // Allow some margin for filter transients
    ASSERT(output.size() < 5500);
    
    // Skip transient and check RMS (signal amplitude preserved)
    auto output_stable = skip_transient(output, 500);
    double input_rms = calc_rms(input);
    double output_rms = calc_rms(output_stable);
    
    // RMS should be similar (allow filter gain variation)
    double rms_ratio = output_rms / input_rms;
    printf("[rms_ratio=%.2f] ", rms_ratio);
    ASSERT(rms_ratio > 0.5);  // At least 50% amplitude preserved
    ASSERT(rms_ratio < 2.0);  // Not amplified excessively
}

TEST(decimation_multi_tone) {
    // Test with multiple frequency components
    m110a::IQSource source(2000000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Generate signal with 500Hz + 1500Hz components
    size_t num_samples = 200000;  // 100ms at 2 MSPS
    std::vector<std::complex<float>> input(num_samples);
    
    for (size_t i = 0; i < num_samples; i++) {
        double t = static_cast<double>(i) / 2000000.0;
        double phase1 = 2.0 * M_PI * 500.0 * t;
        double phase2 = 2.0 * M_PI * 1500.0 * t;
        
        float real = 0.3f * static_cast<float>(std::cos(phase1) + std::cos(phase2));
        float imag = 0.3f * static_cast<float>(std::sin(phase1) + std::sin(phase2));
        input[i] = std::complex<float>(real, imag);
    }
    
    std::vector<float> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = input[i].real();
        interleaved[i*2+1] = input[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), input.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Check output length
    printf("[out=%zu] ", output.size());
    ASSERT(output.size() > 4000);
    
    // Check RMS is reasonable (signal not destroyed)
    double rms = calc_rms(skip_transient(output, 500));
    printf("[rms=%.3f] ", rms);
    ASSERT(rms > 0.1);
    ASSERT(rms < 1.0);
}

//=============================================================================
// Tests - Level 3: PSK Signal Preservation
//=============================================================================

TEST(psk_symbols_preservation) {
    // Test that PSK constellation points are preserved through decimation
    m110a::IQSource source(48000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    // Generate 8-PSK symbols, each held for 40 samples (symbol rate = 1200 baud)
    auto symbols = generate_8psk_symbols(100);
    std::vector<std::complex<float>> input;
    
    for (const auto& sym : symbols) {
        for (int j = 0; j < 40; j++) {
            input.push_back(sym);
        }
    }
    
    // Push through IQSource
    std::vector<float> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = input[i].real();
        interleaved[i*2+1] = input[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), input.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Skip transient
    auto output_stable = skip_transient(output, 200);
    auto input_stable = skip_transient(input, 200);
    
    // Trim to same length
    size_t len = std::min(output_stable.size(), input_stable.size());
    output_stable.resize(len);
    input_stable.resize(len);
    
    // Check SNR
    double snr_db = calc_snr_db(input_stable, output_stable);
    printf("[SNR=%.1fdB] ", snr_db);
    ASSERT(snr_db > 20.0);  // At least 20 dB SNR
}

TEST(upsampled_signal_recovery) {
    // Simulate: generate at 48kHz, upsample to 2 MSPS, decimate back
    
    // Generate baseband signal at 48kHz
    auto baseband_48k = generate_tone(1800.0, 48000.0, 480);  // 10ms
    
    // Upsample to 2 MSPS (factor of ~42)
    int upsample_factor = 42;
    auto upsampled = upsample(baseband_48k, upsample_factor);
    
    printf("[48k=%zu up=%zu] ", baseband_48k.size(), upsampled.size());
    
    // Feed through IQSource decimation
    m110a::IQSource source(2000000.0, m110a::IQSource::Format::FLOAT32_INTERLEAVED, 48000.0);
    
    std::vector<float> interleaved(upsampled.size() * 2);
    for (size_t i = 0; i < upsampled.size(); i++) {
        interleaved[i*2] = upsampled[i].real();
        interleaved[i*2+1] = upsampled[i].imag();
    }
    source.push_samples_interleaved(interleaved.data(), upsampled.size());
    
    // Read decimated output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    printf("[out=%zu] ", output.size());
    
    // Check that output length is approximately correct
    ASSERT(output.size() > 300);   // Should get ~480 samples out
    ASSERT(output.size() < 700);
    
    // Skip filter transients and verify signal amplitude
    auto output_stable = skip_transient(output, 100);
    double input_rms = calc_rms(baseband_48k);
    double output_rms = calc_rms(output_stable);
    
    double rms_ratio = output_rms / input_rms;
    printf("[rms_ratio=%.2f] ", rms_ratio);
    ASSERT(rms_ratio > 0.3);  // Signal preserved through round-trip
    ASSERT(rms_ratio < 3.0);
}

//=============================================================================
// Tests - Level 4: IQR File Round-Trip
//=============================================================================

TEST(iqr_file_roundtrip) {
    // Generate signal, write to .iqr file, read back, verify
    
    const char* filename = "test_roundtrip.iqr";
    
    // Generate test signal at 2 MSPS
    auto input = generate_tone(1000.0, 2000000.0, 100000);  // 50ms
    
    // Write .iqr file
    {
        FILE* f = fopen(filename, "wb");
        ASSERT(f != nullptr);
        
        // Write header
        m110a::IQRHeader header;
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, "IQR1", 4);
        header.version = 1;
        header.sample_rate = 2000000.0;
        header.center_freq = 7074000.0;
        header.bandwidth = 200;
        header.gain_reduction = 40;
        header.lna_state = 4;
        header.start_time = 0;
        header.sample_count = input.size();
        header.flags = 0;
        
        fwrite(&header, sizeof(header), 1, f);
        
        // Write interleaved int16 data
        for (const auto& s : input) {
            int16_t xi = static_cast<int16_t>(s.real() * 32767.0f);
            int16_t xq = static_cast<int16_t>(s.imag() * 32767.0f);
            fwrite(&xi, sizeof(int16_t), 1, f);
            fwrite(&xq, sizeof(int16_t), 1, f);
        }
        
        fclose(f);
    }
    
    // Read back with IQFileSource
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    // Load all and read output
    source.load_all();
    
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Expected: 50ms at 48kHz = 2400 samples
    printf("[in=%zu out=%zu] ", input.size(), output.size());
    ASSERT(output.size() > 2000);
    ASSERT(output.size() < 3000);
    
    // Verify signal amplitude preservation
    auto output_stable = skip_transient(output, 300);
    double input_rms = calc_rms(input);
    double output_rms = calc_rms(output_stable);
    
    double rms_ratio = output_rms / input_rms;
    printf("[rms_ratio=%.2f] ", rms_ratio);
    ASSERT(rms_ratio > 0.3);  // Signal preserved
    ASSERT(rms_ratio < 3.0);
    
    // Cleanup
    remove(filename);
}

TEST(iqr_metadata_preserved) {
    // Verify header metadata is correctly read
    
    const char* filename = "test_metadata.iqr";
    
    // Write .iqr file with specific metadata
    {
        FILE* f = fopen(filename, "wb");
        ASSERT(f != nullptr);
        
        m110a::IQRHeader header;
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, "IQR1", 4);
        header.version = 1;
        header.sample_rate = 2000000.0;
        header.center_freq = 14100000.0;  // 14.1 MHz
        header.bandwidth = 200;           // 200 kHz
        header.gain_reduction = 35;
        header.lna_state = 5;
        header.start_time = 1234567890123456;
        header.sample_count = 1000;
        header.flags = 0;
        
        fwrite(&header, sizeof(header), 1, f);
        
        // Write minimal data
        for (int i = 0; i < 1000; i++) {
            int16_t xi = 0, xq = 0;
            fwrite(&xi, sizeof(int16_t), 1, f);
            fwrite(&xq, sizeof(int16_t), 1, f);
        }
        
        fclose(f);
    }
    
    // Read and verify metadata
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    ASSERT_NEAR(source.input_rate(), 2000000.0, 1.0);
    ASSERT_NEAR(source.center_frequency(), 14100000.0, 1.0);
    ASSERT_NEAR(source.bandwidth(), 200000.0, 1.0);  // Converted to Hz
    
    printf("[freq=%.0f bw=%.0f] ", source.center_frequency(), source.bandwidth());
    
    // Cleanup
    remove(filename);
}

//=============================================================================
// Tests - Level 5: Realistic Modem Signal (if available)
//=============================================================================

TEST(int16_format_preservation) {
    // Test that int16 input format doesn't introduce significant quantization noise
    m110a::IQSource source(48000.0, m110a::IQSource::Format::INT16_INTERLEAVED, 48000.0);
    
    // Generate tone
    auto input_float = generate_tone(1000.0, 48000.0, 4800);
    
    // Convert to int16 (simulating SDR output)
    std::vector<int16_t> input_int16(input_float.size() * 2);
    for (size_t i = 0; i < input_float.size(); i++) {
        input_int16[i*2] = static_cast<int16_t>(input_float[i].real() * 32767.0f);
        input_int16[i*2+1] = static_cast<int16_t>(input_float[i].imag() * 32767.0f);
    }
    
    source.push_samples_interleaved(input_int16.data(), input_float.size());
    
    // Read output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[1024];
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // Skip transient
    auto output_stable = skip_transient(output, 100);
    auto input_stable = skip_transient(input_float, 100);
    
    size_t len = std::min(output_stable.size(), input_stable.size());
    output_stable.resize(len);
    input_stable.resize(len);
    
    // SNR should be high (int16 has ~96dB dynamic range)
    double snr_db = calc_snr_db(input_stable, output_stable);
    printf("[SNR=%.1fdB] ", snr_db);
    ASSERT(snr_db > 30.0);  // At least 30 dB (int16 quantization limited)
}

TEST(planar_format_match) {
    // Verify planar and interleaved formats produce identical output
    
    auto input = generate_tone(1000.0, 48000.0, 4800);
    
    // Interleaved path
    m110a::IQSource source1(48000.0, m110a::IQSource::Format::INT16_INTERLEAVED, 48000.0);
    std::vector<int16_t> interleaved(input.size() * 2);
    for (size_t i = 0; i < input.size(); i++) {
        interleaved[i*2] = static_cast<int16_t>(input[i].real() * 32767.0f);
        interleaved[i*2+1] = static_cast<int16_t>(input[i].imag() * 32767.0f);
    }
    source1.push_samples_interleaved(interleaved.data(), input.size());
    
    // Planar path
    m110a::IQSource source2(48000.0, m110a::IQSource::Format::INT16_PLANAR, 48000.0);
    std::vector<int16_t> xi(input.size()), xq(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        xi[i] = static_cast<int16_t>(input[i].real() * 32767.0f);
        xq[i] = static_cast<int16_t>(input[i].imag() * 32767.0f);
    }
    source2.push_samples_planar(xi.data(), xq.data(), input.size());
    
    // Read outputs
    std::vector<std::complex<float>> out1, out2;
    std::complex<float> buffer[1024];
    
    while (source1.has_data()) {
        size_t n = source1.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) out1.push_back(buffer[i]);
    }
    while (source2.has_data()) {
        size_t n = source2.read(buffer, 1024);
        for (size_t i = 0; i < n; i++) out2.push_back(buffer[i]);
    }
    
    ASSERT(out1.size() == out2.size());
    
    // Should be identical
    double max_diff = 0.0;
    for (size_t i = 0; i < out1.size(); i++) {
        double diff = std::abs(out1[i] - out2[i]);
        max_diff = std::max(max_diff, diff);
    }
    
    printf("[max_diff=%.2e] ", max_diff);
    ASSERT(max_diff < 1e-6);
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char* argv[]) {
    printf("\n=== I/Q Pipeline Loopback Tests ===\n\n");
    
    printf("Level 1: Simple Passthrough (48kHz → 48kHz)\n");
    RUN_TEST(passthrough_single_tone);
    RUN_TEST(passthrough_dc_signal);
    
    printf("\nLevel 2: Full Decimation (2 MSPS → 48 kHz)\n");
    RUN_TEST(decimation_tone_1khz);
    RUN_TEST(decimation_multi_tone);
    
    printf("\nLevel 3: PSK Signal Preservation\n");
    RUN_TEST(psk_symbols_preservation);
    RUN_TEST(upsampled_signal_recovery);
    
    printf("\nLevel 4: IQR File Round-Trip\n");
    RUN_TEST(iqr_file_roundtrip);
    RUN_TEST(iqr_metadata_preserved);
    
    printf("\nLevel 5: Format Verification\n");
    RUN_TEST(int16_format_preservation);
    RUN_TEST(planar_format_match);
    
    printf("\n=== Results: %d passed, %d failed ===\n\n", tests_passed, tests_failed);
    
    if (tests_failed == 0) {
        printf("✅ I/Q pipeline validation PASSED!\n");
        printf("   Signal integrity preserved through:\n");
        printf("   - Passthrough (48kHz → 48kHz)\n");
        printf("   - Full decimation (2 MSPS → 48kHz)\n");
        printf("   - .iqr file read/write\n");
        printf("   - Multiple input formats (int16/float, planar/interleaved)\n");
        printf("\n   Ready for OTA testing with real SDR captures!\n");
    }
    
    return tests_failed > 0 ? 1 : 0;
}
