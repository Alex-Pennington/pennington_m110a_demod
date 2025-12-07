#ifndef M110A_MODE_CONFIG_H
#define M110A_MODE_CONFIG_H

/**
 * MIL-STD-188-110A Multi-Mode Configuration
 * 
 * Supports all standard modes:
 *   - 75 bps BPSK (M75NS, M75NL)
 *   - 150 bps QPSK (M150S, M150L)
 *   - 300 bps QPSK (M300S, M300L)
 *   - 600 bps QPSK (M600S, M600L, M600V)
 *   - 1200 bps QPSK (M1200S, M1200L, M1200V)
 *   - 2400 bps 8PSK (M2400S, M2400L, M2400V)
 *   - 4800 bps 8PSK (M4800S)
 */

#include "common/types.h"
#include <string>
#include <map>
#include <cmath>

namespace m110a {

/**
 * Modulation types
 */
enum class Modulation {
    BPSK,   // 1 bit per symbol
    QPSK,   // 2 bits per symbol
    PSK8    // 3 bits per symbol
};

/**
 * Interleave types
 */
enum class InterleaveType {
    NONE,   // No interleaving (for testing)
    SHORT,  // 0.6 second depth
    LONG,   // 4.8 second depth
    VOICE   // Voice mode (same as SHORT)
};

/**
 * Mode identifier enum matching modes.json
 */
enum class ModeId {
    M75NS   = 0,
    M75NL   = 1,
    M150S   = 2,
    M150L   = 3,
    M300S   = 4,
    M300L   = 5,
    M600S   = 6,
    M600L   = 7,
    M1200S  = 8,
    M1200L  = 9,
    M2400S  = 10,
    M2400L  = 11,
    M600V   = 12,
    M1200V  = 14,
    M2400V  = 16,
    M4800S  = 17
};

/**
 * Interleaver parameters
 */
struct InterleaverParams {
    int rows;
    int cols;
    int row_inc;
    int col_inc;
    int block_count_mod;
    
    int block_size() const { return rows * cols; }
};

/**
 * Complete mode configuration
 */
struct ModeConfig {
    ModeId id;
    std::string name;
    int bps;                    // Data rate in bits per second
    Modulation modulation;
    int bits_per_symbol;
    int symbol_rate;            // Symbols per second (always 2400 for 110A)
    int symbol_repetition;      // Symbol repetition factor (32x for 75bps, etc.)
    InterleaveType interleave_type;
    float interleave_depth_sec;
    int preamble_frames;
    InterleaverParams interleaver;
    int unknown_data_len;       // Data symbols between known probes
    int known_data_len;         // Known probe symbol length
    int d1_sequence;            // D1 mode identification (0-7)
    int d2_sequence;            // D2 mode identification (0-7)
    
    // Derived parameters
    int symbols_per_frame() const {
        // 200ms frame at 2400 baud = 480 symbols per frame
        return 2400 / 5;  // 200ms = 1/5 second = 480 symbols
    }
    
    int data_symbols_per_frame() const {
        // Per MS-DMT: unknown_data_len + known_data_len pattern
        // For 75bps modes without probes, all symbols are data
        if (unknown_data_len == 0) {
            return symbols_per_frame();  // 75bps modes
        }
        // For other modes: ratio of data to probe symbols
        int total = symbols_per_frame();
        int pattern_len = unknown_data_len + known_data_len;
        int patterns_per_frame = total / pattern_len;
        return patterns_per_frame * unknown_data_len;
    }
    
    int probe_symbols_per_frame() const {
        return symbols_per_frame() - data_symbols_per_frame();
    }
    
    int preamble_symbols() const {
        // Each preamble frame is 480 symbols (200ms at 2400 baud)
        return preamble_frames * 480;
    }
    
    // Effective coded bits per symbol (after FEC and repetition)
    float effective_bits_per_symbol() const {
        // bits_per_symbol / (FEC_rate * repetition)
        // For coded modes: FEC_rate = 2 (rate 1/2)
        // For 4800 uncoded: FEC_rate = 1
        int fec_expansion = (bps == 4800) ? 1 : 2;
        return static_cast<float>(bits_per_symbol) / (fec_expansion * symbol_repetition);
    }
};

/**
 * Mode database - all supported modes
 */
class ModeDatabase {
public:
    static const ModeConfig& get(ModeId id) {
        static ModeDatabase db;
        return db.modes_.at(id);
    }
    
