/**
 * AFC (Automatic Frequency Control) Tolerance Tests
 * 
 * Tests receiver ability to acquire and decode signals with
 * carrier frequency offsets typical of HF operation.
 * 
 * NOTE: Frequency offset is simulated by adjusting the RX carrier frequency,
 * which is equivalent to the TX being off-frequency. This avoids issues with
 * trying to frequency-shift a real passband signal.
 * 
 * AFC RANGE: The probe-based AFC can track ±25 Hz (limited by phase aliasing
 * between probe patterns spaced 48 symbols apart).
 */

#include "m110a/mode_config.h"
#include "m110a/multimode_tx.h"
#include "m110a/multimode_rx.h"
#include "channel/awgn.h"
#include "dsp/nco.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>
#include <vector>

using namespace m110a;

std::mt19937 rng(42);

// ============================================================================
// Utilities
// ============================================================================

std::vector<uint8_t> generate_test_data(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++) {
        data[i] = rng() & 0xFF;
    }
    return data;
}

int count_bit_errors(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    int errors = 0;
    size_t len = std::min(tx.size(), rx.size());
    for (size_t i = 0; i < len; i++) {
        uint8_t diff = tx[i] ^ rx[i];
        while (diff) {
            errors += diff & 1;
            diff >>= 1;
        }
    }
    if (rx.size() < tx.size()) {
        errors += (tx.size() - rx.size()) * 8;
    }
    return errors;
}

float calculate_ber(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    int total_bits = tx.size() * 8;
    if (total_bits == 0) return 1.0f;
    return static_cast<float>(count_bit_errors(tx, rx)) / total_bits;
}

// ============================================================================
// AFC Result Structure
// ============================================================================

struct AfcResult {
    float freq_offset_hz;
    float detected_offset_hz;
    float ber;
    bool acquired;
    int bit_errors;
    int total_bits;
};

/**
 * Test decoding at a specific frequency offset
 * 
 * Frequency offset is simulated by adjusting the RX carrier frequency.
 * If RX uses carrier_freq = 1800 - offset, it's equivalent to TX being at 1800 + offset.
 */
AfcResult test_freq_offset(ModeId mode, float offset_hz, float snr_db, 
                           size_t data_len = 100,
                           bool verbose = false) {
    AfcResult result;
    result.freq_offset_hz = offset_hz;
    result.acquired = false;
    result.detected_offset_hz = 0;
    
    // Generate test data
    auto tx_data = generate_test_data(data_len);
    
    // TX at nominal frequency
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = mode;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    auto tx_result = tx.transmit(tx_data);
    
    // Add AWGN
    std::vector<float> noisy_samples = tx_result.rf_samples;
    AWGNChannel awgn(rng());
    awgn.add_noise_snr(noisy_samples, snr_db);
    
    // RX with offset carrier (simulates TX being off-frequency)
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = mode;
    rx_cfg.sample_rate = 48000.0f;
    rx_cfg.carrier_freq = 1800.0f - offset_hz;  // Offset RX = TX at higher freq
    rx_cfg.verbose = verbose;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(noisy_samples);
    
    result.acquired = rx_result.success;
    result.detected_offset_hz = rx_result.freq_offset_hz;
    result.bit_errors = count_bit_errors(tx_data, rx_result.data);
    result.total_bits = tx_data.size() * 8;
    result.ber = static_cast<float>(result.bit_errors) / result.total_bits;
    
    return result;
}

// ============================================================================
// AFC Tolerance Tests (Probe-based AFC range: ±25 Hz)
// ============================================================================

bool test_afc_zero_offset() {
    std::cout << "test_afc_zero_offset: ";
    
    auto result = test_freq_offset(ModeId::M2400S, 0.0f, 20.0f, 100);
    
    bool pass = result.acquired && (result.ber < 0.01f);
    std::cout << (pass ? "PASS" : "FAIL")
              << " (BER=" << std::scientific << result.ber << ")\n";
    return pass;
}

