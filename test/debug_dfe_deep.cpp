/**
 * Deep Debug for DFE and Equalization
 * 
 * Check if DFE is actually being called and what it's doing
 */

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "api/modem.h"
#include "api/modem_rx.h"
#include "src/m110a/msdmt_decoder.h"
#include "src/modem/m110a_codec.h"
#include "src/m110a/mode_config.h"
#include "src/equalizer/dfe.h"
#include "src/equalizer/channel_estimator.h"
#include <iostream>
#include <iomanip>
#include <random>

using namespace m110a;
using namespace m110a::api;

int main() {
    std::cout << "=== Deep DFE Debug ===\n\n";
    
    // Generate test data
    std::vector<uint8_t> tx_data(50);
    std::mt19937 rng(44444);
    for (auto& b : tx_data) b = rng() & 0xFF;
    
    // Encode
    auto encode_result = encode(tx_data, Mode::M2400_SHORT);
    std::vector<float> rf = encode_result.value();
    
    std::cout << "TX samples: " << rf.size() << "\n";
    
    // Apply multipath
    int delay = 48;
    float gain = 0.5f;
    std::vector<float> rf_mp = rf;
    for (size_t i = delay; i < rf_mp.size(); i++) {
        rf_mp[i] += gain * rf_mp[i - delay];
    }
    
    std::cout << "Applied multipath: delay=" << delay << " samples, gain=" << gain << "\n\n";
    
    // Step 1: Manually decode the clean signal to get reference
    std::cout << "=== Step 1: Decode clean signal ===\n";
    {
        m110a::MSDMTDecoderConfig cfg;
        cfg.sample_rate = 48000.0f;
        cfg.carrier_freq = 1800.0f;
        cfg.baud_rate = 2400.0f;
        cfg.unknown_data_len = 32;
        cfg.known_data_len = 16;
        
        m110a::MSDMTDecoder decoder(cfg);
        auto result = decoder.decode(rf);
        
        std::cout << "Preamble found: " << (result.preamble_found ? "YES" : "NO") << "\n";
        std::cout << "Preamble symbols: " << result.preamble_symbols.size() << "\n";
        std::cout << "Data symbols: " << result.data_symbols.size() << "\n";
        std::cout << "Correlation: " << result.correlation << "\n\n";
        
        // Print first 5 preamble symbols
        std::cout << "First 5 preamble symbols:\n";
        for (int i = 0; i < 5 && i < (int)result.preamble_symbols.size(); i++) {
            complex_t s = result.preamble_symbols[i];
            float angle = std::atan2(s.imag(), s.real()) * 180.0f / M_PI;
            std::cout << "  [" << i << "] (" << std::fixed << std::setprecision(3)
                      << s.real() << ", " << s.imag() << ") = " << angle << "°\n";
        }
        
        // Print first 5 data symbols
        std::cout << "\nFirst 5 data symbols:\n";
        for (int i = 0; i < 5 && i < (int)result.data_symbols.size(); i++) {
            complex_t s = result.data_symbols[i];
            float angle = std::atan2(s.imag(), s.real()) * 180.0f / M_PI;
            std::cout << "  [" << i << "] (" << std::fixed << std::setprecision(3)
                      << s.real() << ", " << s.imag() << ") mag=" << std::abs(s) << "\n";
        }
    }
    
    // Step 2: Decode multipath signal
    std::cout << "\n=== Step 2: Decode multipath signal ===\n";
    {
        m110a::MSDMTDecoderConfig cfg;
        cfg.sample_rate = 48000.0f;
        cfg.carrier_freq = 1800.0f;
        cfg.baud_rate = 2400.0f;
        cfg.unknown_data_len = 32;
        cfg.known_data_len = 16;
        
        m110a::MSDMTDecoder decoder(cfg);
        auto result = decoder.decode(rf_mp);
        
        std::cout << "Preamble found: " << (result.preamble_found ? "YES" : "NO") << "\n";
        std::cout << "Preamble symbols: " << result.preamble_symbols.size() << "\n";
        std::cout << "Data symbols: " << result.data_symbols.size() << "\n";
        std::cout << "Correlation: " << result.correlation << "\n\n";
        
        // Print first 5 preamble symbols
        std::cout << "First 5 preamble symbols (should look different due to ISI):\n";
        for (int i = 0; i < 5 && i < (int)result.preamble_symbols.size(); i++) {
            complex_t s = result.preamble_symbols[i];
            float angle = std::atan2(s.imag(), s.real()) * 180.0f / M_PI;
            std::cout << "  [" << i << "] (" << std::fixed << std::setprecision(3)
                      << s.real() << ", " << s.imag() << ") = " << angle << "° mag=" << std::abs(s) << "\n";
        }
        
        // Print first 5 data symbols
        std::cout << "\nFirst 5 data symbols (should have ISI distortion):\n";
        for (int i = 0; i < 5 && i < (int)result.data_symbols.size(); i++) {
            complex_t s = result.data_symbols[i];
            float angle = std::atan2(s.imag(), s.real()) * 180.0f / M_PI;
            std::cout << "  [" << i << "] (" << std::fixed << std::setprecision(3)
                      << s.real() << ", " << s.imag() << ") mag=" << std::abs(s) << "\n";
        }
        
        // Step 3: Estimate channel from preamble
        std::cout << "\n=== Step 3: Channel estimation from preamble ===\n";
        
        auto expected = ChannelEstimator::generate_preamble_reference(288);
        
        ChannelEstimatorConfig est_cfg;
        est_cfg.num_taps = 5;
        est_cfg.normalize = true;
        
        ChannelEstimator estimator(est_cfg);
        // Use first 288 preamble symbols for estimation
        std::vector<complex_t> preamble_288(result.preamble_symbols.begin(),
                                             result.preamble_symbols.begin() + 
                                             std::min(288, (int)result.preamble_symbols.size()));
        
        auto channel = estimator.estimate(preamble_288, expected);
        
        std::cout << "Channel estimate: [";
        for (size_t i = 0; i < channel.taps.size(); i++) {
            std::cout << std::fixed << std::setprecision(3) 
                      << "(" << channel.taps[i].real() << ", " << channel.taps[i].imag() << ")";
            if (i < channel.taps.size() - 1) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "Delay spread: " << channel.delay_spread << " symbols\n";
        std::cout << "Valid: " << (channel.valid ? "YES" : "NO") << "\n";
        
        // Step 4: Manually apply DFE and check output
        std::cout << "\n=== Step 4: Manual DFE on first 10 symbols ===\n";
        
        DFE::Config dfe_cfg;
        dfe_cfg.ff_taps = 11;
        dfe_cfg.fb_taps = 5;
        dfe_cfg.mu_ff = 0.01f;
        dfe_cfg.mu_fb = 0.005f;
        
        DFE dfe(dfe_cfg);
        
        // Pre-train on preamble
        std::cout << "Pre-training on 288 preamble symbols...\n";
        for (int i = 0; i < std::min(288, (int)preamble_288.size()); i++) {
            dfe.process(preamble_288[i], expected[i], true);
        }
        
        std::cout << "DFE FF tap magnitudes after training: [";
        auto mags = dfe.ff_tap_magnitudes();
        for (size_t i = 0; i < mags.size(); i++) {
            std::cout << std::fixed << std::setprecision(3) << mags[i];
            if (i < mags.size() - 1) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "DFE converged: " << (dfe.is_converged() ? "YES" : "NO") << "\n\n";
        
        // Reset delay lines
        dfe.reset_delay_lines();
        
        // Process first 10 data symbols
        std::cout << "Processing first 10 data symbols:\n";
        std::cout << "  #   Input                  Output                 Delta\n";
        
        for (int i = 0; i < 10 && i < (int)result.data_symbols.size(); i++) {
            complex_t input = result.data_symbols[i];
            complex_t output = dfe.process(input, complex_t(0,0), false);
            
            float in_mag = std::abs(input);
            float out_mag = std::abs(output);
            float in_angle = std::atan2(input.imag(), input.real()) * 180.0f / M_PI;
            float out_angle = std::atan2(output.imag(), output.real()) * 180.0f / M_PI;
            
            std::cout << "  " << std::setw(2) << i 
                      << "  (" << std::fixed << std::setprecision(2)
                      << std::setw(6) << input.real() << "," << std::setw(6) << input.imag() << ")"
                      << "  (" << std::setw(6) << output.real() << "," << std::setw(6) << output.imag() << ")"
                      << "  mag: " << std::setprecision(2) << in_mag << " → " << out_mag
                      << "\n";
        }
    }
    
    return 0;
}
