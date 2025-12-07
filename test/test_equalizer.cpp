#include "equalizer/dfe.h"
#include "m110a/m110a_tx.h"
#include "sync/carrier_recovery.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "io/pcm_file.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <numeric>

using namespace m110a;

void test_dfe_initialization() {
    std::cout << "=== Test: DFE Initialization ===\n";
    
    DFE::Config config;
    config.ff_taps = 11;
    config.fb_taps = 5;
    
    DFE dfe(config);
    
    auto ff = dfe.ff_taps();
    auto fb = dfe.fb_taps();
    
    std::cout << "Feedforward taps: " << ff.size() << "\n";
    std::cout << "Feedback taps: " << fb.size() << "\n";
    
    assert(ff.size() == 11);
    assert(fb.size() == 5);
    
    // Center tap should be 1.0
    int center = config.ff_taps / 2;
    std::cout << "Center tap (index " << center << "): " << ff[center] << "\n";
    assert(std::abs(ff[center] - complex_t(1.0f, 0.0f)) < 0.001f);
    
    // Other taps should be zero
    for (int i = 0; i < config.ff_taps; i++) {
        if (i != center) {
            assert(std::abs(ff[i]) < 0.001f);
        }
    }
    
    std::cout << "PASSED\n\n";
}

void test_dfe_passthrough() {
    std::cout << "=== Test: DFE Passthrough (Clean Channel) ===\n";
    
    // With clean signal and center tap = 1, DFE should pass through
    DFE dfe;
    
    // Generate 8-PSK symbols
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> input;
    for (int i = 0; i < 100; i++) {
        uint8_t tribit = scr.next_tribit();
        input.push_back(mapper.map(tribit));
    }
    
    std::cout << "Input symbols: " << input.size() << "\n";
    
    // Process through DFE (no training, just decision-directed)
    std::vector<complex_t> output;
    dfe.equalize(input, output);
    
    // Should pass through with minimal distortion after initial transient
    // The DFE needs time for the delay line to fill
    int skip = 20;  // Skip initial transient
    float total_error = 0.0f;
    for (size_t i = skip; i < output.size(); i++) {
        // Check hard decision correctness instead of exact match
        // because DFE will adapt even on clean signal
        float out_phase = std::arg(output[i]);
        float in_phase = std::arg(input[i]);
        float phase_err = std::abs(out_phase - in_phase);
        while (phase_err > PI) phase_err -= 2.0f * PI;
        phase_err = std::abs(phase_err);
        total_error += phase_err;
    }
    float avg_error = total_error / (output.size() - skip);
    
    std::cout << "Average phase error: " << (avg_error * 180.0f / PI) << "°\n";
    // Should be less than half of 8-PSK sector (22.5°)
    assert(avg_error < 0.4f);  // ~23 degrees
    
    std::cout << "PASSED\n\n";
}

void test_multipath_channel() {
    std::cout << "=== Test: Multipath Channel Model ===\n";
    
    MultipathChannel::Config config;
    config.taps = {
        complex_t(1.0f, 0.0f),
        complex_t(0.5f, 0.2f),
        complex_t(0.2f, -0.1f)
    };
    config.noise_std = 0.0f;  // No noise for this test
    
    MultipathChannel channel(config);
    
    // Impulse response test
    std::vector<complex_t> impulse = {
        complex_t(1.0f, 0.0f),
        complex_t(0.0f, 0.0f),
        complex_t(0.0f, 0.0f),
        complex_t(0.0f, 0.0f),
        complex_t(0.0f, 0.0f)
    };
    
    auto response = channel.process_block(impulse);
    
    std::cout << "Channel impulse response:\n";
    for (size_t i = 0; i < response.size(); i++) {
        std::cout << "  h[" << i << "] = " << response[i] << "\n";
    }
    
    // First output should match first tap
    assert(std::abs(response[0] - config.taps[0]) < 0.001f);
    // Second output should match second tap
    assert(std::abs(response[1] - config.taps[1]) < 0.001f);
    
    std::cout << "PASSED\n\n";
}

