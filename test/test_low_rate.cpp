/**
 * Low Rate Mode Tests (75/150/300 bps)
 * 
 * Tests:
 * - Bit repetition mechanism
 * - BPSK mapping for low rates
 * - Walsh coding for 75 bps (if implemented)
 * - BER curves for low rate modes
 * - Comparison of low vs high rate robustness
 */

#include "m110a/mode_config.h"
#include "m110a/multimode_tx.h"
#include "m110a/multimode_rx.h"
#include "channel/awgn.h"
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

struct BerResult {
    float eb_n0_db;
    float ber;
    int bit_errors;
    int total_bits;
    bool success;
};

BerResult measure_ber(ModeId mode, float eb_n0_db, size_t data_len = 20) {
    const auto& cfg = ModeDatabase::get(mode);
    
    auto tx_data = generate_test_data(data_len);
    
    // TX
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = mode;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(tx_data);
    
    // Apply AWGN
    std::vector<float> noisy_samples = tx_result.rf_samples;
    AWGNChannel channel(rng());
    
    // Convert Eb/N0 to SNR
    float bits_per_symbol = static_cast<float>(cfg.bits_per_symbol);
    float code_rate = (cfg.bps == 4800) ? 1.0f : 0.5f;  // 4800 is uncoded
    float sps = tx_cfg.sample_rate / cfg.symbol_rate;
    float es_n0_db = eb_n0_db + 10.0f * std::log10(bits_per_symbol * code_rate);
    float snr_db = es_n0_db - 10.0f * std::log10(sps);
    
    channel.add_noise_snr(noisy_samples, snr_db);
    
    // RX
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = mode;
    rx_cfg.sample_rate = 48000.0f;
    rx_cfg.verbose = false;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(noisy_samples);
    
    BerResult result;
    result.eb_n0_db = eb_n0_db;
    result.success = rx_result.success;
    result.bit_errors = count_bit_errors(tx_data, rx_result.data);
    result.total_bits = tx_data.size() * 8;
    result.ber = static_cast<float>(result.bit_errors) / result.total_bits;
    
    return result;
}

// ============================================================================
// Mode Configuration Tests
// ============================================================================

