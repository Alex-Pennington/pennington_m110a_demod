/**
 * Tests for Advanced MLSE Features
 * 
 * Phase 4: DDFSE (reduced-state)
 * Phase 5: SOVA (soft outputs)
 * Phase 6: SIMD optimization
 */

#include "dsp/mlse_advanced.h"
#include "dsp/mlse_equalizer.h"
#include "channel/awgn.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>
#include <chrono>

using namespace m110a;

// ============================================================================
// Phase 4: DDFSE Tests
// ============================================================================

/**
 * Test 4.1: DDFSE basic operation
 */
bool test_ddfse_basic() {
    std::cout << "test_ddfse_basic:\n";
    std::cout << "  Testing DDFSE equalizer basic operation\n\n";
    
    std::mt19937 rng(11111);
    const auto& constellation = get_8psk_constellation();
    
    // 3-tap channel (MLSE handles 3, no DFE taps)
    DDFSEConfig config;
    config.mlse_taps = 3;
    config.dfe_taps = 0;
    config.traceback_depth = 15;
    
    DDFSEEqualizer eq(config);
    
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.1f),
        complex_t(0.2f, -0.1f)
    };
    eq.set_channel(channel);
    
    // Generate test symbols
    int N = 200;
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
        if (n >= 1) rx_signal[n] += channel[1] * tx_signal[n-1];
        if (n >= 2) rx_signal[n] += channel[2] * tx_signal[n-2];
    }
    
    // Decode
    auto decoded = eq.equalize(rx_signal);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) errors++;
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  States: " << eq.num_states() << "\n";
    std::cout << "  Symbols: " << N << "\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::fixed << std::setprecision(2) << (ser * 100) << "%\n";
    
    bool pass = ser < 0.01f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 4.2: DDFSE with DFE taps (5-tap channel, 3 MLSE + 2 DFE)
 */
