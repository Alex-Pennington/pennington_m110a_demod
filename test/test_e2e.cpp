/**
 * End-to-End Modem Tests
 * 
 * Tests the complete signal chain:
 * TX: data → scramble → FEC → 8-PSK → SRRC → upconvert
 * RX: downconvert → SRRC → sample → demap → Viterbi → descramble
 */

#include "m110a/simple_tx.h"
#include "m110a/simple_rx.h"
#include "sync/preamble_detector.h"
#include "channel/multipath.h"
#include "dsp/fir_filter.h"
#include "dsp/nco.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include <iostream>
#include <cassert>
#include <random>

using namespace m110a;

// Test simple loopback
void test_simple_loopback() {
    std::cout << "test_simple_loopback... ";
    
    SimpleTx::Config tx_cfg;
    SimpleRx::Config rx_cfg;
    
    SimpleTx tx(tx_cfg);
    SimpleRx rx(rx_cfg);
    
    std::vector<std::string> messages = {
        "Hi",
        "Hello World",
        "The quick brown fox jumps over the lazy dog",
        "MIL-STD-188-110A Test"
    };
    
    for (const auto& msg : messages) {
        std::vector<uint8_t> data(msg.begin(), msg.end());
        auto result = tx.transmit(data);
        auto decoded = rx.decode(result.rf_samples, result.num_symbols);
        
        std::string dec_str(decoded.begin(), decoded.end());
        if (dec_str.length() > msg.length()) {
            dec_str = dec_str.substr(0, msg.length());
        }
        
        assert(dec_str == msg && "Message decode mismatch");
    }
    
    std::cout << "PASSED\n";
}