bool test_afc_small_offset() {
    std::cout << "test_afc_small_offset:\n";
    std::cout << "  Testing ±10 Hz offset:\n";
    
    bool all_pass = true;
    float offsets[] = {-10.0f, 10.0f};
    
    for (float offset : offsets) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 20.0f, 100);
        
        std::cout << "  Offset " << std::showpos << std::fixed << std::setprecision(0) 
                  << offset << " Hz: ";
        
        if (result.acquired && result.ber < 0.01f) {
            std::cout << "PASS (AFC=" << result.detected_offset_hz << " Hz)\n";
        } else {
            std::cout << "FAIL (BER=" << std::scientific << result.ber << ")\n";
            all_pass = false;
        }
    }
    
    std::cout << std::noshowpos << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_afc_medium_offset() {
    std::cout << "test_afc_medium_offset:\n";
    std::cout << "  Testing ±20 Hz offset:\n";
    
    bool all_pass = true;
    float offsets[] = {-20.0f, 20.0f};
    
    for (float offset : offsets) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 20.0f, 100);
        
        std::cout << "  Offset " << std::showpos << std::fixed << std::setprecision(0) 
                  << offset << " Hz: ";
        
        if (result.acquired && result.ber < 0.01f) {
            std::cout << "PASS (AFC=" << result.detected_offset_hz << " Hz)\n";
        } else {
            std::cout << "FAIL (BER=" << std::scientific << result.ber << ")\n";
            all_pass = false;
        }
    }
    
    std::cout << std::noshowpos << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_afc_edge_offset() {
    std::cout << "test_afc_edge_offset:\n";
    std::cout << "  Testing ±22 Hz offset (near AFC limit):\n";
    
    bool all_pass = true;
    float offsets[] = {-22.0f, 22.0f};
    
    for (float offset : offsets) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 25.0f, 100);  // Higher SNR
        
        std::cout << "  Offset " << std::showpos << std::fixed << std::setprecision(0) 
                  << offset << " Hz: ";
        
        if (result.acquired && result.ber < 0.05f) {
            std::cout << "PASS (AFC=" << result.detected_offset_hz << " Hz)\n";
        } else {
            std::cout << "FAIL (BER=" << std::scientific << result.ber << ")\n";
            all_pass = false;
        }
    }
    
    std::cout << std::noshowpos << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_afc_beyond_range() {
    std::cout << "test_afc_beyond_range: ";
    
    // 40 Hz offset - beyond AFC range, should fail
    auto result = test_freq_offset(ModeId::M2400S, 40.0f, 25.0f, 100);
    
    // Expected: high BER due to residual offset
    bool pass = (result.ber > 0.1f);
    
    std::cout << (pass ? "PASS" : "FAIL")
              << " (BER=" << std::scientific << result.ber 
              << ", expected >10%)\n";
    return pass;
}

bool test_afc_sweep() {
    std::cout << "test_afc_sweep:\n";
    std::cout << "  BER vs Frequency Offset (AFC range ±25 Hz):\n";
    std::cout << "  Offset(Hz)  AFC Est   BER\n";
    std::cout << "  ----------  -------   --------\n";
    
    float offsets[] = {-30.0f, -25.0f, -20.0f, -10.0f, 0.0f, 
                       10.0f, 20.0f, 25.0f, 30.0f};
    
    for (float offset : offsets) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 20.0f, 50);
        
        std::cout << "  " << std::showpos << std::fixed << std::setw(8) << std::setprecision(0) << offset
                  << "  " << std::setw(7) << std::setprecision(1) << result.detected_offset_hz
                  << "   " << std::noshowpos << std::scientific << std::setprecision(2) << result.ber << "\n";
    }
    
    std::cout << "  Result: PASS (sweep complete)\n";
    return true;
}

bool test_afc_accuracy() {
    std::cout << "test_afc_accuracy:\n";
    std::cout << "  Testing frequency estimation accuracy:\n";
    std::cout << "  True Offset  Estimated   Error   BER\n";
    std::cout << "  -----------  ---------   ------  --------\n";
    
    float max_error = 0.0f;
    bool all_zero_ber = true;
    float offsets[] = {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f};
    
    for (float offset : offsets) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 25.0f, 100);
        
        float error = std::abs(result.detected_offset_hz - offset);
        max_error = std::max(max_error, error);
        
        std::cout << "  " << std::showpos << std::fixed << std::setw(9) << std::setprecision(0) << offset
                  << "  " << std::setw(9) << std::setprecision(1) << result.detected_offset_hz
                  << "   " << std::noshowpos << std::setw(6) << std::setprecision(1) << error << " Hz"
                  << "  " << std::scientific << std::setprecision(2) << result.ber << "\n";
        
        if (result.ber > 0.001f) all_zero_ber = false;
    }
    
    // Primary check: all BER should be ~0 (offset was corrected)
    // Secondary check: estimate should be reasonably close to true offset
    bool pass = all_zero_ber && (max_error < 5.0f);
    std::cout << "  Max error: " << max_error << " Hz\n";
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << " (0% BER and <5 Hz error)\n";
    return pass;
}

