#ifndef M110A_CODEC_H
#define M110A_CODEC_H

/**
 * MIL-STD-188-110A Unified Codec
 * 
 * Complete encode/decode chain for all modes (75-4800 bps).
 * 
 * TX Pipeline:
 *   Bytes → LSB-first bits → FEC encode → Interleave → Gray → Scramble → PSK symbols
 * 
 * RX Pipeline:
 *   PSK symbols → Descramble → Inverse Gray → Deinterleave → Viterbi → LSB-first bytes
 * 
 * Key implementation details (from reference modem debugging):
 * 1. Message data is transmitted LSB-first (not MSB-first!)
 * 2. Scrambler wraps at 160 symbols (pre-computed, cyclic)
 * 3. Scrambler uses modulo-8 ADDITION (not XOR)
 * 4. Gray code uses modified tables (MGD2/MGD3)
 * 5. Soft bits: 0 → +127, 1 → -127 for Viterbi
 */

#include "m110a/mode_config.h"
#include "modem/scrambler_fixed.h"
#include "modem/gray_code.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <vector>
#include <complex>
#include <cmath>
#include <array>

namespace m110a {

// 8-PSK constellation points (unit circle)
static const std::array<std::complex<float>, 8> PSK8_POINTS = {{
    { 1.000f,  0.000f},  // 0:   0°
    { 0.707f,  0.707f},  // 1:  45°
    { 0.000f,  1.000f},  // 2:  90°
    {-0.707f,  0.707f},  // 3: 135°
    {-1.000f,  0.000f},  // 4: 180°
    {-0.707f, -0.707f},  // 5: 225°
    { 0.000f, -1.000f},  // 6: 270°
    { 0.707f, -0.707f}   // 7: 315°
}};

/**
 * Unified Codec for MIL-STD-188-110A
 */
class M110ACodec {
public:
    using complex_t = std::complex<float>;
    
    /**
     * Construct codec for specific mode
     */
    explicit M110ACodec(ModeId mode) 
        : mode_(mode)
        , config_(ModeDatabase::get(mode))
        , interleaver_(config_.interleaver) {
    }
    
    /**
     * Change mode
     */
    void set_mode(ModeId mode) {
        mode_ = mode;
        config_ = ModeDatabase::get(mode);
        interleaver_ = MultiModeInterleaver(config_.interleaver);
    }
    
    ModeId mode() const { return mode_; }
    const ModeConfig& config() const { return config_; }
    
    // ========================================================================
    // TX Pipeline
    // ========================================================================
    
    /**
     * Encode data bytes to PSK symbols
     * 
     * @param data Input bytes
     * @return Complex PSK symbols (data only, no probes/preamble)
     */
    std::vector<complex_t> encode(const std::vector<uint8_t>& data) {
        // Step 1: Convert bytes to bits (LSB first!)
        std::vector<int> bits = bytes_to_bits_lsb(data);
        
        // Step 2: FEC encode (rate 1/2, K=7) - except 4800 bps uncoded
        std::vector<int> coded;
        if (config_.bps == 4800) {
            coded = bits;  // No FEC for 4800 bps
        } else {
            coded = fec_encode(bits);
        }
        
        // Step 3: Apply bit repetition for low-rate modes
        // For rep > 1, each FEC bit pair (G1, G2) is repeated `rep` times
        int block_size = interleaver_.block_size();
        int rep = config_.symbol_repetition;
        std::vector<int> repeated_coded;
        
        if (rep > 1 && config_.modulation == Modulation::BPSK) {
            // Repeat each pair of FEC bits
            repeated_coded.reserve(coded.size() * rep);
            for (size_t i = 0; i + 1 < coded.size(); i += 2) {
                for (int r = 0; r < rep; r++) {
                    repeated_coded.push_back(coded[i]);      // G1
                    repeated_coded.push_back(coded[i + 1]);  // G2
                }
            }
            // Handle odd bit if present
            if (coded.size() % 2 == 1) {
                for (int r = 0; r < rep; r++) {
                    repeated_coded.push_back(coded.back());
                }
            }
        } else {
            repeated_coded = std::move(coded);
        }
        
        // Step 3.5: Pad to interleaver block size
        while (repeated_coded.size() % block_size != 0) {
            repeated_coded.push_back(0);
        }
        
        // Step 4: Interleave
        std::vector<soft_bit_t> soft_coded(repeated_coded.begin(), repeated_coded.end());
        std::vector<soft_bit_t> interleaved;
        
        for (size_t i = 0; i < soft_coded.size(); i += block_size) {
            std::vector<soft_bit_t> block(soft_coded.begin() + i, 
                                          soft_coded.begin() + i + block_size);
            auto il = interleaver_.interleave(block);
            interleaved.insert(interleaved.end(), il.begin(), il.end());
        }
        
        // Step 5: Map to symbols based on modulation
        std::vector<complex_t> symbols;
        DataScramblerFixed scrambler;
        
        switch (config_.modulation) {
            case Modulation::BPSK:
                symbols = encode_bpsk(interleaved, scrambler);
                break;
            case Modulation::QPSK:
                symbols = encode_qpsk(interleaved, scrambler);
                break;
            case Modulation::PSK8:
                symbols = encode_8psk(interleaved, scrambler);
                break;
        }
        
        return symbols;
    }
    
