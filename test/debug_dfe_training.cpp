/**
 * Debug DFE Pre-Training
 * 
 * Verify that DFE pre-training is working correctly on multipath channel
 */

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "api/modem.h"
#include "src/equalizer/channel_estimator.h"
#include "src/equalizer/dfe.h"
#include <iostream>
#include <iomanip>
#include <random>

using namespace m110a;
using namespace m110a::api;

/**
 * Apply 2-path static multipath (same as test_watterson_api Test 3)
 */
std::vector<float> apply_static_multipath(const std::vector<float>& rf, 
                                          int delay_samples, float path2_gain) {
    std::vector<float> output(rf.size());
    for (size_t i = 0; i < rf.size(); i++) {
        output[i] = rf[i];
        if (i >= static_cast<size_t>(delay_samples)) {
            output[i] += path2_gain * rf[i - delay_samples];
        }
    }
    return output;
}

/**
 * Apply 2-path multipath to complex baseband symbols
 */
std::vector<complex_t> apply_multipath_complex(const std::vector<complex_t>& symbols,
                                               int delay_samples, float path2_gain) {
    // At 2400 baud, 48kHz sample rate, sps=20
    // delay_samples=48 (1ms) = 2.4 symbols
    float delay_symbols = delay_samples / 20.0f;
    
    std::vector<complex_t> output(symbols.size());
    for (size_t i = 0; i < symbols.size(); i++) {
        output[i] = symbols[i];
        // Interpolate delay (fractional sample)
        int delay_int = static_cast<int>(delay_symbols);
        float delay_frac = delay_symbols - delay_int;
        
        if (i >= static_cast<size_t>(delay_int + 1)) {
            // Linear interpolation
            complex_t delayed = (1.0f - delay_frac) * symbols[i - delay_int] +
                               delay_frac * symbols[i - delay_int - 1];
            output[i] += path2_gain * delayed;
        }
    }
    return output;
}

