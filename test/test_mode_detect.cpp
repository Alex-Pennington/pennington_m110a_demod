/**
 * D1/D2 Mode Detection - Chunked Testing
 * 
 * Phase 2: Verify D1/D2 generation and extraction
 */

#include "m110a/mode_config.h"
#include "m110a/multimode_tx.h"
#include "m110a/multimode_rx.h"
#include "m110a/mode_detector.h"
#include "modem/scrambler.h"
#include "modem/multimode_mapper.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "common/constants.h"
#include "channel/awgn.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>
#include <random>

using namespace m110a;

// ============================================================================
// Phase 2.1: Verify TX generates D1/D2 in preamble
// ============================================================================

bool test_d1d2_in_preamble_symbols() {
    std::cout << "test_d1d2_in_preamble_symbols:\n";
    std::cout << "  Verifying D1/D2 are embedded in preamble correctly\n\n";
    
    // Test with M2400S (D1=6, D2=4)
    ModeId test_mode = ModeId::M2400S;
    const auto& cfg = ModeDatabase::get(test_mode);
    
    std::cout << "  Mode: " << cfg.name << " (D1=" << cfg.d1_sequence 
              << ", D2=" << cfg.d2_sequence << ")\n";
    
    // Generate preamble using TX
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = test_mode;
    MultiModeTx tx(tx_cfg);
    
    auto preamble = tx.generate_preamble();
    std::cout << "  Preamble symbols: " << preamble.size() << "\n";
    
    // Now check D1 region (frame 1, symbols 288-383)
    // We need to regenerate scrambler to know expected values
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    MultiModeMapper mapper(Modulation::PSK8);
    
    // Advance scrambler to D1 position (288 symbols into frame 1)
    for (int i = 0; i < 288; i++) {
        scr.next_tribit();
    }
    
    // Check D1 region: symbols 288-383 of frame 1
    std::cout << "\n  D1 region (frame 1, sym 288-335, first 10):\n";
    std::cout << "    Idx   Expected  Got       Match\n";
    
    int d1_matches = 0;
    for (int i = 0; i < 48; i++) {
        uint8_t scr_val = scr.next_tribit();
        uint8_t expected_tribit = (cfg.d1_sequence + scr_val) % 8;
        complex_t expected_sym = mapper.map(expected_tribit);
        complex_t actual_sym = preamble[288 + i];
        
        bool match = (std::abs(expected_sym - actual_sym) < 0.01f);
        if (match) d1_matches++;
        
        if (i < 10) {
            std::cout << "    " << std::setw(3) << i 
                      << "   " << expected_sym << "  " << actual_sym
                      << "   " << (match ? "✓" : "✗") << "\n";
        }
    }
    
    std::cout << "  D1 matches: " << d1_matches << "/48\n";
    
    // Continue scrambler for rest of frame 1 (symbols 336-479)
    for (int i = 0; i < 48; i++) {
        scr.next_tribit();  // D1 repeated
    }
    for (int i = 0; i < 96; i++) {
        scr.next_tribit();  // Scrambled sync
    }
    
    // Check D2 region: symbols 0-95 of frame 2 (= symbols 480-575 overall)
    std::cout << "\n  D2 region (frame 2, sym 0-47, first 10):\n";
    std::cout << "    Idx   Expected  Got       Match\n";
    
    int d2_matches = 0;
    for (int i = 0; i < 48; i++) {
        uint8_t scr_val = scr.next_tribit();
        uint8_t expected_tribit = (cfg.d2_sequence + scr_val) % 8;
        complex_t expected_sym = mapper.map(expected_tribit);
        complex_t actual_sym = preamble[480 + i];
        
        bool match = (std::abs(expected_sym - actual_sym) < 0.01f);
        if (match) d2_matches++;
        
        if (i < 10) {
            std::cout << "    " << std::setw(3) << i 
                      << "   " << expected_sym << "  " << actual_sym
                      << "   " << (match ? "✓" : "✗") << "\n";
        }
    }
    
    std::cout << "  D2 matches: " << d2_matches << "/48\n";
    
    bool pass = (d1_matches == 48 && d2_matches == 48);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Phase 2.2: Extract D1/D2 from known preamble symbols
// ============================================================================

struct D1D2Result {
    int d1;
    int d2;
    int d1_votes[8];
    int d2_votes[8];
};

D1D2Result extract_d1d2_from_symbols(const std::vector<complex_t>& preamble_symbols) {
    D1D2Result result = {};
    
    // Initialize vote counters
    for (int i = 0; i < 8; i++) {
        result.d1_votes[i] = 0;
        result.d2_votes[i] = 0;
    }
    
    // Regenerate scrambler
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    
    // Advance to D1 position (symbol 288)
    for (int i = 0; i < 288; i++) {
        scr.next_tribit();
    }
    
    // Extract D1 from symbols 288-383 (96 symbols)
    for (int i = 0; i < 96; i++) {
        uint8_t scr_val = scr.next_tribit();
        
        // Demodulate symbol to index (hard decision)
        complex_t sym = preamble_symbols[288 + i];
        float angle = std::atan2(sym.imag(), sym.real());
        if (angle < 0) angle += 2.0f * M_PI;
        int sym_idx = static_cast<int>(std::round(angle / (M_PI / 4))) % 8;
        
        // Recover D1: d1 = (sym_idx - scr_val) mod 8
        int d1_est = (sym_idx - scr_val + 8) % 8;
        result.d1_votes[d1_est]++;
    }
    
    // Continue scrambler for rest of frame 1
    for (int i = 0; i < 96; i++) {
        scr.next_tribit();  // symbols 384-479
    }
    
    // Extract D2 from symbols 480-575 (96 symbols)
    for (int i = 0; i < 96; i++) {
        uint8_t scr_val = scr.next_tribit();
        
        // Demodulate symbol to index
        complex_t sym = preamble_symbols[480 + i];
        float angle = std::atan2(sym.imag(), sym.real());
        if (angle < 0) angle += 2.0f * M_PI;
        int sym_idx = static_cast<int>(std::round(angle / (M_PI / 4))) % 8;
        
        // Recover D2
        int d2_est = (sym_idx - scr_val + 8) % 8;
        result.d2_votes[d2_est]++;
    }
    
    // Find majority vote
    result.d1 = 0;
    result.d2 = 0;
    for (int i = 1; i < 8; i++) {
        if (result.d1_votes[i] > result.d1_votes[result.d1]) result.d1 = i;
        if (result.d2_votes[i] > result.d2_votes[result.d2]) result.d2 = i;
    }
    
    return result;
}

bool test_d1d2_extraction_clean() {
    std::cout << "test_d1d2_extraction_clean:\n";
    std::cout << "  Testing D1/D2 extraction from clean preamble\n\n";
    
    bool all_pass = true;
    
    // Test several modes
    ModeId test_modes[] = {
        ModeId::M150S, ModeId::M300S, ModeId::M600S, 
        ModeId::M1200S, ModeId::M2400S, ModeId::M4800S
    };
    
    std::cout << "  Mode      D1_exp  D1_got  D2_exp  D2_got  Status\n";
    std::cout << "  --------  ------  ------  ------  ------  ------\n";
    
    for (ModeId mode : test_modes) {
        const auto& cfg = ModeDatabase::get(mode);
        
        // Generate preamble
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = mode;
        MultiModeTx tx(tx_cfg);
        auto preamble = tx.generate_preamble();
        
        // Extract D1/D2
        auto result = extract_d1d2_from_symbols(preamble);
        
        bool d1_ok = (result.d1 == cfg.d1_sequence);
        bool d2_ok = (result.d2 == cfg.d2_sequence);
        bool pass = d1_ok && d2_ok;
        
        std::cout << "  " << std::setw(8) << cfg.name
                  << "  " << std::setw(6) << cfg.d1_sequence
                  << "  " << std::setw(6) << result.d1
                  << "  " << std::setw(6) << cfg.d2_sequence
                  << "  " << std::setw(6) << result.d2
                  << "  " << (pass ? "✓" : "FAIL") << "\n";
        
        if (!pass) all_pass = false;
    }
    
    std::cout << "\n  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

// ============================================================================
// Phase 2.3: D1/D2 extraction from RF signal (full chain)
// ============================================================================

bool test_d1d2_extraction_from_rf() {
    std::cout << "test_d1d2_extraction_from_rf:\n";
    std::cout << "  Testing D1/D2 extraction from RF signal\n\n";
    
    ModeId test_mode = ModeId::M2400S;
    const auto& cfg = ModeDatabase::get(test_mode);
    
    std::cout << "  Mode: " << cfg.name << " (D1=" << cfg.d1_sequence 
              << ", D2=" << cfg.d2_sequence << ")\n";
    
    // Generate preamble RF
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = test_mode;
    tx_cfg.sample_rate = 48000.0f;
    tx_cfg.carrier_freq = 1800.0f;
    MultiModeTx tx(tx_cfg);
    
    auto preamble_syms = tx.generate_preamble();
    auto rf = tx.modulate_at_rate(preamble_syms, 2400.0f);
    
    std::cout << "  TX: " << preamble_syms.size() << " symbols -> " 
              << rf.size() << " RF samples\n";
    
    // Downconvert and filter
    float sps = 48000.0f / 2400.0f;  // 20 samples/symbol
    int sps_int = static_cast<int>(sps);
    auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
    int filter_delay = (srrc_taps.size() - 1) / 2;
    
    NCO rx_nco(48000.0f, -1800.0f);
    ComplexFirFilter rx_filter(srrc_taps);
    
    std::vector<complex_t> baseband;
    for (float s : rf) {
        baseband.push_back(rx_filter.process(rx_nco.mix(complex_t(s, 0))));
    }
    
    // TX modulate_at_rate adds filter_taps samples at end for flush
    // The first symbol peak is at: filter_delay (RX) samples into the output
    // But TX SRRC also has delay, so total delay = 2 * filter_delay
    int start = 2 * filter_delay;
    
    std::vector<complex_t> rx_symbols;
    for (int i = start; i < static_cast<int>(baseband.size()); i += sps_int) {
        rx_symbols.push_back(baseband[i]);
    }
    
    std::cout << "  RX: " << rx_symbols.size() << " symbols recovered\n";
    std::cout << "  Filter delay: " << filter_delay << ", start sample: " << start << "\n";
    
    if (rx_symbols.size() < 600) {
        std::cout << "  ERROR: Not enough symbols\n";
        return false;
    }
    
    // Phase correction: use first few symbols to estimate phase offset
    // Preamble starts with scrambled sync - compare received vs expected
    Scrambler phase_scr(SCRAMBLER_INIT_PREAMBLE);
    MultiModeMapper mapper(Modulation::PSK8);
    
    // Estimate phase from first 20 symbols
    float phase_sum = 0.0f;
    int phase_count = 0;
    for (int i = 0; i < 20 && i < static_cast<int>(rx_symbols.size()); i++) {
        uint8_t expected_tribit = phase_scr.next_tribit();
        complex_t expected_sym = mapper.map(expected_tribit);
        complex_t received_sym = rx_symbols[i];
        
        // Phase difference
        float expected_angle = std::atan2(expected_sym.imag(), expected_sym.real());
        float received_angle = std::atan2(received_sym.imag(), received_sym.real());
        float diff = received_angle - expected_angle;
        
        // Wrap to [-pi, pi]
        while (diff > M_PI) diff -= 2 * M_PI;
        while (diff < -M_PI) diff += 2 * M_PI;
        
        phase_sum += diff;
        phase_count++;
    }
    
    float phase_offset = phase_sum / phase_count;
    std::cout << "  Estimated phase offset: " << (phase_offset * 180.0f / M_PI) << " degrees\n";
    
    // Apply phase correction to all symbols
    complex_t phase_corr = std::polar(1.0f, -phase_offset);
    for (auto& sym : rx_symbols) {
        sym *= phase_corr;
    }
    
    // Now extract D1/D2
    auto result = extract_d1d2_from_symbols(rx_symbols);
    
    std::cout << "  D1 votes: ";
    for (int i = 0; i < 8; i++) {
        std::cout << result.d1_votes[i] << " ";
    }
    std::cout << "\n  D2 votes: ";
    for (int i = 0; i < 8; i++) {
        std::cout << result.d2_votes[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "  D1: expected=" << cfg.d1_sequence << ", got=" << result.d1 << "\n";
    std::cout << "  D2: expected=" << cfg.d2_sequence << ", got=" << result.d2 << "\n";
    
    bool pass = (result.d1 == cfg.d1_sequence && result.d2 == cfg.d2_sequence);
    std::cout << "\n  Result: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ============================================================================
// Phase 4: D1/D2 to Mode Lookup
// ============================================================================

ModeId lookup_mode_from_d1d2(int d1, int d2) {
    // Build lookup table from mode database
    static std::map<std::pair<int,int>, ModeId> lookup;
    static bool initialized = false;
    
    if (!initialized) {
        for (ModeId mode : ModeDatabase::all_modes()) {
            const auto& cfg = ModeDatabase::get(mode);
            // Skip 75 bps modes (D1=D2=0, special case)
            if (cfg.d1_sequence == 0 && cfg.d2_sequence == 0) continue;
            
            auto key = std::make_pair(cfg.d1_sequence, cfg.d2_sequence);
            // Note: Some modes share D1/D2 (e.g., VOICE same as SHORT)
            // First match wins
            if (lookup.find(key) == lookup.end()) {
                lookup[key] = mode;
            }
        }
        initialized = true;
    }
    
    auto key = std::make_pair(d1, d2);
    auto it = lookup.find(key);
    if (it != lookup.end()) {
        return it->second;
    }
    
    return ModeId::M2400S;  // Default fallback
}

bool test_mode_lookup() {
    std::cout << "test_mode_lookup:\n";
    std::cout << "  Testing D1/D2 -> Mode lookup table\n\n";
    
    std::cout << "  D1  D2  -> Mode\n";
    std::cout << "  --  --  --------\n";
    
    // Test all valid D1/D2 combinations
    struct TestCase {
        int d1, d2;
        const char* expected;
    };
    
    TestCase cases[] = {
        {7, 4, "M150S"},
        {5, 4, "M150L"},
        {6, 7, "M300S"},
        {4, 7, "M300L"},
        {6, 6, "M600S"},
        {4, 6, "M600L"},
        {6, 5, "M1200S"},
        {4, 5, "M1200L"},
        {6, 4, "M2400S"},
        {4, 4, "M2400L"},
        {7, 6, "M4800S"},
    };
    
    bool all_pass = true;
    
    for (const auto& tc : cases) {
        ModeId detected = lookup_mode_from_d1d2(tc.d1, tc.d2);
        const auto& cfg = ModeDatabase::get(detected);
        
        bool pass = (cfg.name == tc.expected);
        
        std::cout << "  " << tc.d1 << "   " << tc.d2 
                  << "   " << std::setw(8) << cfg.name
                  << (pass ? " ✓" : " FAIL") << "\n";
        
        if (!pass) all_pass = false;
    }
    
    std::cout << "\n  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

// ============================================================================
// Phase 3: ModeDetector class test
// ============================================================================

bool test_mode_detector_class() {
    std::cout << "test_mode_detector_class:\n";
    std::cout << "  Testing ModeDetector class on all modes\n\n";
    
    ModeDetector detector;
    bool all_pass = true;
    
    ModeId test_modes[] = {
        ModeId::M150S, ModeId::M150L,
        ModeId::M300S, ModeId::M300L,
        ModeId::M600S, ModeId::M600L,
        ModeId::M1200S, ModeId::M1200L,
        ModeId::M2400S, ModeId::M2400L,
        ModeId::M4800S
    };
    
    std::cout << "  Mode      Detected  D1  D2  Conf\n";
    std::cout << "  --------  --------  --  --  ----\n";
    
    for (ModeId mode : test_modes) {
        const auto& cfg = ModeDatabase::get(mode);
        
        // Generate preamble
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = mode;
        MultiModeTx tx(tx_cfg);
        auto preamble = tx.generate_preamble();
        
        // Detect mode
        auto result = detector.detect(preamble);
        
        bool pass = result.detected && (result.mode == mode);
        
        std::cout << "  " << std::setw(8) << cfg.name
                  << "  " << std::setw(8) << (result.detected ? ModeDatabase::get(result.mode).name : "NONE")
                  << "  " << std::setw(2) << result.d1
                  << "  " << std::setw(2) << result.d2
                  << "  " << result.d1_confidence << "/" << result.d2_confidence
                  << (pass ? " ✓" : " FAIL") << "\n";
        
        if (!pass) all_pass = false;
    }
    
    std::cout << "\n  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

// ============================================================================
// Phase 3: ModeDetector with AWGN
// ============================================================================

bool test_mode_detector_with_noise() {
    std::cout << "test_mode_detector_with_noise:\n";
    std::cout << "  Testing ModeDetector robustness at various SNR\n\n";
    
    std::mt19937 rng(42);
    ModeDetector detector;
    
    float snr_points[] = {5.0f, 10.0f, 15.0f, 20.0f};
    
    std::cout << "  SNR(dB)  D1 Conf  D2 Conf  Detected\n";
    std::cout << "  -------  -------  -------  --------\n";
    
    bool all_high_snr_pass = true;
    
    for (float snr : snr_points) {
        // Generate M2400S preamble
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = ModeId::M2400S;
        tx_cfg.sample_rate = 48000.0f;
        tx_cfg.carrier_freq = 1800.0f;
        MultiModeTx tx(tx_cfg);
        
        auto preamble_syms = tx.generate_preamble();
        auto rf = tx.modulate_at_rate(preamble_syms, 2400.0f);
        
        // Add AWGN
        AWGNChannel channel(rng());
        channel.add_noise_snr(rf, snr);
        
        // Demodulate
        float sps = 48000.0f / 2400.0f;
        int sps_int = static_cast<int>(sps);
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
        int filter_delay = (srrc_taps.size() - 1) / 2;
        
        NCO rx_nco(48000.0f, -1800.0f);
        ComplexFirFilter rx_filter(srrc_taps);
        
        std::vector<complex_t> baseband;
        for (float s : rf) {
            baseband.push_back(rx_filter.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        int start = 2 * filter_delay;
        std::vector<complex_t> rx_symbols;
        for (int i = start; i < static_cast<int>(baseband.size()); i += sps_int) {
            rx_symbols.push_back(baseband[i]);
        }
        
        // Phase correction
        Scrambler phase_scr(SCRAMBLER_INIT_PREAMBLE);
        MultiModeMapper mapper(Modulation::PSK8);
        
        float phase_sum = 0.0f;
        for (int i = 0; i < 20 && i < static_cast<int>(rx_symbols.size()); i++) {
            uint8_t expected_tribit = phase_scr.next_tribit();
            complex_t expected_sym = mapper.map(expected_tribit);
            complex_t received_sym = rx_symbols[i];
            
            float expected_angle = std::atan2(expected_sym.imag(), expected_sym.real());
            float received_angle = std::atan2(received_sym.imag(), received_sym.real());
            float diff = received_angle - expected_angle;
            while (diff > M_PI) diff -= 2 * M_PI;
            while (diff < -M_PI) diff += 2 * M_PI;
            phase_sum += diff;
        }
        
        float phase_offset = phase_sum / 20.0f;
        complex_t phase_corr = std::polar(1.0f, -phase_offset);
        for (auto& sym : rx_symbols) {
            sym *= phase_corr;
        }
        
        // Detect mode
        auto result = detector.detect(rx_symbols);
        
        bool correct = result.detected && (result.mode == ModeId::M2400S);
        
        std::cout << "  " << std::setw(5) << snr
                  << "    " << std::setw(5) << result.d1_confidence
                  << "    " << std::setw(5) << result.d2_confidence
                  << "    " << (correct ? "M2400S ✓" : "FAIL") << "\n";
        
        // At high SNR (>= 15 dB), should always detect correctly
        if (snr >= 15.0f && !correct) {
            all_high_snr_pass = false;
        }
    }
    
    std::cout << "\n  Result: " << (all_high_snr_pass ? "PASS" : "FAIL") << "\n";
    return all_high_snr_pass;
}

// ============================================================================
// Phase 5: Integration Test
// ============================================================================

bool test_auto_detect_integration() {
    std::cout << "test_auto_detect_integration:\n";
    std::cout << "  Testing RX auto-detection of TX mode\n\n";
    
    std::mt19937 rng(123);
    bool all_pass = true;
    
    ModeId test_modes[] = {
        ModeId::M150S, ModeId::M300S, ModeId::M600S,
        ModeId::M1200S, ModeId::M2400S, ModeId::M4800S
    };
    
    std::cout << "  TX Mode   RX Detected  D1/D2 Conf  BER       Status\n";
    std::cout << "  --------  -----------  ----------  --------  ------\n";
    
    for (ModeId tx_mode : test_modes) {
        const auto& cfg = ModeDatabase::get(tx_mode);
        
        // Generate test data
        std::vector<uint8_t> tx_data(20);
        for (auto& b : tx_data) b = rng() & 0xFF;
        
        // TX
        MultiModeTx::Config tx_cfg;
        tx_cfg.mode = tx_mode;
        tx_cfg.sample_rate = 48000.0f;
        MultiModeTx tx(tx_cfg);
        auto tx_result = tx.transmit(tx_data);
        
        // RX with auto-detection (start with wrong mode to verify detection)
        MultiModeRx::Config rx_cfg;
        rx_cfg.mode = ModeId::M2400L;  // Start with different mode
        rx_cfg.sample_rate = 48000.0f;
        rx_cfg.auto_detect = true;     // Enable auto-detection
        rx_cfg.verbose = false;
        MultiModeRx rx(rx_cfg);
        
        auto rx_result = rx.decode(tx_result.rf_samples);
        
        // Calculate BER
        int errors = 0;
        size_t len = std::min(tx_data.size(), rx_result.data.size());
        for (size_t i = 0; i < len; i++) {
            uint8_t diff = tx_data[i] ^ rx_result.data[i];
            while (diff) { errors += diff & 1; diff >>= 1; }
        }
        float ber = (tx_data.size() > 0) ? 
            static_cast<float>(errors) / (tx_data.size() * 8) : 1.0f;
        
        bool mode_correct = (rx_result.detected_mode == tx_mode);
        bool data_ok = (ber < 0.01f);
        bool pass = rx_result.mode_detected && mode_correct && data_ok;
        
        std::cout << "  " << std::setw(8) << cfg.name
                  << "  " << std::setw(11) << (rx_result.mode_detected ? 
                      ModeDatabase::get(rx_result.detected_mode).name : "NONE")
                  << "  " << std::setw(2) << rx_result.d1_confidence 
                  << "/" << std::setw(2) << rx_result.d2_confidence
                  << "      " << std::scientific << std::setprecision(2) << ber
                  << "  " << (pass ? "✓" : "FAIL") << "\n";
        
        if (!pass) all_pass = false;
    }
    
    std::cout << "\n  Result: " << (all_pass ? "PASS" : "FAIL") << "\n";
    return all_pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "D1/D2 Mode Detection Tests\n";
    std::cout << "==========================\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Phase 2: D1/D2 generation verification
    std::cout << "--- Phase 2: D1/D2 Generation ---\n";
    total++; if (test_d1d2_in_preamble_symbols()) passed++;
    total++; if (test_d1d2_extraction_clean()) passed++;
    total++; if (test_d1d2_extraction_from_rf()) passed++;
    
    // Phase 3: ModeDetector class
    std::cout << "\n--- Phase 3: ModeDetector Class ---\n";
    total++; if (test_mode_detector_class()) passed++;
    total++; if (test_mode_detector_with_noise()) passed++;
    
    // Phase 4: Mode lookup
    std::cout << "\n--- Phase 4: Mode Lookup ---\n";
    total++; if (test_mode_lookup()) passed++;
    
    // Phase 5: Integration
    std::cout << "\n--- Phase 5: RX Integration ---\n";
    total++; if (test_auto_detect_integration()) passed++;
    
    std::cout << "\n==========================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