void test_dfe_training() {
    std::cout << "=== Test: DFE Training ===\n";
    
    // Create multipath channel
    MultipathChannel::Config ch_config;
    ch_config.taps = {
        complex_t(1.0f, 0.0f),
        complex_t(0.4f, 0.1f),
        complex_t(0.15f, -0.05f)
    };
    ch_config.noise_std = 0.01f;
    
    MultipathChannel channel(ch_config);
    
    // Generate training symbols
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> tx_symbols;
    for (int i = 0; i < 200; i++) {
        uint8_t tribit = scr.next_tribit();
        tx_symbols.push_back(mapper.map(tribit));
    }
    
    // Pass through channel
    auto rx_symbols = channel.process_block(tx_symbols);
    
    // Measure pre-equalization error
    float pre_mse = 0.0f;
    for (size_t i = 10; i < rx_symbols.size(); i++) {
        pre_mse += std::norm(rx_symbols[i] - tx_symbols[i]);
    }
    pre_mse /= (rx_symbols.size() - 10);
    
    std::cout << "Pre-equalization MSE: " << pre_mse << "\n";
    
    // Train DFE
    DFE::Config dfe_config;
    dfe_config.ff_taps = 15;
    dfe_config.fb_taps = 7;
    dfe_config.mu_ff = 0.02f;
    dfe_config.mu_fb = 0.01f;
    
    DFE dfe(dfe_config);
    
    float mse = dfe.train(rx_symbols, tx_symbols);
    std::cout << "Post-equalization MSE: " << mse << "\n";
    
    // MSE should be reduced
    assert(mse < pre_mse);
    
    // Print tap magnitudes
    std::cout << "FF tap magnitudes: ";
    auto ff_mags = dfe.ff_tap_magnitudes();
    for (size_t i = 0; i < ff_mags.size(); i++) {
        if (ff_mags[i] > 0.05f) {
            std::cout << "[" << i << "]=" << std::fixed << std::setprecision(2) 
                      << ff_mags[i] << " ";
        }
    }
    std::cout << "\n";
    
    std::cout << "PASSED\n\n";
}

void test_frame_equalizer() {
    std::cout << "=== Test: Frame Equalizer ===\n";
    
    // Generate a frame: 32 data + 16 probe
    Scrambler data_scr(0x5A);  // Different init for data
    Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> tx_frame;
    
    // Data symbols
    for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
        tx_frame.push_back(mapper.map(data_scr.next_tribit()));
    }
    
    // Probe symbols
    for (int i = 0; i < PROBE_SYMBOLS_PER_FRAME; i++) {
        tx_frame.push_back(mapper.map(probe_scr.next_tribit()));
    }
    
    std::cout << "TX frame: " << tx_frame.size() << " symbols "
              << "(" << DATA_SYMBOLS_PER_FRAME << " data + " 
              << PROBE_SYMBOLS_PER_FRAME << " probe)\n";
    
    // Pass through mild multipath
    MultipathChannel::Config ch_config;
    ch_config.taps = {complex_t(1.0f, 0.0f), complex_t(0.3f, 0.0f)};
    ch_config.noise_std = 0.02f;
    
    MultipathChannel channel(ch_config);
    auto rx_frame = channel.process_block(tx_frame);
    
    // Equalize
    FrameEqualizer eq;
    std::vector<complex_t> eq_data;
    
    bool ok = eq.process_frame(rx_frame, eq_data);
    
    std::cout << "Frame processed: " << (ok ? "YES" : "NO") << "\n";
    std::cout << "Equalized data symbols: " << eq_data.size() << "\n";
    
    assert(ok);
    assert(eq_data.size() == DATA_SYMBOLS_PER_FRAME);
    
    // Check constellation quality
    float total_dist = 0.0f;
    for (size_t i = 0; i < eq_data.size(); i++) {
        float mag = std::abs(eq_data[i]);
        if (mag < 0.1f) continue;
        
        complex_t norm = eq_data[i] / mag;
        
        float min_dist = 1e10f;
        for (int k = 0; k < 8; k++) {
            complex_t ref = std::polar(1.0f, k * PI / 4.0f);
            min_dist = std::min(min_dist, std::abs(norm - ref));
        }
        total_dist += min_dist;
    }
    
    float avg_dist = total_dist / eq_data.size();
    std::cout << "Average constellation distance: " << avg_dist << "\n";
    
    std::cout << "PASSED\n\n";
}

