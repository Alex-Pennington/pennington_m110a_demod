#ifndef M110A_MULTIMODE_MAPPER_H
#define M110A_MULTIMODE_MAPPER_H

/**
 * Multi-Mode Symbol Mapper
 * 
 * MIL-STD-188-110A uses ABSOLUTE PSK with data scrambling:
 *   - BPSK: 1 bit maps to symbol 0 (0°) or 4 (180°) in 8-point constellation
 *   - QPSK: 2 bits (dibit) map to symbols 0, 2, 4, 6 (0°, 90°, 180°, 270°)
 *   - 8PSK: 3 bits (tribit) map directly to 8-point constellation
 * 
 * The data scrambler (separate from this mapper) rotates the symbol
 * index by adding a scrambler value mod 8, providing phase diversity
 * for repeated symbols.
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/mode_config.h"
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace m110a {

// 8-PSK constellation points (absolute phase)
// Per MS-DMT: Symbol n has phase n*45°
static const std::array<complex_t, 8> PSK8_CONSTELLATION = {{
    complex_t( 1.000f,  0.000f),  // Symbol 0:   0°
    complex_t( 0.707f,  0.707f),  // Symbol 1:  45°
    complex_t( 0.000f,  1.000f),  // Symbol 2:  90°
    complex_t(-0.707f,  0.707f),  // Symbol 3: 135°
    complex_t(-1.000f,  0.000f),  // Symbol 4: 180°
    complex_t(-0.707f, -0.707f),  // Symbol 5: 225°
    complex_t( 0.000f, -1.000f),  // Symbol 6: 270°
    complex_t( 0.707f, -0.707f)   // Symbol 7: 315°
}};

// BPSK symbol indices within 8-PSK constellation
#ifndef M110A_BPSK_SYMBOLS_DEFINED
#define M110A_BPSK_SYMBOLS_DEFINED
static const std::array<int, 2> BPSK_SYMBOLS = {{ 0, 4 }};  // 0° and 180°
#endif

// QPSK symbol indices within 8-PSK constellation  
#ifndef M110A_QPSK_SYMBOLS_DEFINED
#define M110A_QPSK_SYMBOLS_DEFINED
static const std::array<int, 4> QPSK_SYMBOLS = {{ 0, 2, 4, 6 }};  // 0°, 90°, 180°, 270°
#endif

/**
 * Multi-mode PSK mapper with ABSOLUTE phase encoding
 */
class MultiModeMapper {
public:
    explicit MultiModeMapper(Modulation mod = Modulation::PSK8)
        : modulation_(mod)
        , order_(modulation_order(mod))
        , bits_per_sym_(m110a::bits_per_symbol(mod)) {
        
        build_constellation();
    }
    
    void set_modulation(Modulation mod) {
        modulation_ = mod;
        order_ = modulation_order(mod);
        bits_per_sym_ = m110a::bits_per_symbol(mod);
        build_constellation();
    }
    
    void reset() {
        // No state to reset in absolute PSK
    }
    
    Modulation modulation() const { return modulation_; }
    int order() const { return order_; }
    int bits_per_symbol() const { return bits_per_sym_; }
    
    /**
     * Map bits to absolute PSK symbol (before scrambler)
     * @param bits Input bits (1 for BPSK, 2 for QPSK, 3 for 8PSK)
     * @return Symbol index (0-7) for 8-PSK constellation
     */
    int map_to_symbol_index(int bits) const {
        bits &= (order_ - 1);
        
        switch (modulation_) {
            case Modulation::BPSK:
                return BPSK_SYMBOLS[bits & 1];
            case Modulation::QPSK:
                return QPSK_SYMBOLS[bits & 3];
            case Modulation::PSK8:
            default:
                return bits & 7;
        }
    }
    
    /**
     * Map symbol index to complex constellation point
     * @param sym_idx Symbol index (0-7)
     * @return Complex symbol on unit circle
     */
    static complex_t symbol_to_complex(int sym_idx) {
        return PSK8_CONSTELLATION[sym_idx & 7];
    }
    
    /**
     * Map bits directly to complex symbol (convenience function)
     * @param bits Input bits
     * @return Complex symbol on unit circle
     */
    complex_t map(int bits) {
        return symbol_to_complex(map_to_symbol_index(bits));
    }
    