    /**
     * Insert probe symbols into data symbol stream
     * 
     * @param data_symbols Data symbols from encode()
     * @return Symbols with probes inserted
     */
    std::vector<complex_t> insert_probes(const std::vector<complex_t>& data_symbols) {
        int unknown_len = config_.unknown_data_len;
        int known_len = config_.known_data_len;
        
        // 75 bps modes have no probes
        if (unknown_len == 0 || known_len == 0) {
            return data_symbols;
        }
        
        std::vector<complex_t> output;
        DataScramblerFixed scrambler;
        
        size_t data_idx = 0;
        while (data_idx < data_symbols.size()) {
            // Copy data symbols and advance scrambler to stay in sync
            for (int i = 0; i < unknown_len && data_idx < data_symbols.size(); i++) {
                output.push_back(data_symbols[data_idx++]);
                scrambler.next();  // Advance scrambler for each data symbol
            }
            
            // Insert probe symbols (scrambler only, data=0)
            for (int i = 0; i < known_len; i++) {
                int sym_idx = scrambler.next();
                output.push_back(PSK8_POINTS[sym_idx]);
            }
        }
        
        return output;
    }
    
    /**
     * Encode data with probes integrated (for audio transmission)
     * 
     * This produces output compatible with decode_with_probes() where
     * the scrambler runs continuously across both data and probe symbols.
     * 
     * @param data Input bytes
     * @return Complex PSK symbols with probes interleaved
     */
    std::vector<complex_t> encode_with_probes(const std::vector<uint8_t>& data) {
        int unknown_len = config_.unknown_data_len;
        int known_len = config_.known_data_len;
        
        // 75 bps modes have no probes - use regular encode
        if (unknown_len == 0 || known_len == 0) {
            return encode(data);
        }
        
        // Step 1: Convert bytes to bits (LSB first!)
        std::vector<int> bits = bytes_to_bits_lsb(data);
        
        // Step 2: FEC encode (rate 1/2, K=7) - except 4800 bps uncoded
        std::vector<int> coded;
        if (config_.bps == 4800) {
            coded = bits;
        } else {
            coded = fec_encode(bits);
        }
        
        // Step 3: Apply bit repetition for low-rate modes
        int block_size = interleaver_.block_size();
        int rep = config_.symbol_repetition;
        std::vector<int> repeated_coded;
        
        if (rep > 1 && config_.modulation == Modulation::BPSK) {
            repeated_coded.reserve(coded.size() * rep);
            for (size_t i = 0; i + 1 < coded.size(); i += 2) {
                for (int r = 0; r < rep; r++) {
                    repeated_coded.push_back(coded[i]);
                    repeated_coded.push_back(coded[i + 1]);
                }
            }
            if (coded.size() % 2 == 1) {
                for (int r = 0; r < rep; r++) {
                    repeated_coded.push_back(coded.back());
                }
            }
        } else {
            repeated_coded = std::move(coded);
        }
        
        // Step 4: Pad to interleaver block size
        while (repeated_coded.size() % block_size != 0) {
            repeated_coded.push_back(0);
        }
        
        // Step 5: Interleave
        std::vector<soft_bit_t> soft_coded(repeated_coded.begin(), repeated_coded.end());
        std::vector<soft_bit_t> interleaved;
        
        for (size_t i = 0; i < soft_coded.size(); i += block_size) {
            std::vector<soft_bit_t> block(soft_coded.begin() + i, 
                                          soft_coded.begin() + i + block_size);
            auto il = interleaver_.interleave(block);
            interleaved.insert(interleaved.end(), il.begin(), il.end());
        }
        
        // Step 6: Map bits to tribits (before scrambling)
        std::vector<int> tribits;
        int bits_per_sym = config_.bits_per_symbol;
        
        for (size_t i = 0; i + bits_per_sym <= interleaved.size(); i += bits_per_sym) {
            int tribit = 0;
            for (int b = 0; b < bits_per_sym; b++) {
                if (interleaved[i + b] > 0) {
                    tribit |= (1 << (bits_per_sym - 1 - b));
                }
            }
            tribits.push_back(tribit);
        }
        
        // Step 7: Create output with interleaved data/probe structure
        // Apply ONE continuous scrambler to everything
        std::vector<complex_t> output;
        DataScramblerFixed scrambler;
        int pattern_len = unknown_len + known_len;
        
        size_t tribit_idx = 0;
        while (tribit_idx < tribits.size()) {
            // Data symbols
            for (int i = 0; i < unknown_len && tribit_idx < tribits.size(); i++) {
                int tribit = tribits[tribit_idx++];
                
                // Gray encode then scramble
                int gray;
                switch (config_.modulation) {
                    case Modulation::BPSK:
                        gray = (tribit > 0) ? 4 : 0;
                        break;
                    case Modulation::QPSK:
                        gray = QPSK_SYMBOLS[MGD2[tribit & 3]];
                        break;
                    case Modulation::PSK8:
                    default:
                        gray = MGD3[tribit & 7];
                        break;
                }
                
                int scr = scrambler.next();
                int sym_idx = (gray + scr) & 7;
                output.push_back(PSK8_POINTS[sym_idx]);
            }
            
            // Probe symbols (scrambler only, data=0)
            for (int i = 0; i < known_len; i++) {
                int sym_idx = scrambler.next();
                output.push_back(PSK8_POINTS[sym_idx]);
            }
        }
        
        return output;
    }
    
