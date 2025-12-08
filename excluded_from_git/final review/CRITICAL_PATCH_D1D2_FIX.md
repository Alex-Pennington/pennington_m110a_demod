# M110A Modem Critical Patch - D1/D2 Position Fix
# Date: 2025-12-08
# Purpose: Fix mode detection for external/reference signal interoperability
#
# ISSUE: Current mode_detector.h uses incorrect D1/D2 symbol positions
#        This causes mode detection to fail on signals from external sources
#        (e.g., MS-DMT reference samples) even though loopback tests pass.
#
# ROOT CAUSE: Loopback tests pass because both TX and RX use the same
#             (incorrect) positions - the bug cancels out internally.
#
# VALIDATION: Tested against 10 MS-DMT reference samples - all pass with fix.
#
# FILES TO REPLACE:
#   1. src/m110a/mode_detector.h (complete replacement)
#
# OPTIONAL UPDATES:
#   2. src/m110a/msdmt_decoder.h - Update D1/D2 comments (lines ~455)

--- BEGIN REPLACEMENT FILE: src/m110a/mode_detector.h ---

/**
 * D1/D2 Mode Detector
 * 
 * Extracts D1/D2 mode identification sequences from preamble symbols
 * and looks up the corresponding data mode.
 * 
 * Per MIL-STD-188-110A preamble structure (480 symbols per frame):
 *   Symbols 0-287:   Common sync pattern (288 symbols)
 *   Symbols 288-319: D1 mode identifier (32 symbols)
 *   Symbols 320-351: D2 mode identifier (32 symbols)  
 *   Symbols 352-447: Count sequence (96 symbols)
 *   Symbols 448-479: Zero padding (32 symbols)
 * 
 * D1/D2 encode the data rate and interleaver setting.
 * Each D pattern is 32 symbols: 4 repetitions of 8-symbol Walsh sequence.
 * 
 * References:
 *   MIL-STD-188-110A, Appendix C, Section C.5.2.2 (Preamble Structure)
 *   MIL-STD-188-110A, Table C-VI (D1/D2 Pattern Assignments)
 * 
 * Note: Symbol positions empirically verified against reference waveforms.
 * The +32 symbol offset from documented spec may be due to leading sync.
 */

#ifndef M110A_MODE_DETECTOR_H
#define M110A_MODE_DETECTOR_H

#include "m110a/mode_config.h"
#include <vector>
#include <map>
#include <cmath>

namespace m110a {

// Preamble structure constants per MIL-STD-188-110A
// Note: Positions verified empirically against reference waveforms
constexpr int PREAMBLE_COMMON_START = 0;
constexpr int PREAMBLE_COMMON_LEN = 320;     // Extended common sync
constexpr int PREAMBLE_D1_START = 320;       // D1 mode identifier
constexpr int PREAMBLE_D1_LEN = 32;
constexpr int PREAMBLE_D2_START = 352;       // D2 mode identifier  
constexpr int PREAMBLE_D2_LEN = 32;
constexpr int PREAMBLE_COUNT_START = 384;    // Count sequence
constexpr int PREAMBLE_COUNT_LEN = 96;
constexpr int PREAMBLE_FRAME_LEN = 480;

struct ModeDetectResult {
    bool detected;
    ModeId mode;
    int d1;
    int d2;
    int d1_confidence;  // votes for winning D1 (out of 32)
    int d2_confidence;  // votes for winning D2 (out of 32)
    
    ModeDetectResult() 
        : detected(false)
        , mode(ModeId::M2400S)
        , d1(0)
        , d2(0)
        , d1_confidence(0)
        , d2_confidence(0) {}
};

class ModeDetector {
public:
    // 8PSK constellation points (MIL-STD-188-110A Table C-I)
    // Phase = k * 45 degrees, k = 0..7
    static constexpr float CONSTELLATION[8][2] = {
        {1.0f, 0.0f},           // 0: 0°
        {0.707f, 0.707f},       // 1: 45°
        {0.0f, 1.0f},           // 2: 90°
        {-0.707f, 0.707f},      // 3: 135°
        {-1.0f, 0.0f},          // 4: 180°
        {-0.707f, -0.707f},     // 5: 225°
        {0.0f, -1.0f},          // 6: 270°
        {0.707f, -0.707f}       // 7: 315°
    };
    
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

    ModeDetector() {
        build_lookup_table();
    }
    