    /**
     * Map scrambled symbol to complex (for TX after scrambler applied)
     * @param sym_idx Already-scrambled symbol index
     * @return Complex symbol on unit circle
     */
    complex_t map_scrambled(int sym_idx) {
        return symbol_to_complex(sym_idx);
    }
    
    /**
     * Map tribit to 8PSK symbol (always 8PSK, used for probes)
     * @param tribit 3-bit value (0-7)
     * @return Complex symbol on unit circle
     */
    complex_t map_8psk(int tribit) {
        return symbol_to_complex(tribit & 7);
    }
    
    /**
     * Map multiple symbols
     */
    std::vector<complex_t> map_block(const std::vector<int>& bits_vec) {
        std::vector<complex_t> symbols;
        symbols.reserve(bits_vec.size());
        for (int b : bits_vec) {
            symbols.push_back(map(b));
        }
        return symbols;
    }
    
    /**
     * Differential decode: extract bits from phase difference (legacy)
     * @param current Current symbol
     * @param previous Previous symbol
     * @return Decoded bits
     */
    int demap_differential(complex_t current, complex_t previous) const {
        // Compute phase difference
        complex_t diff = current * std::conj(previous);
        float phase = std::atan2(diff.imag(), diff.real());
        if (phase < 0) phase += 2.0f * PI;
        
        // Quantize to nearest constellation point
        float step = 2.0f * PI / order_;
        int bits = static_cast<int>(std::round(phase / step)) % order_;
        
        return bits;
    }
    
    /**
     * Absolute decode: extract symbol index from absolute phase
     * @param symbol Received symbol (normalized to unit circle)
     * @return Symbol index (0-7) in 8-PSK constellation
     */
    int demap_absolute(complex_t symbol) const {
        // Get absolute phase
        float phase = std::atan2(symbol.imag(), symbol.real());
        if (phase < 0) phase += 2.0f * PI;
        
        // Always quantize to 8-PSK constellation (45° steps)
        int sym_idx = static_cast<int>(std::round(phase / (PI / 4))) % 8;
        return sym_idx;
    }
    
    /**
     * Convert 8-PSK symbol index back to data bits based on modulation
     * @param sym_idx Symbol index from demap_absolute() and descrambler
     * @return Data bits (1 for BPSK, 2 for QPSK, 3 for 8PSK)
     */
    int symbol_to_bits(int sym_idx) const {
        sym_idx &= 7;
        
        switch (modulation_) {
            case Modulation::BPSK:
                // BPSK symbols are 0 (0°) and 4 (180°)
                // Nearest: 0,1,7 → bit 0; 3,4,5 → bit 1
                return (sym_idx >= 2 && sym_idx <= 6) ? 1 : 0;
            case Modulation::QPSK:
                // QPSK symbols are 0,2,4,6 (0°, 90°, 180°, 270°)
                // Round to nearest even symbol, then divide by 2
                return ((sym_idx + 1) / 2) % 4;
            case Modulation::PSK8:
            default:
                return sym_idx;
        }
    }
    
