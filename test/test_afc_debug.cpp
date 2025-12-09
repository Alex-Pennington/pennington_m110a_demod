/**
 * AFC Debug Test - Direct Metric Analysis
 * 
 * Shows exactly what metric values each trial frequency produces
 */

#include "../api/modem.h"
#include "../src/m110a/msdmt_decoder.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace m110a;

void apply_freq_offset(std::vector<float>& samples, float offset_hz, float sample_rate = 48000.0f) {
    float phase = 0.0f;
    float phase_inc = 2.0f * 3.14159265f * offset_hz / sample_rate;
    for (float& s : samples) {
        s *= std::cos(phase);
        phase += phase_inc;
        if (phase > 6.28318f) phase -= 6.28318f;
    }
}

int main() {
    std::cout << "=== AFC Metric Debug Analysis ===\n\n";
    
    // Create 600S signal with known frequency offset
    float ACTUAL_OFFSET = 5.0f;  // Test with 5 Hz offset
    
    std::vector<uint8_t> test_data(64, 0x55);
    
    m110a_ModemTxConfig tx_cfg = {};
    tx_cfg.mode = M110A_MODE_600_BPSK_SHORT;
    
    m110a_ModemTx tx = {};
    m110a_modem_tx_init(&tx, tx_cfg);
    
    std::vector<float> tx_samples;
    tx_samples.resize(48000 * 5);  // 5 seconds
    
    size_t tx_count;
    m110a_modem_tx_transmit(&tx, test_data.data(), test_data.size(), tx_samples.data(), tx_samples.size(), &tx_count);
    tx_samples.resize(tx_count);
    
    // Apply frequency offset
    apply_freq_offset(tx_samples, ACTUAL_OFFSET);
    
    // Create decoder - use public API
    MSDMTDecoder decoder(48000.0f, 4, 2400.0f);  // 48kHz sample rate, 4 sps, 2400 symbols/sec
    
    // Test all trial frequencies from -10 to +10 Hz
    std::cout << "Actual Offset: " << ACTUAL_OFFSET << " Hz\n";
    std::cout << "Testing trial frequencies from -10 to +10 Hz:\n\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Trial Freq  Correlation  Error from Actual\n";
    std::cout << "----------  -----------  -----------------\n";
    
    float best_trial = 0.0f;
    float best_metric = 0.0f;
    
    for (float trial = -10.0f; trial <= 10.0f; trial += 0.5f) {
        // Downconvert with this trial frequency
        auto filtered = decoder.downconvert_and_filter_with_offset(tx_samples, trial);
        
        if (filtered.size() < 288 * 4) continue;
        
        // Get correlation metric
        float metric = decoder.quick_preamble_correlation(filtered, trial);
        
        float error = std::abs(trial - ACTUAL_OFFSET);
        
        std::cout << std::setw(10) << trial 
                  << std::setw(13) << metric
                  << std::setw(18) << error;
        
        if (trial == ACTUAL_OFFSET) {
            std::cout << "  <-- CORRECT";
        } else if (metric > best_metric) {
            std::cout << "  ** BEST **";
        }
        std::cout << "\n";
        
        if (metric > best_metric) {
            best_metric = metric;
            best_trial = trial;
        }
    }
    
    std::cout << "\n=== RESULT ===\n";
    std::cout << "Actual offset:   " << ACTUAL_OFFSET << " Hz\n";
    std::cout << "Best trial:      " << best_trial << " Hz\n";
    std::cout << "Error:           " << std::abs(best_trial - ACTUAL_OFFSET) << " Hz\n";
    std::cout << "Best metric:     " << best_metric << "\n";
    
    if (std::abs(best_trial - ACTUAL_OFFSET) < 0.1f) {
        std::cout << "\n✓ AFC WORKS - Found correct frequency!\n";
    } else {
        std::cout << "\n✗ AFC FAILS - Selected wrong frequency by " 
                  << std::abs(best_trial - ACTUAL_OFFSET) << " Hz\n";
    }
    
    return 0;
}
