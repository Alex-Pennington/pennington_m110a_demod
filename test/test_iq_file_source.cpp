// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_iq_file_source.cpp
 * @brief Tests for IQFileSource (.iqr file reader)
 * 
 * Build:
 *   g++ -std=c++17 -o test_iq_file_source.exe test/test_iq_file_source.cpp -I.
 * 
 * Run:
 *   ./test_iq_file_source.exe [path/to/capture.iqr]
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include "api/iq_file_source.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
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
        throw std::runtime_error("Assertion failed: " #cond); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "Expected %s == %s", #a, #b); \
        throw std::runtime_error(buf); \
    } \
} while(0)

//=============================================================================
// Create a synthetic .iqr file for testing
//=============================================================================

static std::string create_test_iqr_file() {
    const char* filename = "test_synthetic.iqr";
    FILE* f = fopen(filename, "wb");
    if (!f) {
        throw std::runtime_error("Failed to create test file");
    }
    
    // Create header
    m110a::IQRHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "IQR1", 4);
    header.version = 1;
    header.sample_rate = 2000000.0;  // 2 MSPS
    header.center_freq = 7074000.0;  // 7.074 MHz
    header.bandwidth = 200;          // 200 kHz
    header.gain_reduction = 40;
    header.lna_state = 4;
    header.start_time = 0;
    header.sample_count = 20000;     // 20000 samples = 10ms at 2 MSPS
    header.flags = 0;
    
    fwrite(&header, sizeof(header), 1, f);
    
    // Write synthetic I/Q data (1 kHz tone at 2 MSPS)
    const double freq = 1000.0;  // 1 kHz test tone
    const double sample_rate = 2000000.0;
    
    for (uint64_t i = 0; i < header.sample_count; i++) {
        double t = static_cast<double>(i) / sample_rate;
        double phase = 2.0 * M_PI * freq * t;
        
        int16_t xi = static_cast<int16_t>(16000.0 * cos(phase));
        int16_t xq = static_cast<int16_t>(16000.0 * sin(phase));
        
        fwrite(&xi, sizeof(int16_t), 1, f);
        fwrite(&xq, sizeof(int16_t), 1, f);
    }
    
    fclose(f);
    return filename;
}

static void cleanup_test_file(const std::string& filename) {
    remove(filename.c_str());
}

//=============================================================================
// Tests
//=============================================================================

TEST(header_size) {
    // Verify header is exactly 64 bytes
    ASSERT_EQ(sizeof(m110a::IQRHeader), 64u);
}

TEST(open_nonexistent) {
    m110a::IQFileSource source("nonexistent_file_12345.iqr");
    ASSERT(!source.is_open());
    ASSERT(!source.error().empty());
}

TEST(open_valid_file) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    ASSERT(source.error().empty());
    
    cleanup_test_file(filename);
}

TEST(read_header_metadata) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    // Check metadata from header
    ASSERT(std::abs(source.input_rate() - 2000000.0) < 1.0);
    ASSERT(std::abs(source.center_frequency() - 7074000.0) < 1.0);
    ASSERT(std::abs(source.bandwidth() - 200000.0) < 1.0);  // Stored in kHz, returned in Hz
    ASSERT_EQ(source.total_samples(), 20000u);
    
    // Check header struct
    const m110a::IQRHeader& hdr = source.header();
    ASSERT(memcmp(hdr.magic, "IQR1", 4) == 0);
    ASSERT_EQ(hdr.version, 1u);
    
    cleanup_test_file(filename);
}

TEST(load_chunk) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    // Load a chunk
    size_t loaded = source.load_chunk(4096);
    ASSERT(loaded > 0);
    ASSERT(source.samples_loaded() > 0);
    
    // Should have decimated output available
    // 2 MSPS -> 48 kHz is ~41.67x decimation
    // 4096 input samples -> ~98 output samples
    ASSERT(source.has_data());
    
    cleanup_test_file(filename);
}

TEST(load_all) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    source.load_all();
    
    ASSERT(source.eof());
    ASSERT_EQ(source.samples_loaded(), 20000u);
    ASSERT(source.has_data());
    
    cleanup_test_file(filename);
}

TEST(read_decimated_output) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    source.load_all();
    
    // Read all decimated output
    std::vector<std::complex<float>> output;
    std::complex<float> buffer[256];
    
    while (source.has_data()) {
        size_t n = source.read(buffer, 256);
        for (size_t i = 0; i < n; i++) {
            output.push_back(buffer[i]);
        }
    }
    
    // 20000 samples at 2 MSPS -> ~480 samples at 48 kHz
    // (20000 / 2000000 * 48000 = 480)
    printf("[got %zu samples] ", output.size());
    ASSERT(output.size() > 400);  // Allow some margin for decimation filter transients
    ASSERT(output.size() < 600);
    
    cleanup_test_file(filename);
}

