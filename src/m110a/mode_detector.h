/**
 * D1/D2 Mode Detector
 * 
 * Extracts D1/D2 mode identification sequences from preamble symbols
 * and looks up the corresponding data mode.
 * 
 * Implementation based on MIL-STD-188-110A Appendix C:
 *   Section C.5.2.2: Preamble Structure
 *   Table C-VI: D1/D2 Pattern Assignments
 * 
 * Preamble structure (per empirical verification):
 *   Frame 1: symbols 288-383 contain D1 (96 symbols)
 *   Frame 2: symbols 0-95 contain D2 (96 symbols)
 */

#ifndef M110A_MODE_DETECTOR_H
#define M110A_MODE_DETECTOR_H

#include "m110a/mode_config.h"
#include "modem/scrambler.h"
#include <vector>
#include <map>
#include <cmath>

namespace m110a {

struct ModeDetectResult {
    bool detected;
    ModeId mode;
    int d1;
    int d2;
    int d1_confidence;  // votes for winning D1 (out of 96)
    int d2_confidence;  // votes for winning D2 (out of 96)
    
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
    ModeDetector() {
        build_lookup_table();
    }
    
    /**
     * Detect mode from baseband preamble symbols
     * 
     * @param symbols Demodulated preamble symbols (at least 576 symbols needed)
     * @return ModeDetectResult with detected mode and confidence
     * 
     * Note: Symbols must be phase-corrected before calling this function.
     */
    ModeDetectResult detect(const std::vector<complex_t>& symbols) {
        ModeDetectResult result;
        
        if (symbols.size() < 576) {
            // Need at least frame 1 (480) + D2 region of frame 2 (96)
            return result;
        }
        
        // Extract D1/D2 with voting
        int d1_votes[8] = {0};
        int d2_votes[8] = {0};
        
        // Regenerate scrambler
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
        
        // Advance to D1 position (symbol 288)
        for (int i = 0; i < 288; i++) {
            scr.next_tribit();
        }
        
        // Extract D1 from symbols 288-383 (96 symbols)
        for (int i = 0; i < 96; i++) {
            if (288 + i >= static_cast<int>(symbols.size())) break;
            
            uint8_t scr_val = scr.next_tribit();
            int sym_idx = demod_symbol(symbols[288 + i]);
            int d1_est = (sym_idx - scr_val + 8) % 8;
            d1_votes[d1_est]++;
        }
        
        // Continue scrambler for rest of frame 1
        for (int i = 0; i < 96; i++) {
            scr.next_tribit();  // symbols 384-479
        }
        
        // Extract D2 from symbols 480-575 (96 symbols)
        for (int i = 0; i < 96; i++) {
            if (480 + i >= static_cast<int>(symbols.size())) break;
            
            uint8_t scr_val = scr.next_tribit();
            int sym_idx = demod_symbol(symbols[480 + i]);
            int d2_est = (sym_idx - scr_val + 8) % 8;
            d2_votes[d2_est]++;
        }
        
        // Find majority vote
        result.d1 = 0;
        result.d2 = 0;
        for (int i = 1; i < 8; i++) {
            if (d1_votes[i] > d1_votes[result.d1]) result.d1 = i;
            if (d2_votes[i] > d2_votes[result.d2]) result.d2 = i;
        }
        result.d1_confidence = d1_votes[result.d1];
        result.d2_confidence = d2_votes[result.d2];
        
        // Lookup mode
        auto key = std::make_pair(result.d1, result.d2);
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            result.detected = true;
            result.mode = it->second;
        } else {
            // Unknown D1/D2 combination
            result.detected = false;
            result.mode = ModeId::M2400S;  // Default fallback
        }
        
        return result;
    }
    
    /**
     * Get minimum confidence threshold for reliable detection
     */
    static int min_confidence() { return 50; }  // Out of 96 votes
    
private:
    std::map<std::pair<int,int>, ModeId> lookup_;
    
    void build_lookup_table() {
        for (ModeId mode : ModeDatabase::all_modes()) {
            const auto& cfg = ModeDatabase::get(mode);
            
            // Skip 75 bps modes (D1=D2=0, handled separately)
            if (cfg.d1_sequence == 0 && cfg.d2_sequence == 0) continue;
            
            auto key = std::make_pair(cfg.d1_sequence, cfg.d2_sequence);
            
            // First match wins (VOICE modes share D1/D2 with SHORT)
            if (lookup_.find(key) == lookup_.end()) {
                lookup_[key] = mode;
            }
        }
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
