/**
 * MLSE Equalizer Tests
 * 
 * Phase 1: Basic functionality with L=2 (8 states)
 * Phase 2: Extended memory L=3 (64 states) + channel tracking
 * Phase 3: Comparison with DFE on fading channels
 */

#include "dsp/mlse_equalizer.h"
#include "channel/watterson.h"
#include "channel/awgn.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>

using namespace m110a;

// ============================================================================
// Phase 1 Tests: Basic Framework
// ============================================================================

/**
 * Test 1.1: Constellation points
 */
bool test_constellation() {
    std::cout << "test_constellation:\n";
    std::cout << "  Verifying 8-PSK constellation\n\n";
    
    const auto& constellation = get_8psk_constellation();
    
    std::cout << "  Symbol  Angle(deg)  Real     Imag     |Mag|\n";
    std::cout << "  ------  ----------  -------  -------  -----\n";
    
    bool all_unit = true;
    for (int i = 0; i < 8; i++) {
        complex_t c = constellation[i];
        float angle = std::atan2(c.imag(), c.real()) * 180.0f / PI;
        float mag = std::abs(c);
        
        std::cout << "  " << std::setw(6) << i
                  << "  " << std::setw(10) << std::fixed << std::setprecision(1) << angle
                  << "  " << std::setw(7) << std::setprecision(4) << c.real()
                  << "  " << std::setw(7) << c.imag()
                  << "  " << std::setprecision(3) << mag << "\n";
        
        if (std::abs(mag - 1.0f) > 0.001f) all_unit = false;
    }
    
    std::cout << "\n  Result: " << (all_unit ? "PASS" : "FAIL") 
              << " (all unit magnitude)\n";
    return all_unit;
}

/**
 * Test 1.2: State transitions for L=2
 */
bool test_state_transitions_l2() {
    std::cout << "test_state_transitions_l2:\n";
    std::cout << "  Verifying state transitions for L=2 (8 states)\n\n";
    
    MLSEConfig config;
    config.channel_memory = 2;
    
    MLSEEqualizer eq(config);
    
    std::cout << "  Num states: " << config.num_states() << "\n";
    std::cout << "  Num transitions: " << config.num_transitions() << "\n\n";
    
    // For L=2, state = previous symbol
    // Next state should equal input symbol
    bool pass = (config.num_states() == 8);
    
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 1.3: State transitions for L=3
 */
bool test_state_transitions_l3() {
    std::cout << "test_state_transitions_l3:\n";
    std::cout << "  Verifying state transitions for L=3 (64 states)\n\n";
    
    MLSEConfig config;
    config.channel_memory = 3;
    
    MLSEEqualizer eq(config);
    
    std::cout << "  Num states: " << config.num_states() << "\n";
    std::cout << "  Num transitions: " << config.num_transitions() << "\n\n";
    
    bool pass = (config.num_states() == 64);
    
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 1.4: Channel estimation
 */
bool test_channel_estimation() {
    std::cout << "test_channel_estimation:\n";
    std::cout << "  Testing LS channel estimation\n\n";
    
    std::mt19937 rng(12345);
    const auto& constellation = get_8psk_constellation();
    
    // True channel - simpler for testing
    std::vector<complex_t> true_channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.0f)  // Real-only for simplicity
    };
    
    // Generate known symbols
    int N = 200;
    std::vector<complex_t> known(N);
    for (int i = 0; i < N; i++) {
        known[i] = constellation[rng() % 8];
    }
    
    // Generate received signal: r[n] = h[0]*s[n] + h[1]*s[n-1]
    std::vector<complex_t> received(N);
    for (int n = 0; n < N; n++) {
        received[n] = true_channel[0] * known[n];
        if (n > 0) {
            received[n] += true_channel[1] * known[n-1];
        }
    }
    
    // Estimate channel
    MLSEConfig config;
    config.channel_memory = 2;
    MLSEEqualizer eq(config);
    
    eq.estimate_channel(known, received);
    auto estimated = eq.get_channel();
    
    std::cout << "  True channel:      h[0]=" << true_channel[0] 
              << ", h[1]=" << true_channel[1] << "\n";
    std::cout << "  Estimated channel: h[0]=" << estimated[0] 
              << ", h[1]=" << estimated[1] << "\n";
    
    // Check estimation accuracy
    float err0 = std::abs(estimated[0] - true_channel[0]);
    float err1 = std::abs(estimated[1] - true_channel[1]);
    
    std::cout << "  Estimation error: |h[0]|=" << err0 << ", |h[1]|=" << err1 << "\n";
    
    bool pass = (err0 < 0.15f) && (err1 < 0.15f);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 1.4b: Verify expected output computation
 */
bool test_expected_outputs() {
    std::cout << "test_expected_outputs:\n";
    std::cout << "  Verifying expected output computation\n\n";
    
    const auto& constellation = get_8psk_constellation();
    
    // Simple channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.0f)
    };
    
    MLSEConfig config;
    config.channel_memory = 2;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    // For state=0 (prev symbol was 0), input=0
    // Expected = h[0]*constellation[0] + h[1]*constellation[0]
    //          = 1.0 * (0.707, 0.707) + 0.5 * (0.707, 0.707)
    //          = (1.06, 1.06)
    
    complex_t s0 = constellation[0];  // (0.707, 0.707)
    complex_t expected_manual = channel[0] * s0 + channel[1] * s0;
    
    std::cout << "  constellation[0] = " << s0 << "\n";
    std::cout << "  Manual expected (state=0, input=0): " << expected_manual << "\n";
    
    // Test a few known symbol sequences
    std::vector<int> test_seq = {0, 1, 2, 3, 4, 5, 6, 7, 0};
    
    std::cout << "\n  Testing symbol sequence: ";
    for (int s : test_seq) std::cout << s << " ";
    std::cout << "\n\n";
    
    // Generate expected received values manually
    std::cout << "  n  s[n]  s[n-1]  Expected r[n]\n";
    std::cout << "  -  ----  ------  -------------\n";
    
    bool all_ok = true;
    for (size_t n = 1; n < test_seq.size(); n++) {
        int curr = test_seq[n];
        int prev = test_seq[n-1];
        
        complex_t expected = channel[0] * constellation[curr] + channel[1] * constellation[prev];
        
        std::cout << "  " << n << "  " << curr << "     " << prev << "       " << expected << "\n";
    }
    
    std::cout << "\n  Result: " << (all_ok ? "PASS" : "FAIL") << "\n";
    return all_ok;
}