// Test with various timing offsets
void test_timing_offsets() {
    std::cout << "test_timing_offsets... ";
    
    float sps = 4.0f;
    auto srrc = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
    int filter_delay = srrc.size() - 1;
    int sps_int = 4;
    
    std::string msg = "Test";
    std::vector<int> offsets = {0, 1, 2, 3, 7, 15, 100};
    
    for (int offset : offsets) {
        // Encode
        std::vector<uint8_t> data(msg.begin(), msg.end());
        std::vector<uint8_t> bits;
        for (uint8_t b : data) {
            for (int i = 7; i >= 0; i--) bits.push_back((b >> i) & 1);
        }
        
        Scrambler tx_scr(SCRAMBLER_INIT_DATA);
        std::vector<uint8_t> scrambled;
        for (uint8_t b : bits) scrambled.push_back(b ^ tx_scr.next_bit());
        
        ConvEncoder enc;
        std::vector<uint8_t> coded;
        enc.encode(scrambled, coded, true);
        
        SymbolMapper mapper;
        std::vector<complex_t> symbols;
        std::vector<uint8_t> tribits;
        for (size_t i = 0; i + 2 < coded.size(); i += 3) {
            uint8_t t = (coded[i] << 2) | (coded[i+1] << 1) | coded[i+2];
            tribits.push_back(t);
            symbols.push_back(mapper.map(t));
        }
        
        // TX with offset
        ComplexFirFilter tx_filt(srrc);
        NCO tx_nco(9600.0f, 1800.0f);
        float gain = std::sqrt(sps);
        
        std::vector<float> rf;
        for (int i = 0; i < offset; i++) {
            rf.push_back(0.0f);
            tx_nco.next();
        }
        
        for (auto& s : symbols) {
            rf.push_back((tx_filt.process(s * gain) * tx_nco.next()).real());
            for (int i = 1; i < sps_int; i++) {
                rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
            }
        }
        for (size_t i = 0; i < srrc.size(); i++) {
            rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
        }
        
        // RX
        NCO rx_nco(9600.0f, -1800.0f);
        ComplexFirFilter rx_filt(srrc);
        
        std::vector<complex_t> filtered;
        for (float s : rf) {
            filtered.push_back(rx_filt.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        std::vector<complex_t> rx_syms;
        for (size_t sym = 0; sym < symbols.size(); sym++) {
            int idx = offset + filter_delay + sym * sps_int;
            if (idx < (int)filtered.size()) {
                rx_syms.push_back(filtered[idx]);
            }
        }
        
        // Decode
        complex_t prev(1.0f, 0.0f);
        std::vector<int> rx_tribits;
        for (auto& s : rx_syms) {
            complex_t d = s * std::conj(prev);
            float p = std::atan2(d.imag(), d.real());
            if (p < 0) p += 2 * PI;
            rx_tribits.push_back((int(std::round(p / (PI/4)))) % 8);
            prev = s;
        }
        
        int match = 0;
        for (size_t i = 0; i < tribits.size() && i < rx_tribits.size(); i++) {
            if (tribits[i] == rx_tribits[i]) match++;
        }
        
        assert(match == (int)tribits.size() && "Tribit mismatch with timing offset");
    }
    
    std::cout << "PASSED\n";
}

// Test preamble detection + data decode
void test_preamble_and_data() {
    std::cout << "test_preamble_and_data... ";
    
    float sps = 4.0f;
    auto srrc = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
    int filter_delay = srrc.size() - 1;
    int sps_int = 4;
    
    // Generate preamble
    Scrambler preamble_scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> preamble;
    for (int i = 0; i < 960; i++) {
        preamble.push_back(mapper.map(preamble_scr.next_tribit()));
    }
    
    // Generate data
    std::string msg = "Hello";
    std::vector<uint8_t> bits;
    for (char c : msg) {
        for (int i = 7; i >= 0; i--) bits.push_back((c >> i) & 1);
    }
    
    Scrambler tx_scr(SCRAMBLER_INIT_DATA);
    std::vector<uint8_t> scrambled;
    for (uint8_t b : bits) scrambled.push_back(b ^ tx_scr.next_bit());
    
    ConvEncoder enc;
    std::vector<uint8_t> coded;
    enc.encode(scrambled, coded, true);
    
    mapper.reset();
    std::vector<complex_t> data_syms;
    std::vector<uint8_t> tribits;
    for (size_t i = 0; i + 2 < coded.size(); i += 3) {
        uint8_t t = (coded[i] << 2) | (coded[i+1] << 1) | coded[i+2];
        tribits.push_back(t);
        data_syms.push_back(mapper.map(t));
    }
    
    // TX
    ComplexFirFilter tx_filt(srrc);
    NCO tx_nco(9600.0f, 1800.0f);
    float gain = std::sqrt(sps);
    
    std::vector<float> rf;
    int offset = 50;
    
    for (int i = 0; i < offset; i++) {
        rf.push_back(0.0f);
        tx_nco.next();
    }
    
    auto add_sym = [&](complex_t s) {
        rf.push_back((tx_filt.process(s * gain) * tx_nco.next()).real());
        for (int i = 1; i < sps_int; i++) {
            rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
        }
    };
    
    for (auto& s : preamble) add_sym(s);
    for (auto& s : data_syms) add_sym(s);
    for (size_t i = 0; i < srrc.size(); i++) add_sym(complex_t(0,0));
    
    // Preamble detection
    PreambleDetector::Config cfg;
    cfg.sample_rate = 9600.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.detection_threshold = 0.3f;
    cfg.confirmation_threshold = 0.3f;
    cfg.required_peaks = 2;
    
    PreambleDetector det(cfg);
    SyncResult sync;
    
    for (float s : rf) {
        sync = det.process_sample(s);
        if (sync.acquired) break;
    }
    
    assert(sync.acquired && "Preamble not detected");
    
    // Data decode
    int data_start = offset + preamble.size() * sps_int;
    
    NCO rx_nco(9600.0f, -1800.0f);
    ComplexFirFilter rx_filt(srrc);
    
    std::vector<complex_t> filtered;
    for (float s : rf) {
        filtered.push_back(rx_filt.process(rx_nco.mix(complex_t(s, 0))));
    }
    
    std::vector<complex_t> rx_syms;
    for (size_t i = 0; i < data_syms.size(); i++) {
        int idx = data_start + filter_delay + i * sps_int;
        if (idx < (int)filtered.size()) rx_syms.push_back(filtered[idx]);
    }
    
    complex_t prev(1.0f, 0.0f);
    std::vector<int> rx_tribits;
    for (auto& s : rx_syms) {
        complex_t d = s * std::conj(prev);
        float p = std::atan2(d.imag(), d.real());
        if (p < 0) p += 2 * PI;
        rx_tribits.push_back((int(std::round(p / (PI/4)))) % 8);
        prev = s;
    }
    
    int match = 0;
    for (size_t i = 0; i < tribits.size() && i < rx_tribits.size(); i++) {
        if (tribits[i] == rx_tribits[i]) match++;
    }
    
    assert(match == (int)tribits.size() && "Preamble+data decode failed");
    
    std::cout << "PASSED\n";
}

// Test frequency offset compensation
void test_freq_offset_compensation() {
    std::cout << "test_freq_offset_compensation... ";
    
    float sps = 4.0f;
    auto srrc = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
    int filter_delay = srrc.size() - 1;
    int sps_int = 4;
    
    std::string msg = "Test";
    std::vector<float> offsets = {0.0f, 10.0f, -10.0f, 25.0f, -25.0f, 40.0f};
    
    for (float freq_offset : offsets) {
        // Generate preamble
        Scrambler preamble_scr(SCRAMBLER_INIT_PREAMBLE);
        SymbolMapper mapper;
        
        std::vector<complex_t> preamble;
        for (int i = 0; i < 960; i++) {
            preamble.push_back(mapper.map(preamble_scr.next_tribit()));
        }
        
        // Generate data
        std::vector<uint8_t> bits;
        for (char c : msg) {
            for (int i = 7; i >= 0; i--) bits.push_back((c >> i) & 1);
        }
        
        Scrambler tx_scr(SCRAMBLER_INIT_DATA);
        std::vector<uint8_t> scrambled;
        for (uint8_t b : bits) scrambled.push_back(b ^ tx_scr.next_bit());
        
        ConvEncoder enc;
        std::vector<uint8_t> coded;
        enc.encode(scrambled, coded, true);
        
        mapper.reset();
        std::vector<complex_t> data_syms;
        std::vector<uint8_t> tribits;
        for (size_t i = 0; i + 2 < coded.size(); i += 3) {
            uint8_t t = (coded[i] << 2) | (coded[i+1] << 1) | coded[i+2];
            tribits.push_back(t);
            data_syms.push_back(mapper.map(t));
        }
        
        // TX with frequency offset
        ComplexFirFilter tx_filt(srrc);
        NCO tx_nco(9600.0f, 1800.0f + freq_offset);
        float gain = std::sqrt(sps);
        
        std::vector<float> rf;
        int offset = 50;
        for (int i = 0; i < offset; i++) {
            rf.push_back(0.0f);
            tx_nco.next();
        }
        
        for (auto& s : preamble) {
            rf.push_back((tx_filt.process(s * gain) * tx_nco.next()).real());
            for (int i = 1; i < sps_int; i++) {
                rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
            }
        }
        for (auto& s : data_syms) {
            rf.push_back((tx_filt.process(s * gain) * tx_nco.next()).real());
            for (int i = 1; i < sps_int; i++) {
                rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
            }
        }
        for (size_t i = 0; i < srrc.size(); i++) {
            rf.push_back((tx_filt.process(complex_t(0,0)) * tx_nco.next()).real());
        }
        
        // Frequency search preamble detection
        float best_corr = 0.0f;
        float best_freq = 0.0f;
        
        for (float search_freq = -50.0f; search_freq <= 50.0f; search_freq += 5.0f) {
            PreambleDetector::Config cfg;
            cfg.sample_rate = 9600.0f;
            cfg.carrier_freq = 1800.0f + search_freq;
            cfg.detection_threshold = 0.3f;
            cfg.confirmation_threshold = 0.3f;
            cfg.required_peaks = 2;
            
            PreambleDetector det(cfg);
            SyncResult sync;
            
            for (float s : rf) {
                sync = det.process_sample(s);
                if (sync.acquired && sync.correlation_peak > best_corr) {
                    best_corr = sync.correlation_peak;
                    best_freq = search_freq + sync.freq_offset_hz;
                    break;
                }
            }
        }
        
        assert(best_corr > 0.0f && "Preamble not detected");
        
        // Decode with compensation
        int data_start = offset + preamble.size() * sps_int;
        
        NCO rx_nco(9600.0f, -1800.0f - best_freq);
        ComplexFirFilter rx_filt(srrc);
        
        std::vector<complex_t> filtered;
        for (float s : rf) {
            filtered.push_back(rx_filt.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        std::vector<complex_t> rx_syms;
        for (size_t i = 0; i < data_syms.size(); i++) {
            int idx = data_start + filter_delay + i * sps_int;
            if (idx < (int)filtered.size()) rx_syms.push_back(filtered[idx]);
        }
        
        complex_t prev(1.0f, 0.0f);
        std::vector<int> rx_tribits;
        for (auto& s : rx_syms) {
            complex_t d = s * std::conj(prev);
            float p = std::atan2(d.imag(), d.real());
            if (p < 0) p += 2 * PI;
            rx_tribits.push_back((int(std::round(p / (PI/4)))) % 8);
            prev = s;
        }
        
        int match = 0;
        for (size_t i = 0; i < tribits.size() && i < rx_tribits.size(); i++) {
            if (tribits[i] == rx_tribits[i]) match++;
        }
        
        assert(match == (int)tribits.size() && "Freq offset decode failed");
    }
    
    std::cout << "PASSED\n";
}

// Test interleaver modes (ZERO, SHORT, LONG)
void test_interleave_modes() {
    std::cout << "test_interleave_modes... ";
    
    std::vector<InterleaveMode> modes = {
        InterleaveMode::ZERO,
        InterleaveMode::SHORT,
        InterleaveMode::LONG
    };
    
    for (auto mode : modes) {
        SimpleTx::Config tx_cfg;
        tx_cfg.interleave_mode = mode;
        
        SimpleRx::Config rx_cfg;
        rx_cfg.interleave_mode = mode;
        
        SimpleTx tx(tx_cfg);
        SimpleRx rx(rx_cfg);
        
        // Test message
        std::string msg = "Interleave test message";
        std::vector<uint8_t> data(msg.begin(), msg.end());
        
        auto result = tx.transmit(data);
        auto decoded = rx.decode(result.rf_samples, result.num_symbols);
        
        std::string dec_str(decoded.begin(), decoded.end());
        if (dec_str.length() > msg.length()) {
            dec_str = dec_str.substr(0, msg.length());
        }
        
        assert(dec_str == msg && "Interleave mode decode failed");
    }
    
    std::cout << "PASSED (ZERO, SHORT, LONG)\n";
}

// Test AWGN performance
void test_awgn_performance() {
    std::cout << "test_awgn_performance... ";
    
    // Test at Es/N0 = 12 dB - should be error-free with FEC
    std::mt19937 rng(42);
    
    SimpleTx::Config tx_cfg;
    tx_cfg.interleave_mode = InterleaveMode::SHORT;
    SimpleTx tx(tx_cfg);
    
    SimpleRx::Config rx_cfg;
    rx_cfg.interleave_mode = InterleaveMode::SHORT;
    SimpleRx rx(rx_cfg);
    
    // Test data
    std::string msg = "AWGN Test Message 12345";
    std::vector<uint8_t> tx_data(msg.begin(), msg.end());
    
    // Transmit
    auto tx_result = tx.transmit(tx_data);
    
    // Add AWGN at Es/N0 = 12 dB
    float es_n0_db = 12.0f;
    float signal_power = 0.0f;
    for (float s : tx_result.rf_samples) signal_power += s * s;
    signal_power /= tx_result.rf_samples.size();
    
    float es_n0_linear = std::pow(10.0f, es_n0_db / 10.0f);
    float noise_power = signal_power / es_n0_linear;
    float noise_std = std::sqrt(noise_power);
    
    std::normal_distribution<float> dist(0.0f, noise_std);
    for (float& s : tx_result.rf_samples) s += dist(rng);
    
    // Receive
    auto rx_data = rx.decode(tx_result.rf_samples, tx_result.num_symbols);
    
    // Verify
    std::string result(rx_data.begin(), rx_data.begin() + std::min(rx_data.size(), msg.size()));
    
    // At Es/N0 = 12 dB with FEC, should decode correctly
    assert(result == msg && "AWGN decode failed at Es/N0=12dB");
    
    std::cout << "PASSED\n";
}

// Test multipath channel performance
void test_multipath_channel() {
    std::cout << "test_multipath_channel... ";
    
    // Test with very mild two-ray multipath
    SimpleTx::Config tx_cfg;
    tx_cfg.interleave_mode = InterleaveMode::SHORT;
    SimpleTx tx(tx_cfg);
    
    SimpleRx::Config rx_cfg;
    rx_cfg.interleave_mode = InterleaveMode::SHORT;
    SimpleRx rx(rx_cfg);
    
    std::string msg = "Multipath Test";
    std::vector<uint8_t> tx_data(msg.begin(), msg.end());
    
    auto tx_result = tx.transmit(tx_data);
    
    // Apply very mild multipath (direct + 0.1ms delayed echo at 0.2 amplitude)
    MultipathRFChannel::Config ch_cfg;
    ch_cfg.sample_rate = 9600.0f;
    ch_cfg.taps = { 
        ChannelTap(0.0f, 1.0f, 0.0f),
        ChannelTap(0.1f, 0.2f, 30.0f)  // Reduced delay and amplitude
    };
    
    MultipathRFChannel channel(ch_cfg);
    auto rf_multipath = channel.process(tx_result.rf_samples);
    
    // Add some noise (Es/N0 = 18 dB)
    std::mt19937 rng(42);
    float signal_power = 0.0f;
    for (float s : rf_multipath) signal_power += s * s;
    signal_power /= rf_multipath.size();
    
    float noise_std = std::sqrt(signal_power / 63.0f);  // 18 dB
    std::normal_distribution<float> dist(0.0f, noise_std);
    for (float& s : rf_multipath) s += dist(rng);
    
    auto rx_data = rx.decode(rf_multipath, tx_result.num_symbols);
    
    std::string result(rx_data.begin(), rx_data.begin() + std::min(rx_data.size(), msg.size()));
    
    // Should decode correctly with mild multipath
    assert(result == msg && "Multipath decode failed");
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== End-to-End Tests ===\n";
    
    test_simple_loopback();
    test_timing_offsets();
    test_preamble_and_data();
    test_freq_offset_compensation();
    test_interleave_modes();
    test_awgn_performance();
    test_multipath_channel();
    
    std::cout << "\nAll E2E tests passed!\n";
    return 0;
}
