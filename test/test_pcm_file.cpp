// test/test_pcm_file.cpp
#include "../src/io/pcm_file.h"
#include "../src/common/types.h"
#include <iostream>
#include <cmath>
#include <cassert>

using namespace m110a;

// Test tolerance for float comparison
constexpr float EPSILON = 1.0f / 32768.0f * 2.0f;  // ~2 LSB tolerance

bool float_equal(float a, float b, float eps = EPSILON) {
    return std::abs(a - b) < eps;
}

void test_pcm_conversion() {
    std::cout << "  Testing PCM conversion... ";
    
    // Test zero
    assert(float_equal(pcm_to_float(0), 0.0f));
    
    // Test max positive
    assert(float_equal(pcm_to_float(32767), 32767.0f / 32768.0f));
    
    // Test max negative
    assert(float_equal(pcm_to_float(-32768), -1.0f));
    
    // Test round-trip
    for (int i = -32768; i <= 32767; i += 1000) {
        pcm_sample_t orig = static_cast<pcm_sample_t>(i);
        sample_t f = pcm_to_float(orig);
        pcm_sample_t back = float_to_pcm(f);
        assert(std::abs(orig - back) <= 1);  // Allow 1 LSB error
    }
    
    // Test clipping
    assert(float_to_pcm(1.5f) == 32767);
    assert(float_to_pcm(-1.5f) == -32767);
    
    std::cout << "PASS" << std::endl;
}

void test_file_round_trip() {
    std::cout << "  Testing file round-trip... ";
    
    const std::string test_file = "test_pcm_roundtrip.pcm";
    
    // Generate test signal (sine wave)
    const size_t num_samples = 8000;  // 1 second at 8kHz
    std::vector<sample_t> original(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        // 1000 Hz sine wave
        original[i] = 0.9f * std::sin(2.0f * 3.14159f * 1000.0f * i / 8000.0f);
    }
    
    // Write to file
    {
        PcmFileWriter writer(test_file);
        writer.write(original);
        assert(writer.samples_written() == num_samples);
    }
    
    // Read back
    std::vector<sample_t> loaded;
    {
        PcmFileReader reader(test_file);
        assert(reader.total_samples() == num_samples);
        loaded = reader.read_all();
        assert(loaded.size() == num_samples);
        assert(reader.eof());
    }
    
    // Compare
    for (size_t i = 0; i < num_samples; i++) {
        if (!float_equal(original[i], loaded[i])) {
            std::cerr << "Mismatch at " << i << ": " << original[i] << " vs " << loaded[i] << std::endl;
            assert(false);
        }
    }
    
    // Cleanup
    std::remove(test_file.c_str());
    
    std::cout << "PASS" << std::endl;
}

void test_incremental_read() {
    std::cout << "  Testing incremental read... ";
    
    const std::string test_file = "test_pcm_incr.pcm";
    const size_t num_samples = 1000;
    
    // Write test data
    std::vector<sample_t> original(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        original[i] = static_cast<float>(i) / num_samples;
    }
    
    {
        PcmFileWriter writer(test_file);
        writer.write(original);
    }
    
    // Read in chunks
    {
        PcmFileReader reader(test_file);
        std::vector<sample_t> buffer(100);
        size_t total_read = 0;
        
        while (!reader.eof()) {
            size_t n = reader.read(buffer.data(), buffer.size());
            for (size_t i = 0; i < n; i++) {
                assert(float_equal(buffer[i], original[total_read + i]));
            }
            total_read += n;
        }
        
        assert(total_read == num_samples);
    }
    
    // Cleanup
    std::remove(test_file.c_str());
    
    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "[PCM File I/O Tests]" << std::endl;
    
    try {
        test_pcm_conversion();
        test_file_round_trip();
        test_incremental_read();
        
        std::cout << "\nAll PCM file tests PASSED!" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