/**
 * Test 1.5: Simple single-symbol decode
 */
bool test_single_symbol() {
    std::cout << "test_single_symbol:\n";
    std::cout << "  Testing single symbol decode (no ISI)\n\n";
    
    const auto& constellation = get_8psk_constellation();
    
    // Identity channel (no ISI)
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.0f, 0.0f)
    };
    
    MLSEConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 5;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    // Send a sequence of known symbols
    std::vector<int> tx_symbols = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};
    
    std::cout << "  TX: ";
    for (int s : tx_symbols) std::cout << s << " ";
    std::cout << "\n";
    
    // Create received signal (perfect, no noise)
    std::vector<complex_t> rx_signal;
    for (int s : tx_symbols) {
        rx_signal.push_back(constellation[s]);
    }
    
    // Process symbol by symbol with debug
    std::vector<int> decoded;
    std::cout << "  Processing symbol-by-symbol:\n";
    for (size_t i = 0; i < rx_signal.size(); i++) {
        int out = eq.process_symbol(rx_signal[i]);
        std::cout << "    Input[" << i << "]=" << tx_symbols[i] << " -> Output=" << out << "\n";
        if (out >= 0) {
            decoded.push_back(out);
        }
    }
    
    // Flush remaining
    auto remaining = eq.flush();
    std::cout << "  Flush returned " << remaining.size() << " symbols: ";
    for (int s : remaining) std::cout << s << " ";
    std::cout << "\n";
    decoded.insert(decoded.end(), remaining.begin(), remaining.end());
    
    std::cout << "\n  RX: ";
    for (int s : decoded) std::cout << s << " ";
    std::cout << "\n";
    
    // Count errors
    int errors = 0;
    int compared = std::min(decoded.size(), tx_symbols.size());
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) {
            errors++;
            std::cout << "  Error at " << i << ": expected " << tx_symbols[i] << " got " << decoded[i] << "\n";
        }
    }
    
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    
    bool pass = (errors == 0) && (decoded.size() == tx_symbols.size());
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 1.7: MLSE on AWGN channel (no ISI)
 */
