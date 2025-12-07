/**
 * Multi-Mode Support Tests
 * 
 * Tests for all MIL-STD-188-110A modes from 75 bps to 4800 bps.
 */

#include "m110a/mode_config.h"
#include "modem/multimode_mapper.h"
#include "modem/multimode_interleaver.h"
#include "m110a/multimode_tx.h"
#include "m110a/multimode_rx.h"
#include <iostream>
#include <random>
#include <cmath>

using namespace m110a;

std::mt19937 rng(42);

// ============================================================================
// Mode Configuration Tests
// ============================================================================

bool test_mode_database() {
    std::cout << "test_mode_database: ";
    
    // Test all modes can be retrieved
    auto modes = ModeDatabase::all_modes();
    
    for (ModeId id : modes) {
        const auto& cfg = ModeDatabase::get(id);
        
        // Verify basic properties
        if (cfg.bps <= 0 || cfg.symbol_rate <= 0) {
            std::cout << "FAIL (invalid config for " << cfg.name << ")\n";
            return false;
        }
        
        // Verify interleaver dimensions
        if (cfg.interleaver.rows <= 0 || cfg.interleaver.cols <= 0) {
            std::cout << "FAIL (invalid interleaver for " << cfg.name << ")\n";
            return false;
        }
    }
    
    // Test name lookup
    const auto& m2400s = ModeDatabase::get("M2400S");
    if (m2400s.bps != 2400 || m2400s.modulation != Modulation::PSK8) {
        std::cout << "FAIL (M2400S lookup)\n";
        return false;
    }
    
    std::cout << "PASS (" << modes.size() << " modes)\n";
    return true;
}