    static const ModeConfig& get(const std::string& name) {
        static ModeDatabase db;
        for (const auto& [id, cfg] : db.modes_) {
            if (cfg.name == name) return cfg;
        }
        throw std::runtime_error("Unknown mode: " + name);
    }
    
    static std::vector<ModeId> all_modes() {
        return {
            ModeId::M75NS, ModeId::M75NL,
            ModeId::M150S, ModeId::M150L,
            ModeId::M300S, ModeId::M300L,
            ModeId::M600S, ModeId::M600L, ModeId::M600V,
            ModeId::M1200S, ModeId::M1200L, ModeId::M1200V,
            ModeId::M2400S, ModeId::M2400L, ModeId::M2400V,
            ModeId::M4800S
        };
    }

private:
    std::map<ModeId, ModeConfig> modes_;
    
    ModeDatabase() {
        // ====================================================================
        // MIL-STD-188-110A Mode Parameters (MS-DMT compatible)
        // Symbol rate is ALWAYS 2400 baud for all modes
        // Different data rates achieved via repetition and FEC
        // ====================================================================
        
        // 75 bps BPSK modes - 32x symbol repetition with Walsh coding
        // No probe symbols for 75bps modes
        modes_[ModeId::M75NS] = {
            ModeId::M75NS, "M75NS", 75, Modulation::BPSK, 1, 2400, 32,
            InterleaveType::SHORT, 0.6f, 3,
            {10, 9, 7, 2, 45},
            0, 0, 0, 0  // No probe symbols, no D1/D2
        };
        modes_[ModeId::M75NL] = {
            ModeId::M75NL, "M75NL", 75, Modulation::BPSK, 1, 2400, 32,
            InterleaveType::LONG, 4.8f, 24,
            {20, 36, 7, 29, 360},
            0, 0, 0, 0
        };
        
        // 150 bps BPSK modes - 8x repetition (rate 1/2 Viterbi + 4x repetition)
        modes_[ModeId::M150S] = {
            ModeId::M150S, "M150S", 150, Modulation::BPSK, 1, 2400, 4,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 18, 9, 1, 36},
            20, 20, 7, 4
        };
        modes_[ModeId::M150L] = {
            ModeId::M150L, "M150L", 150, Modulation::BPSK, 1, 2400, 4,
            InterleaveType::LONG, 4.8f, 24,
            {40, 144, 9, 127, 288},
            20, 20, 5, 4
        };
        
        // 300 bps BPSK modes - 4x repetition (rate 1/2 Viterbi + 2x repetition)
        modes_[ModeId::M300S] = {
            ModeId::M300S, "M300S", 300, Modulation::BPSK, 1, 2400, 2,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 18, 9, 1, 36},
            20, 20, 6, 7
        };
        modes_[ModeId::M300L] = {
            ModeId::M300L, "M300L", 300, Modulation::BPSK, 1, 2400, 2,
            InterleaveType::LONG, 4.8f, 24,
            {40, 144, 9, 127, 288},
            20, 20, 4, 7
        };
        