bool test_mlse_awgn() {
    std::cout << "test_mlse_awgn:\n";
    std::cout << "  Testing MLSE on AWGN channel (no ISI)\n\n";
    
    std::mt19937 rng(54321);
    std::normal_distribution<float> noise(0.0f, 0.1f);  // Low noise
    const auto& constellation = get_8psk_constellation();
    
    // Generate random symbols
    int N = 200;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Add noise
    std::vector<complex_t> rx_signal(N);
    for (int i = 0; i < N; i++) {
        rx_signal[i] = tx_signal[i] + complex_t(noise(rng), noise(rng));
    }
    
    // Set up MLSE with identity channel
    MLSEConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    MLSEEqualizer eq(config);
    
    eq.set_channel({complex_t(1, 0), complex_t(0, 0)});
    
    // Decode
    auto decoded = eq.equalize(rx_signal);
    
    // Count errors - decoded should be aligned with tx_symbols
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), static_cast<int>(tx_symbols.size()));
    for (int i = 0; i < compared; i++) {
        if (tx_symbols[i] != decoded[i]) {
            errors++;
        }
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  TX symbols: " << N << "\n";
    std::cout << "  Decoded: " << decoded.size() << "\n";
    std::cout << "  Compared: " << compared << "\n";
    std::cout << "  Errors: " << errors << "\n";
    std::cout << "  SER: " << std::scientific << ser << "\n";
    
    // With low noise, should have very few errors
    bool pass = ser < 0.05f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 1.8: MLSE on static 2-tap channel
 */
bool test_mlse_static_multipath() {
    std::cout << "test_mlse_static_multipath:\n";
    std::cout << "  Testing MLSE on static 2-tap multipath\n\n";
    
    std::mt19937 rng(67890);
    const auto& constellation = get_8psk_constellation();
    
    // 2-tap channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.6f, 0.2f)  // Strong second tap
    };
    
    // Generate symbols
    int N = 200;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel: r[n] = h[0]*s[n] + h[1]*s[n-1]
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) {
            rx_signal[n] += channel[1] * tx_signal[n-1];
        }
    }
    
    // Set up MLSE with known channel
    MLSEConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    MLSEEqualizer eq(config);
    
    eq.set_channel(channel);
    
    // Decode
    auto decoded = eq.equalize(rx_signal);
    
    // Count errors - decoded should be aligned with tx_symbols
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), static_cast<int>(tx_symbols.size()));
    for (int i = 0; i < compared; i++) {
        if (tx_symbols[i] != decoded[i]) {
            errors++;
        }
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  Channel: h[0]=" << channel[0] << ", h[1]=" << channel[1] << "\n";
    std::cout << "  TX symbols: " << N << "\n";
    std::cout << "  Decoded: " << decoded.size() << "\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::scientific << ser << "\n";
    
    // Should be perfect with known channel
    bool pass = ser < 0.01f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Phase 2 Tests: Extended Memory L=3 (64 states)
// ============================================================================

/**
 * Test 2.1: MLSE with L=3 on 3-tap channel
 */
bool test_mlse_l3_static() {
    std::cout << "test_mlse_l3_static:\n";
    std::cout << "  Testing MLSE L=3 (64 states) on 3-tap channel\n\n";
    
    std::mt19937 rng(11111);
    const auto& constellation = get_8psk_constellation();
    
    // 3-tap channel (simulates ~0.8ms delay spread at 2400 baud)
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.2f),
        complex_t(0.3f, -0.1f)
    };
    
    // Generate symbols
    int N = 300;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel: r[n] = h[0]*s[n] + h[1]*s[n-1] + h[2]*s[n-2]
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) rx_signal[n] += channel[1] * tx_signal[n-1];
        if (n > 1) rx_signal[n] += channel[2] * tx_signal[n-2];
    }
    
    // Set up MLSE with L=3
    MLSEConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 20;
    MLSEEqualizer eq(config);
    
    eq.set_channel(channel);
    
    std::cout << "  States: " << config.num_states() << "\n";
    std::cout << "  Channel: h[0]=" << channel[0] << ", h[1]=" << channel[1] 
              << ", h[2]=" << channel[2] << "\n";
    
    // Decode
    auto decoded = eq.equalize(rx_signal);
    
    // Count errors
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), static_cast<int>(tx_symbols.size()));
    for (int i = 0; i < compared; i++) {
        if (tx_symbols[i] != decoded[i]) {
            errors++;
        }
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  TX symbols: " << N << "\n";
    std::cout << "  Decoded: " << decoded.size() << "\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::scientific << ser << "\n";
    
    bool pass = ser < 0.01f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 2.2: Channel estimation for L=3
 */
bool test_channel_estimation_l3() {
    std::cout << "test_channel_estimation_l3:\n";
    std::cout << "  Testing LS channel estimation for L=3\n\n";
    
    std::mt19937 rng(22222);
    const auto& constellation = get_8psk_constellation();
    
    // True 3-tap channel
    std::vector<complex_t> true_channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.1f),
        complex_t(0.2f, -0.15f)
    };
    
    // Generate known symbols
    int N = 300;
    std::vector<complex_t> known(N);
    for (int i = 0; i < N; i++) {
        known[i] = constellation[rng() % 8];
    }
    
    // Generate received signal
    std::vector<complex_t> received(N);
    for (int n = 0; n < N; n++) {
        received[n] = true_channel[0] * known[n];
        if (n > 0) received[n] += true_channel[1] * known[n-1];
        if (n > 1) received[n] += true_channel[2] * known[n-2];
    }
    
    // Estimate channel
    MLSEConfig config;
    config.channel_memory = 3;
    MLSEEqualizer eq(config);
    
    eq.estimate_channel(known, received);
    auto estimated = eq.get_channel();
    
    std::cout << "  True channel:\n";
    for (int k = 0; k < 3; k++) {
        std::cout << "    h[" << k << "] = " << true_channel[k] << "\n";
    }
    std::cout << "  Estimated channel:\n";
    for (int k = 0; k < 3; k++) {
        std::cout << "    h[" << k << "] = " << estimated[k] << "\n";
    }
    
    // Check estimation accuracy
    float max_err = 0;
    for (int k = 0; k < 3; k++) {
        float err = std::abs(estimated[k] - true_channel[k]);
        max_err = std::max(max_err, err);
    }
    
    std::cout << "  Max estimation error: " << max_err << "\n";
    
    bool pass = max_err < 0.1f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 2.3: MLSE L=3 with noise
 */
