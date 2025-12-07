/**
 * @file test_pcm_loopback.cpp
 * @brief Full PCM loopback test via API
 * 
 * Tests encode -> save PCM -> load PCM -> decode for all modes.
 */

#include "api/modem.h"
#include <iostream>
#include <iomanip>
#include <cstdio>

using namespace m110a::api;

// Test message
const std::string TEST_MESSAGE = "MIL-STD-188-110A Modem API Test - Phoenix Nest LLC";

struct TestResult {
    Mode mode;
    bool encode_ok;
    bool save_ok;
    bool load_ok;
    bool decode_ok;
    bool data_match;
    size_t samples_generated;
    size_t bytes_decoded;
    float duration_sec;
    std::string error;
};

TestResult test_mode(Mode mode, const std::string& temp_dir) {
    TestResult result;
    result.mode = mode;
    result.encode_ok = false;
    result.save_ok = false;
    result.load_ok = false;
    result.decode_ok = false;
    result.data_match = false;
    result.samples_generated = 0;
    result.bytes_decoded = 0;
    result.duration_sec = 0.0f;
    
    std::string mode_str = mode_name(mode);
    std::string pcm_file = temp_dir + "/api_test_" + mode_str + ".pcm";
    
    // Step 1: Encode
    auto encode_result = encode(TEST_MESSAGE, mode);
    if (!encode_result.ok()) {
        result.error = "Encode failed: " + encode_result.error().message;
        return result;
    }
    result.encode_ok = true;
    result.samples_generated = encode_result.value().size();
    result.duration_sec = result.samples_generated / 48000.0f;
    
    // Step 2: Save to PCM
    auto save_result = save_pcm(pcm_file, encode_result.value());
    if (!save_result.ok()) {
        result.error = "Save PCM failed: " + save_result.error().message;
        return result;
    }
    result.save_ok = true;
    
    // Step 3: Load from PCM
    auto load_result = load_pcm(pcm_file);
    if (!load_result.ok()) {
        result.error = "Load PCM failed: " + load_result.error().message;
        return result;
    }
    result.load_ok = true;
    
    // Verify loaded size matches saved size
    if (load_result.value().size() != encode_result.value().size()) {
        result.error = "Size mismatch: saved " + 
                       std::to_string(encode_result.value().size()) +
                       " loaded " + std::to_string(load_result.value().size());
        return result;
    }
    
    // Step 4: Decode
    auto decode_result = decode(load_result.value());
    if (!decode_result.success) {
        result.error = "Decode failed";
        if (decode_result.error.has_value()) {
            result.error += ": " + decode_result.error->message;
        }
        return result;
    }
    result.decode_ok = true;
    result.bytes_decoded = decode_result.data.size();
    
    // Step 5: Verify data matches
    std::string decoded_str = decode_result.as_string();
    
    // Check if original message is contained in decoded data
    // (there may be padding at the end due to block alignment)
    if (decoded_str.find(TEST_MESSAGE) == 0 || 
        decoded_str.substr(0, TEST_MESSAGE.size()) == TEST_MESSAGE) {
        result.data_match = true;
    } else {
        result.error = "Data mismatch: expected '" + TEST_MESSAGE.substr(0, 20) + 
                       "...' got '" + decoded_str.substr(0, 20) + "...'";
    }
    
    // Clean up temp file
    std::remove(pcm_file.c_str());
    
    return result;
}

void print_result(const TestResult& r) {
    std::cout << std::setw(12) << mode_name(r.mode) << " | ";
    std::cout << (r.encode_ok ? "✓" : "✗") << " | ";
    std::cout << (r.save_ok ? "✓" : "✗") << " | ";
    std::cout << (r.load_ok ? "✓" : "✗") << " | ";
    std::cout << (r.decode_ok ? "✓" : "✗") << " | ";
    std::cout << (r.data_match ? "✓" : "✗") << " | ";
    std::cout << std::setw(8) << r.samples_generated << " | ";
    std::cout << std::fixed << std::setprecision(2) << std::setw(6) << r.duration_sec << "s";
    
    if (!r.error.empty()) {
        std::cout << " [" << r.error << "]";
    }
    std::cout << "\n";
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "M110A API Full PCM Loopback Test\n";
    std::cout << "==============================================\n";
    std::cout << "API Version: " << version() << "\n";
    std::cout << "Test Message: \"" << TEST_MESSAGE << "\"\n";
    std::cout << "Message Length: " << TEST_MESSAGE.size() << " bytes\n";
    std::cout << "\n";
    
    // Use temp directory
    std::string temp_dir = ".";  // Current directory
    
    // All modes to test
    std::vector<Mode> modes = {
        Mode::M150_SHORT,
        Mode::M150_LONG,
        Mode::M300_SHORT,
        Mode::M300_LONG,
        Mode::M600_SHORT,
        Mode::M600_LONG,
        Mode::M1200_SHORT,
        Mode::M1200_LONG,
        Mode::M2400_SHORT,
        Mode::M2400_LONG,
        Mode::M4800_SHORT
    };
    
    std::cout << std::setw(12) << "Mode" << " | E | S | L | D | M | "
              << std::setw(8) << "Samples" << " | Duration\n";
    std::cout << std::string(70, '-') << "\n";
    
    int passed = 0;
    int failed = 0;
    
    for (Mode mode : modes) {
        auto result = test_mode(mode, temp_dir);
        print_result(result);
        
        if (result.encode_ok && result.save_ok && result.load_ok && 
            result.decode_ok && result.data_match) {
            passed++;
        } else {
            failed++;
        }
    }
    
    std::cout << std::string(70, '-') << "\n";
    std::cout << "Legend: E=Encode, S=Save, L=Load, D=Decode, M=Match\n";
    std::cout << "\n";
    std::cout << "Results: " << passed << "/" << (passed + failed) << " passed\n";
    
    if (failed == 0) {
        std::cout << "\n✓ All PCM loopback tests PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ " << failed << " tests FAILED!\n";
        return 1;
    }
}
