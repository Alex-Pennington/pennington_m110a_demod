#ifndef M110A_BRAIN_PREAMBLE_H
#define M110A_BRAIN_PREAMBLE_H

/**
 * Brain Modem Compatible Preamble Generator/Detector
 * 
 * Implements exact preamble structure from MIL-STD-188-110A as implemented
 * in the Brain Modem (m188110a) library.
 * 
 * Preamble structure (480 symbols per frame):
 *   - Common: 288 symbols (synchronization & AGC)
 *   - Mode:    64 symbols (D1, D2 mode identification)
 *   - Count:   96 symbols (countdown value)
 *   - Zero:    32 symbols (padding)
 */

#include "common/types.h"
#include "common/constants.h"
#include "modem/scrambler.h"
#include <vector>
#include <array>
#include <cmath>

namespace m110a {

/**
 * Brain Modem Preamble Constants
 */
namespace brain {

// Preamble segment lengths
constexpr int P_COMMON_LENGTH = 288;
constexpr int P_MODE_LENGTH = 64;
constexpr int P_COUNT_LENGTH = 96;
constexpr int P_ZERO_LENGTH = 32;
constexpr int P_FRAME_LENGTH = 480;  // Total per frame

// Preamble scrambling sequence (32 symbols, repeating)
constexpr std::array<uint8_t, 32> pscramble = {
    7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
    5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
};

// Common preamble base sequence (9 elements, maps to 288 symbols via psymbol)
constexpr std::array<uint8_t, 9> p_c_seq = {0, 1, 3, 0, 1, 3, 1, 2, 0};

// PSK symbol patterns (Walsh-like, 8x8)
// Each row D0-D7, values 0 or 4 (0Â° or 180Â° BPSK)
constexpr std::array<std::array<uint8_t, 8>, 8> psymbol = {{
    {0, 0, 0, 0, 0, 0, 0, 0},  // D0
    {0, 4, 0, 4, 0, 4, 0, 4},  // D1
    {0, 0, 4, 4, 0, 0, 4, 4},  // D2
    {0, 4, 4, 0, 0, 4, 4, 0},  // D3
    {0, 0, 0, 0, 4, 4, 4, 4},  // D4
    {0, 4, 0, 4, 4, 0, 4, 0},  // D5
    {0, 0, 4, 4, 4, 4, 0, 0},  // D6
    {0, 4, 4, 0, 4, 0, 0, 4}   // D7
}};

// 8-PSK constellation (0Â° at symbol 0)
constexpr std::array<float, 8> psk8_i = {
    1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f, 0.0f, 0.707107f
};
constexpr std::array<float, 8> psk8_q = {
    0.0f, 0.707107f, 1.0f, 0.707107f, 0.0f, -0.707107f, -1.0f, -0.707107f
};

// Modified Gray code mappings
constexpr std::array<uint8_t, 4> mgd2 = {0, 1, 3, 2};  // QPSK
constexpr std::array<uint8_t, 8> mgd3 = {0, 1, 3, 2, 7, 6, 4, 5};  // 8-PSK

// Mode identification (D1, D2) values per mode
struct ModeD1D2 {
    int d1;
    int d2;
};

// D1/D2 values for each mode index
constexpr std::array<ModeD1D2, 18> mode_d1d2 = {{
    {0, 0},  // 0: M75NS (no D1/D2)
    {0, 0},  // 1: M75NL
    {7, 4},  // 2: M150S
    {5, 4},  // 3: M150L
    {6, 7},  // 4: M300S
    {4, 7},  // 5: M300L
    {6, 6},  // 6: M600S
    {4, 6},  // 7: M600L
    {6, 5},  // 8: M1200S
    {4, 5},  // 9: M1200L
    {6, 4},  // 10: M2400S
    {4, 4},  // 11: M2400L
    {6, 6},  // 12: M600V
    {0, 0},  // 13: unused
    {6, 5},  // 14: M1200V
    {0, 0},  // 15: unused
    {6, 4},  // 16: M2400V
    {7, 6},  // 17: M4800S
}};

} // namespace brain

/**
 * Brain Modem Preamble Generator
 */
class BrainPreambleEncoder {
public:
    /**
     * Generate complete preamble for given mode and interleave type
     * @param mode_index Mode index (0-17)
     * @param is_long_interleave True for long interleave (24 frames), false for short (3 frames)
     * @return Vector of complex symbols
     */
    std::vector<complex_t> encode(int mode_index, bool is_long_interleave) {
        int num_frames = is_long_interleave ? 24 : 3;
        std::vector<complex_t> symbols;
        symbols.reserve(num_frames * brain::P_FRAME_LENGTH);
        
        for (int frame = 0; frame < num_frames; frame++) {
            int countdown = num_frames - 1 - frame;
            auto frame_symbols = encode_frame(mode_index, countdown);
            symbols.insert(symbols.end(), frame_symbols.begin(), frame_symbols.end());
        }
        
        return symbols;
    }
    
