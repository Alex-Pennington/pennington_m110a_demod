/**
 * Debug DFE Core Algorithm
 * 
 * Test DFE in isolation to understand why it's producing ~50% BER
 */

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "src/equalizer/dfe.h"
#include "src/modem/symbol_mapper.h"
#include <iostream>
#include <iomanip>
#include <random>

using namespace m110a;

int main() {
    std::cout << "=== Debug DFE Core Algorithm ===\n\n";
    
    SymbolMapper mapper;
    std::mt19937 rng(12345);
    
    // Generate a sequence of random 8-PSK symbols
    std::vector<complex_t> tx_symbols;
    for (int i = 0; i < 200; i++) {
        tx_symbols.push_back(mapper.map(rng() % 8));
    }
    
    std::cout << "Test 1: Clean channel (no ISI)\n";
    {
        DFE::Config cfg;
        cfg.ff_taps = 11;
        cfg.fb_taps = 5;
        cfg.mu_ff = 0.01f;
        cfg.mu_fb = 0.005f;
        DFE dfe(cfg);
        
        // Process symbols through DFE with training
        int errors = 0;
        for (int i = 0; i < static_cast<int>(tx_symbols.size()); i++) {
            complex_t out = dfe.process(tx_symbols[i], tx_symbols[i], true);
            
            // Check if output matches input
            float phase_in = std::arg(tx_symbols[i]);
            float phase_out = std::arg(out);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            
            if (phase_err > M_PI/8) errors++;  // Wrong sector
        }
        
        float ser = static_cast<float>(errors) / tx_symbols.size();
        std::cout << "  Training mode SER: " << ser << "\n";
        std::cout << "  Center tap after training: " << std::abs(dfe.ff_taps()[5]) << "\n";
        
        // Now test decision-directed (using trained taps)
        dfe.reset_delay_lines();
        errors = 0;
        for (int i = 0; i < static_cast<int>(tx_symbols.size()); i++) {
            complex_t out = dfe.process(tx_symbols[i], complex_t(0,0), false);
            
            // Check if output matches input
            float phase_in = std::arg(tx_symbols[i]);
            float phase_out = std::arg(out);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            
            if (phase_err > M_PI/8) errors++;
        }
        
        float ser_dd = static_cast<float>(errors) / tx_symbols.size();
        std::cout << "  Decision-directed SER: " << ser_dd << "\n\n";
    }
    
    std::cout << "Test 2: Multipath channel h = [1.0, 0.5*exp(j*pi/4)] with 1 symbol delay\n";
    {
        // Create multipath channel: direct + delayed reflection
        std::vector<complex_t> rx_symbols(tx_symbols.size());
        complex_t h1 = std::polar(0.5f, static_cast<float>(M_PI/4));  // Reflection with phase shift
        
        for (size_t i = 0; i < tx_symbols.size(); i++) {
            rx_symbols[i] = tx_symbols[i];
            if (i > 0) {
                rx_symbols[i] += h1 * tx_symbols[i-1];  // ISI from previous symbol
            }
        }
        
        std::cout << "  RX[0]: " << rx_symbols[0] << " TX[0]: " << tx_symbols[0] << "\n";
        std::cout << "  RX[1]: " << rx_symbols[1] << " TX[1]: " << tx_symbols[1] << "\n";
        std::cout << "  RX[2]: " << rx_symbols[2] << " TX[2]: " << tx_symbols[2] << "\n";
        
        // Test A: Without equalizer
        int errors_none = 0;
        for (size_t i = 0; i < rx_symbols.size(); i++) {
            // Hard decision on received symbol
            float phase_in = std::arg(tx_symbols[i]);
            float phase_out = std::arg(rx_symbols[i]);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            if (phase_err > M_PI/8) errors_none++;
        }
        float ser_none = static_cast<float>(errors_none) / rx_symbols.size();
        std::cout << "  Without equalizer SER: " << ser_none << "\n";
        
        // Test B: With DFE (training on all symbols)
        DFE::Config cfg;
        cfg.ff_taps = 11;
        cfg.fb_taps = 5;
        cfg.mu_ff = 0.01f;
        cfg.mu_fb = 0.005f;
        DFE dfe(cfg);
        
        int errors_dfe = 0;
        for (size_t i = 0; i < rx_symbols.size(); i++) {
            complex_t out = dfe.process(rx_symbols[i], tx_symbols[i], true);
            
            // Check error
            float phase_in = std::arg(tx_symbols[i]);
            float phase_out = std::arg(out);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            if (phase_err > M_PI/8) errors_dfe++;
        }
        float ser_dfe_train = static_cast<float>(errors_dfe) / rx_symbols.size();
        std::cout << "  DFE (training) SER: " << ser_dfe_train << "\n";
        
        std::cout << "  FF taps after training:\n";
        for (int i = 0; i < cfg.ff_taps; i++) {
            std::cout << "    ff[" << i << "] = " << std::abs(dfe.ff_taps()[i]) 
                      << " @ " << std::arg(dfe.ff_taps()[i]) * 180/M_PI << " deg\n";
        }
        std::cout << "  FB taps after training:\n";
        for (int i = 0; i < cfg.fb_taps; i++) {
            std::cout << "    fb[" << i << "] = " << std::abs(dfe.fb_taps()[i])
                      << " @ " << std::arg(dfe.fb_taps()[i]) * 180/M_PI << " deg\n";
        }
        
        std::cout << "  Converged: " << (dfe.is_converged() ? "YES" : "NO") << "\n\n";
    }
    
    std::cout << "Test 3: Verify hard_decision magnitude\n";
    {
        DFE::Config cfg;
        DFE dfe(cfg);
        
        // Feed symbols with magnitude ~1.9 (typical after RRC filter)
        complex_t scaled_sym = std::polar(1.9f, static_cast<float>(M_PI/4));
        
        // Process in decision-directed mode
        complex_t out = dfe.process(scaled_sym, complex_t(0,0), false);
        
        std::cout << "  Input: " << scaled_sym << " (mag=" << std::abs(scaled_sym) << ")\n";
        std::cout << "  Output: " << out << " (mag=" << std::abs(out) << ")\n";
        
        // The hard decision inside DFE has unit magnitude
        // This creates a mismatch: we train with scaled symbols but
        // feedback uses unit magnitude decisions
    }
    
    std::cout << "\nTest 4: Pre-train then decision-directed on multipath\n";
    {
        // Generate training and test sequences
        std::vector<complex_t> train_tx, train_rx;
        std::vector<complex_t> test_tx, test_rx;
        
        complex_t h1 = std::polar(0.5f, static_cast<float>(M_PI/4));
        
        // Training sequence (100 symbols)
        for (int i = 0; i < 100; i++) {
            complex_t s = mapper.map(rng() % 8);
            train_tx.push_back(s);
            train_rx.push_back(s);
            if (i > 0) train_rx[i] += h1 * train_tx[i-1];
        }
        
        // Test sequence (100 symbols)
        for (int i = 0; i < 100; i++) {
            complex_t s = mapper.map(rng() % 8);
            test_tx.push_back(s);
            test_rx.push_back(s);
            if (i > 0) test_rx[i] += h1 * test_tx[i-1];
        }
        // First test symbol also gets ISI from last train symbol
        test_rx[0] += h1 * train_tx.back();
        
        DFE::Config cfg;
        cfg.ff_taps = 11;
        cfg.fb_taps = 5;
        cfg.mu_ff = 0.01f;
        cfg.mu_fb = 0.005f;
        DFE dfe(cfg);
        
        // Pre-train on training sequence
        float mse = dfe.train(train_rx, train_tx);
        std::cout << "  Training MSE: " << mse << "\n";
        std::cout << "  Converged: " << (dfe.is_converged() ? "YES" : "NO") << "\n";
        std::cout << "  Center tap: " << std::abs(dfe.ff_taps()[5]) << "\n";
        
        // Reset delay lines (but keep taps)
        dfe.reset_delay_lines();
        
        // Test in decision-directed mode
        int errors = 0;
        for (size_t i = 0; i < test_rx.size(); i++) {
            complex_t out = dfe.process(test_rx[i], complex_t(0,0), false);
            
            float phase_in = std::arg(test_tx[i]);
            float phase_out = std::arg(out);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            if (phase_err > M_PI/8) errors++;
        }
        
        float ser = static_cast<float>(errors) / test_rx.size();
        std::cout << "  Decision-directed SER: " << ser << "\n";
        
        // Without equalizer
        int errors_none = 0;
        for (size_t i = 0; i < test_rx.size(); i++) {
            float phase_in = std::arg(test_tx[i]);
            float phase_out = std::arg(test_rx[i]);
            float phase_err = std::abs(phase_out - phase_in);
            if (phase_err > M_PI) phase_err = 2*M_PI - phase_err;
            if (phase_err > M_PI/8) errors_none++;
        }
        float ser_none = static_cast<float>(errors_none) / test_rx.size();
        std::cout << "  Without equalizer SER: " << ser_none << "\n";
    }
    
    return 0;
}
