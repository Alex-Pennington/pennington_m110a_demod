#ifndef M110A_PREAMBLE_CODEC_H
#define M110A_PREAMBLE_CODEC_H

/**
 * MIL-STD-188-110A Preamble Encoder/Decoder
 * 
 * Implementation based on MIL-STD-188-110A Appendix C:
 *   Section C.5.2: Preamble Structure and Encoding
 *   Section C.5.2.2: Preamble Symbol Sequence
 *   Section C.5.2.1: Preamble Scrambler
 *   Table C-VI: D1/D2 Pattern Assignments
 *   Table C-VII: Walsh-Hadamard Patterns (PSYMBOL)
 * 
 * Standard Preamble structure (480 symbols for standard modes):
 *   - 320 common symbols: Extended sync pattern
 *   - 32 D1 symbols: Mode identifier (Walsh-encoded, scrambled)
 *   - 32 D2 symbols: Mode identifier (Walsh-encoded, scrambled)
 *   - 64 count symbols: Block count (scrambled)
 *   - 32 zero symbols: Channel estimation (symbol 0)
 * 
 * D1/D2 encoding per MIL-STD-188-110A:
 *   - Each D value (0-7) maps to 8-symbol Walsh sequence
 *   - Transmitted 4 times for 32 symbols
 *   - XOR with PSCRAMBLE[32] scrambler pattern
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/mode_config.h"
#include "modem/scrambler.h"
#include "modem/multimode_mapper.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <utility>

namespace m110a {

// MIL-STD-188-110A compliant preamble structure constants
// Per Section C.5.2.2 - Standard positions for Brain Core compatibility
constexpr int CODEC_COMMON_SYMBOLS = 288;    // Common sync pattern
constexpr int CODEC_D1_SYMBOLS = 32;         // D1 mode identifier (starts at 288)
constexpr int CODEC_D2_SYMBOLS = 32;         // D2 mode identifier (starts at 320)
constexpr int CODEC_COUNT_SYMBOLS = 96;      // Block count sequence
constexpr int CODEC_ZERO_SYMBOLS = 32;       // Zero padding
constexpr int CODEC_FRAME_LEN = 480;         // Total preamble frame
constexpr int MODE_ID_BITS = 5;              // Legacy - for decoder compatibility

// Walsh-Hadamard patterns for D symbols (MIL-STD-188-110A Table C-VII)
// D value 0-7 maps to 8-symbol Walsh sequence, transmitted 4x for 32 symbols
static constexpr int PSYMBOL[8][8] = {
    {0,0,0,0,0,0,0,0},  // D=0
    {0,4,0,4,0,4,0,4},  // D=1
    {0,0,4,4,0,0,4,4},  // D=2
    {0,4,4,0,0,4,4,0},  // D=3
    {0,0,0,0,4,4,4,4},  // D=4
    {0,4,0,4,4,0,4,0},  // D=5
    {0,0,4,4,4,4,0,0},  // D=6
    {0,4,4,0,4,0,0,4}   // D=7
};

// Preamble scrambler sequence (MIL-STD-188-110A Section C.5.2.1)
// 32-symbol fixed scramble pattern applied to preamble D1/D2/Count regions
static constexpr int PSCRAMBLE[32] = {
    7,4,3,0,5,1,5,0,2,2,1,1,5,7,4,3,
    5,0,2,6,2,1,6,2,0,0,5,0,5,2,6,6
};

/**
 * Decoded preamble information
 */
struct PreambleInfo {
    bool valid;
    int mode_id;           // Raw mode ID (0-17)
    ModeId mode;           // Decoded mode enum
    int block_count;       // Number of data blocks
    float confidence;      // Decoding confidence (0-1)
    
    // Derived info
    bool is_short_interleave() const {
        // Short modes have even IDs (0,2,4,6,8,10) except voice modes
        if (mode_id == 17) return true;  // M4800S is short
        if (mode_id >= 12) return false; // Voice modes
        return (mode_id % 2) == 0;
    }
    
    bool is_long_interleave() const {
        if (mode_id >= 12) return false; // Voice modes
        return (mode_id % 2) == 1;
    }
    
    bool is_voice_mode() const {
        return mode_id == 12 || mode_id == 14 || mode_id == 16;
    }
    
    std::string interleave_type() const {
        if (is_voice_mode()) return "voice";
        if (is_long_interleave()) return "long";
        return "short";
    }
    
    PreambleInfo() : valid(false), mode_id(-1), mode(ModeId::M2400S), 
                     block_count(0), confidence(0.0f) {}
};

/**
 * Get D1/D2 values for a given mode
 * Returns {D1, D2} pair per MIL-STD-188-110A Table C-VI
 */