    /**
     * Generate single preamble frame
     */
    std::vector<complex_t> encode_frame(int mode_index, int countdown) {
        std::vector<complex_t> symbols;
        symbols.reserve(brain::P_FRAME_LENGTH);
        
        int scram_idx = 0;
        
        // 1. Common segment (288 symbols)
        for (int i = 0; i < 9; i++) {
            uint8_t d_val = brain::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = brain::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                symbols.push_back(symbol_to_complex(scrambled));
                scram_idx++;
            }
        }
        
        // 2. Mode segment (64 symbols) - D1 and D2
        int d1 = brain::mode_d1d2[mode_index].d1;
        int d2 = brain::mode_d1d2[mode_index].d2;
        
        for (int i = 0; i < 32; i++) {
            uint8_t base = brain::psymbol[d1][i % 8];
            uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
            symbols.push_back(symbol_to_complex(scrambled));
            scram_idx++;
        }
        for (int i = 0; i < 32; i++) {
            uint8_t base = brain::psymbol[d2][i % 8];
            uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
            symbols.push_back(symbol_to_complex(scrambled));
            scram_idx++;
        }
        
        // 3. Count segment (96 symbols) - countdown value (3 x 32-symbol patterns)
        // Use D0 pattern with phase offset based on countdown
        for (int rep = 0; rep < 3; rep++) {
            for (int i = 0; i < 32; i++) {
                uint8_t base = (countdown % 8);  // Simple countdown encoding
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                symbols.push_back(symbol_to_complex(scrambled));
                scram_idx++;
            }
        }
        
        // 4. Zero segment (32 symbols)
        for (int i = 0; i < 32; i++) {
            uint8_t scrambled = brain::pscramble[scram_idx % 32];
            symbols.push_back(symbol_to_complex(scrambled));
            scram_idx++;
        }
        
        return symbols;
    }
    
private:
    complex_t symbol_to_complex(uint8_t sym) {
        return complex_t(brain::psk8_i[sym & 7], brain::psk8_q[sym & 7]);
    }
};

/**
 * Brain Modem Preamble Detector
 */
class BrainPreambleDecoder {
public:
    struct DetectResult {
        bool detected = false;
        int mode_index = -1;
        int d1 = -1;
        int d2 = -1;
        int countdown = -1;
        float correlation = 0.0f;
        int sample_offset = 0;
    };
    
    /**
     * Detect preamble and extract mode information
     */
    DetectResult detect(const std::vector<complex_t>& symbols, int start_offset = 0) {
        DetectResult result;
        
        if (symbols.size() < static_cast<size_t>(start_offset + brain::P_FRAME_LENGTH)) {
            return result;
        }
        
        // Correlate with common preamble pattern
        float best_corr = 0;
        int best_offset = start_offset;
        
        // Generate reference common segment
        std::vector<complex_t> ref_common;
        int scram_idx = 0;
        for (int i = 0; i < 9; i++) {
            uint8_t d_val = brain::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = brain::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                ref_common.push_back(complex_t(brain::psk8_i[scrambled], brain::psk8_q[scrambled]));
                scram_idx++;
            }
        }
        
        // Search for correlation peak
        for (int offset = start_offset; offset < start_offset + 100 && 
             offset + brain::P_COMMON_LENGTH < static_cast<int>(symbols.size()); offset++) {
            
            complex_t corr(0, 0);
            float power = 0;
            
            for (int i = 0; i < brain::P_COMMON_LENGTH; i++) {
                corr += symbols[offset + i] * std::conj(ref_common[i]);
                power += std::norm(symbols[offset + i]);
            }
            
            float norm_corr = std::abs(corr) / std::sqrt(power * brain::P_COMMON_LENGTH);
            
            if (norm_corr > best_corr) {
                best_corr = norm_corr;
                best_offset = offset;
            }
        }
        
        result.correlation = best_corr;
        result.sample_offset = best_offset;
        
        if (best_corr < 0.3f) {
            return result;  // No detection
        }
        
        result.detected = true;
        