bool test_mode_parameters() {
    std::cout << "test_mode_parameters: ";
    
    // Verify specific mode parameters match modes.json
    const auto& m75ns = ModeDatabase::get(ModeId::M75NS);
    if (m75ns.bps != 75 || m75ns.modulation != Modulation::BPSK ||
        m75ns.interleaver.rows != 10 || m75ns.interleaver.cols != 9) {
        std::cout << "FAIL (M75NS params)\n";
        return false;
    }
    
    const auto& m2400l = ModeDatabase::get(ModeId::M2400L);
    if (m2400l.bps != 2400 || m2400l.modulation != Modulation::PSK8 ||
        m2400l.interleaver.rows != 40 || m2400l.interleaver.cols != 576) {
        std::cout << "FAIL (M2400L params)\n";
        return false;
    }
    
    const auto& m4800s = ModeDatabase::get(ModeId::M4800S);
    if (m4800s.bps != 4800 || m4800s.symbol_rate != 2400) {
        std::cout << "FAIL (M4800S params)\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// Multi-Mode Mapper Tests
// ============================================================================

bool test_bpsk_mapper() {
    std::cout << "test_bpsk_mapper: ";
    
    MultiModeMapper mapper(Modulation::BPSK);
    
    // Test absolute BPSK mapping
    // Bit 0 -> symbol 0 (0°), Bit 1 -> symbol 4 (180°)
    complex_t s0 = mapper.map(0);
    complex_t s1 = mapper.map(1);
    
    // Check phases
    float phase0 = std::arg(s0);
    float phase1 = std::arg(s1);
    
    // s0 should be near 0° (symbol 0)
    if (std::abs(phase0) > 0.1f) {
        std::cout << "FAIL (bit 0 phase=" << phase0 << ", expected 0)\n";
        return false;
    }
    
    // s1 should be near 180° (symbol 4)
    if (std::abs(std::abs(phase1) - PI) > 0.1f) {
        std::cout << "FAIL (bit 1 phase=" << phase1 << ", expected π)\n";
        return false;
    }
    
    // Test demapping with absolute method
    int demap0 = mapper.symbol_to_bits(mapper.demap_absolute(s0));
    int demap1 = mapper.symbol_to_bits(mapper.demap_absolute(s1));
    
    if (demap0 != 0 || demap1 != 1) {
        std::cout << "FAIL (demap: " << demap0 << ", " << demap1 << ")\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

bool test_qpsk_mapper() {
    std::cout << "test_qpsk_mapper: ";
    
    MultiModeMapper mapper(Modulation::QPSK);
    
    // Test all 4 QPSK phases (absolute, not differential)
    // Dibit 0 -> 0°, 1 -> 90°, 2 -> 180°, 3 -> 270°
    float expected_phases[] = {0, PI/2, PI, 3*PI/2};
    
    for (int i = 0; i < 4; i++) {
        complex_t sym = mapper.map(i);
        float phase = std::arg(sym);
        if (phase < 0) phase += 2*PI;
        
        float error = std::abs(phase - expected_phases[i]);
        if (error > PI) error = 2*PI - error;
        
        if (error > 0.2f) {
            std::cout << "FAIL (dibit " << i << " phase=" << phase 
                      << ", expected " << expected_phases[i] << ")\n";
            return false;
        }
        
        // Test demap
        int demap = mapper.symbol_to_bits(mapper.demap_absolute(sym));
        if (demap != i) {
            std::cout << "FAIL (dibit " << i << " demapped to " << demap << ")\n";
            return false;
        }
    }
    
    std::cout << "PASS\n";
    return true;
}

bool test_8psk_mapper() {
    std::cout << "test_8psk_mapper: ";
    
    MultiModeMapper mapper(Modulation::PSK8);
    
    // Test absolute PSK mapping (not differential)
    for (int tribit = 0; tribit < 8; tribit++) {
        complex_t sym = mapper.map(tribit);
        int demap = mapper.demap_absolute(sym);
        
        if (demap != tribit) {
            std::cout << "FAIL (tribit " << tribit << " -> " << demap << ")\n";
            return false;
        }
    }
    
    std::cout << "PASS\n";
    return true;
}

bool test_soft_demap() {
    std::cout << "test_soft_demap: ";
    
    MultiModeMapper mapper(Modulation::PSK8);
    
    // Test soft demapping at constellation points
    for (int i = 0; i < 8; i++) {
        complex_t ref = mapper.get_constellation_point(i);
        auto soft = mapper.soft_demap(ref, 0.01f);
        
        // Convert soft bits back to hard bits
        // Viterbi convention: positive soft = bit is 1
        int hard = 0;
        for (int j = 0; j < 3; j++) {
            hard = (hard << 1) | (soft[j] > 0 ? 1 : 0);
        }
        
        if (hard != i) {
            std::cout << "FAIL (point " << i << " -> " << hard << ")\n";
            return false;
        }
    }
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// Multi-Mode Interleaver Tests
// ============================================================================

bool test_interleaver_round_trip() {
    std::cout << "test_interleaver_round_trip: ";
    
    // Test several modes
    ModeId test_modes[] = {ModeId::M75NS, ModeId::M300S, ModeId::M1200S, ModeId::M2400S};
    
    for (ModeId mode : test_modes) {
        MultiModeInterleaver interleaver(mode);
        int bs = interleaver.block_size();
        
        // Generate random data
        std::vector<soft_bit_t> input(bs);
        for (int i = 0; i < bs; i++) {
            input[i] = (rng() % 256) - 128;
        }
        
        // Interleave and deinterleave
        auto interleaved = interleaver.interleave(input);
        auto recovered = interleaver.deinterleave(interleaved);
        
        // Compare
        if (input != recovered) {
            std::cout << "FAIL (mode " << mode_to_string(mode) << ")\n";
            return false;
        }
    }
    
    std::cout << "PASS\n";
    return true;
}

bool test_interleaver_spreading() {
    std::cout << "test_interleaver_spreading: ";
    
    MultiModeInterleaver interleaver(ModeId::M2400S);
    int bs = interleaver.block_size();
    
    // Create burst error pattern (consecutive 1s)
    std::vector<soft_bit_t> burst(bs, 0);
    int burst_len = 20;
    int burst_start = bs / 3;
    for (int i = 0; i < burst_len; i++) {
        burst[burst_start + i] = 127;
    }
    
    // Deinterleave (simulates RX after burst error)
    auto spread = interleaver.deinterleave(burst);
    
    // Count maximum consecutive errors
    int max_consec = 0;
    int consec = 0;
    for (auto s : spread) {
        if (s > 0) {
            consec++;
            max_consec = std::max(max_consec, consec);
        } else {
            consec = 0;
        }
    }
    
    // After deinterleaving, burst should be spread out
    // Max consecutive should be much less than original burst_len
    if (max_consec >= burst_len / 2) {
        std::cout << "FAIL (max_consec=" << max_consec << ")\n";
        return false;
    }
    
    std::cout << "PASS (burst " << burst_len << " -> max " << max_consec << ")\n";
    return true;
}

// ============================================================================
// Baseband Codec Test (no RF, tests encode/decode chain)
// ============================================================================

bool test_baseband_codec_8psk() {
    std::cout << "test_baseband_codec_8psk: ";
    
    // Test message
    std::string msg = "Test";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    // Get mode config
    ModeConfig mode_cfg = ModeDatabase::get(ModeId::M2400S);
    
    // Simulate TX encode chain
    // 1. Convert to bits
    std::vector<uint8_t> bits;
    for (uint8_t byte : data) {
        for (int i = 7; i >= 0; i--) {
            bits.push_back((byte >> i) & 1);
        }
    }
    
    // 2. Data scramble
    Scrambler data_scr(SCRAMBLER_INIT_DATA);
    for (auto& b : bits) {
        b ^= data_scr.next_bit();
    }
    
    // 3. FEC encode
    ConvEncoder encoder;
    std::vector<uint8_t> coded;
    encoder.encode(bits, coded, true);
    
    // 4. Interleave(simplified - just pad to block size)
    MultiModeInterleaver interleaver(mode_cfg.interleaver);
    int bs = interleaver.block_size();
    std::vector<soft_bit_t> soft_coded(coded.begin(), coded.end());
    while (soft_coded.size() % bs != 0) {
        soft_coded.push_back(0);
    }
    
    std::vector<soft_bit_t> interleaved;
    for (size_t i = 0; i < soft_coded.size(); i += bs) {
        std::vector<soft_bit_t> block(soft_coded.begin() + i, 
                                      soft_coded.begin() + i + bs);
        auto il = interleaver.interleave(block);
        interleaved.insert(interleaved.end(), il.begin(), il.end());
    }
    
    // 5. Map to symbols with scrambler
    MultiModeMapper mapper(mode_cfg.modulation);
    Scrambler sym_scr(SCRAMBLER_INIT_DATA);
    std::vector<complex_t> symbols;
    int bps = mode_cfg.bits_per_symbol;
    
    for (size_t i = 0; i + bps <= interleaved.size(); i += bps) {
        int sym_bits = 0;
        for (int j = 0; j < bps; j++) {
            sym_bits = (sym_bits << 1) | (interleaved[i + j] > 0 ? 1 : 0);
        }
        int sym_idx = mapper.map_to_symbol_index(sym_bits);
        int scr_val = sym_scr.next_tribit();
        sym_idx = (sym_idx + scr_val) % 8;
        symbols.push_back(MultiModeMapper::symbol_to_complex(sym_idx));
    }
    
    // ---- RX ----    // 1. Demap with descrambler
    std::vector<soft_bit_t> rx_soft;
    Scrambler rx_sym_scr(SCRAMBLER_INIT_DATA);
    
    for (const auto& sym : symbols) {
        float mag = std::abs(sym);
        complex_t norm_sym = (mag > 0.01f) ? sym / mag : complex_t(1.0f, 0.0f);
        
        int scr_val = rx_sym_scr.next_tribit();
        float scr_phase = -scr_val * (PI / 4.0f);
        norm_sym *= std::polar(1.0f, scr_phase);
        
        auto soft = mapper.soft_demap_absolute(norm_sym, 0.1f);
        rx_soft.insert(rx_soft.end(), soft.begin(), soft.end());
    }
    
    // 2. Deinterleave
    MultiModeInterleaver deinterleaver(mode_cfg.interleaver);
    std::vector<soft_bit_t> deinterleaved;
    for (size_t i = 0; i + bs <= rx_soft.size(); i += bs) {
        std::vector<soft_bit_t> block(rx_soft.begin() + i,
                                      rx_soft.begin() + i + bs);
        auto dil = deinterleaver.deinterleave(block);
        deinterleaved.insert(deinterleaved.end(), dil.begin(), dil.end());
    }
    
    // 3. Viterbi decode
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deinterleaved, decoded_bits, true);
    
    // 4. Descramble
    Scrambler rx_scr(SCRAMBLER_INIT_DATA);
    for (auto& b : decoded_bits) {
        b ^= rx_scr.next_bit();
    }
    
    // 5. Pack to bytes (only first data.size() bytes)
    std::vector<uint8_t> rx_data;
    size_t max_bytes = std::min(decoded_bits.size() / 8, data.size());
    for (size_t i = 0; i < max_bytes * 8; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | decoded_bits[i + j];
        }
        rx_data.push_back(byte);
    }
    
    // Compare
    bool match = (rx_data == data);
    
    std::cout << (match ? "PASS" : "FAIL");
    if (!match) {
        std::cout << " (expected: ";
        for (uint8_t b : data) std::cout << std::hex << int(b) << " ";
        std::cout << ", got: ";
        for (uint8_t b : rx_data) std::cout << std::hex << int(b) << " ";
        std::cout << std::dec << ")";
    }
    std::cout << "\n";
    return match;
}

// ============================================================================
// End-to-End Loopback Tests
// ============================================================================

bool test_loopback_2400() {
    std::cout << "test_loopback_2400: ";
    
    std::string msg = "M2400S test message!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    // TX
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M2400S;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    // RX
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M2400S;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    // Check - look for partial match or any output
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    // Debug: show first few decoded bytes
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded
              << ", bytes=" << rx_result.data.size();
    if (!found && rx_result.data.size() > 0) {
        std::cout << ", first_bytes=";
        for (size_t i = 0; i < std::min(rx_result.data.size(), size_t(10)); i++) {
            std::cout << std::hex << int(rx_result.data[i]) << " ";
        }
        std::cout << std::dec;
    }
    std::cout << ")\n";
    return found;
}

bool test_loopback_1200() {
    std::cout << "test_loopback_1200: ";
    
    std::string msg = "M1200S QPSK test!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M1200S;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M1200S;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

bool test_loopback_600() {
    std::cout << "test_loopback_600: ";
    
    std::string msg = "M600S test";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M600S;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M600S;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

// ============================================================================
// LONG Interleave Loopback Tests
// ============================================================================

bool test_loopback_2400L() {
    std::cout << "test_loopback_2400L: ";
    
    std::string msg = "M2400L LONG interleave test!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M2400L;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M2400L;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

bool test_loopback_1200L() {
    std::cout << "test_loopback_1200L: ";
    
    std::string msg = "M1200L LONG test!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M1200L;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M1200L;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

bool test_loopback_600L() {
    std::cout << "test_loopback_600L: ";
    
    std::string msg = "M600L LONG test!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M600L;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M600L;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

// ============================================================================
// Voice Mode Loopback Tests (R27)
// ============================================================================

bool test_voice_interleaver_passthrough() {
    std::cout << "test_voice_interleaver_passthrough: ";
    
    // M2400V should have passthrough interleaver (row_inc=0, col_inc=0)
    MultiModeInterleaver interleaver(ModeId::M2400V);
    
    if (!interleaver.is_passthrough()) {
        std::cout << "FAIL (expected passthrough)\n";
        return false;
    }
    
    int bs = interleaver.block_size();
    std::vector<soft_bit_t> input(bs);
    for (int i = 0; i < bs; i++) {
        input[i] = static_cast<soft_bit_t>(i % 256 - 128);
    }
    
    // Interleave should return same data
    auto interleaved = interleaver.interleave(input);
    auto deinterleaved = interleaver.deinterleave(interleaved);
    
    // All three should be identical for passthrough
    bool pass = (input == interleaved) && (input == deinterleaved);
    
    std::cout << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_loopback_2400v() {
    std::cout << "test_loopback_2400v: ";
    
    std::string msg = "M2400V voice test!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M2400V;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M2400V;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

bool test_loopback_1200v() {
    std::cout << "test_loopback_1200v: ";
    
    std::string msg = "M1200V voice!";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M1200V;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M1200V;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

bool test_loopback_600v() {
    std::cout << "test_loopback_600v: ";
    
    std::string msg = "M600V test";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    
    MultiModeTx::Config tx_cfg;
    tx_cfg.mode = ModeId::M600V;
    tx_cfg.sample_rate = 48000.0f;
    MultiModeTx tx(tx_cfg);
    
    auto tx_result = tx.transmit(data);
    
    MultiModeRx::Config rx_cfg;
    rx_cfg.mode = ModeId::M600V;
    rx_cfg.sample_rate = 48000.0f;
    MultiModeRx rx(rx_cfg);
    
    auto rx_result = rx.decode(tx_result.rf_samples);
    
    std::string decoded(rx_result.data.begin(), rx_result.data.end());
    bool found = decoded.find(msg) != std::string::npos;
    
    std::cout << (found ? "PASS" : "FAIL") 
              << " (symbols=" << rx_result.symbols_decoded << ")\n";
    return found;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Multi-Mode Support Tests\n";
    std::cout << "========================\n\n";
    
    int passed = 0;
    int total = 0;
    
    // Mode configuration tests
    std::cout << "--- Mode Configuration ---\n";
    total++; if (test_mode_database()) passed++;
    total++; if (test_mode_parameters()) passed++;
    
    // Mapper tests
    std::cout << "\n--- Multi-Mode Mapper ---\n";
    total++; if (test_bpsk_mapper()) passed++;
    total++; if (test_qpsk_mapper()) passed++;
    total++; if (test_8psk_mapper()) passed++;
    total++; if (test_soft_demap()) passed++;
    
    // Interleaver tests
    std::cout << "\n--- Multi-Mode Interleaver ---\n";
    total++; if (test_interleaver_round_trip()) passed++;
    total++; if (test_interleaver_spreading()) passed++;
    
    // Baseband codec test
    std::cout << "\n--- Baseband Codec Test ---\n";
    total++; if (test_baseband_codec_8psk()) passed++;
    
    // Loopback tests - SHORT interleave
    std::cout << "\n--- Loopback Tests (SHORT) ---\n";
    total++; if (test_loopback_2400()) passed++;
    total++; if (test_loopback_1200()) passed++;
    total++; if (test_loopback_600()) passed++;
    
    // Loopback tests - LONG interleave
    std::cout << "\n--- Loopback Tests (LONG) ---\n";
    total++; if (test_loopback_2400L()) passed++;
    total++; if (test_loopback_1200L()) passed++;
    total++; if (test_loopback_600L()) passed++;
    
    // Voice mode tests (R27)
    std::cout << "\n--- Voice Mode Tests (R27) ---\n";
    total++; if (test_voice_interleaver_passthrough()) passed++;
    total++; if (test_loopback_2400v()) passed++;
    total++; if (test_loopback_1200v()) passed++;
    total++; if (test_loopback_600v()) passed++;
    
    std::cout << "\n========================\n";
    std::cout << "Passed: " << passed << "/" << total << "\n";
    
    return (passed == total) ? 0 : 1;
}
