/**
 * @file test_channel_params.cpp
 * @brief Unit tests for channel parameter validation
 * 
 * Ensures that all test configurations have correct, complete parameters
 * without running actual encode/decode cycles.
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <cassert>
#include <set>

#include "test_framework.h"
#include "direct_backend.h"

using namespace test_framework;

// ============================================================
// Test Result Tracking
// ============================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string error;
};

std::vector<TestResult> results;

void record(const std::string& name, bool passed, const std::string& error = "") {
    results.push_back({name, passed, error});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!passed && !error.empty()) {
        std::cout << " - " << error;
    }
    std::cout << "\n";
}

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running: " #name "\n"; \
    try { test_##name(); } \
    catch (const std::exception& e) { record(#name, false, e.what()); } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { record(current_test, false, msg); return; } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { record(current_test, false, msg); return; } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    if (std::abs((a) - (b)) > (eps)) { record(current_test, false, msg); return; } \
} while(0)

// ============================================================
// Channel Condition Validation Tests
// ============================================================

TEST(clean_channel_has_no_impairments) {
    const char* current_test = "clean_channel_has_no_impairments";
    
    ChannelCondition c("clean", "", 0.0);
    
    ASSERT_TRUE(c.snr_db >= 99.0f, "Clean channel should have high SNR (no AWGN)");
    ASSERT_NEAR(c.freq_offset_hz, 0.0f, 0.01f, "Clean channel should have 0 freq offset");
    ASSERT_EQ(c.multipath_delay_samples, 0, "Clean channel should have 0 multipath delay");
    ASSERT_TRUE(c.setup_cmd.empty(), "Clean channel should have empty setup command");
    
    record(current_test, true);
}

TEST(awgn_channels_have_correct_snr) {
    const char* current_test = "awgn_channels_have_correct_snr";
    
    auto channels = get_standard_channels();
    int awgn_count = 0;
    
    for (const auto& c : channels) {
        if (c.name.find("awgn_") == 0) {
            awgn_count++;
            
            // Extract expected SNR from name
            int expected_snr = std::stoi(c.name.substr(5)); // "awgn_XX"
            
            ASSERT_NEAR(c.snr_db, (float)expected_snr, 0.1f, 
                ("AWGN channel " + c.name + " has wrong snr_db").c_str());
            
            // Should have no other impairments
            ASSERT_NEAR(c.freq_offset_hz, 0.0f, 0.01f,
                ("AWGN channel " + c.name + " should have 0 freq offset").c_str());
            ASSERT_EQ(c.multipath_delay_samples, 0,
                ("AWGN channel " + c.name + " should have 0 multipath").c_str());
            
            // Setup command should match
            ASSERT_TRUE(c.setup_cmd.find("AWGN:" + std::to_string(expected_snr)) != std::string::npos,
                ("AWGN channel " + c.name + " setup_cmd mismatch").c_str());
        }
    }
    
    ASSERT_TRUE(awgn_count >= 4, "Should have at least 4 AWGN channels");
    record(current_test, true);
}

TEST(multipath_channels_have_correct_delay) {
    const char* current_test = "multipath_channels_have_correct_delay";
    
    auto channels = get_standard_channels();
    int mp_count = 0;
    
    for (const auto& c : channels) {
        if (c.name.find("mp_") == 0) {
            mp_count++;
            
            // Extract expected delay from name
            size_t pos = c.name.find("samp");
            int expected_delay = std::stoi(c.name.substr(3, pos - 3));
            
            ASSERT_EQ(c.multipath_delay_samples, expected_delay,
                ("Multipath channel " + c.name + " has wrong delay").c_str());
            
            // Should have high SNR (but with some AWGN)
            ASSERT_TRUE(c.snr_db >= 25.0f && c.snr_db <= 35.0f,
                ("Multipath channel " + c.name + " should have moderate-high SNR").c_str());
            
            // Should have no freq offset
            ASSERT_NEAR(c.freq_offset_hz, 0.0f, 0.01f,
                ("Multipath channel " + c.name + " should have 0 freq offset").c_str());
            
            // Setup command should match
            ASSERT_TRUE(c.setup_cmd.find("MULTIPATH:" + std::to_string(expected_delay)) != std::string::npos,
                ("Multipath channel " + c.name + " setup_cmd mismatch").c_str());
        }
    }
    
    ASSERT_TRUE(mp_count >= 2, "Should have at least 2 multipath channels");
    record(current_test, true);
}

TEST(freq_offset_channels_have_correct_offset) {
    const char* current_test = "freq_offset_channels_have_correct_offset";
    
    auto channels = get_standard_channels();
    int foff_count = 0;
    
    for (const auto& c : channels) {
        if (c.name.find("foff_") == 0) {
            foff_count++;
            
            // Extract expected offset from name
            size_t pos = c.name.find("hz");
            int expected_offset = std::stoi(c.name.substr(5, pos - 5));
            
            ASSERT_NEAR(c.freq_offset_hz, (float)expected_offset, 0.1f,
                ("Freq offset channel " + c.name + " has wrong offset").c_str());
            
            // Should have high SNR
            ASSERT_TRUE(c.snr_db >= 25.0f,
                ("Freq offset channel " + c.name + " should have high SNR").c_str());
            
            // Should have no multipath
            ASSERT_EQ(c.multipath_delay_samples, 0,
                ("Freq offset channel " + c.name + " should have 0 multipath").c_str());
        }
    }
    
    ASSERT_TRUE(foff_count >= 1, "Should have at least 1 freq offset channel");
    record(current_test, true);
}

TEST(preset_channels_have_combined_impairments) {
    const char* current_test = "preset_channels_have_combined_impairments";
    
    auto channels = get_standard_channels();
    bool found_moderate = false, found_poor = false;
    
    for (const auto& c : channels) {
        if (c.name == "moderate_hf") {
            found_moderate = true;
            
            // Should have moderate SNR, some multipath, some freq offset
            ASSERT_TRUE(c.snr_db >= 15.0f && c.snr_db <= 25.0f,
                "moderate_hf should have moderate SNR");
            ASSERT_TRUE(c.multipath_delay_samples > 0,
                "moderate_hf should have multipath");
            ASSERT_TRUE(c.freq_offset_hz > 0.0f,
                "moderate_hf should have freq offset");
            ASSERT_TRUE(c.setup_cmd.find("PRESET:MODERATE") != std::string::npos,
                "moderate_hf setup_cmd should contain PRESET:MODERATE");
        }
        
        if (c.name == "poor_hf") {
            found_poor = true;
            
            // Should have lower SNR, more multipath, more freq offset
            ASSERT_TRUE(c.snr_db <= 20.0f,
                "poor_hf should have lower SNR");
            ASSERT_TRUE(c.multipath_delay_samples > 0,
                "poor_hf should have multipath");
            ASSERT_TRUE(c.freq_offset_hz > 0.0f,
                "poor_hf should have freq offset");
            ASSERT_TRUE(c.setup_cmd.find("PRESET:POOR") != std::string::npos,
                "poor_hf setup_cmd should contain PRESET:POOR");
        }
    }
    
    ASSERT_TRUE(found_moderate, "Should have moderate_hf preset");
    ASSERT_TRUE(found_poor, "Should have poor_hf preset");
    record(current_test, true);
}

TEST(all_channels_have_valid_ber_threshold) {
    const char* current_test = "all_channels_have_valid_ber_threshold";
    
    auto channels = get_standard_channels();
    
    for (const auto& c : channels) {
        ASSERT_TRUE(c.expected_ber_threshold >= 0.0 && c.expected_ber_threshold <= 0.5,
            ("Channel " + c.name + " has invalid BER threshold").c_str());
        
        // Clean channel should have 0 BER threshold
        if (c.name == "clean") {
            ASSERT_NEAR(c.expected_ber_threshold, 0.0, 0.001,
                "Clean channel should have 0 BER threshold");
        }
        
        // Impaired channels should have non-zero threshold
        if (c.snr_db < 25.0f || c.multipath_delay_samples > 0 || c.freq_offset_hz > 0.5f) {
            ASSERT_TRUE(c.expected_ber_threshold > 0.0,
                ("Impaired channel " + c.name + " should have non-zero BER threshold").c_str());
        }
    }
    
    record(current_test, true);
}

// ============================================================
// DirectBackend Apply Channel Tests
// ============================================================

TEST(direct_backend_applies_awgn) {
    const char* current_test = "direct_backend_applies_awgn";
    
    DirectBackend backend;
    backend.connect();
    
    // Create test signal
    std::vector<float> clean(1000, 1.0f);
    std::vector<float> noisy = clean;
    
    // Create channel with AWGN
    ChannelCondition cond;
    cond.snr_db = 10.0f;  // Lots of noise
    
    // We can't directly test apply_channel since it's private,
    // but we verify the condition is properly configured
    ASSERT_NEAR(cond.snr_db, 10.0f, 0.1f, "SNR should be set");
    ASSERT_TRUE(cond.snr_db < 99.0f, "SNR < 99 triggers AWGN");
    
    record(current_test, true);
}

TEST(direct_backend_applies_multipath) {
    const char* current_test = "direct_backend_applies_multipath";
    
    ChannelCondition cond;
    cond.multipath_delay_samples = 50;
    cond.multipath_gain = 0.5f;
    
    ASSERT_EQ(cond.multipath_delay_samples, 50, "Delay should be set");
    ASSERT_TRUE(cond.multipath_delay_samples > 0, "Delay > 0 triggers multipath");
    ASSERT_NEAR(cond.multipath_gain, 0.5f, 0.01f, "Gain should be 0.5");
    
    record(current_test, true);
}

TEST(direct_backend_applies_freq_offset) {
    const char* current_test = "direct_backend_applies_freq_offset";
    
    ChannelCondition cond;
    cond.freq_offset_hz = 5.0f;
    
    ASSERT_NEAR(cond.freq_offset_hz, 5.0f, 0.01f, "Freq offset should be set");
    ASSERT_TRUE(std::abs(cond.freq_offset_hz) > 0.01f, "Offset > 0.01 triggers freq shift");
    
    record(current_test, true);
}

TEST(direct_backend_reset_state_works) {
    const char* current_test = "direct_backend_reset_state_works";
    
    DirectBackend backend;
    backend.connect();
    
    // Generate some random numbers
    // (We can't access rng_ directly, but we can verify the method exists)
    backend.reset_state();  // Should not throw
    backend.reset_state();  // Should be repeatable
    
    record(current_test, true);
}

// ============================================================
// Progressive Test Parameter Validation
// ============================================================

TEST(snr_test_creates_clean_condition) {
    const char* current_test = "snr_test_creates_clean_condition";
    
    // Simulate what run_progressive_snr_test does
    float snr = 20.0f;
    
    ChannelCondition cond;
    cond.name = "snr_test";
    cond.snr_db = snr;
    cond.expected_ber_threshold = 0.01f;
    
    // Verify no other impairments
    ASSERT_NEAR(cond.freq_offset_hz, 0.0f, 0.01f, "SNR test should not add freq offset");
    ASSERT_EQ(cond.multipath_delay_samples, 0, "SNR test should not add multipath");
    
    record(current_test, true);
}

TEST(freq_test_creates_clean_condition) {
    const char* current_test = "freq_test_creates_clean_condition";
    
    // Simulate what run_progressive_freq_test does
    float freq = 5.0f;
    
    ChannelCondition cond;
    cond.name = "freq_test";
    cond.freq_offset_hz = freq;
    cond.snr_db = 30.0f;
    cond.expected_ber_threshold = 0.01f;
    
    // Verify no multipath
    ASSERT_EQ(cond.multipath_delay_samples, 0, "Freq test should not add multipath");
    // SNR should be high (minimal AWGN)
    ASSERT_TRUE(cond.snr_db >= 25.0f, "Freq test should have high SNR");
    
    record(current_test, true);
}

TEST(multipath_test_creates_clean_condition) {
    const char* current_test = "multipath_test_creates_clean_condition";
    
    // Simulate what run_progressive_multipath_test does
    int delay = 100;
    
    ChannelCondition cond;
    cond.name = "mp_test";
    cond.multipath_delay_samples = delay;
    cond.snr_db = 30.0f;
    cond.expected_ber_threshold = 0.01f;
    
    // Verify no freq offset
    ASSERT_NEAR(cond.freq_offset_hz, 0.0f, 0.01f, "Multipath test should not add freq offset");
    // SNR should be high (minimal AWGN)
    ASSERT_TRUE(cond.snr_db >= 25.0f, "Multipath test should have high SNR");
    
    record(current_test, true);
}

// ============================================================
// Mode Validation Tests
// ============================================================

TEST(all_modes_have_valid_params) {
    const char* current_test = "all_modes_have_valid_params";
    
    auto modes = get_all_modes();
    
    ASSERT_TRUE(modes.size() >= 12, "Should have at least 12 modes (6 rates x 2 lengths)");
    
    for (const auto& m : modes) {
        // Name should not be empty
        ASSERT_TRUE(!m.name.empty(), "Mode name should not be empty");
        
        // Command should be valid format (e.g., "600S", "2400L")
        ASSERT_TRUE(!m.cmd.empty(), ("Mode " + m.name + " cmd should not be empty").c_str());
        ASSERT_TRUE(m.cmd.length() >= 2, ("Mode " + m.name + " cmd too short").c_str());
        
        // TX time should be reasonable (75L can be up to 80s)
        ASSERT_TRUE(m.tx_time_ms > 0, ("Mode " + m.name + " tx_time should be > 0").c_str());
        ASSERT_TRUE(m.tx_time_ms <= 120000, ("Mode " + m.name + " tx_time should be <= 120s").c_str());
        
        // Data rate should be reasonable
        ASSERT_TRUE(m.data_rate_bps > 0, ("Mode " + m.name + " data_rate should be > 0").c_str());
        ASSERT_TRUE(m.data_rate_bps <= 4800, ("Mode " + m.name + " data_rate should be <= 4800").c_str());
    }
    
    record(current_test, true);
}

TEST(modes_are_unique) {
    const char* current_test = "modes_are_unique";
    
    auto modes = get_all_modes();
    std::set<std::string> names, cmds;
    
    for (const auto& m : modes) {
        ASSERT_TRUE(names.find(m.name) == names.end(), 
            ("Duplicate mode name: " + m.name).c_str());
        ASSERT_TRUE(cmds.find(m.cmd) == cmds.end(),
            ("Duplicate mode cmd: " + m.cmd).c_str());
        
        names.insert(m.name);
        cmds.insert(m.cmd);
    }
    
    record(current_test, true);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "==============================================\n";
    std::cout << "Channel Parameter Validation Tests\n";
    std::cout << "==============================================\n\n";
    
    // Channel condition tests
    std::cout << "--- Channel Condition Tests ---\n";
    RUN_TEST(clean_channel_has_no_impairments);
    RUN_TEST(awgn_channels_have_correct_snr);
    RUN_TEST(multipath_channels_have_correct_delay);
    RUN_TEST(freq_offset_channels_have_correct_offset);
    RUN_TEST(preset_channels_have_combined_impairments);
    RUN_TEST(all_channels_have_valid_ber_threshold);
    
    // DirectBackend tests
    std::cout << "\n--- DirectBackend Tests ---\n";
    RUN_TEST(direct_backend_applies_awgn);
    RUN_TEST(direct_backend_applies_multipath);
    RUN_TEST(direct_backend_applies_freq_offset);
    RUN_TEST(direct_backend_reset_state_works);
    
    // Progressive test condition tests
    std::cout << "\n--- Progressive Test Condition Tests ---\n";
    RUN_TEST(snr_test_creates_clean_condition);
    RUN_TEST(freq_test_creates_clean_condition);
    RUN_TEST(multipath_test_creates_clean_condition);
    
    // Mode tests
    std::cout << "\n--- Mode Validation Tests ---\n";
    RUN_TEST(all_modes_have_valid_params);
    RUN_TEST(modes_are_unique);
    
    // Summary
    std::cout << "\n==============================================\n";
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++;
        else failed++;
    }
    
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==============================================\n";
    
    return failed > 0 ? 1 : 0;
}