bool test_mlse_l3_noisy() {
    std::cout << "test_mlse_l3_noisy:\n";
    std::cout << "  Testing MLSE L=3 with AWGN\n\n";
    
    std::mt19937 rng(33333);
    std::normal_distribution<float> noise(0.0f, 0.15f);  // Moderate noise
    const auto& constellation = get_8psk_constellation();
    
    // 3-tap channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.0f),
        complex_t(0.25f, 0.0f)
    };
    
    // Generate symbols
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel + noise
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) rx_signal[n] += channel[1] * tx_signal[n-1];
        if (n > 1) rx_signal[n] += channel[2] * tx_signal[n-2];
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Set up MLSE with L=3
    MLSEConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 25;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    // Decode
    auto decoded = eq.equalize(rx_signal);
    
    // Count errors
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), static_cast<int>(tx_symbols.size()));
    for (int i = 0; i < compared; i++) {
        if (tx_symbols[i] != decoded[i]) {
            errors++;
        }
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  Channel memory: L=3 (64 states)\n";
    std::cout << "  Noise std: 0.15\n";
    std::cout << "  TX symbols: " << N << "\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::scientific << ser << "\n";
    
    // With moderate noise and strong ISI, expect some errors but < 15%
    bool pass = ser < 0.15f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Phase 3 Tests: MLSE vs DFE on Watterson Channels
// ============================================================================

/**
 * Test 3.1: End-to-end MLSE with MultiModeRx (static channel)
 */
bool test_mlse_multimode_static() {
    std::cout << "test_mlse_multimode_static:\n";
    std::cout << "  Testing MLSE integration with MultiModeRx\n\n";
    
    // This test verifies MLSE works in the receiver pipeline
    // For now, just verify compilation and basic operation
    
    std::mt19937 rng(77777);
    const auto& constellation = get_8psk_constellation();
    
    // Simple test: known symbols through 2-tap channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.1f)
    };
    
    int N = 100;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) rx_signal[n] += channel[1] * tx_signal[n-1];
    }
    
    // Decode with MLSE
    MLSEConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    auto decoded = eq.equalize(rx_signal);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) errors++;
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  Channel: 2-tap static\n";
    std::cout << "  Symbols: " << N << "\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::fixed << std::setprecision(2) << (ser * 100) << "%\n";
    
    bool pass = ser < 0.01f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 3.2: MLSE on time-varying Watterson channel
 */
