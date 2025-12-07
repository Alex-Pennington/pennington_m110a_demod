#include "sync/carrier_recovery.h"
#include "sync/timing_recovery.h"
#include "sync/preamble_detector.h"
#include "m110a/m110a_tx.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "io/pcm_file.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <numeric>

using namespace m110a;

void test_phase_detector() {
    std::cout << "=== Test: 8-PSK Phase Detector ===\n";
    
    PhaseDetector8PSK pd;
    
    // Test ideal constellation points - should have zero error
    std::cout << "Testing ideal points:\n";
    for (int i = 0; i < 8; i++) {
        float phase = i * PI / 4.0f;
        complex_t symbol = std::polar(1.0f, phase);
        float error = pd.compute(symbol);
        
        std::cout << "  Point " << i << " (phase=" << (phase * 180.0f / PI) 
                  << "°): error=" << (error * 180.0f / PI) << "°\n";
        
        assert(std::abs(error) < 0.01f);
    }
    
    // Test with small phase offset
    float offset = 0.1f;  // ~5.7 degrees
    std::cout << "\nTesting with " << (offset * 180.0f / PI) << "° offset:\n";
    
    for (int i = 0; i < 8; i++) {
        float phase = i * PI / 4.0f + offset;
        complex_t symbol = std::polar(1.0f, phase);
        float error = pd.compute(symbol);
        
        // Error should be close to the offset
        assert(std::abs(error - offset) < 0.01f);
    }
    std::cout << "  All offsets detected correctly\n";
    
    // Test hard decision
    std::cout << "\nTesting hard decisions:\n";
    for (int i = 0; i < 8; i++) {
        float phase = i * PI / 4.0f + 0.1f;  // Small offset
        complex_t symbol = std::polar(1.0f, phase);
        int decision = pd.hard_decision(symbol);
        
        std::cout << "  Input phase " << i << " -> decision " << decision << "\n";
        assert(decision == i);
    }
    
    std::cout << "PASSED\n\n";
}

void test_loop_filter() {
    std::cout << "=== Test: Carrier Loop Filter ===\n";
    
    CarrierLoopFilter::Config config;
    config.bandwidth = 0.02f;
    config.damping = 0.707f;
    
    CarrierLoopFilter filter(config);
    
    // Apply constant phase error
    float constant_error = 0.1f;  // radians
    
    std::cout << "Applying constant error of " << constant_error << " rad:\n";
    for (int i = 0; i < 50; i++) {
        float output = filter.filter(constant_error);
        if (i < 10 || i >= 45) {
            std::cout << "  [" << i << "] output=" << output 
                      << " freq=" << filter.frequency_estimate() << "\n";
        }
    }
    
    // Frequency estimate should have accumulated
    std::cout << "Final frequency estimate: " << filter.frequency_hz() << " Hz\n";
    assert(filter.frequency_estimate() > 0.0f);
    
    std::cout << "PASSED\n\n";
}

void test_carrier_recovery_static() {
    std::cout << "=== Test: Carrier Recovery (Static Phase) ===\n";
    
    // Generate symbols with fixed phase offset
    float phase_offset = 0.3f;  // ~17 degrees
    
    std::vector<complex_t> input_symbols;
    for (int i = 0; i < 200; i++) {
        // Generate ideal 8-PSK symbol
        int point = i % 8;
        float phase = point * PI / 4.0f;
        complex_t ideal = std::polar(1.0f, phase);
        
        // Add fixed phase rotation
        complex_t rotated = ideal * std::polar(1.0f, phase_offset);
        input_symbols.push_back(rotated);
    }
    
    std::cout << "Input: 200 symbols with " << (phase_offset * 180.0f / PI) 
              << "° phase offset\n";
    
    // Process through carrier recovery
    CarrierRecovery cr;
    std::vector<complex_t> output;
    cr.process_block(input_symbols, output);
    
    // Check phase of output symbols after settling
    int skip = 50;
    float total_error = 0.0f;
    
    for (size_t i = skip; i < output.size(); i++) {
        int expected_point = i % 8;
        float expected_phase = expected_point * PI / 4.0f;
        
        float actual_phase = std::arg(output[i]);
        float error = actual_phase - expected_phase;
        
        // Wrap error
        while (error > PI) error -= 2.0f * PI;
        while (error < -PI) error += 2.0f * PI;
        
        total_error += std::abs(error);
    }
    
    float avg_error = total_error / (output.size() - skip);
    std::cout << "Average phase error after settling: " 
              << (avg_error * 180.0f / PI) << "°\n";
    std::cout << "Final phase estimate: " << (cr.phase() * 180.0f / PI) << "°\n";
    
    // Phase error should be small after recovery
    assert(avg_error < 0.2f);  // Less than ~11 degrees
    
    std::cout << "PASSED\n\n";
}