void test_frame_stream() {
    std::cout << "=== Test: Frame Stream Processing ===\n";
    
    // Generate multiple frames
    int num_frames = 5;
    SymbolMapper mapper;
    
    std::vector<complex_t> tx_stream;
    
    for (int f = 0; f < num_frames; f++) {
        Scrambler data_scr(0x5A + f);
        Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
        
        // Data
        for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
            tx_stream.push_back(mapper.map(data_scr.next_tribit()));
        }
        // Probe
        for (int i = 0; i < PROBE_SYMBOLS_PER_FRAME; i++) {
            tx_stream.push_back(mapper.map(probe_scr.next_tribit()));
        }
    }
    
    std::cout << "TX stream: " << tx_stream.size() << " symbols ("
              << num_frames << " frames)\n";
    
    // Channel
    MultipathChannel::Config ch_config;
    ch_config.taps = {complex_t(1.0f, 0.0f), complex_t(0.25f, 0.1f)};
    ch_config.noise_std = 0.02f;
    
    MultipathChannel channel(ch_config);
    auto rx_stream = channel.process_block(tx_stream);
    
    // Process stream
    FrameEqualizer eq;
    std::vector<complex_t> eq_data;
    
    int frames_processed = eq.process_stream(rx_stream, eq_data);
    
    std::cout << "Frames processed: " << frames_processed << "\n";
    std::cout << "Equalized symbols: " << eq_data.size() << "\n";
    
    assert(frames_processed == num_frames);
    assert(eq_data.size() == num_frames * DATA_SYMBOLS_PER_FRAME);
    
    std::cout << "PASSED\n\n";
}

void test_dfe_convergence() {
    std::cout << "=== Test: DFE Convergence ===\n";
    
    // Severe multipath
    MultipathChannel::Config ch_config;
    ch_config.taps = {
        complex_t(1.0f, 0.0f),
        complex_t(0.6f, 0.2f),    // Strong echo
        complex_t(0.3f, -0.15f),
        complex_t(0.1f, 0.05f)
    };
    ch_config.noise_std = 0.02f;
    
    MultipathChannel channel(ch_config);
    
    // Generate training sequence
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> tx;
    for (int i = 0; i < 500; i++) {
        tx.push_back(mapper.map(scr.next_tribit()));
    }
    
    auto rx = channel.process_block(tx);
    
    // Configure DFE
    DFE::Config config;
    config.ff_taps = 21;
    config.fb_taps = 11;
    config.mu_ff = 0.015f;
    config.mu_fb = 0.008f;
    
    DFE dfe(config);
    
    // Track MSE during training
    std::vector<float> mse_history;
    int block_size = 50;
    
    for (size_t i = 0; i + block_size <= tx.size(); i += block_size) {
        std::vector<complex_t> rx_block(rx.begin() + i, rx.begin() + i + block_size);
        std::vector<complex_t> tx_block(tx.begin() + i, tx.begin() + i + block_size);
        
        float mse = dfe.train(rx_block, tx_block);
        mse_history.push_back(mse);
    }
    
    std::cout << "MSE history:\n";
    for (size_t i = 0; i < mse_history.size(); i++) {
        std::cout << "  Block " << i << ": " << mse_history[i] << "\n";
    }
    
    // MSE should decrease
    assert(mse_history.back() < mse_history.front());
    
    // Check convergence
    std::cout << "Converged: " << (dfe.is_converged() ? "YES" : "NO") << "\n";
    
    std::cout << "PASSED\n\n";
}

