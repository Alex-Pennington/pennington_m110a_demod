/**
 * @file test_reference_decode.cpp
 * @brief Test API decoder against reference PCM files
 */

#include "api/modem.h"
#include <iostream>
#include <iomanip>

using namespace m110a::api;

const std::string EXPECTED_MESSAGE = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

struct TestResult {
    std::string filename;
    Mode expected_mode;
    bool load_ok;
    bool decode_ok;
    bool data_match;
    size_t bytes_decoded;
    std::string decoded_preview;
    std::string error;
};

TestResult test_file(const std::string& filename, Mode expected_mode) {
    TestResult result;
    result.filename = filename;
    result.expected_mode = expected_mode;
    result.load_ok = false;
    result.decode_ok = false;
    result.data_match = false;
    result.bytes_decoded = 0;
    
    // Load PCM
    auto load_result = load_pcm(filename);
    if (!load_result.ok()) {
        result.error = "Load failed: " + load_result.error().message;
        return result;
    }
    result.load_ok = true;
    
    std::cout << "  Loaded " << load_result.value().size() << " samples\n";
    
    // Decode
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
    
    std::string decoded = decode_result.as_string();
    result.decoded_preview = decoded.substr(0, std::min(size_t(40), decoded.size()));
    
    // Check match
    if (decoded.find(EXPECTED_MESSAGE) == 0 || 
        decoded.substr(0, EXPECTED_MESSAGE.size()) == EXPECTED_MESSAGE) {
        result.data_match = true;
    } else {
        result.error = "Data mismatch";
    }
    
    return result;
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "M110A API Reference PCM Decode Test\n";
    std::cout << "==============================================\n";
    std::cout << "Expected: \"" << EXPECTED_MESSAGE << "\"\n\n";
    
    std::string base = "refrence_pcm/";
    
    struct TestCase {
        std::string file;
        Mode mode;
    };
    
    std::vector<TestCase> tests = {
        {"tx_150S_20251206_202440_580.pcm", Mode::M150_SHORT},
        {"tx_150L_20251206_202446_986.pcm", Mode::M150_LONG},
        {"tx_300S_20251206_202501_840.pcm", Mode::M300_SHORT},
        {"tx_300L_20251206_202506_058.pcm", Mode::M300_LONG},
        {"tx_600S_20251206_202518_709.pcm", Mode::M600_SHORT},
        {"tx_600L_20251206_202521_953.pcm", Mode::M600_LONG},
        {"tx_1200S_20251206_202533_636.pcm", Mode::M1200_SHORT},
        {"tx_1200L_20251206_202536_295.pcm", Mode::M1200_LONG},
        {"tx_2400S_20251206_202547_345.pcm", Mode::M2400_SHORT},
        {"tx_2400L_20251206_202549_783.pcm", Mode::M2400_LONG},
    };
    
    int passed = 0, failed = 0;
    
    for (const auto& tc : tests) {
        std::cout << "Testing " << mode_name(tc.mode) << "...\n";
        auto result = test_file(base + tc.file, tc.mode);
        
        std::cout << "  Load: " << (result.load_ok ? "OK" : "FAIL") << "\n";
        std::cout << "  Decode: " << (result.decode_ok ? "OK" : "FAIL") << "\n";
        
        if (result.decode_ok) {
            std::cout << "  Bytes: " << result.bytes_decoded << "\n";
            std::cout << "  Preview: \"" << result.decoded_preview << "\"\n";
            std::cout << "  Match: " << (result.data_match ? "YES" : "NO") << "\n";
        }
        
        if (!result.error.empty()) {
            std::cout << "  Error: " << result.error << "\n";
        }
        
        if (result.load_ok && result.decode_ok && result.data_match) {
            std::cout << "  => PASS\n";
            passed++;
        } else {
            std::cout << "  => FAIL\n";
            failed++;
        }
        std::cout << "\n";
    }
    
    std::cout << "==============================================\n";
    std::cout << "Results: " << passed << "/" << (passed + failed) << " passed\n";
    
    return (failed == 0) ? 0 : 1;
}