void test_carrier_recovery_frequency() {
    std::cout << "=== Test: Carrier Recovery (Frequency Offset) ===\n";
    
    // Generate symbols with frequency offset
    float freq_offset = 10.0f;  // 10 Hz at symbol rate
    float phase_per_symbol = 2.0f * PI * freq_offset / SYMBOL_RATE;
    
    std::vector<complex_t> input_symbols;
    float running_phase = 0.0f;
    
    for (int i = 0; i < 500; i++) {
        // Generate ideal 8-PSK symbol
        int point = i % 8;
        float phase = point * PI / 4.0f;
        complex_t ideal = std::polar(1.0f, phase);
        
        // Add running phase rotation (frequency offset)
        complex_t rotated = ideal * std::polar(1.0f, running_phase);
        input_symbols.push_back(rotated);
        
        running_phase += phase_per_symbol;
    }
    
    std::cout << "Input: 500 symbols with " << freq_offset << " Hz frequency offset\n";
    
    // Process through carrier recovery
    CarrierRecovery::Config config;
    config.loop_bandwidth = 0.03f;  // Wider bandwidth for faster acquisition
    
    CarrierRecovery cr(config);
    std::vector<complex_t> output;
    cr.process_block(input_symbols, output);
    
    std::cout << "Estimated frequency offset: " << cr.frequency_offset() << " Hz\n";
    std::cout << "Actual frequency offset: " << freq_offset << " Hz\n";
    
    // Check constellation quality after settling
    int skip = 200;  // Allow time for frequency acquisition
    float total_dist = 0.0f;
    
    for (size_t i = skip; i < output.size(); i++) {
        // Find distance to nearest constellation point
        float min_dist = 1e10f;
        for (int k = 0; k < 8; k++) {
            float ref_phase = k * PI / 4.0f;
            complex_t ref = std::polar(1.0f, ref_phase);
            float dist = std::abs(output[i] - ref);
            min_dist = std::min(min_dist, dist);
        }
        total_dist += min_dist;
    }
    
    float avg_dist = total_dist / (output.size() - skip);
    std::cout << "Average distance to constellation: " << avg_dist << "\n";
    
    // Should be close to constellation after frequency lock
    assert(avg_dist < 0.3f);
    
    std::cout << "PASSED\n\n";
}