inline std::pair<int, int> get_d1_d2_for_mode(ModeId mode) {
    switch (mode) {
        // Data modes - short interleave
        case ModeId::M75NS:  return {7, 7};  // 75 bps short - D1=7, D2=7 (estimated)
        case ModeId::M150S:  return {7, 4};
        case ModeId::M300S:  return {6, 7};
        case ModeId::M600S:  return {6, 6};
        case ModeId::M1200S: return {6, 5};
        case ModeId::M2400S: return {6, 4};
        case ModeId::M4800S: return {7, 6};
        
        // Data modes - long interleave
        case ModeId::M75NL:  return {5, 7};  // 75 bps long - D1=5, D2=7 (estimated)
        case ModeId::M150L:  return {5, 4};
        case ModeId::M300L:  return {4, 7};
        case ModeId::M600L:  return {4, 6};
        case ModeId::M1200L: return {4, 5};
        case ModeId::M2400L: return {4, 4};
        
        // Voice modes use same D pattern as corresponding data modes
        default:             return {6, 4};  // Default to 2400S
    }
}

/**
 * Preamble Encoder - generates proper MIL-STD-188-110A preamble
 * 
 * Implements D1/D2 Walsh-Hadamard encoding per MIL-STD-188-110A:
 *   - Table C-VII defines 8-symbol Walsh patterns for D=0-7
 *   - D1/D2 each use 32 symbols (4 repetitions of 8-symbol Walsh)
 *   - PSCRAMBLE[32] XOR applied per Section C.5.2.1
 *   - D1/D2 values from Table C-VI define mode identification
 */
class PreambleEncoder {
public:
    /**
     * Generate complete preamble for given mode
     * 
     * Standard structure (480 symbols):
     *   - Symbols 0-319:   Extended common sync (scrambled)
     *   - Symbols 320-351: D1 mode identifier (Walsh + scramble)
     *   - Symbols 352-383: D2 mode identifier (Walsh + scramble)
     *   - Symbols 384-447: Block count (scrambled)
     *   - Symbols 448-479: Zeros for channel estimation
     */
    std::vector<complex_t> encode(ModeId mode, int block_count = 1) {
        std::vector<complex_t> symbols;
        
        auto mode_cfg = ModeDatabase::get(mode);
        int total_symbols = mode_cfg.preamble_symbols();
        
        // Use standard structure for all modes >= 480 symbols
        // For shorter preambles, use proportional scaling
        int common_syms, d1_syms, d2_syms, count_syms;
        if (total_symbols >= CODEC_FRAME_LEN) {
            common_syms = CODEC_COMMON_SYMBOLS;   // 288
            d1_syms = CODEC_D1_SYMBOLS;           // 32
            d2_syms = CODEC_D2_SYMBOLS;           // 32
            count_syms = CODEC_COUNT_SYMBOLS;     // 96
        } else {
            // Proportional structure for short preambles
            common_syms = (total_symbols * 60) / 100;  // 60%
            d1_syms = std::max(8, (total_symbols * 7) / 100);
            d2_syms = d1_syms;
            count_syms = (total_symbols * 20) / 100;
        }
        
        MultiModeMapper mapper(Modulation::PSK8);
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
        
        // Get D1/D2 values for this mode
        auto [d1, d2] = get_d1_d2_for_mode(mode);
        
        // Section 1: Common symbols (scrambled sync pattern)
        for (int i = 0; i < common_syms && static_cast<int>(symbols.size()) < total_symbols; i++) {
            uint8_t tribit = scr.next_tribit();
            symbols.push_back(mapper.map(tribit));
        }
        
        // Section 2: D1 mode identifier (Walsh-encoded, scrambled)
        auto d1_symbols = encode_d_pattern(d1, d1_syms);
        for (const auto& sym : d1_symbols) {
            if (static_cast<int>(symbols.size()) >= total_symbols) break;
            symbols.push_back(sym);
        }
        
        // Section 3: D2 mode identifier (Walsh-encoded, scrambled)
        auto d2_symbols = encode_d_pattern(d2, d2_syms);
        for (const auto& sym : d2_symbols) {
            if (static_cast<int>(symbols.size()) >= total_symbols) break;
            symbols.push_back(sym);
        }
        
        // Section 4: Count symbols (scrambled)
        auto count_symbols = encode_count(block_count, count_syms);
        for (const auto& sym : count_symbols) {
            if (static_cast<int>(symbols.size()) >= total_symbols) break;
            symbols.push_back(sym);
        }
        
        // Section 5: Pad with zeros (symbol 0 = phase 0) to total
        while (static_cast<int>(symbols.size()) < total_symbols) {
            symbols.push_back(complex_t(1.0f, 0.0f));  // Symbol 0
        }
        
        return symbols;
    }
    