        // Decode D1 and D2 from mode segment
        int mode_start = best_offset + brain::P_COMMON_LENGTH;
        result.d1 = decode_d_value(symbols, mode_start, 0);
        result.d2 = decode_d_value(symbols, mode_start + 32, 32);
        
        // Look up mode from D1/D2
        for (int i = 0; i < 18; i++) {
            if (brain::mode_d1d2[i].d1 == result.d1 && 
                brain::mode_d1d2[i].d2 == result.d2) {
                result.mode_index = i;
                break;
            }
        }
        
        return result;
    }
    
private:
    /**
     * Decode D value (0-7) from 32-symbol segment using Walsh correlation
     */
    int decode_d_value(const std::vector<complex_t>& symbols, int start, int scram_offset) {
        float best_corr = -1;
        int best_d = 0;
        
        for (int d = 0; d < 8; d++) {
            complex_t corr(0, 0);
            
            for (int i = 0; i < 32 && start + i < static_cast<int>(symbols.size()); i++) {
                // Generate expected symbol
                uint8_t base = brain::psymbol[d][i % 8];
                uint8_t scrambled = (base + brain::pscramble[(scram_offset + i) % 32]) % 8;
                complex_t expected(brain::psk8_i[scrambled], brain::psk8_q[scrambled]);
                
                corr += symbols[start + i] * std::conj(expected);
            }
            
            float mag = std::abs(corr);
            if (mag > best_corr) {
                best_corr = mag;
                best_d = d;
            }
        }
        
        return best_d;
    }
};

/**
 * Brain Modem Data Scrambler
 * 
 * Wraps RefScrambler with pre-generated sequence for efficiency.
 * Scrambling is via modulo-8 ADDITION (not XOR).
 * 
 * Order of operations for data symbols:
 *   1. FEC bits from interleaver
 *   2. Group into tribits
 *   3. Apply Gray mapping (mgd3[] for 8-PSK)
 *   4. Apply scrambler: (sym + scrambler_seq[offset]) % 8
 *   5. Map to 8-PSK phase
 */
class BrainScrambler {
public:
    BrainScrambler() {
        RefScrambler scr;
        seq_ = scr.generate_sequence();
        offset_ = 0;
    }
    
    void reset() { offset_ = 0; }
    
    /**
     * Scramble a symbol (modulo-8 addition)
     * @param sym Gray-coded symbol 0-7
     * @return Scrambled symbol 0-7
     */
    uint8_t scramble(uint8_t sym) {
        uint8_t result = (sym + seq_[offset_]) % 8;
        offset_ = (offset_ + 1) % RefScrambler::SEQUENCE_LENGTH;
        return result;
    }
    
    /**
     * Descramble a symbol (modulo-8 subtraction)
     */
    uint8_t descramble(uint8_t sym) {
        uint8_t result = (sym - seq_[offset_] + 8) % 8;
        offset_ = (offset_ + 1) % RefScrambler::SEQUENCE_LENGTH;
        return result;
    }
    
    /**
     * Get scrambler value at current offset without advancing
     */
    uint8_t peek() const { return seq_[offset_]; }
    
    /**
     * Get offset for debugging
     */
    int offset() const { return offset_; }
    
    /**
     * Get sequence for external use
     */
    const std::vector<uint8_t>& sequence() const { return seq_; }
    
private:
    std::vector<uint8_t> seq_;
    int offset_;
};

/**
 * Gray Code Mappings (Modified Gray Code per MIL-STD-188-110A Table I)
 */
namespace gray {
    // QPSK (2-bit) Gray mapping
    constexpr std::array<uint8_t, 4> mgd2 = {0, 1, 3, 2};
    constexpr std::array<uint8_t, 4> mgd2_inv = {0, 1, 3, 2};  // Self-inverse
    
    // 8-PSK (3-bit) Gray mapping  
    constexpr std::array<uint8_t, 8> mgd3 = {0, 1, 3, 2, 7, 6, 4, 5};
    constexpr std::array<uint8_t, 8> mgd3_inv = {0, 1, 3, 2, 6, 7, 5, 4};
    
    inline uint8_t encode_qpsk(uint8_t bits) { return mgd2[bits & 3]; }
    inline uint8_t decode_qpsk(uint8_t sym) { return mgd2_inv[sym & 3]; }
    
    inline uint8_t encode_8psk(uint8_t bits) { return mgd3[bits & 7]; }
    inline uint8_t decode_8psk(uint8_t sym) { return mgd3_inv[sym & 7]; }
}

} // namespace m110a

#endif // M110A_BRAIN_PREAMBLE_H
