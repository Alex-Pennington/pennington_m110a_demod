#ifndef M110A_SCRAMBLER_H
#define M110A_SCRAMBLER_H

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <cstdint>

namespace m110a {

/**
 * MIL-STD-188-110A Scrambler/Descrambler (7-bit version)
 * 
 * Polynomial: 1 + x^-6 + x^-7
 * 
 * This is a 7-bit LFSR that produces a pseudo-random sequence.
 * For 8-PSK, we clock it 3 times to get a tribit (3 bits) which
 * maps to one of 8 phase increments.
 * 
 * The scrambler is used for:
 * 1. Preamble generation (known sequence for correlation)
 * 2. Data scrambling (spread spectrum, avoid long runs)
 * 3. Channel probe symbols (known sequence for equalizer training)
 */
class Scrambler {
public:
    /**
     * Create scrambler with initial state
     * @param initial_state 7-bit initial shift register value
     */
    explicit Scrambler(uint8_t initial_state = SCRAMBLER_INIT_PREAMBLE);
    
    /**
     * Reset to specified state
     */
    void reset(uint8_t state = SCRAMBLER_INIT_PREAMBLE);
    
    /**
     * Get next single bit from scrambler
     */
    uint8_t next_bit();
    
    /**
     * Get next 3 bits (tribit) for 8-PSK symbol
     * Bits are packed as: (b2 << 2) | (b1 << 1) | b0
     * where b0 is first bit out, b2 is third bit out
     */
    uint8_t next_tribit();
    
    /**
     * Generate N tribits
     */
    std::vector<uint8_t> generate_tribits(size_t count);
    
    /**
     * Generate N bits
     */
    std::vector<uint8_t> generate_bits(size_t count);
    
    /**
     * Descramble received tribit by XOR with scrambler output
     * Note: Scrambler state advances, so call in correct sequence
     */
    uint8_t descramble_tribit(uint8_t received);
    
    /**
     * Descramble a single bit
     */
    uint8_t descramble_bit(uint8_t received);
    
    /**
     * Descramble a vector of bits and assemble into bytes
     */
    std::vector<uint8_t> descramble_bits_to_bytes(const std::vector<uint8_t>& bits);
    
    /**
     * Get current shift register state (for debugging)
     */
    uint8_t state() const { return state_; }
    
private:
    uint8_t state_;  // 7-bit shift register
};

/**
 * Reference Implementation Scrambler (12-bit version)
 * 
 * As per MS-DMT (m188110a) t110a.cpp lines 201-220:
 * - 12-bit LFSR stored as sreg[12] array
 * - Seed: 0xBAD = sreg[11:0] = 101110101101
 *   sreg[0]=1, sreg[1]=0, sreg[2]=1, sreg[3]=1, sreg[4]=0, sreg[5]=1
 *   sreg[6]=0, sreg[7]=1, sreg[8]=1, sreg[9]=1, sreg[10]=0, sreg[11]=1
 * - Polynomial: x^12 + x^6 + x^4 + x^1 + 1
 * - Clock 8 times, then read bits 0,1,2 as tribit
 * - Sequence length: 160 tribits
 * - Applied via modulo-8 ADDITION (NOT XOR!)
 * 
 * Expected first 16 tribits: 0, 2, 4, 3, 3, 6, 4, 5, 7, 6, 7, 0, 5, 5, 4, 3
 */
class RefScrambler {
public:
    static constexpr uint16_t SEED = 0xBAD;  // Correct MS-DMT seed
    static constexpr int SEQUENCE_LENGTH = 160;
    
    explicit RefScrambler(uint16_t initial_state = SEED) {
        reset(initial_state);
    }
    
    void reset(uint16_t state = SEED) {
        // Load state into sreg array (LSB = sreg[0])
        for (int i = 0; i < 12; i++) {
            sreg_[i] = (state >> i) & 1;
        }
    }
    
    /**
     * Clock LFSR once (exactly as MS-DMT t110a.cpp)
     * 
     * From source code:
     *   carry = sreg[11];
     *   sreg[11] = sreg[10];
     *   sreg[10] = sreg[9];
     *   sreg[9]  = sreg[8];
     *   sreg[8]  = sreg[7];
     *   sreg[7]  = sreg[6];
     *   sreg[6]  = sreg[5] ^ carry;  // TAP at position 6
     *   sreg[5]  = sreg[4];
     *   sreg[4]  = sreg[3] ^ carry;  // TAP at position 4
     *   sreg[3]  = sreg[2];
     *   sreg[2]  = sreg[1];
     *   sreg[1]  = sreg[0] ^ carry;  // TAP at position 1
     *   sreg[0]  = carry;
     */
    void clock_once() {
        uint8_t carry = sreg_[11];
        
        sreg_[11] = sreg_[10];
        sreg_[10] = sreg_[9];
        sreg_[9]  = sreg_[8];
        sreg_[8]  = sreg_[7];
        sreg_[7]  = sreg_[6];
        sreg_[6]  = sreg_[5] ^ carry;  // TAP
        sreg_[5]  = sreg_[4];
        sreg_[4]  = sreg_[3] ^ carry;  // TAP
        sreg_[3]  = sreg_[2];
        sreg_[2]  = sreg_[1];
        sreg_[1]  = sreg_[0] ^ carry;  // TAP
        sreg_[0]  = carry;
    }
    