bool test_afc_with_noise() {
    std::cout << "test_afc_with_noise:\n";
    std::cout << "  Testing AFC at various SNR levels (offset=+15 Hz):\n";
    std::cout << "  SNR(dB)  Acquired  BER\n";
    std::cout << "  -------  --------  --------\n";
    
    float snr_points[] = {12.0f, 15.0f, 20.0f, 25.0f};
    bool all_pass = true;
    
    for (float snr : snr_points) {
        auto result = test_freq_offset(ModeId::M2400S, 15.0f, snr, 50);
        
        std::cout << "  " << std::fixed << std::setw(5) << std::setprecision(0) << snr
                  << "    " << std::setw(8) << (result.acquired ? "YES" : "NO")
                  << "  " << std::scientific << std::setprecision(2) << result.ber << "\n";
        
        // At 15+ dB SNR, should work
        if (snr >= 15.0f && (!result.acquired || result.ber > 0.05f)) {
            all_pass = false;
        }
    }
    
    std::cout << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_afc_different_modes() {
    std::cout << "test_afc_different_modes:\n";
    std::cout << "  Testing AFC across modes (+15 Hz offset):\n";
    std::cout << "  Mode      Acquired  AFC Est    BER\n";
    std::cout << "  --------  --------  ---------  --------\n";
    
    struct TestCase {
        ModeId mode;
        const char* name;
    };
    
    TestCase cases[] = {
        {ModeId::M600S, "M600S"},
        {ModeId::M1200S, "M1200S"},
        {ModeId::M2400S, "M2400S"},
    };
    
    bool all_pass = true;
    
    for (const auto& tc : cases) {
        auto result = test_freq_offset(tc.mode, 15.0f, 20.0f, 50);
        
        std::cout << "  " << std::setw(8) << tc.name
                  << "  " << std::setw(8) << (result.acquired ? "YES" : "NO")
                  << "  " << std::showpos << std::fixed << std::setw(9) << std::setprecision(1) 
                  << result.detected_offset_hz
                  << "  " << std::noshowpos << std::scientific << std::setprecision(2) 
                  << result.ber << "\n";
        
        if (!result.acquired || result.ber > 0.05f) {
            all_pass = false;
        }
    }
    
    std::cout << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_afc_pull_in_range() {
    std::cout << "test_afc_pull_in_range:\n";
    std::cout << "  Finding maximum pull-in range:\n";
    
    float max_acquired = 0.0f;
    
    // Test increasing offsets until acquisition fails
    for (float offset = 5.0f; offset <= 50.0f; offset += 5.0f) {
        auto result = test_freq_offset(ModeId::M2400S, offset, 25.0f, 50);
        
        if (result.acquired && result.ber < 0.05f) {
            max_acquired = offset;
        } else {
            break;
        }
    }
    
    std::cout << "  Maximum pull-in: ±" << max_acquired << " Hz\n";
    
    // Should be able to pull in at least ±20 Hz
    bool pass = (max_acquired >= 20.0f);
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << " (≥20 Hz required)\n";
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "AFC Tolerance Tests\n";
    std::cout << "===================\n";
    std::cout << "(Probe-based AFC range: ±25 Hz)\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Basic AFC tests
    std::cout << "--- Basic AFC Tests ---\n";
    total++; if (test_afc_zero_offset()) passed++;
    total++; if (test_afc_small_offset()) passed++;
    total++; if (test_afc_medium_offset()) passed++;
    total++; if (test_afc_edge_offset()) passed++;
    total++; if (test_afc_beyond_range()) passed++;
    
    // AFC performance tests
    std::cout << "\n--- AFC Performance ---\n";
    total++; if (test_afc_sweep()) passed++;
    total++; if (test_afc_accuracy()) passed++;
    total++; if (test_afc_with_noise()) passed++;
    
    // Mode coverage
    std::cout << "\n--- Mode Coverage ---\n";
    total++; if (test_afc_different_modes()) passed++;
    total++; if (test_afc_pull_in_range()) passed++;
    
    std::cout << "\n===================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