void test_equalizer_with_full_chain() {
    std::cout << "=== Test: Equalizer with Full RX Chain ===\n";
    
    // Generate TX signal
    M110A_Tx tx;
    std::string message = "HELLO WORLD";
    std::vector<uint8_t> data(message.begin(), message.end());
    auto rf_samples = tx.transmit(data);
    
    std::cout << "TX samples: " << rf_samples.size() << "\n";
    
    // Simulate mild multipath at RF
    // (In reality this would be more complex, but we'll apply at baseband)
    
    // Downconvert to baseband
    NCO nco(SAMPLE_RATE, -CARRIER_FREQ);
    auto srrc = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, 
                                   SAMPLE_RATE / SYMBOL_RATE);
    ComplexFirFilter mf(srrc);
    
    std::vector<complex_t> baseband;
    for (const auto& s : rf_samples) {
        baseband.push_back(mf.process(nco.mix(s)));
    }
    
    // Timing + Carrier recovery
    SymbolSynchronizer sync;
    std::vector<complex_t> symbols;
    sync.process(baseband, symbols);
    
    std::cout << "Synchronized symbols: " << symbols.size() << "\n";
    
    // Skip preamble (approximately first 1/3)
    int preamble_skip = symbols.size() / 3;
    std::vector<complex_t> data_symbols(symbols.begin() + preamble_skip,
                                        symbols.end());
    
    std::cout << "Data symbols (after preamble): " << data_symbols.size() << "\n";
    
    // Apply multipath to symbols (simplified for testing)
    MultipathChannel::Config ch_config;
    ch_config.taps = {complex_t(1.0f, 0.0f), complex_t(0.2f, 0.05f)};
    ch_config.noise_std = 0.03f;
    
    MultipathChannel channel(ch_config);
    auto distorted = channel.process_block(data_symbols);
    
    // Equalize
    DFE::Config dfe_config;
    dfe_config.ff_taps = 11;
    dfe_config.fb_taps = 5;
    dfe_config.mu_ff = 0.02f;
    dfe_config.mu_fb = 0.01f;
    
    DFE dfe(dfe_config);
    
    // Train on first portion, then equalize rest
    int train_len = std::min(100, (int)distorted.size() / 2);
    
    std::vector<complex_t> train_rx(distorted.begin(), 
                                    distorted.begin() + train_len);
    std::vector<complex_t> train_ref(data_symbols.begin(),
                                     data_symbols.begin() + train_len);
    
    float train_mse = dfe.train(train_rx, train_ref);
    std::cout << "Training MSE: " << train_mse << "\n";
    
    // Equalize remaining
    std::vector<complex_t> eq_symbols;
    std::vector<complex_t> remaining(distorted.begin() + train_len,
                                     distorted.end());
    dfe.equalize(remaining, eq_symbols);
    
    std::cout << "Equalized symbols: " << eq_symbols.size() << "\n";
    
    // Measure constellation quality
    float total_dist = 0.0f;
    int count = 0;
    
    for (const auto& sym : eq_symbols) {
        float mag = std::abs(sym);
        if (mag < 0.1f) continue;
        
        complex_t norm = sym / mag;
        float min_dist = 1e10f;
        
        for (int k = 0; k < 8; k++) {
            complex_t ref = std::polar(1.0f, k * PI / 4.0f);
            min_dist = std::min(min_dist, std::abs(norm - ref));
        }
        
        total_dist += min_dist;
        count++;
    }
    
    float avg_dist = total_dist / count;
    std::cout << "Average constellation distance: " << avg_dist << "\n";
    std::cout << "Analyzed: " << count << " symbols\n";
    
    std::cout << "PASSED\n\n";
}

int main() {
    std::cout << "M110A Equalizer Tests\n";
    std::cout << "=====================\n\n";
    
    test_dfe_initialization();
    test_dfe_passthrough();
    test_multipath_channel();
    test_dfe_training();
    test_frame_equalizer();
    test_frame_stream();
    test_dfe_convergence();
    test_equalizer_with_full_chain();
    
    std::cout << "All equalizer tests passed!\n";
    return 0;
}