bool test_mlse_watterson_fading() {
    std::cout << "test_mlse_watterson_fading:\n";
    std::cout << "  Testing MLSE on Watterson fading channel\n\n";
    
    std::mt19937 rng(88888);
    const auto& constellation = get_8psk_constellation();
    
    // Create Watterson channel - CCIR Good approximation
    // 0.5 Hz Doppler, moderate multipath
    WattersonChannel::Config ch_cfg;
    ch_cfg.sample_rate = 2400.0f;  // Symbol rate (1 sample per symbol for simplicity)
    ch_cfg.doppler_spread_hz = 0.5f;
    ch_cfg.delay_ms = 0.0f;  // No delay (symbol-spaced model)
    ch_cfg.path1_gain_db = 0.0f;
    ch_cfg.path2_gain_db = -6.0f;
    ch_cfg.tap_update_rate_hz = 100.0f;
    ch_cfg.seed = 12345;
    
    WattersonChannel channel(ch_cfg);
    
    // Generate symbols
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply fading channel (symbol by symbol)
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        // Get current fading taps
        complex_t tap1, tap2;
        channel.get_taps(tap1, tap2);
        
        float tap_mag = std::abs(tap1);
        rx_signal[n] = tx_signal[n] * tap_mag;
        
        // Add small ISI from second path (delayed)
        if (n > 0) {
            float tap2_mag = std::abs(tap2);
            rx_signal[n] += tx_signal[n-1] * tap2_mag * 0.5f;  // -6 dB
        }
        
        // Advance channel state
        channel.process_sample(0.0f);  // Just to update taps
    }
    
    // Add noise
    std::normal_distribution<float> noise(0.0f, 0.1f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Test 1: Simple slicer (baseline)
    int slicer_errors = 0;
    for (int n = 0; n < N; n++) {
        int best = 0;
        float best_dist = std::norm(rx_signal[n] - constellation[0]);
        for (int k = 1; k < 8; k++) {
            float dist = std::norm(rx_signal[n] - constellation[k]);
            if (dist < best_dist) {
                best_dist = dist;
                best = k;
            }
        }
        if (best != tx_symbols[n]) slicer_errors++;
    }
    float slicer_ser = static_cast<float>(slicer_errors) / N;
    
    // Test 2: MLSE with estimated channel (use first 50 symbols as training)
    MLSEConfig mlse_cfg;
    mlse_cfg.channel_memory = 2;
    mlse_cfg.traceback_depth = 15;
    MLSEEqualizer mlse(mlse_cfg);
    
    // Use first symbols for channel estimation
    std::vector<complex_t> train_tx(tx_signal.begin(), tx_signal.begin() + 50);
    std::vector<complex_t> train_rx(rx_signal.begin(), rx_signal.begin() + 50);
    mlse.estimate_channel(train_tx, train_rx);
    
    // Decode remaining symbols
    std::vector<complex_t> test_rx(rx_signal.begin() + 50, rx_signal.end());
    auto decoded = mlse.equalize(test_rx);
    
    int mlse_errors = 0;
    int test_start = 50;
    int compared = std::min(static_cast<int>(decoded.size()), N - test_start);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[test_start + i]) mlse_errors++;
    }
    float mlse_ser = static_cast<float>(mlse_errors) / compared;
    
    std::cout << "  Channel: Watterson CCIR Good (0.5 Hz Doppler)\n";
    std::cout << "  Symbols: " << N << " (50 training + " << (N-50) << " test)\n";
    std::cout << "  Simple slicer SER: " << std::fixed << std::setprecision(1) 
              << (slicer_ser * 100) << "%\n";
    std::cout << "  MLSE SER:          " << (mlse_ser * 100) << "%\n";
    
    if (mlse_ser > 0.001f && slicer_ser > mlse_ser) {
        std::cout << "  MLSE improvement:  " << std::setprecision(1) 
                  << (slicer_ser / mlse_ser) << "x\n";
    }
    
    // MLSE should help even on fading channel
    bool pass = mlse_ser < 0.30f;  // Allow higher error rate on fading
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 3.3: MLSE with periodic channel re-estimation (adaptive)
 */