    /**
     * Soft demap absolute PSK for Viterbi decoder
     * Returns soft bits (LLRs) for each bit position based on absolute phase
     * @param symbol Received symbol (normalized)
     * @param noise_var Estimated noise variance
     * @return Soft bits (-127 to +127), 1 for BPSK, 2 for QPSK, 3 for 8PSK
     */
    std::vector<soft_bit_t> soft_demap_absolute(complex_t symbol, float noise_var = 0.1f) const {
        std::vector<soft_bit_t> soft(bits_per_sym_);
        
        // Normalize
        float mag = std::abs(symbol);
        if (mag > 0.01f) symbol /= mag;
        
        // Compute distances to all 8-PSK constellation points
        float distances[8];
        for (int i = 0; i < 8; i++) {
            float dr = symbol.real() - PSK8_CONSTELLATION[i].real();
            float di = symbol.imag() - PSK8_CONSTELLATION[i].imag();
            distances[i] = dr*dr + di*di;
        }
        
        float nvar = std::max(0.01f, noise_var);
        
        if (modulation_ == Modulation::BPSK) {
            // BPSK: bit 0 → symbol 0, bit 1 → symbol 4
            // Include nearby symbols for more robust soft decision
            float d0 = std::min({distances[0], distances[1], distances[7]});  // Near 0°
            float d1 = std::min({distances[3], distances[4], distances[5]});  // Near 180°
            float llr = (d0 - d1) / (2.0f * nvar);
            soft[0] = std::max(-127, std::min(127, static_cast<int>(llr * 32.0f)));
        }
        else if (modulation_ == Modulation::QPSK) {
            // QPSK: symbols 0,2,4,6 (0°, 90°, 180°, 270°)
            // Bit 0: 0 for symbols 0,2; 1 for symbols 4,6
            float d_b0_0 = std::min(distances[0], distances[2]);
            float d_b0_1 = std::min(distances[4], distances[6]);
            float llr0 = (d_b0_0 - d_b0_1) / (2.0f * nvar);
            soft[0] = std::max(-127, std::min(127, static_cast<int>(llr0 * 32.0f)));
            
            // Bit 1: 0 for symbols 0,4; 1 for symbols 2,6
            float d_b1_0 = std::min(distances[0], distances[4]);
            float d_b1_1 = std::min(distances[2], distances[6]);
            float llr1 = (d_b1_0 - d_b1_1) / (2.0f * nvar);
            soft[1] = std::max(-127, std::min(127, static_cast<int>(llr1 * 32.0f)));
        }
        else {
            // 8PSK: all 8 symbols
            for (int bit = 0; bit < 3; bit++) {
                float min_d0 = 1e10f;  // Min distance where bit=0
                float min_d1 = 1e10f;  // Min distance where bit=1
                
                for (int sym = 0; sym < 8; sym++) {
                    int b = (sym >> (2 - bit)) & 1;  // MSB first
                    if (b == 0) {
                        if (distances[sym] < min_d0) min_d0 = distances[sym];
                    } else {
                        if (distances[sym] < min_d1) min_d1 = distances[sym];
                    }
                }
                
                float llr = (min_d0 - min_d1) / (2.0f * nvar);
                soft[bit] = std::max(-127, std::min(127, static_cast<int>(llr * 32.0f)));
            }
        }
        
        return soft;
    }
    
    /**
     * Soft demap for Viterbi decoder (legacy differential - for backward compat)
     * Returns soft bits (LLRs) for each bit position
     * @param diff Differential symbol (current * conj(prev))
     * @param noise_var Estimated noise variance
     * @return Soft bits (-127 to +127)
     */
    std::vector<soft_bit_t> soft_demap(complex_t diff, float noise_var = 0.1f) const {
        std::vector<soft_bit_t> soft(bits_per_sym_);
        
        // Normalize
        float mag = std::abs(diff);
        if (mag > 0.01f) diff /= mag;
        
        // Compute distances to all constellation points
        std::vector<float> distances(order_);
        for (int i = 0; i < order_; i++) {
            float dr = diff.real() - constellation_[i].real();
            float di = diff.imag() - constellation_[i].imag();
            distances[i] = dr*dr + di*di;
        }
        
        // Find minimum distance for noise estimate
        float min_dist = *std::min_element(distances.begin(), distances.end());
        float nvar = std::max(noise_var, min_dist + 0.01f);
        
        // Compute LLR for each bit
        for (int bit = 0; bit < bits_per_sym_; bit++) {
            float min_d0 = 1e10f;  // Min distance where bit=0
            float min_d1 = 1e10f;  // Min distance where bit=1
            
            for (int sym = 0; sym < order_; sym++) {
                // Check bit position (MSB first)
                int b = (sym >> (bits_per_sym_ - 1 - bit)) & 1;
                
                if (b == 0) {
                    if (distances[sym] < min_d0) min_d0 = distances[sym];
                } else {
                    if (distances[sym] < min_d1) min_d1 = distances[sym];
                }
            }
            
            // LLR = (d1 - d0) / (2 * noise_var)
            // Viterbi decoder convention: positive = bit=1, negative = bit=0
            // So we need -(d1 - d0) = (d0 - d1)
            float llr = (min_d0 - min_d1) / (2.0f * nvar);
            
            // Scale and clip
            int scaled = static_cast<int>(llr * 32.0f);
            scaled = std::max(-127, std::min(127, scaled));
            soft[bit] = static_cast<soft_bit_t>(scaled);
        }
        
        return soft;
    }
    
    /**
     * Get constellation point for given symbol index
     */
    complex_t get_constellation_point(int index) const {
        return constellation_[index % order_];
    }
    
    /**
     * Get current differential phase
     */
    float current_phase() const { return current_phase_; }
    