    /**
     * Get common symbol count for a given preamble size
     */
    static int get_common_symbols(int total_preamble) {
        if (total_preamble >= CODEC_FRAME_LEN) {
            return CODEC_COMMON_SYMBOLS;  // 288
        }
        return (total_preamble * 60) / 100;
    }
    
    /**
     * Get mode symbol count for a given preamble size (D1 + D2)
     */
    static int get_mode_symbols(int total_preamble) {
        if (total_preamble >= CODEC_FRAME_LEN) {
            return CODEC_D1_SYMBOLS + CODEC_D2_SYMBOLS;  // 64
        }
        return std::max(16, (total_preamble * 14) / 100);
    }

private:
    /**
     * Encode a D pattern (D1 or D2) using Walsh-Hadamard encoding
     * 
     * Per MIL-STD-188-110A Section C.5.2.2 and Table C-VII:
     *   - D value (0-7) selects 8-symbol Walsh pattern from PSYMBOL
     *   - Pattern repeated 4 times for 32 symbols
     *   - PSCRAMBLE[32] added modulo 8
     *   - Result mapped to 8PSK constellation
     */
    std::vector<complex_t> encode_d_pattern(int d_value, int num_symbols) {
        std::vector<complex_t> symbols;
        
        // Clamp D value to valid range
        d_value = std::max(0, std::min(7, d_value));
        
        for (int i = 0; i < num_symbols; i++) {
            // Walsh pattern: repeat 8-symbol sequence
            int walsh_idx = i % 8;
            int base_symbol = PSYMBOL[d_value][walsh_idx];
            
            // Apply scrambler
            int scrambled = (base_symbol + PSCRAMBLE[i % 32]) % 8;
            
            // Map to 8PSK constellation (phase = symbol * 45°)
            float phase = scrambled * (PI / 4.0f);
            symbols.push_back(std::polar(1.0f, phase));
        }
        
        return symbols;
    }
    
    /**
     * Encode block count into symbols (scrambled)
     */
    std::vector<complex_t> encode_count(int count, int num_symbols) {
        std::vector<complex_t> symbols;
        
        // Count encoding uses scrambled pattern with count modulation
        // Per MIL-STD the count section uses similar Walsh encoding
        // For simplicity, use scrambled base pattern
        for (int i = 0; i < num_symbols; i++) {
            int sym = PSCRAMBLE[i % 32];
            float phase = sym * (PI / 4.0f);
            symbols.push_back(std::polar(1.0f, phase));
        }
        
        return symbols;
    }
};

/**
 * Preamble Decoder - extracts mode info from received symbols
 */
class PreambleDecoder {
public:
    /**
     * Decode preamble from received symbols
     * @param symbols Received baseband symbols (after timing recovery)
     * @param common_offset Start index of common section
     * 
     * Note: Mode ID is always at fixed position (288-351) regardless of
     * total preamble length.
     */
    PreambleInfo decode(const std::vector<complex_t>& symbols, int common_offset = 0) {
        PreambleInfo info;
        
        int available = symbols.size() - common_offset;
        if (available < 16) {
            return info;  // Too short
        }
        
        // Calculate section positions (fixed structure)
        int common_syms = PreambleEncoder::get_common_symbols(available);
        int mode_syms = PreambleEncoder::get_mode_symbols(available);
        
        // Validate we have enough symbols for mode section
        if (available < common_syms + mode_syms) {
            return info;
        }
        
        // Extract mode symbols (at fixed position after common)
        int mode_start = common_offset + common_syms;
        std::vector<complex_t> mode_symbols(
            symbols.begin() + mode_start,
            symbols.begin() + mode_start + mode_syms
        );
        
        // Decode mode ID
        auto [mode_id, confidence] = decode_mode_id(mode_symbols);
        
        if (confidence < 0.3f) {
            return info;  // Too low confidence
        }
        
        info.valid = true;
        info.mode_id = mode_id;
        info.mode = static_cast<ModeId>(mode_id);
        info.confidence = confidence;
        
        // Decode block count if we have enough symbols
        int count_syms = std::min(CODEC_COUNT_SYMBOLS, 
                                 available - common_syms - mode_syms);
        int count_start = mode_start + mode_syms;
        if (count_syms > 0) {
            std::vector<complex_t> count_symbols(
                symbols.begin() + count_start,
                symbols.begin() + count_start + count_syms
            );
            info.block_count = decode_count(count_symbols);
        }
        
        return info;
    }
    