TEST(reset) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    // Load and read some data
    source.load_all();
    std::complex<float> buffer[256];
    while (source.has_data()) {
        source.read(buffer, 256);
    }
    
    ASSERT(!source.has_data());
    ASSERT(source.eof());
    
    // Reset and verify we can load again
    source.reset();
    ASSERT(!source.eof());
    ASSERT_EQ(source.samples_loaded(), 0u);
    
    // Load again
    source.load_all();
    ASSERT(source.eof());
    ASSERT_EQ(source.samples_loaded(), 20000u);
    
    cleanup_test_file(filename);
}

TEST(progress_and_duration) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    // Duration should be 10ms (20000 samples at 2 MSPS)
    double duration = source.duration_seconds();
    ASSERT(std::abs(duration - 0.01) < 0.001);
    
    // Progress starts at 0
    ASSERT(std::abs(source.progress_percent()) < 0.1);
    
    // After loading all, progress should be 100%
    source.load_all();
    ASSERT(std::abs(source.progress_percent() - 100.0) < 0.1);
    
    cleanup_test_file(filename);
}

TEST(source_type) {
    std::string filename = create_test_iqr_file();
    
    m110a::IQFileSource source(filename);
    ASSERT(source.is_open());
    
    ASSERT(strcmp(source.source_type(), "iq_file") == 0);
    ASSERT(std::abs(source.sample_rate() - 48000.0) < 1.0);
    
    cleanup_test_file(filename);
}

TEST(invalid_magic) {
    const char* filename = "test_bad_magic.iqr";
    FILE* f = fopen(filename, "wb");
    if (!f) throw std::runtime_error("Failed to create test file");
    
    // Write header with wrong magic
    m110a::IQRHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "BAD!", 4);  // Wrong magic
    header.version = 1;
    header.sample_rate = 2000000.0;
    fwrite(&header, sizeof(header), 1, f);
    fclose(f);
    
    m110a::IQFileSource source(filename);
    ASSERT(!source.is_open());
    ASSERT(source.error().find("magic") != std::string::npos);
    
    remove(filename);
}

//=============================================================================
// Test with real .iqr file (optional)
//=============================================================================

static void test_real_file(const char* filename) {
    printf("\n--- Testing real file: %s ---\n\n", filename);
    
    m110a::IQFileSource source(filename);
    
    if (!source.is_open()) {
        printf("  Failed to open: %s\n", source.error().c_str());
        return;
    }
    
    printf("  File: %s\n", source.filename().c_str());
    printf("  Input rate: %.0f Hz\n", source.input_rate());
    printf("  Center freq: %.0f Hz\n", source.center_frequency());
    printf("  Bandwidth: %.0f Hz\n", source.bandwidth());
    printf("  Total samples: %llu\n", (unsigned long long)source.total_samples());
    printf("  Duration: %.3f seconds\n", source.duration_seconds());
    printf("  Output rate: %.0f Hz\n", source.sample_rate());
    printf("\n");
    
    // Load all and count output samples
    source.load_all();
    
    size_t total_output = 0;
    std::complex<float> buffer[1024];
    
    // Track signal statistics
    float max_amp = 0.0f;
    double power_sum = 0.0;
    
    while (source.has_data()) {
        size_t n = source.read(buffer, 1024);
        total_output += n;
        
        for (size_t i = 0; i < n; i++) {
            float amp = std::abs(buffer[i]);
            max_amp = std::max(max_amp, amp);
            power_sum += amp * amp;
        }
    }
    
    double rms = sqrt(power_sum / total_output);
    double rms_db = 20.0 * log10(rms + 1e-10);
    double peak_db = 20.0 * log10(max_amp + 1e-10);
    
    printf("  Output samples: %zu\n", total_output);
    printf("  Decimation ratio: %.2f:1\n", 
           static_cast<double>(source.total_samples()) / total_output);
    printf("  Peak level: %.1f dB\n", peak_db);
    printf("  RMS level: %.1f dB\n", rms_db);
    printf("\n");
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char* argv[]) {
    printf("\n=== IQFileSource Tests ===\n\n");
    
    // Run unit tests
    RUN_TEST(header_size);
    RUN_TEST(open_nonexistent);
    RUN_TEST(open_valid_file);
    RUN_TEST(read_header_metadata);
    RUN_TEST(load_chunk);
    RUN_TEST(load_all);
    RUN_TEST(read_decimated_output);
    RUN_TEST(reset);
    RUN_TEST(progress_and_duration);
    RUN_TEST(source_type);
    RUN_TEST(invalid_magic);
    
    printf("\n--- Results: %d passed, %d failed ---\n", tests_passed, tests_failed);
    
    // Test with real file if provided
    if (argc > 1) {
        test_real_file(argv[1]);
    } else {
        printf("\nTip: Run with path to .iqr file to test real captures:\n");
        printf("  %s path/to/capture.iqr\n", argv[0]);
    }
    
    return tests_failed > 0 ? 1 : 0;
}