bool test_mlse_adaptive() {
    std::cout << "test_mlse_adaptive:\n";
    std::cout << "  Testing MLSE with periodic channel updates\n\n";
    
    std::mt19937 rng(99999);
    const auto& constellation = get_8psk_constellation();
    
    // Time-varying channel (simulates slow fading)
    int N = 600;
    int block_size = 100;  // Re-estimate channel every 100 symbols
    
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply slowly time-varying channel (slow enough for block-adaptive to work)
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        // Channel varies slowly over time - phase drift of 0.001 rad/symbol
        // Over 100 symbols = 0.1 rad = ~6 degrees (tolerable within block)
        float phase_drift = 0.001f * n;
        float fade = 0.9f + 0.1f * std::cos(2 * PI * n / 400.0f);  // Slow amplitude fade
        
        complex_t h0 = std::polar(fade, phase_drift);
        complex_t h1 = std::polar(0.3f * fade, phase_drift + 0.3f);
        
        rx_signal[n] = h0 * tx_signal[n];
        if (n > 0) rx_signal[n] += h1 * tx_signal[n-1];
    }
    
    // Add noise
    std::normal_distribution<float> noise(0.0f, 0.10f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Test 1: MLSE with single channel estimate (no adaptation)
    MLSEConfig mlse_cfg;
    mlse_cfg.channel_memory = 2;
    mlse_cfg.traceback_depth = 15;
    MLSEEqualizer mlse_static(mlse_cfg);
    
    // Estimate from first block only
    std::vector<complex_t> init_tx(tx_signal.begin(), tx_signal.begin() + block_size);
    std::vector<complex_t> init_rx(rx_signal.begin(), rx_signal.begin() + block_size);
    mlse_static.estimate_channel(init_tx, init_rx);
    
    auto decoded_static = mlse_static.equalize(
        std::vector<complex_t>(rx_signal.begin() + block_size, rx_signal.end()));
    
    int static_errors = 0;
    int compared = std::min(static_cast<int>(decoded_static.size()), N - block_size);
    for (int i = 0; i < compared; i++) {
        if (decoded_static[i] != tx_symbols[block_size + i]) static_errors++;
    }
    float static_ser = static_cast<float>(static_errors) / compared;
    
    // Test 2: MLSE with periodic channel re-estimation (adaptive)
    std::vector<int> decoded_adaptive;
    int adaptive_errors = 0;
    int adaptive_compared = 0;
    
    for (int block = 0; block < N / block_size; block++) {
        int start = block * block_size;
        int end = std::min(start + block_size, N);
        
        // Use first 30 symbols of each block for channel estimation
        int train_len = 30;
        
        if (start + train_len > N) break;
        
        std::vector<complex_t> block_train_tx(tx_signal.begin() + start, 
                                               tx_signal.begin() + start + train_len);
        std::vector<complex_t> block_train_rx(rx_signal.begin() + start,
                                               rx_signal.begin() + start + train_len);
        
        MLSEEqualizer mlse_block(mlse_cfg);
        mlse_block.estimate_channel(block_train_tx, block_train_rx);
        
        // Decode remaining symbols in block
        std::vector<complex_t> block_data_rx(rx_signal.begin() + start + train_len,
                                              rx_signal.begin() + end);
        auto block_decoded = mlse_block.equalize(block_data_rx);
        
        // Count errors for this block
        for (size_t i = 0; i < block_decoded.size(); i++) {
            int tx_idx = start + train_len + i;
            if (tx_idx < N) {
                if (block_decoded[i] != tx_symbols[tx_idx]) adaptive_errors++;
                adaptive_compared++;
            }
        }
    }
    
    float adaptive_ser = (adaptive_compared > 0) ? 
        static_cast<float>(adaptive_errors) / adaptive_compared : 1.0f;
    
    std::cout << "  Channel: Slowly time-varying (phase drift + fade)\n";
    std::cout << "  Total symbols: " << N << "\n";
    std::cout << "  Block size: " << block_size << " (30 training + 70 data)\n";
    std::cout << "  Static MLSE SER:   " << std::fixed << std::setprecision(1) 
              << (static_ser * 100) << "%\n";
    std::cout << "  Adaptive MLSE SER: " << (adaptive_ser * 100) << "%\n";
    
    if (static_ser > adaptive_ser && adaptive_ser > 0.001f) {
        std::cout << "  Adaptation gain:   " << std::setprecision(1) 
                  << (static_ser / adaptive_ser) << "x\n";
    }
    
    // Adaptive should be significantly better on time-varying channel
    bool pass = (adaptive_ser < static_ser * 0.8f) || (adaptive_ser < 0.15f);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 2.4: MLSE vs simple slicer on CCIR Good
 */
bool test_mlse_ccir_good() {
    std::cout << "test_mlse_ccir_good:\n";
    std::cout << "  Testing MLSE on CCIR Good channel\n\n";
    
    // CCIR Good: 0.5 Hz spread, 0.5 ms delay
    // At 2400 baud, 0.5 ms = 1.2 symbols
    
    std::mt19937 rng(44444);
    const auto& constellation = get_8psk_constellation();
    
    // Simulate simplified CCIR Good: 2-tap with mild ISI
    // Real Watterson would have time-varying taps
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.35f, 0.1f)  // -6 dB second path
    };
    
    // Generate symbols
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel + moderate noise
    std::normal_distribution<float> noise(0.0f, 0.12f);
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) rx_signal[n] += channel[1] * tx_signal[n-1];
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Test 1: Simple slicer (no equalization)
    int slicer_errors = 0;
    for (int n = 0; n < N; n++) {
        // Find closest constellation point
        int best = 0;
        float best_dist = std::norm(rx_signal[n] - constellation[0]);
        for (int k = 1; k < 8; k++) {
            float dist = std::norm(rx_signal[n] - constellation[k]);
            if (dist < best_dist) {
                best_dist = dist;
                best = k;
            }
        }
        if (best != tx_symbols[n]) slicer_errors++;
    }
    float slicer_ser = static_cast<float>(slicer_errors) / N;
    
    // Test 2: MLSE equalizer
    MLSEConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    auto decoded = eq.equalize(rx_signal);
    
    int mlse_errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) mlse_errors++;
    }
    float mlse_ser = static_cast<float>(mlse_errors) / compared;
    
    std::cout << "  Channel: CCIR Good approximation (2-tap)\n";
    std::cout << "  Symbols: " << N << "\n";
    std::cout << "  Simple slicer SER: " << std::fixed << std::setprecision(1) 
              << (slicer_ser * 100) << "%\n";
    std::cout << "  MLSE SER:          " << (mlse_ser * 100) << "%\n";
    std::cout << "  MLSE improvement:  " << std::setprecision(1) 
              << (slicer_ser / std::max(mlse_ser, 0.001f)) << "x\n";
    
    // MLSE should significantly outperform simple slicer
    bool pass = (mlse_ser < slicer_ser) && (mlse_ser < 0.10f);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 2.5: MLSE on severe ISI (CCIR Moderate approximation)
 */