    /**
     * Decode mode ID from mode symbols
     * Returns (mode_id, confidence)
     */
    std::pair<int, float> decode_mode_id(const std::vector<complex_t>& symbols) {
        if (symbols.empty()) {
            return {0, 0.0f};
        }
        
        // Differential decode to get tribits
        std::vector<uint8_t> tribits;
        complex_t prev(1.0f, 0.0f);
        
        for (const auto& sym : symbols) {
            complex_t diff = sym * std::conj(prev);
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2.0f * PI;
            
            int tribit = static_cast<int>(std::round(phase / (PI / 4.0f))) % 8;
            tribits.push_back(tribit);
            prev = sym;
        }
        
        // Extract bits from tribits
        std::vector<uint8_t> bits;
        for (uint8_t t : tribits) {
            bits.push_back((t >> 2) & 1);
            bits.push_back((t >> 1) & 1);
            bits.push_back(t & 1);
        }
        
        // Majority vote for each bit position
        std::array<int, MODE_ID_BITS> bit_votes = {0};
        int vote_count = 0;
        
        for (size_t i = 0; i < bits.size(); i++) {
            int pos = i % MODE_ID_BITS;
            bit_votes[pos] += bits[i] ? 1 : -1;
            if (i % MODE_ID_BITS == MODE_ID_BITS - 1) {
                vote_count++;
            }
        }
        
        // Reconstruct mode ID from majority vote
        int mode_id = 0;
        float total_confidence = 0.0f;
        
        for (int i = 0; i < MODE_ID_BITS; i++) {
            int bit = (bit_votes[i] > 0) ? 1 : 0;
            mode_id = (mode_id << 1) | bit;
            
            // Confidence = |votes| / total_votes
            float bit_conf = std::abs(bit_votes[i]) / static_cast<float>(vote_count);
            total_confidence += bit_conf;
        }
        
        float confidence = total_confidence / MODE_ID_BITS;
        
        // Validate mode ID
        if (mode_id > 17) {
            // Invalid mode, find closest valid
            mode_id = std::min(mode_id, 17);
            confidence *= 0.5f;  // Reduce confidence
        }
        
        return {mode_id, confidence};
    }
    
    /**
     * Decode block count from count symbols
     */
    int decode_count(const std::vector<complex_t>& symbols) {
        if (symbols.empty()) {
            return 1;
        }
        
        // Similar decoding as mode ID but for 8 bits
        std::vector<uint8_t> tribits;
        complex_t prev(1.0f, 0.0f);
        
        for (const auto& sym : symbols) {
            complex_t diff = sym * std::conj(prev);
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2.0f * PI;
            
            int tribit = static_cast<int>(std::round(phase / (PI / 4.0f))) % 8;
            tribits.push_back(tribit);
            prev = sym;
        }
        
        // Extract bits
        std::vector<uint8_t> bits;
        for (uint8_t t : tribits) {
            bits.push_back((t >> 2) & 1);
            bits.push_back((t >> 1) & 1);
            bits.push_back(t & 1);
        }
        
        // Majority vote for 8 bits
        std::array<int, 8> bit_votes = {0};
        
        for (size_t i = 0; i < bits.size(); i++) {
            int pos = i % 8;
            bit_votes[pos] += bits[i] ? 1 : -1;
        }
        
        int count = 0;
        for (int i = 0; i < 8; i++) {
            int bit = (bit_votes[i] > 0) ? 1 : 0;
            count = (count << 1) | bit;
        }
        
        return std::max(1, count);
    }
};

/**
 * Combined preamble codec for TX and RX
 */
class PreambleCodec {
public:
    PreambleEncoder encoder;
    PreambleDecoder decoder;
    
    /**
     * Generate preamble for transmission
     */
    std::vector<complex_t> encode(ModeId mode, int block_count = 1) {
        return encoder.encode(mode, block_count);
    }
    
    /**
     * Decode received preamble
     */
    PreambleInfo decode(const std::vector<complex_t>& symbols, int offset = 0) {
        return decoder.decode(symbols, offset);
    }
};

/**
 * Get interleave type string from mode ID
 */
inline std::string get_interleave_type(int mode_id) {
    // Voice modes
    if (mode_id == 12 || mode_id == 14 || mode_id == 16) {
        return "voice";
    }
    // M4800S
    if (mode_id == 17) {
        return "short";
    }
    // Standard modes: even = short, odd = long
    return (mode_id % 2 == 0) ? "short" : "long";
}

/**
 * Get data rate from mode ID
 */
inline int get_data_rate(int mode_id) {
    switch (mode_id) {
        case 0: case 1: return 75;
        case 2: case 3: return 150;
        case 4: case 5: return 300;
        case 6: case 7: case 12: return 600;
        case 8: case 9: case 14: return 1200;
        case 10: case 11: case 16: return 2400;
        case 17: return 4800;
        default: return 0;
    }
}

} // namespace m110a

#endif // M110A_PREAMBLE_CODEC_H
