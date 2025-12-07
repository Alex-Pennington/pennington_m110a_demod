#ifndef M110A_SYMBOL_MAPPER_H
#define M110A_SYMBOL_MAPPER_H

#include "common/types.h"
#include "common/constants.h"
#include <cmath>
#include <vector>

namespace m110a {

/**
 * 8-PSK Symbol Mapper with Differential Encoding
 * 
 * MIL-STD-188-110A uses differential 8-PSK:
 * - Each tribit (3 bits) specifies a PHASE INCREMENT, not absolute phase
 * - This provides robustness against phase ambiguity
 * 
 * Tribit to Phase Increment mapping:
 *   000 → +0°    (0)
 *   001 → +45°   (π/4)
 *   010 → +90°   (π/2)
 *   011 → +135°  (3π/4)
 *   100 → +180°  (π)
 *   101 → +225°  (5π/4)
 *   110 → +270°  (3π/2)
 *   111 → +315°  (7π/4)
 */
class SymbolMapper {
public:
    SymbolMapper();
    
    /**
     * Map a tribit to complex symbol using differential encoding
     * Updates internal phase state
     */
    complex_t map(uint8_t tribit);
    
    /**
     * Map multiple tribits to symbols
     */
    std::vector<complex_t> map(const std::vector<uint8_t>& tribits);
    
    /**
     * Reset phase to initial state
     */
    void reset();
    
    /**
     * Get current accumulated phase (radians)
     */
    float phase() const { return phase_; }
    
    /**
     * Set phase (for sync)
     */
    void set_phase(float phase);
    
    // Static utilities
    
    /**
     * Get phase increment for a tribit (no state change)
     */
    static float get_phase_increment(uint8_t tribit);
    
    /**
     * Get the 8 constellation points (unit circle)
     */
    static const complex_t* constellation();
    
    /**
     * Find closest constellation point (hard decision)
     * Returns tribit index 0-7
     */
    static uint8_t hard_decision(complex_t symbol);
    
    /**
     * Compute soft bits for a received symbol
     * Returns 3 soft bits in range [-127, +127]
     * Positive = more likely '1', Negative = more likely '0'
     */
    static void soft_decision(complex_t symbol, float noise_var,
                              soft_bit_t& b0, soft_bit_t& b1, soft_bit_t& b2);

private:
    float phase_;  // Accumulated phase in radians
    
    void wrap_phase();
};

// ============================================================================
// Implementation
// ============================================================================

// Pre-computed constellation points (on unit circle)
namespace detail {
    inline const complex_t CONSTELLATION_8PSK[8] = {
        complex_t(1.0f, 0.0f),                              // 0°
        complex_t(0.707106781f, 0.707106781f),              // 45°
        complex_t(0.0f, 1.0f),                              // 90°
        complex_t(-0.707106781f, 0.707106781f),             // 135°
        complex_t(-1.0f, 0.0f),                             // 180°
        complex_t(-0.707106781f, -0.707106781f),            // 225°
        complex_t(0.0f, -1.0f),                             // 270°
        complex_t(0.707106781f, -0.707106781f)              // 315°
    };
}

inline SymbolMapper::SymbolMapper() : phase_(0.0f) {}

inline void SymbolMapper::reset() {
    phase_ = 0.0f;
}

inline void SymbolMapper::set_phase(float phase) {
    phase_ = phase;
    wrap_phase();
}

inline void SymbolMapper::wrap_phase() {
    while (phase_ >= 2.0f * PI) phase_ -= 2.0f * PI;
    while (phase_ < 0.0f) phase_ += 2.0f * PI;
}

inline float SymbolMapper::get_phase_increment(uint8_t tribit) {
    return PSK8_PHASE_INCREMENT[tribit & 0x07];
}

inline const complex_t* SymbolMapper::constellation() {
    return detail::CONSTELLATION_8PSK;
}

inline complex_t SymbolMapper::map(uint8_t tribit) {
    // Add phase increment
    phase_ += get_phase_increment(tribit & 0x07);
    wrap_phase();
    
    // Generate symbol at current phase
    return complex_t(std::cos(phase_), std::sin(phase_));
}

inline std::vector<complex_t> SymbolMapper::map(const std::vector<uint8_t>& tribits) {
    std::vector<complex_t> symbols;
    symbols.reserve(tribits.size());
    
    for (uint8_t t : tribits) {
        symbols.push_back(map(t));
    }
    
    return symbols;
}

inline uint8_t SymbolMapper::hard_decision(complex_t symbol) {
    // Find angle of received symbol
    float angle = std::atan2(symbol.imag(), symbol.real());
    if (angle < 0) angle += 2.0f * PI;
    
    // Quantize to nearest 45° sector
    // Add 22.5° (π/8) to center decision regions
    float adjusted = angle + PI / 8.0f;
    if (adjusted >= 2.0f * PI) adjusted -= 2.0f * PI;
    
    int sector = static_cast<int>(adjusted / (PI / 4.0f));
    return static_cast<uint8_t>(sector & 0x07);
}

inline void SymbolMapper::soft_decision(complex_t symbol, float noise_var,
                                        soft_bit_t& b0, soft_bit_t& b1, soft_bit_t& b2) {
    // Simplified soft decision based on distance to constellation points
    // For each bit position, compute LLR approximation
    
    const float scale = 127.0f / (4.0f * noise_var + 0.001f);  // Avoid div by zero
    
    // Get symbol angle
    float angle = std::atan2(symbol.imag(), symbol.real());
    if (angle < 0) angle += 2.0f * PI;
    
    // Bit 0 (LSB): separates {0,2,4,6} from {1,3,5,7}
    // Regions near 0°, 90°, 180°, 270° → bit0=0
    // Regions near 45°, 135°, 225°, 315° → bit0=1
    float d0 = std::sin(4.0f * angle);  // Positive when bit0=1 likely
    
    // Bit 1: separates {0,1,4,5} from {2,3,6,7}
    // Regions near 0°, 45°, 180°, 225° → bit1=0
    // Regions near 90°, 135°, 270°, 315° → bit1=1
    float d1 = std::sin(2.0f * angle - PI / 4.0f);
    
    // Bit 2 (MSB): separates {0,1,2,3} from {4,5,6,7}
    // Upper half (0° to 180°) → bit2=0
    // Lower half (180° to 360°) → bit2=1
    float d2 = -std::cos(angle);  // Positive when bit2=1 likely
    
    // Scale and clamp to soft bit range
    auto clamp_soft = [scale](float d) -> soft_bit_t {
        float v = d * scale;
        if (v > 127.0f) return 127;
        if (v < -127.0f) return -127;
        return static_cast<soft_bit_t>(v);
    };
    
    b0 = clamp_soft(d0);
    b1 = clamp_soft(d1);
    b2 = clamp_soft(d2);
}

} // namespace m110a

#endif // M110A_SYMBOL_MAPPER_H