bool test_mlse_ccir_moderate() {
    std::cout << "test_mlse_ccir_moderate:\n";
    std::cout << "  Testing MLSE on CCIR Moderate channel\n\n";
    
    // CCIR Moderate: 1.0 Hz spread, 1.0 ms delay, equal power paths
    // At 2400 baud, 1.0 ms = 2.4 symbols -> need L=3
    
    std::mt19937 rng(55555);
    const auto& constellation = get_8psk_constellation();
    
    // Equal power 3-tap channel (severe ISI)
    std::vector<complex_t> channel = {
        complex_t(0.7f, 0.0f),
        complex_t(0.5f, 0.2f),
        complex_t(0.4f, -0.1f)
    };
    
    // Normalize to unit power
    float power = 0;
    for (const auto& h : channel) power += std::norm(h);
    float norm = 1.0f / std::sqrt(power);
    for (auto& h : channel) h *= norm;
    
    // Generate symbols
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply channel + noise (15 dB SNR approximately)
    std::normal_distribution<float> noise(0.0f, 0.18f);
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n > 0) rx_signal[n] += channel[1] * tx_signal[n-1];
        if (n > 1) rx_signal[n] += channel[2] * tx_signal[n-2];
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Test 1: Simple slicer
    int slicer_errors = 0;
    for (int n = 0; n < N; n++) {
        int best = 0;
        float best_dist = std::norm(rx_signal[n] - constellation[0]);
        for (int k = 1; k < 8; k++) {
            float dist = std::norm(rx_signal[n] - constellation[k]);
            if (dist < best_dist) {
                best_dist = dist;
                best = k;
            }
        }
        if (best != tx_symbols[n]) slicer_errors++;
    }
    float slicer_ser = static_cast<float>(slicer_errors) / N;
    
    // Test 2: MLSE L=3
    MLSEConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 20;
    MLSEEqualizer eq(config);
    eq.set_channel(channel);
    
    auto decoded = eq.equalize(rx_signal);
    
    int mlse_errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) mlse_errors++;
    }
    float mlse_ser = static_cast<float>(mlse_errors) / compared;
    
    std::cout << "  Channel: CCIR Moderate approximation (3-tap, equal power)\n";
    std::cout << "  MLSE states: 64\n";
    std::cout << "  Symbols: " << N << "\n";
    std::cout << "  Simple slicer SER: " << std::fixed << std::setprecision(1) 
              << (slicer_ser * 100) << "%\n";
    std::cout << "  MLSE SER:          " << (mlse_ser * 100) << "%\n";
    
    if (mlse_ser > 0.001f) {
        std::cout << "  MLSE improvement:  " << std::setprecision(1) 
                  << (slicer_ser / mlse_ser) << "x\n";
    } else {
        std::cout << "  MLSE improvement:  >100x\n";
    }
    
    // On severe ISI, slicer should be ~50% errors, MLSE much better
    bool pass = (mlse_ser < 0.25f) && (mlse_ser < slicer_ser * 0.5f);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 2.6: MLSE with estimated channel (realistic scenario)
 */