void test_carrier_recovery_with_tx() {
    std::cout << "=== Test: Carrier Recovery with TX Signal ===\n";
    
    // Generate test signal with TX
    M110A_Tx tx;
    auto rf_samples = tx.generate_preamble(false);
    
    std::cout << "TX samples: " << rf_samples.size() << "\n";
    
    // Downconvert to baseband with small frequency offset
    float freq_offset = 5.0f;  // 5 Hz offset
    NCO downconvert_nco(SAMPLE_RATE, -CARRIER_FREQ - freq_offset);
    
    auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, 
                                        SAMPLE_RATE / SYMBOL_RATE);
    ComplexFirFilter matched_filter(srrc_taps);
    
    std::vector<complex_t> baseband;
    for (const auto& s : rf_samples) {
        complex_t bb = downconvert_nco.mix(s);
        complex_t filtered = matched_filter.process(bb);
        baseband.push_back(filtered);
    }
    
    std::cout << "Baseband samples: " << baseband.size() 
              << " (with " << freq_offset << " Hz offset)\n";
    
    // Timing recovery
    TimingRecovery::Config tr_config;
    tr_config.loop_bandwidth = 0.01f;
    TimingRecovery timing(tr_config);
    
    std::vector<complex_t> timed_symbols;
    timing.process_block(baseband, timed_symbols);
    
    std::cout << "After timing recovery: " << timed_symbols.size() << " symbols\n";
    
    // Carrier recovery
    CarrierRecovery::Config cr_config;
    cr_config.loop_bandwidth = 0.02f;
    CarrierRecovery carrier(cr_config);
    
    std::vector<complex_t> synced_symbols;
    carrier.process_block(timed_symbols, synced_symbols);
    
    std::cout << "After carrier recovery: " << synced_symbols.size() << " symbols\n";
    std::cout << "Estimated frequency: " << carrier.frequency_offset() << " Hz\n";
    std::cout << "Final phase: " << (carrier.phase() * 180.0f / PI) << "°\n";
    
    // Analyze constellation after settling
    int skip = synced_symbols.size() / 3;
    
    float total_dist = 0.0f;
    int count = 0;
    
    for (size_t i = skip; i < synced_symbols.size(); i++) {
        float mag = std::abs(synced_symbols[i]);
        if (mag < 0.1f) continue;
        
        // Normalize and find distance to nearest point
        complex_t normalized = synced_symbols[i] / mag;
        
        float min_dist = 1e10f;
        for (int k = 0; k < 8; k++) {
            float ref_phase = k * PI / 4.0f;
            complex_t ref = std::polar(1.0f, ref_phase);
            float dist = std::abs(normalized - ref);
            min_dist = std::min(min_dist, dist);
        }
        
        total_dist += min_dist;
        count++;
    }
    
    float avg_dist = total_dist / count;
    std::cout << "Average constellation error: " << avg_dist << "\n";
    std::cout << "Analyzed " << count << " symbols\n";
    
    // Print sample symbols
    std::cout << "Sample synced symbols:\n";
    for (int i = 0; i < 10 && skip + i < (int)synced_symbols.size(); i++) {
        size_t idx = skip + i;
        float mag = std::abs(synced_symbols[idx]);
        float phase = std::arg(synced_symbols[idx]) * 180.0f / PI;
        std::cout << "  [" << idx << "] mag=" << std::fixed << std::setprecision(3) 
                  << mag << " phase=" << std::setprecision(1) << phase << "°\n";
    }
    
    std::cout << "PASSED\n\n";
}

void test_symbol_synchronizer() {
    std::cout << "=== Test: Symbol Synchronizer ===\n";
    
    // Generate test signal
    M110A_Tx tx;
    auto rf_samples = tx.generate_preamble(false);
    
    // Downconvert to baseband
    NCO nco(SAMPLE_RATE, -CARRIER_FREQ);
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, 
                                   SAMPLE_RATE / SYMBOL_RATE);
    ComplexFirFilter mf(taps);
    
    std::vector<complex_t> baseband;
    for (const auto& s : rf_samples) {
        baseband.push_back(mf.process(nco.mix(s)));
    }
    
    // Use combined synchronizer with disabled timing loop for 48kHz (SPS=20)
    SymbolSynchronizer::Config config;
    config.timing_bandwidth = 0.0f;   // Disable loop at high SPS
    config.samples_per_symbol = 20.0f; // 48kHz / 2400 baud
    config.carrier_bandwidth = 0.02f;
    
    SymbolSynchronizer sync(config);
    
    std::vector<complex_t> symbols;
    int count = sync.process(baseband, symbols);
    
    std::cout << "Input: " << baseband.size() << " samples\n";
    std::cout << "Output: " << count << " symbols\n";
    std::cout << "Timing mu: " << sync.timing().mu() << "\n";
    std::cout << "Carrier phase: " << (sync.carrier().phase() * 180.0f / PI) << "°\n";
    std::cout << "Carrier freq: " << sync.carrier().frequency_offset() << " Hz\n";
    
    // Verify reasonable output
    float expected_symbols = baseband.size() / (SAMPLE_RATE / SYMBOL_RATE);
    float ratio = count / expected_symbols;
    std::cout << "Expected symbols: " << expected_symbols << "\n";
    std::cout << "Symbol ratio: " << ratio << "\n";
    std::cout << "Actual SPS observed: " << (float(baseband.size()) / count) << "\n";
    
    // At 48kHz, we should get very close to expected symbol count
    assert(ratio > 0.95f && ratio < 1.05f);
    
    std::cout << "PASSED\n\n";
}