int main() {
    std::cout << "================================================\n";
    std::cout << "DFE Pre-Training Debug\n";
    std::cout << "================================================\n\n";
    
    // Generate test data
    std::vector<uint8_t> tx_data(50);
    std::mt19937 rng(44444);
    for (auto& b : tx_data) b = rng() & 0xFF;
    
    // Encode
    auto encode_result = encode(tx_data, Mode::M2400_SHORT);
    if (!encode_result.ok()) {
        std::cout << "Encode failed\n";
        return 1;
    }
    
    // Apply static multipath
    int delay_samples = 48;  // 1 ms at 48 kHz = 2.4 symbols
    float path2_gain = 0.5f; // -6 dB
    
    std::vector<float> rf = encode_result.value();
    std::vector<float> rf_multipath = apply_static_multipath(rf, delay_samples, path2_gain);
    
    std::cout << "Channel Configuration:\n";
    std::cout << "  Delay: 1.0 ms (" << delay_samples << " samples, " 
              << (delay_samples / 20.0f) << " symbols)\n";
    std::cout << "  Path 2 gain: " << path2_gain << " (-6 dB)\n\n";
    
    // Expected channel at symbol rate: h[0]=1, h[2]=0.5 (approximately)
    std::cout << "Expected channel (symbol rate): [1.0, 0.0, 0.5, 0.0, 0.0]\n";
    std::cout << "  (Note: 2.4 symbol delay spreads energy between taps 2 and 3)\n\n";
    
    // ============================================================
    // Test 1: Estimate channel from preamble symbols
    // ============================================================
    std::cout << "--- Test 1: Channel Estimation from Signal ---\n";
    
    // We need to extract preamble symbols after multipath
    // For now, generate synthetic preamble with known multipath
    auto clean_preamble = ChannelEstimator::generate_preamble_reference(200);
    
    // Apply multipath to preamble symbols
    auto mp_preamble = apply_multipath_complex(clean_preamble, delay_samples, path2_gain);
    
    ChannelEstimatorConfig est_cfg;
    est_cfg.num_taps = 5;
    est_cfg.normalize = true;
    
    ChannelEstimator estimator(est_cfg);
    auto channel_est = estimator.estimate(mp_preamble, clean_preamble);
    
    std::cout << "Estimated channel: [";
    for (size_t i = 0; i < channel_est.taps.size(); i++) {
        std::cout << std::fixed << std::setprecision(3) 
                  << "(" << channel_est.taps[i].real() << ", " 
                  << channel_est.taps[i].imag() << ")";
        if (i < channel_est.taps.size() - 1) std::cout << ", ";
    }
    std::cout << "]\n";
    std::cout << "Delay spread: " << channel_est.delay_spread << " symbols\n\n";
    
    // ============================================================
    // Test 2: DFE convergence
    // ============================================================
    std::cout << "--- Test 2: DFE Convergence Test ---\n";
    
    DFE::Config dfe_cfg;
    dfe_cfg.ff_taps = 11;
    dfe_cfg.fb_taps = 5;
    dfe_cfg.mu_ff = 0.01f;
    dfe_cfg.mu_fb = 0.005f;
    
    DFE dfe(dfe_cfg);
    
    // Train on preamble
    std::cout << "Training DFE on 200 preamble symbols...\n";
    for (size_t i = 0; i < mp_preamble.size(); i++) {
        dfe.process(mp_preamble[i], clean_preamble[i], true);
    }
    
    // Check tap values
    std::cout << "Feedforward tap magnitudes after training:\n  ";
    auto ff_mags = dfe.ff_tap_magnitudes();
    for (size_t i = 0; i < ff_mags.size(); i++) {
        std::cout << std::fixed << std::setprecision(3) << ff_mags[i];
        if (i < ff_mags.size() - 1) std::cout << ", ";
    }
    std::cout << "\n";
    
    // Check convergence
    std::cout << "DFE converged: " << (dfe.is_converged() ? "YES" : "NO") << "\n\n";
    
    // ============================================================
    // Test 3: Process data symbols
    // ============================================================
    std::cout << "--- Test 3: Process Data Symbols ---\n";
    
    // Generate some random data symbols
    std::vector<complex_t> data_symbols(100);
    const float psk8_angles[] = {45, 90, 135, 180, 225, 270, 315, 0};
    for (size_t i = 0; i < data_symbols.size(); i++) {
        float angle = psk8_angles[rng() % 8] * M_PI / 180.0f;
        data_symbols[i] = complex_t(std::cos(angle), std::sin(angle));
    }
    
    // Apply multipath
    auto mp_data = apply_multipath_complex(data_symbols, delay_samples, path2_gain);
    
    // Equalize
    std::vector<complex_t> equalized;
    for (size_t i = 0; i < mp_data.size(); i++) {
        equalized.push_back(dfe.process(mp_data[i], complex_t(0,0), false));
    }
    
    // Measure symbol error rate
    int errors = 0;
    for (size_t i = 0; i < data_symbols.size(); i++) {
        // Hard decision
        float orig_angle = std::atan2(data_symbols[i].imag(), data_symbols[i].real());
        float eq_angle = std::atan2(equalized[i].imag(), equalized[i].real());
        
        int orig_sym = (static_cast<int>(std::round(orig_angle * 4 / M_PI)) + 8) % 8;
        int eq_sym = (static_cast<int>(std::round(eq_angle * 4 / M_PI)) + 8) % 8;
        
        if (orig_sym != eq_sym) errors++;
    }
    
    float ser = static_cast<float>(errors) / data_symbols.size();
    std::cout << "Symbol Error Rate after DFE: " << std::fixed << std::setprecision(2) 
              << (ser * 100) << "%\n";
    std::cout << "Expected: < 10% with proper training\n\n";
    
    // ============================================================
    // Test 4: Full decode path
    // ============================================================
    std::cout << "--- Test 4: Full Decode Path ---\n";
    
    auto decode_result = decode(rf_multipath);
    
    // Calculate BER
    int bit_errors = 0;
    int bit_count = 0;
    size_t n = std::min(tx_data.size(), decode_result.data.size());
    for (size_t i = 0; i < n; i++) {
        uint8_t diff = tx_data[i] ^ decode_result.data[i];
        while (diff) {
            bit_errors += diff & 1;
            diff >>= 1;
        }
        bit_count += 8;
    }
    
    float ber = (bit_count > 0) ? static_cast<float>(bit_errors) / bit_count : 1.0f;
    
    std::cout << "Mode detected: " << mode_name(decode_result.mode) << "\n";
    std::cout << "BER: " << std::scientific << ber << "\n";
    std::cout << "Decoded " << decode_result.data.size() << " bytes, expected " 
              << tx_data.size() << "\n";
    
    bool pass = (ber < 0.10f);
    std::cout << "Result: " << (pass ? "PASS" : "FAIL") << "\n\n";
    
    if (!pass) {
        std::cout << "=== DIAGNOSIS ===\n";
        std::cout << "If Test 2 shows DFE converges but Test 4 fails:\n";
        std::cout << "  - Check that preamble symbols are being passed correctly\n";
        std::cout << "  - Verify timing alignment between preamble and data\n";
        std::cout << "  - The multipath channel may be too severe for DFE\n";
        std::cout << "If Test 2 shows DFE does NOT converge:\n";
        std::cout << "  - Step size (mu) may be too small\n";
        std::cout << "  - Training length may be insufficient\n";
    }
    
    return pass ? 0 : 1;
}