    /**
     * Set current phase (for synchronization)
     */
    void set_phase(float phase) { current_phase_ = phase; }

private:
    Modulation modulation_;
    float current_phase_;
    int order_;
    int bits_per_sym_;
    std::vector<complex_t> constellation_;
    
    void build_constellation() {
        constellation_.clear();
        constellation_.reserve(order_);
        
        float step = 2.0f * PI / order_;
        for (int i = 0; i < order_; i++) {
            // Phase increments: 0, step, 2*step, ...
            constellation_.push_back(std::polar(1.0f, i * step));
        }
    }
};

/**
 * BPSK-specific mapper (simplified)
 */
class BPSKMapper {
public:
    BPSKMapper() : phase_(0.0f) {}
    
    void reset() { phase_ = 0.0f; }
    
    complex_t map(int bit) {
        // BPSK: bit 0 = 0°, bit 1 = 180°
        float inc = (bit & 1) ? PI : 0.0f;
        phase_ += inc;
        while (phase_ >= 2.0f * PI) phase_ -= 2.0f * PI;
        return std::polar(1.0f, phase_);
    }
    
    int demap(complex_t current, complex_t previous) const {
        complex_t diff = current * std::conj(previous);
        return (diff.real() < 0) ? 1 : 0;
    }

private:
    float phase_;
};

/**
 * QPSK-specific mapper (simplified)
 */
class QPSKMapper {
public:
    QPSKMapper() : phase_(0.0f) {}
    
    void reset() { phase_ = 0.0f; }
    
    complex_t map(int dibit) {
        // QPSK: 00=0°, 01=90°, 10=180°, 11=270°
        static const float increments[4] = {0.0f, PI/2, PI, 3*PI/2};
        phase_ += increments[dibit & 3];
        while (phase_ >= 2.0f * PI) phase_ -= 2.0f * PI;
        return std::polar(1.0f, phase_);
    }
    
    int demap(complex_t current, complex_t previous) const {
        complex_t diff = current * std::conj(previous);
        float phase = std::atan2(diff.imag(), diff.real());
        if (phase < 0) phase += 2.0f * PI;
        return static_cast<int>(std::round(phase / (PI/2))) % 4;
    }
    
    std::vector<soft_bit_t> soft_demap(complex_t diff, float nvar = 0.1f) const {
        // Normalize
        float mag = std::abs(diff);
        if (mag > 0.01f) diff /= mag;
        
        // QPSK constellation: 0°, 90°, 180°, 270°
        static const complex_t cons[4] = {
            {1, 0}, {0, 1}, {-1, 0}, {0, -1}
        };
        
        float d[4];
        for (int i = 0; i < 4; i++) {
            float dr = diff.real() - cons[i].real();
            float di = diff.imag() - cons[i].imag();
            d[i] = dr*dr + di*di;
        }
        
        // Bit 0 (MSB): 0 for symbols 0,1; 1 for symbols 2,3
        // Viterbi convention: positive = bit=1, negative = bit=0
        float llr0 = (std::min(d[0], d[1]) - std::min(d[2], d[3])) / (2*nvar);
        // Bit 1 (LSB): 0 for symbols 0,2; 1 for symbols 1,3
        float llr1 = (std::min(d[0], d[2]) - std::min(d[1], d[3])) / (2*nvar);
        
        std::vector<soft_bit_t> soft(2);
        soft[0] = std::max(-127, std::min(127, static_cast<int>(llr0 * 32)));
        soft[1] = std::max(-127, std::min(127, static_cast<int>(llr1 * 32)));
        return soft;
    }

private:
    float phase_;
};

/**
 * 8PSK mapper (original implementation, for reference)
 */
class PSK8Mapper {
public:
    PSK8Mapper() : phase_(0.0f) {}
    
    void reset() { phase_ = 0.0f; }
    
    complex_t map(int tribit) {
        float inc = (tribit & 7) * (PI / 4);
        phase_ += inc;
        while (phase_ >= 2.0f * PI) phase_ -= 2.0f * PI;
        return std::polar(1.0f, phase_);
    }
    
    int demap(complex_t current, complex_t previous) const {
        complex_t diff = current * std::conj(previous);
        float phase = std::atan2(diff.imag(), diff.real());
        if (phase < 0) phase += 2.0f * PI;
        return static_cast<int>(std::round(phase / (PI/4))) % 8;
    }

private:
    float phase_;
};

} // namespace m110a

#endif // M110A_MULTIMODE_MAPPER_H