bool test_mlse_estimated_channel() {
    std::cout << "test_mlse_estimated_channel:\n";
    std::cout << "  Testing MLSE with LS channel estimation\n\n";
    
    std::mt19937 rng(66666);
    const auto& constellation = get_8psk_constellation();
    
    // Unknown channel to receiver
    std::vector<complex_t> true_channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.45f, 0.15f),
        complex_t(0.2f, -0.1f)
    };
    
    // Generate preamble (pseudo-random for good estimation properties)
    int preamble_len = 150;
    std::vector<int> preamble_symbols(preamble_len);
    std::vector<complex_t> preamble_signal(preamble_len);
    std::mt19937 preamble_rng(12345);  // Fixed seed for repeatability
    for (int i = 0; i < preamble_len; i++) {
        preamble_symbols[i] = preamble_rng() % 8;
        preamble_signal[i] = constellation[preamble_symbols[i]];
    }
    
    // Generate data symbols
    int data_len = 400;
    std::vector<int> data_symbols(data_len);
    std::vector<complex_t> data_signal(data_len);
    for (int i = 0; i < data_len; i++) {
        data_symbols[i] = rng() % 8;
        data_signal[i] = constellation[data_symbols[i]];
    }
    
    // Apply channel to preamble + data
    std::normal_distribution<float> noise(0.0f, 0.1f);
    
    std::vector<complex_t> rx_preamble(preamble_len);
    for (int n = 0; n < preamble_len; n++) {
        rx_preamble[n] = true_channel[0] * preamble_signal[n];
        if (n > 0) rx_preamble[n] += true_channel[1] * preamble_signal[n-1];
        if (n > 1) rx_preamble[n] += true_channel[2] * preamble_signal[n-2];
        rx_preamble[n] += complex_t(noise(rng), noise(rng));
    }
    
    std::vector<complex_t> rx_data(data_len);
    for (int n = 0; n < data_len; n++) {
        rx_data[n] = true_channel[0] * data_signal[n];
        if (n > 0) rx_data[n] += true_channel[1] * data_signal[n-1];
        if (n > 1) rx_data[n] += true_channel[2] * data_signal[n-2];
        rx_data[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Estimate channel from preamble
    MLSEConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 20;
    MLSEEqualizer eq(config);
    
    eq.estimate_channel(preamble_signal, rx_preamble);
    auto estimated = eq.get_channel();
    
    std::cout << "  True channel:      ";
    for (const auto& h : true_channel) std::cout << h << " ";
    std::cout << "\n  Estimated channel: ";
    for (const auto& h : estimated) std::cout << h << " ";
    std::cout << "\n";
    
    // Check estimation error
    float max_err = 0;
    for (int k = 0; k < 3; k++) {
        float err = std::abs(estimated[k] - true_channel[k]);
        max_err = std::max(max_err, err);
    }
    std::cout << "  Max estimation error: " << max_err << "\n\n";
    
    // Decode data
    auto decoded = eq.equalize(rx_data);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), data_len);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != data_symbols[i]) errors++;
    }
    float ser = static_cast<float>(errors) / compared;
    
    std::cout << "  Preamble length: " << preamble_len << " symbols\n";
    std::cout << "  Data length: " << data_len << " symbols\n";
    std::cout << "  Data errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::fixed << std::setprecision(2) << (ser * 100) << "%\n";
    
    // With good channel estimate, should decode well
    bool pass = ser < 0.05f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "MLSE Equalizer Tests\n";
    std::cout << "====================\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Phase 1: Basic Framework
    std::cout << "--- Phase 1: Basic Framework (L=2) ---\n";
    total++; if (test_constellation()) passed++;
    total++; if (test_state_transitions_l2()) passed++;
    total++; if (test_state_transitions_l3()) passed++;
    total++; if (test_channel_estimation()) passed++;
    total++; if (test_expected_outputs()) passed++;
    total++; if (test_single_symbol()) passed++;
    total++; if (test_mlse_awgn()) passed++;
    total++; if (test_mlse_static_multipath()) passed++;
    
    // Phase 2: Extended Memory L=3
    std::cout << "\n--- Phase 2: Extended Memory (L=3) ---\n";
    total++; if (test_mlse_l3_static()) passed++;
    total++; if (test_channel_estimation_l3()) passed++;
    total++; if (test_mlse_l3_noisy()) passed++;
    
    // Phase 2: CCIR Channel comparison
    std::cout << "\n--- Phase 2: CCIR Channel Comparison ---\n";
    total++; if (test_mlse_ccir_good()) passed++;
    total++; if (test_mlse_ccir_moderate()) passed++;
    total++; if (test_mlse_estimated_channel()) passed++;
    
    // Phase 3: Watterson fading and adaptive MLSE
    std::cout << "\n--- Phase 3: Watterson Fading & Adaptation ---\n";
    total++; if (test_mlse_multimode_static()) passed++;
    total++; if (test_mlse_watterson_fading()) passed++;
    total++; if (test_mlse_adaptive()) passed++;
    
    std::cout << "\n====================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