    // ========================================================================
    // RX Pipeline
    // ========================================================================
    
    /**
     * Decode PSK symbols to data bytes
     * 
     * @param symbols Complex PSK symbols (data only, probes already removed)
     * @return Decoded bytes
     */
    std::vector<uint8_t> decode(const std::vector<complex_t>& symbols) {
        // Step 1: Demap and descramble to soft bits
        DataScramblerFixed scrambler;
        std::vector<soft_bit_t> soft_bits;
        
        switch (config_.modulation) {
            case Modulation::BPSK:
                soft_bits = decode_bpsk(symbols, scrambler);
                break;
            case Modulation::QPSK:
                soft_bits = decode_qpsk(symbols, scrambler);
                break;
            case Modulation::PSK8:
                soft_bits = decode_8psk(symbols, scrambler);
                break;
        }
        
        return decode_soft_bits(soft_bits);
    }
    
    /**
     * Decode all symbols including frame structure (data + probes)
     * This properly handles scrambler sync across frame boundaries.
     * Also handles symbol repetition for low-rate modes (rep >= 4).
     * Detects and corrects 180° phase ambiguity using probe symbols.
     * 
     * @param all_symbols All received symbols (interleaved data + probes)
     * @return Decoded bytes
     */
    std::vector<uint8_t> decode_with_probes(const std::vector<complex_t>& all_symbols) {
        int unknown_len = config_.unknown_data_len;
        int known_len = config_.known_data_len;
        int rep = config_.symbol_repetition;
        
        // 75 bps modes have no probes - decode directly
        if (unknown_len == 0 || known_len == 0) {
            return decode(all_symbols);
        }
        
        int pattern_len = unknown_len + known_len;
        
        // Detect 180° phase ambiguity using probe symbols
        // Probes should descramble to 0; if they're closer to 4, we have phase inversion
        int phase_offset = detect_phase_offset(all_symbols, unknown_len, known_len);
        
        DataScramblerFixed scrambler;
        std::vector<soft_bit_t> soft_bits;
        
        size_t idx = 0;
        while (idx + pattern_len <= all_symbols.size()) {
            // Process data symbols - apply detected phase correction
            for (int i = 0; i < unknown_len && idx + i < all_symbols.size(); i++) {
                complex_t sym = all_symbols[idx + i];
                int pos = (symbol_to_position(sym) + phase_offset) & 7;
                int scr = scrambler.next();
                int descrambled = (pos - scr + 8) & 7;
                
                switch (config_.modulation) {
                    case Modulation::BPSK:
                        soft_bits.push_back(bpsk_soft_bit(descrambled));
                        break;
                    case Modulation::QPSK:
                        add_qpsk_soft_bits(descrambled, soft_bits);
                        break;
                    case Modulation::PSK8:
                        add_8psk_soft_bits(descrambled, soft_bits);
                        break;
                }
            }
            
            // Skip probe symbols but advance scrambler
            for (int i = 0; i < known_len; i++) {
                scrambler.next();
            }
            
            idx += pattern_len;
        }
        
        return decode_soft_bits(soft_bits);
    }
    
private:
    /**
     * Detect phase offset using probe symbols (full 8-way 45° resolution)
     * 
     * Tests all 8 possible phase offsets (0°, 45°, 90°, ... 315°) and returns
     * the one that best matches expected probe descrambling to 0.
     * 
     * @return Phase offset 0-7 corresponding to 0°, 45°, 90°, etc.
     */
    int detect_phase_offset(const std::vector<complex_t>& symbols, 
                            int unknown_len, int known_len) const {
        int pattern_len = unknown_len + known_len;
        
        // Count matches for each of 8 possible phase offsets
        std::array<int, 8> match_counts = {0};
        const int max_patterns = 5;
        
        for (int phase_try = 0; phase_try < 8; phase_try++) {
            DataScramblerFixed scrambler;
            int patterns_checked = 0;
            
            size_t idx = 0;
            while (idx + pattern_len <= symbols.size() && patterns_checked < max_patterns) {
                // Skip data symbols in scrambler
                for (int i = 0; i < unknown_len; i++) scrambler.next();
                
                // Check probe symbols with this phase offset
                for (int i = 0; i < known_len && idx + unknown_len + i < symbols.size(); i++) {
                    complex_t sym = symbols[idx + unknown_len + i];
                    int pos = (symbol_to_position(sym) + phase_try) & 7;
                    int scr = scrambler.next();
                    int descr = (pos - scr + 8) & 7;
                    
                    // Probes should descramble to 0 if phase is correct
                    // Allow ±1 tolerance for noise
                    if (descr == 0 || descr == 1 || descr == 7) {
                        match_counts[phase_try]++;
                    }
                }
                
                idx += pattern_len;
                patterns_checked++;
            }
        }
        
        // Find best phase offset
        int best_phase = 0;
        int best_count = match_counts[0];
        for (int i = 1; i < 8; i++) {
            if (match_counts[i] > best_count) {
                best_count = match_counts[i];
                best_phase = i;
            }
        }
        
        return best_phase;
    }
    