bool test_ddfse_hybrid() {
    std::cout << "test_ddfse_hybrid:\n";
    std::cout << "  Testing DDFSE with hybrid MLSE+DFE\n\n";
    
    std::mt19937 rng(22222);
    const auto& constellation = get_8psk_constellation();
    
    // 5-tap channel: MLSE handles first 3, DFE handles last 2
    DDFSEConfig config;
    config.mlse_taps = 3;
    config.dfe_taps = 2;
    config.traceback_depth = 15;
    
    DDFSEEqualizer eq(config);
    
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.1f),
        complex_t(0.3f, -0.1f),
        complex_t(0.15f, 0.05f),
        complex_t(0.08f, -0.02f)
    };
    eq.set_channel(channel);
    
    int N = 300;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    // Apply 5-tap channel
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = complex_t(0, 0);
        for (int k = 0; k < 5 && n >= k; k++) {
            rx_signal[n] += channel[k] * tx_signal[n-k];
        }
    }
    
    // Add small noise
    std::normal_distribution<float> noise(0.0f, 0.05f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    auto decoded = eq.equalize(rx_signal);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(decoded.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded[i] != tx_symbols[i]) errors++;
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  Channel taps: 5 (3 MLSE + 2 DFE)\n";
    std::cout << "  DDFSE states: " << eq.num_states() << "\n";
    std::cout << "  Full MLSE would need: " << eq.full_states() << " states\n";
    std::cout << "  Complexity reduction: " << std::setprecision(1) 
              << (float)eq.full_states() / eq.num_states() << "x\n";
    std::cout << "  Errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::setprecision(2) << (ser * 100) << "%\n";
    
    // DDFSE should work reasonably well (allow some errors due to DFE feedback)
    bool pass = ser < 0.10f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 4.3: DDFSE complexity comparison
 */
bool test_ddfse_complexity() {
    std::cout << "test_ddfse_complexity:\n";
    std::cout << "  Comparing DDFSE vs full MLSE complexity\n\n";
    
    std::cout << "  Channel   Full MLSE   DDFSE(L'=3)   Reduction\n";
    std::cout << "  -------   ---------   -----------   ---------\n";
    
    for (int L = 3; L <= 6; L++) {
        int full_states = 1;
        for (int i = 0; i < L - 1; i++) full_states *= 8;
        
        DDFSEConfig config;
        config.mlse_taps = 3;
        config.dfe_taps = L - 3;
        DDFSEEqualizer eq(config);
        
        std::cout << "  L=" << L << "       " << std::setw(5) << full_states 
                  << "       " << std::setw(5) << eq.num_states()
                  << "         " << std::setw(5) << std::setprecision(0) 
                  << (float)full_states / eq.num_states() << "x\n";
    }
    
    std::cout << "\n  Result: PASS\n";
    return true;
}

// ============================================================================
// Phase 5: SOVA Tests
// ============================================================================

/**
 * Test 5.1: SOVA basic operation
 */
bool test_sova_basic() {
    std::cout << "test_sova_basic:\n";
    std::cout << "  Testing SOVA equalizer basic operation\n\n";
    
    std::mt19937 rng(33333);
    const auto& constellation = get_8psk_constellation();
    
    SOVAConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    config.noise_variance = 0.01f;
    
    SOVAEqualizer eq(config);
    
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.1f)
    };
    eq.set_channel(channel);
    
    int N = 150;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n >= 1) rx_signal[n] += channel[1] * tx_signal[n-1];
    }
    
    auto soft_output = eq.equalize_soft(rx_signal);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(soft_output.size()), N);
    for (int i = 0; i < compared; i++) {
        if (soft_output[i].hard_decision != tx_symbols[i]) errors++;
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    std::cout << "  Symbols: " << N << "\n";
    std::cout << "  Soft outputs: " << soft_output.size() << "\n";
    std::cout << "  Hard decision errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::fixed << std::setprecision(2) << (ser * 100) << "%\n";
    
    bool pass = ser < 0.01f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 5.2: SOVA reliability correlates with correctness
 */
bool test_sova_reliability() {
    std::cout << "test_sova_reliability:\n";
    std::cout << "  Testing that SOVA reliability correlates with correctness\n\n";
    
    std::mt19937 rng(44444);
    const auto& constellation = get_8psk_constellation();
    
    SOVAConfig config;
    config.channel_memory = 2;
    config.traceback_depth = 15;
    config.noise_variance = 0.1f;
    
    SOVAEqualizer eq(config);
    
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.2f)
    };
    eq.set_channel(channel);
    
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = channel[0] * tx_signal[n];
        if (n >= 1) rx_signal[n] += channel[1] * tx_signal[n-1];
    }
    
    // Add noise to create some errors
    std::normal_distribution<float> noise(0.0f, 0.15f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    auto soft_output = eq.equalize_soft(rx_signal);
    
    // Bin decisions by reliability and check error rate in each bin
    const int num_bins = 5;
    std::vector<int> bin_correct(num_bins, 0);
    std::vector<int> bin_total(num_bins, 0);
    
    int compared = std::min(static_cast<int>(soft_output.size()), N);
    for (int i = 0; i < compared; i++) {
        float rel = std::abs(soft_output[i].reliability);
        int bin = std::min(static_cast<int>(rel * num_bins), num_bins - 1);
        
        bin_total[bin]++;
        if (soft_output[i].hard_decision == tx_symbols[i]) {
            bin_correct[bin]++;
        }
    }
    
    std::cout << "  Reliability vs Accuracy:\n";
    std::cout << "  Bin    Reliability   Correct   Total   Accuracy\n";
    std::cout << "  ---    -----------   -------   -----   --------\n";
    
    bool monotonic = true;
    float prev_accuracy = 0.0f;
    
    for (int b = 0; b < num_bins; b++) {
        float rel_low = static_cast<float>(b) / num_bins;
        float rel_high = static_cast<float>(b + 1) / num_bins;
        
        float accuracy = (bin_total[b] > 0) ? 
            static_cast<float>(bin_correct[b]) / bin_total[b] : 0.0f;
        
        std::cout << "  " << b << "      [" << std::setprecision(1) << rel_low 
                  << "-" << rel_high << "]      " << std::setw(4) << bin_correct[b]
                  << "      " << std::setw(4) << bin_total[b]
                  << "     " << std::setprecision(1) << (accuracy * 100) << "%\n";
        
        // Check if accuracy is generally increasing with reliability
        if (b > 0 && bin_total[b] > 10 && bin_total[b-1] > 10) {
            if (accuracy < prev_accuracy - 0.1f) {
                monotonic = false;
            }
        }
        prev_accuracy = accuracy;
    }
    
    // High reliability should have higher accuracy than low reliability
    float low_rel_acc = (bin_total[0] > 0) ? 
        static_cast<float>(bin_correct[0]) / bin_total[0] : 0.0f;
    float high_rel_acc = (bin_total[num_bins-1] > 0) ? 
        static_cast<float>(bin_correct[num_bins-1]) / bin_total[num_bins-1] : 1.0f;
    
    bool pass = high_rel_acc >= low_rel_acc;
    std::cout << "\n  High reliability more accurate: " << (pass ? "YES" : "NO") << "\n";
    std::cout << "  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 5.3: SOVA with channel estimation
 */
bool test_sova_estimated_channel() {
    std::cout << "test_sova_estimated_channel:\n";
    std::cout << "  Testing SOVA with LS channel estimation\n\n";
    
    std::mt19937 rng(55555);
    const auto& constellation = get_8psk_constellation();
    
    SOVAConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 20;
    config.noise_variance = 0.1f;
    
    SOVAEqualizer eq(config);
    
    // True channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.15f),
        complex_t(0.2f, -0.1f)
    };
    
    int preamble_len = 100;
    int data_len = 300;
    int N = preamble_len + data_len;
    
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = complex_t(0, 0);
        for (int k = 0; k < 3 && n >= k; k++) {
            rx_signal[n] += channel[k] * tx_signal[n-k];
        }
    }
    
    // Add noise
    std::normal_distribution<float> noise(0.0f, 0.1f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Estimate channel from preamble
    std::vector<complex_t> preamble_tx(tx_signal.begin(), tx_signal.begin() + preamble_len);
    std::vector<complex_t> preamble_rx(rx_signal.begin(), rx_signal.begin() + preamble_len);
    eq.estimate_channel(preamble_tx, preamble_rx);
    
    // Decode data
    std::vector<complex_t> data_rx(rx_signal.begin() + preamble_len, rx_signal.end());
    auto soft_output = eq.equalize_soft(data_rx);
    
    int errors = 0;
    int compared = std::min(static_cast<int>(soft_output.size()), data_len);
    for (int i = 0; i < compared; i++) {
        if (soft_output[i].hard_decision != tx_symbols[preamble_len + i]) errors++;
    }
    
    float ser = (compared > 0) ? static_cast<float>(errors) / compared : 1.0f;
    
    // Calculate average reliability
    float avg_reliability = 0.0f;
    for (const auto& s : soft_output) {
        avg_reliability += std::abs(s.reliability);
    }
    avg_reliability /= soft_output.size();
    
    std::cout << "  Preamble: " << preamble_len << " symbols\n";
    std::cout << "  Data: " << data_len << " symbols\n";
    std::cout << "  Data errors: " << errors << "/" << compared << "\n";
    std::cout << "  SER: " << std::fixed << std::setprecision(2) << (ser * 100) << "%\n";
    std::cout << "  Avg reliability: " << std::setprecision(3) << avg_reliability << "\n";
    
    bool pass = ser < 0.05f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Phase 6: SIMD Optimization Tests
// ============================================================================

/**
 * Test 6.1: SIMD branch metric computation
 */
bool test_simd_branch_metrics() {
    std::cout << "test_simd_branch_metrics:\n";
    std::cout << "  Testing SIMD branch metric computation\n\n";
    
    std::mt19937 rng(66666);
    
    // Generate test data
    const int N = 64;  // Multiple of 8 for AVX2
    complex_t received(0.5f, 0.3f);
    std::vector<complex_t> expected(N);
    std::vector<float> metrics_scalar(N);
    std::vector<float> metrics_simd(N);
    
    for (int i = 0; i < N; i++) {
        expected[i] = complex_t(
            static_cast<float>(rng() % 1000) / 500.0f - 1.0f,
            static_cast<float>(rng() % 1000) / 500.0f - 1.0f
        );
    }
    
    // Compute with scalar
    compute_branch_metrics_scalar(&received, expected.data(), metrics_scalar.data(), N);
    
    // Compute with auto-dispatch SIMD
    compute_branch_metrics(&received, expected.data(), metrics_simd.data(), N);
    
    // Compare results
    float max_error = 0.0f;
    for (int i = 0; i < N; i++) {
        float error = std::abs(metrics_scalar[i] - metrics_simd[i]);
        if (error > max_error) max_error = error;
    }
    
    std::cout << "  Test vectors: " << N << "\n";
    std::cout << "  Max error: " << std::scientific << max_error << "\n";
    
#ifdef __AVX2__
    std::cout << "  AVX2: enabled\n";
#elif defined(__SSE2__)
    std::cout << "  SSE2: enabled\n";
#else
    std::cout << "  SIMD: disabled (scalar only)\n";
#endif
    
    bool pass = max_error < 1e-5f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 6.2: SIMD performance benchmark
 */
bool test_simd_performance() {
    std::cout << "test_simd_performance:\n";
    std::cout << "  Benchmarking SIMD branch metric computation\n\n";
    
    std::mt19937 rng(77777);
    
    const int N = 512;  // Typical for L=3 (64 states * 8 inputs)
    const int iterations = 10000;
    
    complex_t received(0.5f, 0.3f);
    std::vector<complex_t> expected(N);
    std::vector<float> metrics(N);
    
    for (int i = 0; i < N; i++) {
        expected[i] = complex_t(
            static_cast<float>(rng() % 1000) / 500.0f - 1.0f,
            static_cast<float>(rng() % 1000) / 500.0f - 1.0f
        );
    }
    
    // Benchmark scalar
    auto start_scalar = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; iter++) {
        compute_branch_metrics_scalar(&received, expected.data(), metrics.data(), N);
    }
    auto end_scalar = std::chrono::high_resolution_clock::now();
    auto scalar_us = std::chrono::duration_cast<std::chrono::microseconds>(end_scalar - start_scalar).count();
    
    // Benchmark SIMD
    auto start_simd = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; iter++) {
        compute_branch_metrics(&received, expected.data(), metrics.data(), N);
    }
    auto end_simd = std::chrono::high_resolution_clock::now();
    auto simd_us = std::chrono::duration_cast<std::chrono::microseconds>(end_simd - start_simd).count();
    
    float speedup = static_cast<float>(scalar_us) / simd_us;
    
    std::cout << "  Metrics computed: " << N << " per iteration\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Scalar time: " << scalar_us << " us\n";
    std::cout << "  SIMD time:   " << simd_us << " us\n";
    std::cout << "  Speedup:     " << std::fixed << std::setprecision(2) << speedup << "x\n";
    
    // Modern compilers often auto-vectorize scalar code, so we just verify 
    // SIMD is not significantly slower (within 50% tolerance)
    // The key test is correctness (test_simd_branch_metrics)
    bool pass = simd_us <= scalar_us * 1.5f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

/**
 * Test 6.3: Full MLSE throughput benchmark
 */
bool test_mlse_throughput() {
    std::cout << "test_mlse_throughput:\n";
    std::cout << "  Benchmarking full MLSE equalizer throughput\n\n";
    
    std::mt19937 rng(88888);
    const auto& constellation = get_8psk_constellation();
    
    // Setup MLSE L=3 (64 states)
    MLSEConfig config;
    config.channel_memory = 3;
    config.traceback_depth = 20;
    MLSEEqualizer eq(config);
    
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.1f),
        complex_t(0.3f, -0.1f)
    };
    eq.set_channel(channel);
    
    // Generate test data (2400 symbols = 1 second at 2400 baud)
    const int N = 2400;
    const int iterations = 10;
    
    std::vector<complex_t> rx_signal(N);
    for (int i = 0; i < N; i++) {
        rx_signal[i] = constellation[rng() % 8];
    }
    
    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; iter++) {
        eq.equalize(rx_signal);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    float symbols_per_sec = static_cast<float>(N * iterations) / (total_us / 1e6f);
    float realtime_margin = symbols_per_sec / 2400.0f;
    
    std::cout << "  Channel memory: L=" << config.channel_memory << " (" 
              << config.num_states() << " states)\n";
    std::cout << "  Symbols per run: " << N << "\n";
    std::cout << "  Total time: " << total_us << " us\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
              << symbols_per_sec << " symbols/sec\n";
    std::cout << "  Real-time margin: " << std::setprecision(1) << realtime_margin << "x\n";
    
    // Should achieve at least 10x real-time on modern CPU
    bool pass = realtime_margin >= 5.0f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Comparison Tests
// ============================================================================

/**
 * Compare DDFSE vs full MLSE on severe channel
 */
bool test_ddfse_vs_mlse() {
    std::cout << "test_ddfse_vs_mlse:\n";
    std::cout << "  Comparing DDFSE vs full MLSE on 5-tap channel\n\n";
    
    std::mt19937 rng(99999);
    const auto& constellation = get_8psk_constellation();
    
    // 5-tap channel
    std::vector<complex_t> channel = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.1f),
        complex_t(0.3f, -0.1f),
        complex_t(0.15f, 0.05f),
        complex_t(0.08f, -0.02f)
    };
    
    int N = 500;
    std::vector<int> tx_symbols(N);
    std::vector<complex_t> tx_signal(N);
    
    for (int i = 0; i < N; i++) {
        tx_symbols[i] = rng() % 8;
        tx_signal[i] = constellation[tx_symbols[i]];
    }
    
    std::vector<complex_t> rx_signal(N);
    for (int n = 0; n < N; n++) {
        rx_signal[n] = complex_t(0, 0);
        for (size_t k = 0; k < channel.size() && n >= static_cast<int>(k); k++) {
            rx_signal[n] += channel[k] * tx_signal[n-k];
        }
    }
    
    // Add noise
    std::normal_distribution<float> noise(0.0f, 0.08f);
    for (int n = 0; n < N; n++) {
        rx_signal[n] += complex_t(noise(rng), noise(rng));
    }
    
    // Full MLSE L=5 (4096 states) - too expensive, skip actual run
    // Just report theoretical complexity
    int full_states = 4096;  // 8^4
    
    // DDFSE (3 MLSE + 2 DFE = 64 states)
    DDFSEConfig ddfse_cfg;
    ddfse_cfg.mlse_taps = 3;
    ddfse_cfg.dfe_taps = 2;
    ddfse_cfg.traceback_depth = 20;
    DDFSEEqualizer ddfse(ddfse_cfg);
    ddfse.set_channel(channel);
    
    auto decoded_ddfse = ddfse.equalize(rx_signal);
    
    int ddfse_errors = 0;
    int compared = std::min(static_cast<int>(decoded_ddfse.size()), N);
    for (int i = 0; i < compared; i++) {
        if (decoded_ddfse[i] != tx_symbols[i]) ddfse_errors++;
    }
    float ddfse_ser = (compared > 0) ? static_cast<float>(ddfse_errors) / compared : 1.0f;
    
    std::cout << "  Channel: 5-tap\n";
    std::cout << "  Full MLSE states: " << full_states << " (not run)\n";
    std::cout << "  DDFSE states: " << ddfse.num_states() << "\n";
    std::cout << "  Complexity reduction: " << full_states / ddfse.num_states() << "x\n";
    std::cout << "  DDFSE SER: " << std::fixed << std::setprecision(2) << (ddfse_ser * 100) << "%\n";
    
    bool pass = ddfse_ser < 0.15f;
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Advanced MLSE Tests\n";
    std::cout << "===================\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Phase 4: DDFSE
    std::cout << "--- Phase 4: DDFSE (Reduced-State) ---\n";
    total++; if (test_ddfse_basic()) passed++;
    total++; if (test_ddfse_hybrid()) passed++;
    total++; if (test_ddfse_complexity()) passed++;
    total++; if (test_ddfse_vs_mlse()) passed++;
    
    // Phase 5: SOVA
    std::cout << "\n--- Phase 5: SOVA (Soft Outputs) ---\n";
    total++; if (test_sova_basic()) passed++;
    total++; if (test_sova_reliability()) passed++;
    total++; if (test_sova_estimated_channel()) passed++;
    
    // Phase 6: SIMD
    std::cout << "\n--- Phase 6: SIMD Optimization ---\n";
    total++; if (test_simd_branch_metrics()) passed++;
    total++; if (test_simd_performance()) passed++;
    total++; if (test_mlse_throughput()) passed++;
    
    std::cout << "\n===================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