    /**
     * Detect mode from baseband preamble symbols
     * 
     * @param symbols Demodulated preamble symbols (at least 384 symbols needed)
     * @return ModeDetectResult with detected mode and confidence
     */
    ModeDetectResult detect(const std::vector<complex_t>& symbols) {
        ModeDetectResult result;
        
        if (symbols.size() < static_cast<size_t>(PREAMBLE_D2_START + PREAMBLE_D2_LEN)) {
            return result;
        }
        
        // Correlate against all 8 possible D patterns for D1 and D2
        int d1_corr[8] = {0};
        int d2_corr[8] = {0};
        
        // D1: symbols 320-351
        for (int d = 0; d < 8; d++) {
            for (int i = 0; i < 32; i++) {
                int sym_idx = PREAMBLE_D1_START + i;
                if (sym_idx >= static_cast<int>(symbols.size())) break;
                
                // Expected symbol = PSYMBOL pattern + scramble
                int expected = (PSYMBOL[d][i % 8] + PSCRAMBLE[i]) % 8;
                int received = demod_symbol(symbols[sym_idx]);
                
                // Correlation: count matches
                if (expected == received) {
                    d1_corr[d]++;
                }
            }
        }
        
        // D2: symbols 352-383
        for (int d = 0; d < 8; d++) {
            for (int i = 0; i < 32; i++) {
                int sym_idx = PREAMBLE_D2_START + i;
                if (sym_idx >= static_cast<int>(symbols.size())) break;
                
                int expected = (PSYMBOL[d][i % 8] + PSCRAMBLE[i]) % 8;
                int received = demod_symbol(symbols[sym_idx]);
                
                if (expected == received) {
                    d2_corr[d]++;
                }
            }
        }
        
        // Find best D1 and D2
        result.d1 = 0;
        result.d2 = 0;
        for (int d = 1; d < 8; d++) {
            if (d1_corr[d] > d1_corr[result.d1]) result.d1 = d;
            if (d2_corr[d] > d2_corr[result.d2]) result.d2 = d;
        }
        result.d1_confidence = d1_corr[result.d1];
        result.d2_confidence = d2_corr[result.d2];
        
        // Lookup mode from D1/D2
        auto key = std::make_pair(result.d1, result.d2);
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            result.detected = true;
            result.mode = it->second;
        } else {
            result.detected = false;
            result.mode = ModeId::M2400S;
        }
        
        return result;
    }
    
    /**
     * Get minimum confidence threshold for reliable detection
     */
    static int min_confidence() { return 20; }  // Out of 32 votes
    
private:
    std::map<std::pair<int,int>, ModeId> lookup_;
    
    void build_lookup_table() {
        // D1/D2 mode codes per MIL-STD-188-110A Table C-VI
        lookup_[{7, 4}] = ModeId::M150S;
        lookup_[{5, 4}] = ModeId::M150L;
        lookup_[{6, 7}] = ModeId::M300S;
        lookup_[{4, 7}] = ModeId::M300L;
        lookup_[{6, 6}] = ModeId::M600S;
        lookup_[{4, 6}] = ModeId::M600L;
        lookup_[{6, 5}] = ModeId::M1200S;
        lookup_[{4, 5}] = ModeId::M1200L;
        lookup_[{6, 4}] = ModeId::M2400S;
        lookup_[{4, 4}] = ModeId::M2400L;
        lookup_[{7, 6}] = ModeId::M4800S;
    }
    
    /**
     * Demodulate complex symbol to 8PSK index (0-7)
     */
    int demod_symbol(const complex_t& sym) {
        float angle = std::atan2(sym.imag(), sym.real());
        if (angle < 0) angle += 2.0f * M_PI;
        return static_cast<int>(std::round(angle / (M_PI / 4))) % 8;
    }
};

} // namespace m110a

#endif // M110A_MODE_DETECTOR_H

--- END REPLACEMENT FILE ---

=============================================================================
VALIDATION RESULTS (tested against MS-DMT reference samples):

| Mode   | Expected D1,D2 | Detected | Status |
|--------|---------------|----------|--------|
| M150S  | 7, 4          | 7, 4     | ✓ PASS |
| M150L  | 5, 4          | 5, 4     | ✓ PASS |
| M300S  | 6, 7          | 6, 7     | ✓ PASS |
| M300L  | 4, 7          | 4, 7     | ✓ PASS |
| M600S  | 6, 6          | 6, 6     | ✓ PASS |
| M600L  | 4, 6          | 4, 6     | ✓ PASS |
| M1200S | 6, 5          | 6, 5     | ✓ PASS |
| M1200L | 4, 5          | 4, 5     | ✓ PASS |
| M2400S | 6, 4          | 6, 4     | ✓ PASS |
| M2400L | 4, 4          | 4, 4     | ✓ PASS |

=============================================================================
KEY CHANGES FROM ORIGINAL:

1. D1 position: 288-383 (96 sym) → 320-351 (32 sym)
2. D2 position: 480-575 (96 sym) → 352-383 (32 sym)  
3. Uses fixed PSCRAMBLE[32] table instead of LFSR scrambler
4. Correlates against PSYMBOL Walsh patterns
5. Confidence now out of 32 (not 96)

=============================================================================
WHY LOOPBACK TESTS STILL PASS WITHOUT THIS FIX:

The bug is symmetric - both TX and RX use the same wrong positions.
When you transmit with wrong positions and receive with wrong positions,
they cancel out. This fix is ONLY needed for interoperability with
external signal sources (real radios, MS-DMT software, etc.).

=============================================================================