    /**
     * Get next tribit by clocking 8 times then reading bits 0,1,2
     */
    uint8_t next_tribit() {
        // Clock 8 times
        for (int j = 0; j < 8; j++) {
            clock_once();
        }
        
        // Read (sreg[2]<<2) + (sreg[1]<<1) + sreg[0]
        return (sreg_[2] << 2) | (sreg_[1] << 1) | sreg_[0];
    }
    
    /**
     * Generate entire 160-tribit sequence
     */
    std::vector<uint8_t> generate_sequence() {
        reset();
        std::vector<uint8_t> seq(SEQUENCE_LENGTH);
        for (int i = 0; i < SEQUENCE_LENGTH; i++) {
            seq[i] = next_tribit();
        }
        return seq;
    }
    
    /**
     * Scramble symbol using modulo-8 ADDITION (as per MS-DMT)
     * sym = (sym + data_scrambler_bits[offset]) % 8
     */
    static uint8_t scramble_symbol(uint8_t sym, int offset, const std::vector<uint8_t>& seq) {
        return (sym + seq[offset % SEQUENCE_LENGTH]) % 8;
    }
    
    /**
     * Descramble symbol using modulo-8 SUBTRACTION
     */
    static uint8_t descramble_symbol(uint8_t sym, int offset, const std::vector<uint8_t>& seq) {
        return (sym - seq[offset % SEQUENCE_LENGTH] + 8) % 8;
    }
    
    uint16_t state() const {
        uint16_t s = 0;
        for (int i = 0; i < 12; i++) {
            s |= (sreg_[i] << i);
        }
        return s;
    }
    
private:
    uint8_t sreg_[12];  // Individual bit array like MS-DMT
};

// ============================================================================
// Implementation (header-only for simplicity)
// ============================================================================

inline Scrambler::Scrambler(uint8_t initial_state)
    : state_(initial_state & 0x7F) {  // Mask to 7 bits
}

inline void Scrambler::reset(uint8_t state) {
    state_ = state & 0x7F;
}

inline uint8_t Scrambler::next_bit() {
    // MIL-STD-188-110A scrambler: polynomial x^7 + x + 1
    // Feedback taps at positions 6 and 0 (maximal length = 127)
    //
    // Register: [b6 b5 b4 b3 b2 b1 b0]
    //   - b0 is output (oldest bit)
    //   - b6 receives new feedback bit
    //   - Feedback = b6 XOR b0
    
    uint8_t bit0 = state_ & 1;           // Output tap
    uint8_t bit6 = (state_ >> 6) & 1;    // Feedback tap
    uint8_t feedback = bit0 ^ bit6;
    
    // Output the oldest bit
    uint8_t output = bit0;
    
    // Shift right, insert feedback at bit 6
    state_ = (state_ >> 1) | (feedback << 6);
    
    return output;
}

inline uint8_t Scrambler::next_tribit() {
    uint8_t b0 = next_bit();
    uint8_t b1 = next_bit();
    uint8_t b2 = next_bit();
    
    // Pack as tribit: b2 is MSB, b0 is LSB
    return (b2 << 2) | (b1 << 1) | b0;
}

inline std::vector<uint8_t> Scrambler::generate_tribits(size_t count) {
    std::vector<uint8_t> result(count);
    for (size_t i = 0; i < count; i++) {
        result[i] = next_tribit();
    }
    return result;
}

inline std::vector<uint8_t> Scrambler::generate_bits(size_t count) {
    std::vector<uint8_t> result(count);
    for (size_t i = 0; i < count; i++) {
        result[i] = next_bit();
    }
    return result;
}

inline uint8_t Scrambler::descramble_tribit(uint8_t received) {
    return received ^ next_tribit();
}

inline uint8_t Scrambler::descramble_bit(uint8_t received) {
    return (received ^ next_bit()) & 1;
}

inline std::vector<uint8_t> Scrambler::descramble_bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    bytes.reserve((bits.size() + 7) / 8);
    
    uint8_t byte = 0;
    int bit_count = 0;
    
    for (uint8_t bit : bits) {
        // Descramble the bit
        uint8_t descrambled = descramble_bit(bit);
        
        // Accumulate into byte (MSB first)
        byte = (byte << 1) | (descrambled & 1);
        bit_count++;
        
        if (bit_count == 8) {
            bytes.push_back(byte);
            byte = 0;
            bit_count = 0;
        }
    }
    
    // Don't output partial bytes
    
    return bytes;
}

} // namespace m110a

#endif // M110A_SCRAMBLER_H