void test_constellation_after_sync() {
    std::cout << "=== Test: Constellation Quality After Full Sync ===\n";
    
    // Generate and process through full chain
    M110A_Tx tx;
    
    // Use test pattern (has data, not just preamble)
    std::string message = "TEST MESSAGE 123";
    std::vector<uint8_t> data(message.begin(), message.end());
    auto rf_samples = tx.transmit(data);
    
    std::cout << "TX signal: " << rf_samples.size() << " samples\n";
    
    // Downconvert
    NCO nco(SAMPLE_RATE, -CARRIER_FREQ);
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, 
                                   SAMPLE_RATE / SYMBOL_RATE);
    ComplexFirFilter mf(taps);
    
    std::vector<complex_t> baseband;
    for (const auto& s : rf_samples) {
        baseband.push_back(mf.process(nco.mix(s)));
    }
    
    // Full synchronization
    SymbolSynchronizer sync;
    std::vector<complex_t> symbols;
    sync.process(baseband, symbols);
    
    std::cout << "Recovered " << symbols.size() << " symbols\n";
    
    // Skip preamble portion and analyze data
    int skip = symbols.size() / 2;  // Analyze second half
    
    // Compute constellation metrics
    std::vector<float> magnitudes;
    std::vector<float> phase_errors;
    
    for (size_t i = skip; i < symbols.size(); i++) {
        float mag = std::abs(symbols[i]);
        if (mag < 0.1f) continue;
        
        magnitudes.push_back(mag);
        
        // Phase error to nearest constellation point
        float phase = std::arg(symbols[i]);
        float sector = std::round(phase / (PI / 4.0f));
        float ideal = sector * (PI / 4.0f);
        float error = std::abs(phase - ideal);
        if (error > PI / 8.0f) error = PI / 4.0f - error;
        
        phase_errors.push_back(error);
    }
    
    // Compute statistics
    float avg_mag = std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0f) 
                    / magnitudes.size();
    float avg_phase_err = std::accumulate(phase_errors.begin(), phase_errors.end(), 0.0f)
                          / phase_errors.size();
    
    // RMS phase error
    float rms_phase_err = 0.0f;
    for (float e : phase_errors) {
        rms_phase_err += e * e;
    }
    rms_phase_err = std::sqrt(rms_phase_err / phase_errors.size());
    
    std::cout << "Symbol statistics (after settling):\n";
    std::cout << "  Analyzed: " << magnitudes.size() << " symbols\n";
    std::cout << "  Average magnitude: " << avg_mag << "\n";
    std::cout << "  Average phase error: " << (avg_phase_err * 180.0f / PI) << "°\n";
    std::cout << "  RMS phase error: " << (rms_phase_err * 180.0f / PI) << "°\n";
    
    // For 8-PSK, max allowable phase error before wrong decision is 22.5°
    // We should be well under that
    assert(rms_phase_err < 0.25f);  // Less than ~14 degrees RMS
    
    std::cout << "PASSED\n\n";
}

int main() {
    std::cout << "M110A Carrier Recovery Tests\n";
    std::cout << "============================\n\n";
    
    test_phase_detector();
    test_loop_filter();
    test_carrier_recovery_static();
    test_carrier_recovery_frequency();
    test_carrier_recovery_with_tx();
    test_symbol_synchronizer();
    test_constellation_after_sync();
    
    std::cout << "All carrier recovery tests passed!\n";
    return 0;
}
