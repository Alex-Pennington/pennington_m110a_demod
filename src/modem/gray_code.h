#ifndef M110A_GRAY_CODE_H
#define M110A_GRAY_CODE_H

/**
 * MIL-STD-188-110A Gray Code Tables
 * 
 * These modified Gray codes are used to map bits to PSK symbol indices.
 * The mapping minimizes bit errors when adjacent symbols are confused.
 * 
 * Source: Reference modem t110a.cpp
 */

#include <cstdint>
#include <array>

namespace m110a {

// ============================================================================
// Modified Gray Code Tables (from reference modem)
// ============================================================================

/**
 * QPSK Modified Gray Code (1200 bps)
 * Maps 2-bit dibit to symbol index in 8-PSK constellation
 * Symbols used: 0, 2, 4, 6 (0°, 90°, 180°, 270°)
 */
constexpr std::array<int, 4> MGD2 = {0, 1, 3, 2};

/**
 * 8PSK Modified Gray Code (2400 bps)
 * Maps 3-bit tribit to symbol index
 * 
 * tribit → symbol index (phase)
 *   0 → 0 (0°)
 *   1 → 1 (45°)
 *   2 → 3 (135°)
 *   3 → 2 (90°)
 *   4 → 7 (315°)
 *   5 → 6 (270°)
 *   6 → 4 (180°)
 *   7 → 5 (225°)
 */
constexpr std::array<int, 8> MGD3 = {0, 1, 3, 2, 7, 6, 4, 5};

/**
 * Inverse Gray Code Tables (for demapping)
 * symbol index → tribit/dibit
 */
constexpr std::array<int, 4> INV_MGD2 = {0, 1, 3, 2};  // Self-inverse for QPSK
constexpr std::array<int, 8> INV_MGD3 = {0, 1, 3, 2, 6, 7, 5, 4};

// ============================================================================
// BPSK Symbol Mapping
// ============================================================================

/**
 * BPSK uses symbols 0 and 4 in the 8-PSK constellation
 * bit 0 → symbol 0 (0°)
 * bit 1 → symbol 4 (180°)
 */
#ifndef M110A_BPSK_SYMBOLS_DEFINED
#define M110A_BPSK_SYMBOLS_DEFINED
constexpr std::array<int, 2> BPSK_SYMBOLS = {0, 4};
#endif

/**
 * QPSK uses symbols 0, 2, 4, 6 in the 8-PSK constellation
 */
#ifndef M110A_QPSK_SYMBOLS_DEFINED
#define M110A_QPSK_SYMBOLS_DEFINED
constexpr std::array<int, 4> QPSK_SYMBOLS = {0, 2, 4, 6};
#endif

// ============================================================================
// Inline Utility Functions
// ============================================================================

/**
 * Gray encode a tribit (3 bits) to 8PSK symbol index
 */
inline int gray_encode_8psk(int tribit) {
    return MGD3[tribit & 7];
}

/**
 * Gray decode an 8PSK symbol index to tribit
 */
inline int gray_decode_8psk(int symbol) {
    return INV_MGD3[symbol & 7];
}

/**
 * Gray encode a dibit (2 bits) to QPSK symbol index
 * Note: Returns index in 8-PSK constellation (0, 2, 4, 6)
 */
inline int gray_encode_qpsk(int dibit) {
    return QPSK_SYMBOLS[MGD2[dibit & 3]];
}

/**
 * Gray decode a QPSK symbol index to dibit
 */
inline int gray_decode_qpsk(int symbol) {
    // Symbol should be 0, 2, 4, or 6
    int qpsk_idx = (symbol / 2) & 3;
    return INV_MGD2[qpsk_idx];
}

/**
 * BPSK encode: bit to symbol index
 */
inline int bpsk_encode(int bit) {
    return BPSK_SYMBOLS[bit & 1];
}

/**
 * BPSK decode: symbol index to bit
 * Symbols near 0° → bit 0, symbols near 180° → bit 1
 */
inline int bpsk_decode(int symbol) {
    // Symbols 0,1,7 are near 0° → bit 0
    // Symbols 3,4,5 are near 180° → bit 1
    // Symbols 2,6 are ambiguous (90°, 270°)
    symbol &= 7;
    if (symbol <= 1 || symbol == 7) return 0;
    if (symbol >= 3 && symbol <= 5) return 1;
    // For 2 and 6, use nearest (2→0, 6→1)
    return (symbol == 6) ? 1 : 0;
}

/**
 * Scramble a symbol using modulo-8 addition (TX side)
 * sym_out = (gray_symbol + scrambler_value) % 8
 */
inline int scramble_symbol(int gray_symbol, int scr_value) {
    return (gray_symbol + scr_value) & 7;
}

/**
 * Descramble a symbol using modulo-8 subtraction (RX side)
 * gray_symbol = (received_symbol - scrambler_value + 8) % 8
 */
inline int descramble_symbol(int received_symbol, int scr_value) {
    return (received_symbol - scr_value + 8) & 7;
}

} // namespace m110a

#endif // M110A_GRAY_CODE_H
