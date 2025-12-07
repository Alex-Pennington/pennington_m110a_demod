/**
 * Minimal DFE Test for Static Multipath
 * 
 * Tests DFE on the exact same multipath channel as test_watterson_api Test 3
 * to understand why it's not helping.
 */

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "api/modem.h"
#include "src/equalizer/dfe.h"
#include <iostream>
#include <iomanip>
#include <random>

using namespace m110a;
using namespace m110a::api;

int main() {
    std::cout << "=== Minimal DFE Static Multipath Test ===\n\n";
    
    // Generate test data (same as watterson test)
    std::vector<uint8_t> tx_data(50);
    std::mt19937 rng(44444);
    for (auto& b : tx_data) b = rng() & 0xFF;
    
    // Test 1: Clean channel (should pass)
    std::cout << "Test 1: Clean Channel (reference)\n";
    {
        auto encode_result = encode(tx_data, Mode::M2400_SHORT);
        std::vector<float> rf = encode_result.value();
        
        auto decode_result = decode(rf);
        
        int errors = 0;
        for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
            uint8_t diff = tx_data[i] ^ decode_result.data[i];
            while (diff) { errors += diff & 1; diff >>= 1; }
        }
        float ber = errors / (8.0f * tx_data.size());
        std::cout << "  BER: " << std::scientific << ber << "\n";
        std::cout << "  Result: " << (ber < 0.01 ? "PASS" : "FAIL") << "\n\n";
    }
    
    // Test 2: With multipath but NO DFE (Equalizer::NONE)
    std::cout << "Test 2: Multipath + NO DFE (reference)\n";
    {
        auto encode_result = encode(tx_data, Mode::M2400_SHORT);
        std::vector<float> rf = encode_result.value();
        
        // Apply multipath
        int delay = 48;
        float gain = 0.5f;
        for (size_t i = delay; i < rf.size(); i++) {
            rf[i] += gain * rf[i - delay];
        }
        
        RxConfig cfg;
        cfg.equalizer = Equalizer::NONE;
        auto decode_result = decode(rf, cfg);
        
        int errors = 0;
        for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
            uint8_t diff = tx_data[i] ^ decode_result.data[i];
            while (diff) { errors += diff & 1; diff >>= 1; }
        }
        float ber = errors / (8.0f * tx_data.size());
        std::cout << "  BER: " << std::scientific << ber << " (Equalizer::NONE)\n\n";
    }
    
    // Test 3: With multipath + DFE (default)
    std::cout << "Test 3: Multipath + DFE (default config)\n";
    {
        auto encode_result = encode(tx_data, Mode::M2400_SHORT);
        std::vector<float> rf = encode_result.value();
        
        // Apply multipath
        int delay = 48;
        float gain = 0.5f;
        for (size_t i = delay; i < rf.size(); i++) {
            rf[i] += gain * rf[i - delay];
        }
        
        RxConfig cfg;
        cfg.equalizer = Equalizer::DFE;
        auto decode_result = decode(rf, cfg);
        
        int errors = 0;
        for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
            uint8_t diff = tx_data[i] ^ decode_result.data[i];
            while (diff) { errors += diff & 1; diff >>= 1; }
        }
        float ber = errors / (8.0f * tx_data.size());
        std::cout << "  BER: " << std::scientific << ber << " (Equalizer::DFE)\n";
        std::cout << "  Expected: Lower than NO DFE\n\n";
    }
    
    // Test 4: With multipath + MLSE_L3
    std::cout << "Test 4: Multipath + MLSE_L3\n";
    {
        auto encode_result = encode(tx_data, Mode::M2400_SHORT);
        std::vector<float> rf = encode_result.value();
        
        // Apply multipath
        int delay = 48;
        float gain = 0.5f;
        for (size_t i = delay; i < rf.size(); i++) {
            rf[i] += gain * rf[i - delay];
        }
        
        RxConfig cfg;
        cfg.equalizer = Equalizer::MLSE_L3;
        auto decode_result = decode(rf, cfg);
        
        int errors = 0;
        for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
            uint8_t diff = tx_data[i] ^ decode_result.data[i];
            while (diff) { errors += diff & 1; diff >>= 1; }
        }
        float ber = errors / (8.0f * tx_data.size());
        std::cout << "  BER: " << std::scientific << ber << " (Equalizer::MLSE_L3)\n\n";
    }
    
    // Test 5: Different multipath delays
    std::cout << "Test 5: DFE vs multipath delay\n";
    std::cout << "  Delay(samples)  Delay(symbols)  BER(NONE)  BER(DFE)  BER(MLSE)\n";
    std::cout << "  --------------------------------------------------------\n";
    
    for (int delay : {10, 20, 30, 40, 48, 60, 80}) {
        float delay_symbols = delay / 20.0f;
        float ber_none, ber_dfe, ber_mlse;
        
        // NO DFE
        {
            auto encode_result = encode(tx_data, Mode::M2400_SHORT);
            std::vector<float> rf = encode_result.value();
            for (size_t i = delay; i < rf.size(); i++) rf[i] += 0.5f * rf[i - delay];
            
            RxConfig cfg; cfg.equalizer = Equalizer::NONE;
            auto decode_result = decode(rf, cfg);
            
            int errors = 0;
            for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
                uint8_t diff = tx_data[i] ^ decode_result.data[i];
                while (diff) { errors += diff & 1; diff >>= 1; }
            }
            ber_none = errors / (8.0f * tx_data.size());
        }
        
        // DFE
        {
            auto encode_result = encode(tx_data, Mode::M2400_SHORT);
            std::vector<float> rf = encode_result.value();
            for (size_t i = delay; i < rf.size(); i++) rf[i] += 0.5f * rf[i - delay];
            
            RxConfig cfg; cfg.equalizer = Equalizer::DFE;
            auto decode_result = decode(rf, cfg);
            
            int errors = 0;
            for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
                uint8_t diff = tx_data[i] ^ decode_result.data[i];
                while (diff) { errors += diff & 1; diff >>= 1; }
            }
            ber_dfe = errors / (8.0f * tx_data.size());
        }
        
        // MLSE
        {
            auto encode_result = encode(tx_data, Mode::M2400_SHORT);
            std::vector<float> rf = encode_result.value();
            for (size_t i = delay; i < rf.size(); i++) rf[i] += 0.5f * rf[i - delay];
            
            RxConfig cfg; cfg.equalizer = Equalizer::MLSE_L3;
            auto decode_result = decode(rf, cfg);
            
            int errors = 0;
            for (size_t i = 0; i < std::min(tx_data.size(), decode_result.data.size()); i++) {
                uint8_t diff = tx_data[i] ^ decode_result.data[i];
                while (diff) { errors += diff & 1; diff >>= 1; }
            }
            ber_mlse = errors / (8.0f * tx_data.size());
        }
        
        std::cout << "  " << std::setw(12) << delay 
                  << "  " << std::setw(14) << std::fixed << std::setprecision(2) << delay_symbols
                  << "  " << std::scientific << std::setprecision(2) << ber_none
                  << "  " << ber_dfe 
                  << "  " << ber_mlse << "\n";
    }
    
    return 0;
}