    /**
     * Complete soft bits to bytes decoding
     * Handles symbol repetition by combining repeated FEC bit pairs after deinterleaving
     * 
     * For rep=2, deinterleaver outputs: G1a, G2a, G1b, G2b, G1a', G2a', G1b', G2b', ...
     * Combine to: G1 = G1a + G1b, G2 = G2a + G2b
     */
    std::vector<uint8_t> decode_soft_bits(std::vector<soft_bit_t>& soft_bits) {
        // Step 2: Deinterleave
        int block_size = interleaver_.block_size();
        std::vector<soft_bit_t> deinterleaved;
        
        // Pad to block size if needed
        while (soft_bits.size() % block_size != 0) {
            soft_bits.push_back(0);
        }
        
        for (size_t i = 0; i < soft_bits.size(); i += block_size) {
            std::vector<soft_bit_t> block(soft_bits.begin() + i,
                                          soft_bits.begin() + i + block_size);
            auto dil = interleaver_.deinterleave(block);
            deinterleaved.insert(deinterleaved.end(), dil.begin(), dil.end());
        }
        
        // Step 2.5: Combine repeated FEC bit pairs (for modes with rep > 1)
        // Deinterleaver output for rep=2: G1a, G2a, G1b, G2b (repeating)
        // Reference modem fetches: metric1=G1a+G1b, metric2=G2a+G2b
        int rep = config_.symbol_repetition;
        std::vector<soft_bit_t> combined;
        
        if (rep > 1 && config_.modulation == Modulation::BPSK) {
            // Each info bit produces 2 coded bits (rate 1/2)
            // Each coded bit is repeated `rep` times
            // So each info bit = 2 * rep values in deinterleaver
            int values_per_info_bit = 2 * rep;
            
            combined.reserve(deinterleaved.size() / rep);
            
            for (size_t i = 0; i + values_per_info_bit <= deinterleaved.size(); i += values_per_info_bit) {
                // Combine G1 (first half: positions 0, 2, 4, ... for rep=2: 0, 2)
                float metric1 = 0;
                for (int r = 0; r < rep; r++) {
                    metric1 += deinterleaved[i + r * 2];
                }
                
                // Combine G2 (second half: positions 1, 3, 5, ... for rep=2: 1, 3)
                float metric2 = 0;
                for (int r = 0; r < rep; r++) {
                    metric2 += deinterleaved[i + r * 2 + 1];
                }
                
                // Store combined metrics as soft bits for Viterbi
                combined.push_back(static_cast<soft_bit_t>(
                    std::max(-127.0f, std::min(127.0f, metric1))));
                combined.push_back(static_cast<soft_bit_t>(
                    std::max(-127.0f, std::min(127.0f, metric2))));
            }
        } else {
            combined = std::move(deinterleaved);
        }
        
        // Step 3: Viterbi decode (except 4800 bps uncoded)
        std::vector<uint8_t> decoded_bits;
        if (config_.bps == 4800) {
            // No FEC - just hard decision
            for (auto sb : combined) {
                decoded_bits.push_back(sb < 0 ? 1 : 0);
            }
        } else {
            // Convert to int8_t for Viterbi
            std::vector<int8_t> soft_for_viterbi;
            for (auto sb : combined) {
                soft_for_viterbi.push_back(static_cast<int8_t>(sb));
            }
            
            ViterbiDecoder viterbi;
            viterbi.decode_block(soft_for_viterbi, decoded_bits, true);
        }
        
        // Step 4: Convert bits to bytes (LSB first!)
        return bits_to_bytes_lsb(decoded_bits);
    }
    