        // 600 bps BPSK modes - 2x repetition (rate 1/2 Viterbi only)
        modes_[ModeId::M600S] = {
            ModeId::M600S, "M600S", 600, Modulation::BPSK, 1, 2400, 1,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 18, 9, 1, 36},
            20, 20, 6, 6
        };
        modes_[ModeId::M600L] = {
            ModeId::M600L, "M600L", 600, Modulation::BPSK, 1, 2400, 1,
            InterleaveType::LONG, 4.8f, 24,
            {40, 144, 9, 127, 288},
            20, 20, 4, 6
        };
        modes_[ModeId::M600V] = {
            ModeId::M600V, "M600V", 600, Modulation::BPSK, 1, 2400, 1,
            InterleaveType::VOICE, 0.6f, 3,
            {40, 18, 9, 1, 36},
            20, 20, 6, 6
        };
        
        // 1200 bps QPSK modes - 1x (no repetition, rate 1/2 Viterbi only)
        modes_[ModeId::M1200S] = {
            ModeId::M1200S, "M1200S", 1200, Modulation::QPSK, 2, 2400, 1,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 36, 9, 19, 36},
            20, 20, 6, 5
        };
        modes_[ModeId::M1200L] = {
            ModeId::M1200L, "M1200L", 1200, Modulation::QPSK, 2, 2400, 1,
            InterleaveType::LONG, 4.8f, 24,
            {40, 288, 9, 271, 288},
            20, 20, 4, 5
        };
        modes_[ModeId::M1200V] = {
            ModeId::M1200V, "M1200V", 1200, Modulation::QPSK, 2, 2400, 1,
            InterleaveType::VOICE, 0.6f, 3,
            {40, 36, 9, 19, 36},
            20, 20, 6, 5
        };
        
        // 2400 bps 8PSK modes - 1x (no repetition, rate 1/2 Viterbi)
        modes_[ModeId::M2400S] = {
            ModeId::M2400S, "M2400S", 2400, Modulation::PSK8, 3, 2400, 1,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 72, 9, 55, 30},
            32, 16, 6, 4
        };
        modes_[ModeId::M2400L] = {
            ModeId::M2400L, "M2400L", 2400, Modulation::PSK8, 3, 2400, 1,
            InterleaveType::LONG, 4.8f, 24,
            {40, 576, 9, 559, 240},
            32, 16, 4, 4
        };
        modes_[ModeId::M2400V] = {
            ModeId::M2400V, "M2400V", 2400, Modulation::PSK8, 3, 2400, 1,
            InterleaveType::VOICE, 0.6f, 3,
            {40, 72, 0, 0, 30},
            32, 16, 6, 4
        };
        
        // 4800 bps 8PSK mode - NO FEC (uncoded), 1x repetition
        modes_[ModeId::M4800S] = {
            ModeId::M4800S, "M4800S", 4800, Modulation::PSK8, 3, 2400, 1,
            InterleaveType::SHORT, 0.6f, 3,
            {40, 72, 0, 0, 30},  // row_inc=0 means no row interleaving
            32, 16, 7, 6
        };
    }
};

/**
 * Get modulation order (number of constellation points)
 */
inline int modulation_order(Modulation mod) {
    switch (mod) {
        case Modulation::BPSK: return 2;
        case Modulation::QPSK: return 4;
        case Modulation::PSK8: return 8;
        default: return 8;
    }
}

/**
 * Get bits per symbol for modulation
 */
inline int bits_per_symbol(Modulation mod) {
    switch (mod) {
        case Modulation::BPSK: return 1;
        case Modulation::QPSK: return 2;
        case Modulation::PSK8: return 3;
        default: return 3;
    }
}

/**
 * Convert mode name string to ModeId
 */
inline ModeId mode_from_string(const std::string& name) {
    static const std::map<std::string, ModeId> name_map = {
        {"M75NS", ModeId::M75NS}, {"M75NL", ModeId::M75NL},
        {"M150S", ModeId::M150S}, {"M150L", ModeId::M150L},
        {"M300S", ModeId::M300S}, {"M300L", ModeId::M300L},
        {"M600S", ModeId::M600S}, {"M600L", ModeId::M600L}, {"M600V", ModeId::M600V},
        {"M1200S", ModeId::M1200S}, {"M1200L", ModeId::M1200L}, {"M1200V", ModeId::M1200V},
        {"M2400S", ModeId::M2400S}, {"M2400L", ModeId::M2400L}, {"M2400V", ModeId::M2400V},
        {"M4800S", ModeId::M4800S}
    };
    
    auto it = name_map.find(name);
    if (it != name_map.end()) return it->second;
    throw std::runtime_error("Unknown mode: " + name);
}

/**
 * Convert ModeId to string
 */
inline std::string mode_to_string(ModeId id) {
    return ModeDatabase::get(id).name;
}

} // namespace m110a

#endif // M110A_MODE_CONFIG_H
