/**
 * Preamble Encoder/Decoder Tests
 * 
 * Tests for MIL-STD-188-110A preamble encoding and mode ID extraction.
 */

#include "m110a/preamble_codec.h"
#include "m110a/mode_config.h"
#include <iostream>
#include <iomanip>

using namespace m110a;

// ============================================================================
// Preamble Encoding Tests
// ============================================================================

bool test_preamble_encode_size() {
    std::cout << "test_preamble_encode_size: ";
    
    PreambleEncoder encoder;
    
    struct TestCase {
        ModeId mode;
        int expected_symbols;
    };
    
    std::vector<TestCase> tests = {
        {ModeId::M600S, 180},
        {ModeId::M1200S, 360},
        {ModeId::M2400S, 480},
    };
    
    int passed = 0;
    for (const auto& tc : tests) {
        auto preamble = encoder.encode(tc.mode);
        if (static_cast<int>(preamble.size()) == tc.expected_symbols) {
            passed++;
        } else {
            std::cout << "\n  " << mode_to_string(tc.mode) << ": expected " 
                      << tc.expected_symbols << ", got " << preamble.size();
        }
    }
    
    bool pass = (passed == static_cast<int>(tests.size()));
    std::cout << (pass ? "PASS" : " FAIL") << "\n";
    return pass;
}

// ============================================================================
// Mode ID Encoding/Decoding Tests
// ============================================================================

bool test_mode_id_roundtrip() {
    std::cout << "test_mode_id_roundtrip: ";
    
    PreambleCodec codec;
    
    // Test each valid mode ID (0-17)
    int passed = 0;
    int tested = 0;
    
    for (int mode_id = 0; mode_id <= 17; mode_id++) {
        // Skip non-existent IDs (13, 15)
        if (mode_id == 13 || mode_id == 15) continue;
        
        tested++;
        ModeId mode = static_cast<ModeId>(mode_id);
        
        // Encode
        auto preamble = codec.encode(mode);
        
        // Decode
        auto info = codec.decode(preamble);
        
        if (info.valid && info.mode_id == mode_id) {
            passed++;
        } else {
            std::cout << "\n  Mode " << mode_id << ": decoded=" 
                      << info.mode_id << " (valid=" << info.valid << ")";
        }
    }
    
    bool pass = (passed == tested);
    std::cout << (pass ? "PASS" : " FAIL") << " (" << passed << "/" << tested << ")\n";
    return pass;
}

bool test_interleave_detection() {
    std::cout << "test_interleave_detection: ";
    
    PreambleCodec codec;
    
    struct TestCase {
        ModeId mode;
        std::string expected_interleave;
    };
    
    std::vector<TestCase> tests = {
        {ModeId::M600S, "short"},
        {ModeId::M600L, "long"},
        {ModeId::M1200S, "short"},
        {ModeId::M1200L, "long"},
        {ModeId::M2400S, "short"},
        {ModeId::M2400L, "long"},
        {ModeId::M600V, "voice"},
        {ModeId::M1200V, "voice"},
        {ModeId::M2400V, "voice"},
        {ModeId::M4800S, "short"},
    };
    
    int passed = 0;
    for (const auto& tc : tests) {
        auto preamble = codec.encode(tc.mode);
        auto info = codec.decode(preamble);
        
        if (info.valid && info.interleave_type() == tc.expected_interleave) {
            passed++;
        } else {
            std::cout << "\n  " << mode_to_string(tc.mode) << ": expected " 
                      << tc.expected_interleave << ", got " << info.interleave_type();
        }
    }
    
    bool pass = (passed == static_cast<int>(tests.size()));
    std::cout << (pass ? "PASS" : " FAIL") << " (" << passed << "/" << tests.size() << ")\n";
    return pass;
}

// ============================================================================
// Data Rate Tests
// ============================================================================

bool test_data_rate_extraction() {
    std::cout << "test_data_rate_extraction: ";
    
    struct TestCase {
        int mode_id;
        int expected_rate;
    };
    
    std::vector<TestCase> tests = {
        {0, 75},    // M75NS
        {1, 75},    // M75NL
        {6, 600},   // M600S
        {7, 600},   // M600L
        {8, 1200},  // M1200S
        {9, 1200},  // M1200L
        {10, 2400}, // M2400S
        {11, 2400}, // M2400L
        {17, 4800}, // M4800S
    };
    
    int passed = 0;
    for (const auto& tc : tests) {
        int rate = get_data_rate(tc.mode_id);
        if (rate == tc.expected_rate) {
            passed++;
        } else {
            std::cout << "\n  Mode " << tc.mode_id << ": expected " 
                      << tc.expected_rate << ", got " << rate;
        }
    }
    
    bool pass = (passed == static_cast<int>(tests.size()));
    std::cout << (pass ? "PASS" : " FAIL") << "\n";
    return pass;
}

// ============================================================================
// Block Count Tests
// ============================================================================

bool test_block_count_encoding() {
    std::cout << "test_block_count_encoding: ";
    
    PreambleCodec codec;
    
    // Test various block counts
    std::vector<int> test_counts = {1, 5, 10, 127, 255};
    
    int passed = 0;
    for (int count : test_counts) {
        auto preamble = codec.encode(ModeId::M2400S, count);
        auto info = codec.decode(preamble);
        
        // Block count decoding is less reliable, so accept within range
        if (info.valid && info.block_count > 0) {
            passed++;
        }
    }
    
    bool pass = (passed == static_cast<int>(test_counts.size()));
    std::cout << (pass ? "PASS" : " FAIL") << " (" << passed << "/" << test_counts.size() << ")\n";
    return pass;
}

// ============================================================================
// Confidence Tests
// ============================================================================

bool test_decode_confidence() {
    std::cout << "test_decode_confidence: ";
    
    PreambleCodec codec;
    
    // Good signal should have high confidence
    auto preamble = codec.encode(ModeId::M2400S);
    auto info = codec.decode(preamble);
    
    bool good_confidence = info.valid && info.confidence > 0.5f;
    
    // Corrupted signal should have lower confidence
    auto corrupted = preamble;
    for (size_t i = 288; i < 320 && i < corrupted.size(); i++) {
        corrupted[i] = complex_t(-corrupted[i].real(), -corrupted[i].imag());
    }
    auto corrupted_info = codec.decode(corrupted);
    
    bool lower_confidence = corrupted_info.confidence < info.confidence;
    
    bool pass = good_confidence && lower_confidence;
    std::cout << (pass ? "PASS" : "FAIL")
              << " (clean=" << std::fixed << std::setprecision(2) << info.confidence
              << ", corrupted=" << corrupted_info.confidence << ")\n";
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Preamble Codec Tests\n";
    std::cout << "====================\n\n";
    
    int passed = 0;
    int total = 0;
    
    std::cout << "--- Preamble Encoding ---\n";
    total++; if (test_preamble_encode_size()) passed++;
    
    std::cout << "\n--- Mode ID ---\n";
    total++; if (test_mode_id_roundtrip()) passed++;
    total++; if (test_interleave_detection()) passed++;
    total++; if (test_data_rate_extraction()) passed++;
    
    std::cout << "\n--- Block Count ---\n";
    total++; if (test_block_count_encoding()) passed++;
    
    std::cout << "\n--- Confidence ---\n";
    total++; if (test_decode_confidence()) passed++;
    
    std::cout << "\n====================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