    /**
     * Extract data symbols from stream (removes probe symbols)
     * 
     * @param all_symbols All received symbols (data + probes)
     * @param scr_offset Starting scrambler offset (for sync verification)
     * @return Data symbols only
     */
    std::vector<complex_t> extract_data_symbols(
            const std::vector<complex_t>& all_symbols,
            int scr_offset = 0) {
        
        int unknown_len = config_.unknown_data_len;
        int known_len = config_.known_data_len;
        
        // 75 bps modes have no probes
        if (unknown_len == 0 || known_len == 0) {
            return all_symbols;
        }
        
        int pattern_len = unknown_len + known_len;
        std::vector<complex_t> data_symbols;
        
        size_t idx = 0;
        while (idx + pattern_len <= all_symbols.size()) {
            // Extract data symbols
            for (int i = 0; i < unknown_len; i++) {
                data_symbols.push_back(all_symbols[idx + i]);
            }
            // Skip probe symbols
            idx += pattern_len;
        }
        
        return data_symbols;
    }

private:
    ModeId mode_;
    ModeConfig config_;
    MultiModeInterleaver interleaver_;
    
    // ========================================================================
    // Bit Conversion Utilities
    // ========================================================================
    
    /**
     * Convert bytes to bits (LSB first - CRITICAL!)
     */
    static std::vector<int> bytes_to_bits_lsb(const std::vector<uint8_t>& bytes) {
        std::vector<int> bits;
        bits.reserve(bytes.size() * 8);
        for (uint8_t byte : bytes) {
            for (int i = 0; i < 8; i++) {
                bits.push_back((byte >> i) & 1);  // LSB first!
            }
        }
        return bits;
    }
    
    /**
     * Convert bits to bytes (LSB first - CRITICAL!)
     */
    static std::vector<uint8_t> bits_to_bytes_lsb(const std::vector<uint8_t>& bits) {
        std::vector<uint8_t> bytes;
        bytes.reserve((bits.size() + 7) / 8);
        
        for (size_t i = 0; i + 8 <= bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                if (bits[i + j]) {
                    byte |= (1 << j);  // LSB first!
                }
            }
            bytes.push_back(byte);
        }
        return bytes;
    }
    
