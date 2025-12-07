#ifndef M110A_PREAMBLE_CODEC_H
#define M110A_PREAMBLE_CODEC_H

/**
 * MIL-STD-188-110A Preamble Encoder/Decoder
 * 
 * Preamble structure (all 8PSK at mode's symbol rate):
 *   - 288 common symbols: Known scrambled pattern (sync)
 *   - 64 mode symbols: Mode ID with repetition coding
 *   - 96 count symbols: Block count (not decoded here)
 *   - 32 zero symbols: All zeros (channel estimation)
 * 
 * Mode ID encoding (64 symbols = 192 bits):
 *   - 5-bit mode ID (0-17) repeated with majority voting
 *   - Each tribit = 3 bits, so 64 tribits = 192 bits
 *   - Mode ID repeated 192/5 ≈ 38 times
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

namespace m110a {

// Preamble structure constants (redefined locally to avoid conflict with mode_detector.h)
constexpr int CODEC_COMMON_SYMBOLS = 288;
constexpr int CODEC_MODE_SYMBOLS = 64;
constexpr int CODEC_COUNT_SYMBOLS = 96;
constexpr int CODEC_ZERO_SYMBOLS = 32;
constexpr int MODE_ID_BITS = 5;

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
 * Preamble Encoder - generates proper MIL-STD-188-110A preamble
 */
class PreambleEncoder {
public:
    /**
     * Generate complete preamble for given mode
     * 
     * Structure:
     *   For preambles >= 480 symbols (standard):
     *     - First 288 symbols: Scrambled known pattern (sync)
     *     - Next 64 symbols: Mode ID with repetition coding  
     *     - Next 96 symbols: Block count
     *     - Remaining: Zeros for channel estimation
     *   
     *   For shorter preambles (scaled proportionally):
     *     - Common: 60% of total
     *     - Mode: 13% of total (minimum 8)
     *     - Count: 20% of total
     *     - Zero: 7% of total
     */
    std::vector<complex_t> encode(ModeId mode, int block_count = 1) {
        std::vector<complex_t> symbols;
        
        auto mode_cfg = ModeDatabase::get(mode);
        int total_symbols = mode_cfg.preamble_symbols();
        
        // Calculate section sizes
        int common_syms, mode_syms, count_syms;
        if (total_symbols >= 480) {
            // Standard structure
            common_syms = CODEC_COMMON_SYMBOLS;  // 288
            mode_syms = CODEC_MODE_SYMBOLS;      // 64
            count_syms = CODEC_COUNT_SYMBOLS;    // 96
        } else {
            // Proportional structure for short preambles
            common_syms = (total_symbols * 60) / 100;
            mode_syms = std::max(8, (total_symbols * 13) / 100);
            count_syms = (total_symbols * 20) / 100;
        }
        
        MultiModeMapper mapper(Modulation::PSK8);
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);  // TODO: Use RefScrambler for reference compatibility
        // RefScrambler scr;  // Uncomment for reference file compatibility
        
        int mode_id = static_cast<int>(mode);
        
        // Section 1: Common symbols (scrambled)
        for (int i = 0; i < common_syms && static_cast<int>(symbols.size()) < total_symbols; i++) {
            uint8_t tribit = scr.next_tribit();
            symbols.push_back(mapper.map(tribit));
        }
        
        // Section 2: Mode symbols
        mode_syms = std::min(mode_syms, total_symbols - static_cast<int>(symbols.size()));
        if (mode_syms > 0) {
            auto mode_symbols = encode_mode_id(mode_id, mode_syms);
            symbols.insert(symbols.end(), mode_symbols.begin(), mode_symbols.end());
        }
        
        // Section 3: Count symbols
        count_syms = std::min(count_syms, total_symbols - static_cast<int>(symbols.size()));
        if (count_syms > 0) {
            auto count_symbols = encode_count(block_count, count_syms);
            symbols.insert(symbols.end(), count_symbols.begin(), count_symbols.end());
        }
        
        // Section 4: Pad with zeros to total
        while (static_cast<int>(symbols.size()) < total_symbols) {
            symbols.push_back(complex_t(1.0f, 0.0f));
        }
        
        return symbols;
    }
    
    /**
     * Get common symbol count for a given preamble size
     */
    static int get_common_symbols(int total_preamble) {
        if (total_preamble >= 480) {
            return CODEC_COMMON_SYMBOLS;  // 288
        }
        return (total_preamble * 60) / 100;
    }
    
    /**
     * Get mode symbol count for a given preamble size
     */
    static int get_mode_symbols(int total_preamble) {
        if (total_preamble >= 480) {
            return CODEC_MODE_SYMBOLS;  // 64
        }
        return std::max(8, (total_preamble * 13) / 100);
    }

private:
    /**
     * Encode mode ID into symbols using repetition
     */
    std::vector<complex_t> encode_mode_id(int mode_id, int num_symbols) {
        std::vector<complex_t> symbols;
        MultiModeMapper mapper(Modulation::PSK8);
        
        // Convert mode ID to bit pattern and repeat
        std::vector<uint8_t> bits;
        for (int i = MODE_ID_BITS - 1; i >= 0; i--) {
            bits.push_back((mode_id >> i) & 1);
        }
        
        // Generate tribits from repeated bit pattern
        int bit_idx = 0;
        complex_t prev(1.0f, 0.0f);
        
        for (int i = 0; i < num_symbols; i++) {
            // Pack 3 bits into tribit
            uint8_t tribit = 0;
            for (int b = 0; b < 3; b++) {
                tribit = (tribit << 1) | bits[bit_idx % bits.size()];
                bit_idx++;
            }
            
            // Differential encode
            float phase_inc = tribit * (PI / 4.0f);
            complex_t sym = prev * std::polar(1.0f, phase_inc);
            symbols.push_back(sym);
            prev = sym;
        }
        
        return symbols;
    }
    
    /**
     * Encode block count into symbols
     */
    std::vector<complex_t> encode_count(int count, int num_symbols) {
        std::vector<complex_t> symbols;
        MultiModeMapper mapper(Modulation::PSK8);
        
        // Similar to mode encoding but with count value
        std::vector<uint8_t> bits;
        for (int i = 7; i >= 0; i--) {  // 8-bit count
            bits.push_back((count >> i) & 1);
        }
        
        int bit_idx = 0;
        complex_t prev(1.0f, 0.0f);
        
        for (int i = 0; i < num_symbols; i++) {
            uint8_t tribit = 0;
            for (int b = 0; b < 3; b++) {
                tribit = (tribit << 1) | bits[bit_idx % bits.size()];
                bit_idx++;
            }
            
            float phase_inc = tribit * (PI / 4.0f);
            complex_t sym = prev * std::polar(1.0f, phase_inc);
            symbols.push_back(sym);
            prev = sym;
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