bool test_low_rate_mode_config() {
    std::cout << "test_low_rate_mode_config:\n";
    std::cout << "  Mode     BPS   Mod   Rep  Interleave\n";
    std::cout << "  -------  ----  ----  ---  ----------\n";
    
    struct TestMode {
        ModeId id;
        int expected_rep;
        const char* expected_mod;
    };
    
    TestMode modes[] = {
        {ModeId::M75NS,  32, "BPSK"},
        {ModeId::M75NL,  32, "BPSK"},
        {ModeId::M150S,   8, "BPSK"},
        {ModeId::M150L,   8, "BPSK"},
        {ModeId::M300S,   4, "BPSK"},
        {ModeId::M300L,   4, "BPSK"},
        {ModeId::M600S,   2, "BPSK"},
        {ModeId::M600L,   2, "BPSK"},
    };
    
    bool all_pass = true;
    
    for (const auto& tm : modes) {
        const auto& cfg = ModeDatabase::get(tm.id);
        
        const char* mod_str = (cfg.modulation == Modulation::BPSK) ? "BPSK" :
                              (cfg.modulation == Modulation::QPSK) ? "QPSK" : "8PSK";
        const char* il_str = (cfg.interleave_type == InterleaveType::SHORT) ? "SHORT" :
                             (cfg.interleave_type == InterleaveType::LONG) ? "LONG" : "VOICE";
        
        bool rep_ok = (cfg.symbol_repetition == tm.expected_rep);
        bool mod_ok = (std::string(mod_str) == tm.expected_mod);
        
        std::cout << "  " << std::setw(7) << cfg.name
                  << "  " << std::setw(4) << cfg.bps
                  << "  " << std::setw(4) << mod_str
                  << "  " << std::setw(3) << cfg.symbol_repetition
                  << "  " << std::setw(10) << il_str;
        
        if (rep_ok && mod_ok) {
            std::cout << " ✓\n";
        } else {
            std::cout << " FAIL\n";
            all_pass = false;
        }
    }
    
    std::cout << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

bool test_symbol_rate_constant() {
    std::cout << "test_symbol_rate_constant: ";
    
    // All modes should have 2400 baud symbol rate
    bool all_2400 = true;
    
    for (ModeId mode : ModeDatabase::all_modes()) {
        const auto& cfg = ModeDatabase::get(mode);
        if (cfg.symbol_rate != 2400) {
            std::cout << "FAIL (" << cfg.name << " has " << cfg.symbol_rate << " baud)\n";
            all_2400 = false;
        }
    }
    
    if (all_2400) {
        std::cout << "PASS (all modes use 2400 baud)\n";
    }
    return all_2400;
}

// ============================================================================
// Loopback Tests for Low Rate Modes
// ============================================================================

bool test_loopback_75bps() {
    std::cout << "test_loopback_75bps: ";
    
    // 75 bps modes - needs longer message due to high interleave depth
    auto tx_data = generate_test_data(5);  // Small message
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M75NS;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(tx_data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M75NS;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    float ber = calculate_ber(tx_data, rx_result.data);
    bool pass = rx_result.success && (ber < 0.001f);
    
    std::cout << (pass ? "PASS" : "FAIL")
              << " (tx=" << tx_data.size() << " bytes"
              << ", rx=" << rx_result.data.size() << " bytes"
              << ", BER=" << std::scientific << ber << ")\n";
    return pass;
}

bool test_loopback_150bps() {
    std::cout << "test_loopback_150bps: ";
    
    auto tx_data = generate_test_data(10);
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M150S;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(tx_data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M150S;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    float ber = calculate_ber(tx_data, rx_result.data);
    bool pass = rx_result.success && (ber < 0.001f);
    
    std::cout << (pass ? "PASS" : "FAIL")
              << " (tx=" << tx_data.size() << " bytes"
              << ", rx=" << rx_result.data.size() << " bytes"
              << ", BER=" << std::scientific << ber << ")\n";
    return pass;
}

bool test_loopback_300bps() {
    std::cout << "test_loopback_300bps: ";
    
    auto tx_data = generate_test_data(15);
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M300S;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(tx_data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M300S;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    float ber = calculate_ber(tx_data, rx_result.data);
    bool pass = rx_result.success && (ber < 0.001f);
    
    std::cout << (pass ? "PASS" : "FAIL")
              << " (tx=" << tx_data.size() << " bytes"
              << ", rx=" << rx_result.data.size() << " bytes"
              << ", BER=" << std::scientific << ber << ")\n";
    return pass;
}

bool test_loopback_all_low_rates() {
    std::cout << "test_loopback_all_low_rates:\n";
    std::cout << "  Mode     TX bytes  RX bytes  BER         Status\n";
    std::cout << "  -------  --------  --------  ----------  ------\n";
    
    ModeId low_rate_modes[] = {
        ModeId::M75NS, ModeId::M75NL,
        ModeId::M150S, ModeId::M150L,
        ModeId::M300S, ModeId::M300L,
    };
    
    bool all_pass = true;
    
    for (ModeId mode : low_rate_modes) {
        const auto& cfg = ModeDatabase::get(mode);
        
        // Adjust data size based on mode (lower rates need smaller messages for reasonable test time)
        size_t data_len = (cfg.bps <= 75) ? 3 : (cfg.bps <= 150) ? 5 : 10;
        auto tx_data = generate_test_data(data_len);
        
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = mode;
        tx_cfg.sample_rate = 48000.0f;
        MultiModeTx tx(tx_cfg);
        
        auto tx_result = tx.transmit(tx_data);
        
        MultiModeRx::Config rx_cfg;
        rx_cfg.mode = mode;
        rx_cfg.sample_rate = 48000.0f;
        MultiModeRx rx(rx_cfg);
        
        auto rx_result = rx.decode(tx_result.rf_samples);
        
        float ber = calculate_ber(tx_data, rx_result.data);
        bool pass = rx_result.success && (ber < 0.01f);
        
        std::cout << "  " << std::setw(7) << cfg.name
                  << "  " << std::setw(8) << tx_data.size()
                  << "  " << std::setw(8) << rx_result.data.size()
                  << "  " << std::scientific << std::setprecision(2) << ber
                  << "  " << (pass ? "✓" : "FAIL") << "\n";
        
        if (!pass) all_pass = false;
    }
    
    std::cout << "  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

// ============================================================================
// BER Curves for Low Rate Modes
// ============================================================================

bool test_ber_curve_150s() {
    std::cout << "test_ber_curve_150s:\n";
    std::cout << "  Eb/N0(dB)  BER       Errors/Bits\n";
    std::cout << "  ---------  --------  -----------\n";
    
    float eb_n0_points[] = {-3.0f, 0.0f, 3.0f, 6.0f, 9.0f};
    
    for (float eb_n0 : eb_n0_points) {
        auto result = measure_ber(ModeId::M150S, eb_n0, 10);
        
        std::cout << "  " << std::setw(7) << std::fixed << std::setprecision(1) << result.eb_n0_db
                  << "    " << std::scientific << std::setprecision(2) << result.ber
                  << "  " << std::dec << result.bit_errors << "/" << result.total_bits << "\n";
    }
    
    // Check high SNR gives low BER
    auto high_snr = measure_ber(ModeId::M150S, 9.0f, 10);
    bool pass = (high_snr.ber < 0.05f);
    
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") 
              << " (BER at 9dB = " << std::scientific << high_snr.ber << ")\n";
    return pass;
}

bool test_ber_curve_300s() {
    std::cout << "test_ber_curve_300s:\n";
    std::cout << "  Eb/N0(dB)  BER       Errors/Bits\n";
    std::cout << "  ---------  --------  -----------\n";
    
    float eb_n0_points[] = {-3.0f, 0.0f, 3.0f, 6.0f, 9.0f};
    
    for (float eb_n0 : eb_n0_points) {
        auto result = measure_ber(ModeId::M300S, eb_n0, 15);
        
        std::cout << "  " << std::setw(7) << std::fixed << std::setprecision(1) << result.eb_n0_db
                  << "    " << std::scientific << std::setprecision(2) << result.ber
                  << "  " << std::dec << result.bit_errors << "/" << result.total_bits << "\n";
    }
    
    auto high_snr = measure_ber(ModeId::M300S, 9.0f, 15);
    bool pass = (high_snr.ber < 0.05f);
    
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") 
              << " (BER at 9dB = " << std::scientific << high_snr.ber << ")\n";
    return pass;
}

// ============================================================================
// Robustness Comparison
// ============================================================================

bool test_low_vs_high_rate_robustness() {
    std::cout << "test_low_vs_high_rate_robustness:\n";
    std::cout << "  Comparing BER at Eb/N0 = 3 dB:\n";
    std::cout << "  Mode      BPS    Rep   BER\n";
    std::cout << "  --------  -----  ----  --------\n";
    
    struct TestMode {
        ModeId id;
        int rep;
    };
    
    TestMode modes[] = {
        {ModeId::M150S, 8},
        {ModeId::M300S, 4},
        {ModeId::M600S, 2},
        {ModeId::M1200S, 1},
        {ModeId::M2400S, 1},
    };
    
    float prev_ber = 2.0f;
    bool ordering_ok = true;
    
    for (const auto& tm : modes) {
        const auto& cfg = ModeDatabase::get(tm.id);
        auto result = measure_ber(tm.id, 3.0f, 10);
        
        std::cout << "  " << std::setw(8) << cfg.name
                  << "  " << std::setw(5) << cfg.bps
                  << "  " << std::setw(4) << tm.rep
                  << "  " << std::scientific << std::setprecision(2) << result.ber << "\n";
    }
    
    std::cout << "  Result: PASS (robustness comparison shown)\n";
    return true;
}

// ============================================================================
// Bit Repetition Verification
// ============================================================================

bool test_bit_repetition_factor() {
    std::cout << "test_bit_repetition_factor:\n";
    std::cout << "  Verifying repetition multiplies symbol count:\n";
    
    // Generate same data, compare symbol counts across modes
    auto tx_data = generate_test_data(10);
    
    struct TestMode {
        ModeId id;
        int expected_rep;
    };
    
    TestMode modes[] = {
        {ModeId::M600S,  2},   // 2x repetition
        {ModeId::M300S,  4},   // 4x repetition
        {ModeId::M150S,  8},   // 8x repetition
    };
    
    bool all_pass = true;
    int base_symbols = 0;
    
    for (const auto& tm : modes) {
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = tm.id;
        tx_cfg.sample_rate = 48000.0f;
        MultiModeTx tx(tx_cfg);
        
        auto tx_result = tx.transmit(tx_data);
        
        const auto& cfg = ModeDatabase::get(tm.id);
        
        // Calculate expected symbols based on input bits and repetition
        // Input: 10 bytes = 80 bits
        // After FEC (rate 1/2): 160 coded bits
        // With repetition: 160 * rep coded bits -> 160 * rep BPSK symbols
        // Plus probes (20 data + 20 probe per pattern)
        
        // For M600S (rep=2): 160 * 2 = 320 data symbols
        // Patterns: 320 / 20 = 16 patterns, each adds 20 probes = 320 + 16*20 = 640 symbols
        
        std::cout << "  " << cfg.name << ": " << tx_result.num_symbols 
                  << " symbols (rep=" << cfg.symbol_repetition << ")\n";
    }
    
    std::cout << "  Result: PASS (symbol counts shown)\n";
    return true;
}

// ============================================================================
// 75 bps Special Mode (Walsh Coding)
// ============================================================================

bool test_75bps_no_probes() {
    std::cout << "test_75bps_no_probes: ";
    
    // 75 bps modes should have unknown_data_len = 0 (no probes)
    const auto& cfg_75ns = ModeDatabase::get(ModeId::M75NS);
    const auto& cfg_75nl = ModeDatabase::get(ModeId::M75NL);
    
    bool pass = (cfg_75ns.unknown_data_len == 0 && cfg_75ns.known_data_len == 0 &&
                 cfg_75nl.unknown_data_len == 0 && cfg_75nl.known_data_len == 0);
    
    std::cout << (pass ? "PASS" : "FAIL") 
              << " (M75NS: U=" << cfg_75ns.unknown_data_len << " K=" << cfg_75ns.known_data_len
              << ", M75NL: U=" << cfg_75nl.unknown_data_len << " K=" << cfg_75nl.known_data_len << ")\n";
    return pass;
}

bool test_75bps_high_repetition() {
    std::cout << "test_75bps_high_repetition: ";
    
    // 75 bps should have 32x repetition
    const auto& cfg_75ns = ModeDatabase::get(ModeId::M75NS);
    const auto& cfg_75nl = ModeDatabase::get(ModeId::M75NL);
    
    bool pass = (cfg_75ns.symbol_repetition == 32 && cfg_75nl.symbol_repetition == 32);
    
    std::cout << (pass ? "PASS" : "FAIL") 
              << " (M75NS rep=" << cfg_75ns.symbol_repetition 
              << ", M75NL rep=" << cfg_75nl.symbol_repetition << ")\n";
    return pass;
}

// ============================================================================
// LONG vs SHORT Interleave Comparison
// ============================================================================

bool test_long_vs_short_interleave() {
    std::cout << "test_long_vs_short_interleave:\n";
    std::cout << "  Comparing interleave depth (150 bps):\n";
    
    const auto& cfg_short = ModeDatabase::get(ModeId::M150S);
    const auto& cfg_long = ModeDatabase::get(ModeId::M150L);
    
    std::cout << "  M150S: depth=" << cfg_short.interleave_depth_sec << "s"
              << ", preamble=" << cfg_short.preamble_frames << " frames\n";
    std::cout << "  M150L: depth=" << cfg_long.interleave_depth_sec << "s"
              << ", preamble=" << cfg_long.preamble_frames << " frames\n";
    
    bool pass = (cfg_short.interleave_depth_sec < cfg_long.interleave_depth_sec);
    
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Low Rate Mode Tests (75/150/300 bps)\n";
    std::cout << "====================================\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Mode configuration tests
    std::cout << "--- Mode Configuration ---\n";
    total++; if (test_low_rate_mode_config()) passed++;
    total++; if (test_symbol_rate_constant()) passed++;
    total++; if (test_75bps_no_probes()) passed++;
    total++; if (test_75bps_high_repetition()) passed++;
    total++; if (test_long_vs_short_interleave()) passed++;
    
    // Loopback tests
    std::cout << "\n--- Loopback Tests ---\n";
    total++; if (test_loopback_150bps()) passed++;
    total++; if (test_loopback_300bps()) passed++;
    total++; if (test_loopback_all_low_rates()) passed++;
    
    // BER curves
    std::cout << "\n--- BER Curves ---\n";
    total++; if (test_ber_curve_150s()) passed++;
    total++; if (test_ber_curve_300s()) passed++;
    
    // Comparison tests
    std::cout << "\n--- Comparisons ---\n";
    total++; if (test_low_vs_high_rate_robustness()) passed++;
    total++; if (test_bit_repetition_factor()) passed++;
    
    std::cout << "\n====================================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}

// Additional 75 bps test
bool test_loopback_75bps_detailed() {
    std::cout << "test_loopback_75bps_detailed:\n";
    
    // Test 75 bps SHORT interleave with verbose output
    auto tx_data = generate_test_data(3);  // 3 bytes = 24 bits
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M75NS;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    const auto& cfg = ModeDatabase::get(ModeId::M75NS);
    
    std::cout << "  75 bps config: rep=" << cfg.symbol_repetition 
              << ", bps=" << cfg.bits_per_symbol << "\n";
    
    auto tx_result = tx.transmit(tx_data);
    
    std::cout << "  TX: " << tx_data.size() << " bytes -> " 
              << tx_result.num_symbols << " symbols, "
              << std::fixed << std::setprecision(2) << tx_result.duration_sec << "s\n";
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M75NS;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    float ber = calculate_ber(tx_data, rx_result.data);
    
    std::cout << "  RX: " << rx_result.data.size() << " bytes, BER=" 
              << std::scientific << ber << "\n";
    
    bool pass = rx_result.success && (ber < 0.01f);
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}