    // ========================================================================
    // FEC Encoding
    // ========================================================================
    
    /**
     * Convolutional encode (K=7, rate 1/2, G1=0x5B, G2=0x79)
     */
    std::vector<int> fec_encode(const std::vector<int>& bits) {
        std::vector<int> coded;
        coded.reserve(bits.size() * 2 + 12);  // +12 for tail bits
        
        int state = 0;
        
        // Encode data bits
        for (int bit : bits) {
            state = state >> 1;
            if (bit) state |= 0x40;
            
            coded.push_back(__builtin_popcount(state & 0x5B) & 1);  // G1
            coded.push_back(__builtin_popcount(state & 0x79) & 1);  // G2
        }
        
        // Flush with 6 zero bits
        for (int i = 0; i < 6; i++) {
            state = state >> 1;
            coded.push_back(__builtin_popcount(state & 0x5B) & 1);
            coded.push_back(__builtin_popcount(state & 0x79) & 1);
        }
        
        return coded;
    }
    
    // ========================================================================
    // Modulation-Specific Encoding
    // ========================================================================
    
    /**
     * BPSK encoding (75-600 bps modes)
     * Each coded bit → one BPSK symbol
     */
    std::vector<complex_t> encode_bpsk(const std::vector<soft_bit_t>& coded,
                                        DataScramblerFixed& scrambler) {
        std::vector<complex_t> symbols;
        symbols.reserve(coded.size());
        
        for (auto bit : coded) {
            // BPSK: bit 0 → symbol 0, bit 1 → symbol 4
            int sym_idx = (bit > 0) ? 4 : 0;
            
            // Apply scrambler rotation
            int scr = scrambler.next();
            sym_idx = (sym_idx + scr) & 7;
            
            symbols.push_back(PSK8_POINTS[sym_idx]);
        }
        
        return symbols;
    }
    
    /**
     * QPSK encoding (1200 bps mode)
     * Each 2 coded bits → one QPSK symbol
     */
    std::vector<complex_t> encode_qpsk(const std::vector<soft_bit_t>& coded,
                                        DataScramblerFixed& scrambler) {
        std::vector<complex_t> symbols;
        symbols.reserve(coded.size() / 2);
        
        for (size_t i = 0; i + 1 < coded.size(); i += 2) {
            // Pack 2 bits into dibit
            int dibit = ((coded[i] > 0 ? 1 : 0) << 1) | 
                        (coded[i+1] > 0 ? 1 : 0);
            
            // Gray encode to symbol index
            int sym_idx = QPSK_SYMBOLS[MGD2[dibit]];
            
            // Apply scrambler rotation
            int scr = scrambler.next();
            sym_idx = (sym_idx + scr) & 7;
            
            symbols.push_back(PSK8_POINTS[sym_idx]);
        }
        
        return symbols;
    }
    
    /**
     * 8PSK encoding (2400-4800 bps modes)
     * Each 3 coded bits → one 8PSK symbol
     */
    std::vector<complex_t> encode_8psk(const std::vector<soft_bit_t>& coded,
                                        DataScramblerFixed& scrambler) {
        std::vector<complex_t> symbols;
        symbols.reserve(coded.size() / 3);
        
        for (size_t i = 0; i + 2 < coded.size(); i += 3) {
            // Pack 3 bits into tribit
            int tribit = ((coded[i] > 0 ? 1 : 0) << 2) | 
                         ((coded[i+1] > 0 ? 1 : 0) << 1) | 
                         (coded[i+2] > 0 ? 1 : 0);
            
            // Gray encode to symbol index
            int sym_idx = MGD3[tribit];
            
            // Apply scrambler rotation
            int scr = scrambler.next();
            sym_idx = (sym_idx + scr) & 7;
            
            symbols.push_back(PSK8_POINTS[sym_idx]);
        }
        
        return symbols;
    }
    
    // ========================================================================
    // Modulation-Specific Decoding
    // ========================================================================
    
    /**
     * Decode symbol to 8-PSK position (0-7)
     */
    static int symbol_to_position(complex_t sym) {
        float angle = std::atan2(sym.imag(), sym.real());
        int pos = static_cast<int>(std::round(angle * 4.0f / PI));
        return ((pos % 8) + 8) % 8;
    }
    
    /**
     * Get BPSK soft bit from descrambled symbol
     */
    static soft_bit_t bpsk_soft_bit(int descrambled) {
        // BPSK: symbols near 0 → bit 0 (+127), symbols near 4 → bit 1 (-127)
        if (descrambled == 0 || descrambled == 1 || descrambled == 7) {
            return 127;
        } else if (descrambled >= 3 && descrambled <= 5) {
            return -127;
        } else {
            return (descrambled < 4) ? 64 : -64;
        }
    }
    
    /**
     * Add QPSK soft bits from descrambled symbol
     */
    static void add_qpsk_soft_bits(int descrambled, std::vector<soft_bit_t>& soft) {
        int qpsk_idx = ((descrambled + 1) / 2) & 3;
        int dibit = INV_MGD2[qpsk_idx];
        soft.push_back((dibit & 2) ? -127 : 127);
        soft.push_back((dibit & 1) ? -127 : 127);
    }
    
    /**
     * Add 8PSK soft bits from descrambled symbol
     */
    static void add_8psk_soft_bits(int descrambled, std::vector<soft_bit_t>& soft) {
        int tribit = INV_MGD3[descrambled];
        soft.push_back((tribit & 4) ? -127 : 127);
        soft.push_back((tribit & 2) ? -127 : 127);
        soft.push_back((tribit & 1) ? -127 : 127);
    }
    
    /**
     * BPSK decoding
     */
    std::vector<soft_bit_t> decode_bpsk(const std::vector<complex_t>& symbols,
                                         DataScramblerFixed& scrambler) {
        std::vector<soft_bit_t> soft;
        soft.reserve(symbols.size());
        
        for (auto sym : symbols) {
            int pos = symbol_to_position(sym);
            
            // Descramble
            int scr = scrambler.next();
            int descrambled = (pos - scr + 8) & 7;
            
            // BPSK: symbols near 0 → bit 0 (+127), symbols near 4 → bit 1 (-127)
            // Position 0,1,7 → +127; Position 3,4,5 → -127; 2,6 ambiguous
            soft_bit_t sb;
            if (descrambled == 0 || descrambled == 1 || descrambled == 7) {
                sb = 127;
            } else if (descrambled >= 3 && descrambled <= 5) {
                sb = -127;
            } else {
                // Ambiguous - use soft decision based on magnitude
                sb = (descrambled < 4) ? 64 : -64;
            }
            soft.push_back(sb);
        }
        
        return soft;
    }
    
    /**
     * QPSK decoding
     */
    std::vector<soft_bit_t> decode_qpsk(const std::vector<complex_t>& symbols,
                                         DataScramblerFixed& scrambler) {
        std::vector<soft_bit_t> soft;
        soft.reserve(symbols.size() * 2);
        
        for (auto sym : symbols) {
            int pos = symbol_to_position(sym);
            
            // Descramble
            int scr = scrambler.next();
            int descrambled = (pos - scr + 8) & 7;
            
            // Round to nearest QPSK symbol (0, 2, 4, 6)
            int qpsk_idx = ((descrambled + 1) / 2) & 3;
            
            // Inverse Gray decode
            int dibit = INV_MGD2[qpsk_idx];
            
            // Output 2 soft bits
            soft.push_back((dibit & 2) ? -127 : 127);
            soft.push_back((dibit & 1) ? -127 : 127);
        }
        
        return soft;
    }
    
    /**
     * 8PSK decoding
     */
    std::vector<soft_bit_t> decode_8psk(const std::vector<complex_t>& symbols,
                                         DataScramblerFixed& scrambler) {
        std::vector<soft_bit_t> soft;
        soft.reserve(symbols.size() * 3);
        
        for (auto sym : symbols) {
            int pos = symbol_to_position(sym);
            
            // Descramble
            int scr = scrambler.next();
            int descrambled = (pos - scr + 8) & 7;
            
            // Inverse Gray decode
            int tribit = INV_MGD3[descrambled];
            
            // Output 3 soft bits
            soft.push_back((tribit & 4) ? -127 : 127);
            soft.push_back((tribit & 2) ? -127 : 127);
            soft.push_back((tribit & 1) ? -127 : 127);
        }
        
        return soft;
    }
};

} // namespace m110a

#endif // M110A_CODEC_H
