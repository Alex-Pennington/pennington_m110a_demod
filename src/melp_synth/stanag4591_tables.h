// stanag4591_tables.h: the quantiser and bit-layout tables printed in STANAG 4591 Ed. 1.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Data only. Every table is entered from the printed STANAG 4591 PDF [1] and cites its
// page as "p. <printed page> (PDF p. <page of the file>)". Nothing here comes from any
// MELP or MELPe implementation. Table 1 was taken from the PDF's text layer by a
// script and checked against the page image; the others were entered by hand from
// the page.
//
// What [1] does not print, and so is not here (docs/melp-synth.md, Tables):
//   the LSF MSVQ (Tables C-1 to C-4), the Fourier magnitude VQ (Table C-5), and every
//   1200 bit/s codebook (pitch VQ A-D and the 9-bit pitch/mode map, the 9-bit unvoiced
//   LSF codebook, the LSF interpolation and residual codebooks, the 10-bit gain VQ).
//   [1] Annex C says only that they "are available in electronic form".
//
// Where a layout below is MEASURED rather than printed, it says so: the position of a
// field's bits that [1] names but does not order. It was measured by running the
// reference coder as an external program on public speech (docs/melp-synth.md).
//
// [1] The 600 bit/s, 1200 bit/s and 2400 bit/s NATO Interoperable Narrow Band Voice
//     Coder, NATO STANAG 4591, Ed. 1.
#pragma once

#include <cstdint>

namespace melp {
namespace s4591 {

// ---- 2400 bit/s: uniform quantisers, [1] s5.3 and s5.3.7, pp. 6-9 (PDF pp. 9-12) ----
// An n-level quantiser over x1..x2 has step s = (x2 - x1)/(n - 1) and outputs
// x1 + i*s, i = 0..n-1 ([1] s5.3.7, p. 9 (PDF p. 12)).
struct Uniform {
  double x1, x2;
  int levels;
  double value(int i) const { return x1 + i * (x2 - x1) / (levels - 1); }
};
// Pitch: log of the pitch quantised with 99 levels from log 20 to log 160 samples.
// The table holds log10 of the pitch; any base gives the same levels.
constexpr double kLog10Pitch20 = 1.3010299956639813;   // log10(20)
constexpr double kLog10Pitch160 = 2.2041199826559246;  // log10(160)
constexpr Uniform kPitch{kLog10Pitch20, kLog10Pitch160, 99};
// G2: 5 bits, 32 levels from 10.0 to 77.0 dB; the index is the codeword.
constexpr Uniform kGain2{10.0, 77.0, 32};
// G1: 3 bits. Code 0 = steady state, G1 = mean of this and the previous frame's G2.
// Otherwise code - 1 indexes a 7-level quantiser from min(G2, G2p) - 6 dB to
// max(G2, G2p) + 6 dB, clamped to 10..77 dB.
constexpr int kGain1Levels = 7;
constexpr double kGain1Margin = 6.0, kGainMin = 10.0, kGainMax = 77.0;

// ---- Table 1, p. 7 (PDF p. 10): encode/decode table for pitch and overall voicing ----
// Index = the 7-bit pitch code (P6..P0). Value: 0..98 = pitch quantiser index,
// kUnvoiced, or kErasure. The codes of Hamming weight 0 or 1 are unvoiced, the 21 of
// weight 2 are erasures, and the other 99 carry indices 0..98 in increasing order.
constexpr int8_t kUnvoiced = -1;
constexpr int8_t kErasure = -2;
constexpr int8_t kPitchCode[128] = {
     -1,  -1,  -1,  -2,  -1,  -2,  -2,   0,  -1,  -2,  -2,   1,  -2,   2,   3,   4,  // 0x00-0x0F
     -1,  -2,  -2,   5,  -2,   6,   7,   8,  -2,   9,  10,  11,  12,  13,  14,  15,  // 0x10-0x1F
     -1,  -2,  -2,  16,  -2,  17,  18,  19,  -2,  20,  21,  22,  23,  24,  25,  26,  // 0x20-0x2F
     -2,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  // 0x30-0x3F
     -1,  -2,  -2,  42,  -2,  43,  44,  45,  -2,  46,  47,  48,  49,  50,  51,  52,  // 0x40-0x4F
     -2,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  // 0x50-0x5F
     -2,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  // 0x60-0x6F
     83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  // 0x70-0x7F
};

// ---- Table 3, p. 11 (PDF p. 14): 2400 bit/s bit transmission order ----
// Entry k-1 is frame bit k (bit 1 is sent first). The subscript is the bit's weight
// in its field (subscript 0 = LSB, per the table's note). Voiced-frame names; in an
// unvoiced frame the BP, FM and AF positions carry FEC parity instead (kFec2400UV).
enum Field2400 : uint8_t { G2, G1, P, LSF1, LSF2, LSF3, LSF4, BP, FM, AF, SYNC };
struct Bit2400 { Field2400 field; uint8_t sub; };
constexpr Bit2400 kOrder2400[54] = {
    {G2, 0},   {BP, 0},   {P, 0},    {LSF2, 0}, {LSF3, 0}, {G2, 3},   {G2, 4},   {LSF3, 5},  //  1- 8
    {G2, 1},   {G2, 2},   {P, 4},    {LSF3, 4}, {P, 5},    {P, 1},    {P, 2},    {LSF4, 0},  //  9-16
    {P, 6},    {LSF1, 0}, {LSF1, 6}, {LSF4, 5}, {P, 3},    {LSF1, 5}, {LSF1, 4}, {LSF2, 5},  // 17-24
    {BP, 3},   {LSF1, 3}, {LSF1, 2}, {LSF2, 4}, {LSF4, 4}, {FM, 0},   {LSF1, 1}, {LSF2, 3},  // 25-32
    {FM, 7},   {FM, 6},   {FM, 5},   {G1, 1},   {G1, 0},   {BP, 2},   {BP, 1},   {LSF2, 1},  // 33-40
    {LSF3, 3}, {LSF2, 2}, {LSF3, 2}, {LSF3, 1}, {LSF4, 3}, {LSF4, 2}, {AF, 0},   {LSF4, 1},  // 41-48
    {FM, 4},   {FM, 3},   {FM, 2},   {FM, 1},   {G1, 2},   {SYNC, 0},                        // 49-54
};
// Unvoiced frames: frame bit -> FEC parity bit (code c of Hamming codes 1..4, bit j),
// from the "Unvoiced" columns of Table 3. Bits not listed keep their voiced meaning.
struct Fec2400 { uint8_t bit, code, sub; };
constexpr Fec2400 kFec2400UV[13] = {
    {2, 1, 0},  {25, 1, 3}, {30, 4, 0}, {33, 2, 2}, {34, 2, 1}, {35, 2, 0}, {38, 1, 2},
    {39, 1, 1}, {47, 4, 2}, {49, 3, 2}, {50, 3, 1}, {51, 3, 0}, {52, 4, 1},
};

// ---- Table D-2, p. D-6 (PDF p. 61): 1200 bit/s band-pass voicing codebook ----
// The four codewords, as the voicing of bands 2, 3, 4, 5 (band 2 leftmost). Each
// stands for a cutoff of 500, 1000, 2000 and 4000 Hz ([1] D.2.3).
// The 2-bit index of a codeword is its column in the table (0..3): MEASURED, see top.
constexpr uint8_t kBpCodeword[4][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 1, 0, 0}, {1, 1, 1, 1}};
constexpr double kBpCutoffHz[4] = {500, 1000, 2000, 4000};
// The voicing pattern of bands 2..5, as the 4-bit number b2 b3 b4 b5 (b2 = MSB), -> the
// index of the codeword it is assigned to.
constexpr uint8_t kBpPatternToIndex[16] = {
    0, 0, 0, 0, 0, 0, 0, 3,  // 0000 0001 0010 0011 0100 0101 0110 | 0111
    1, 1, 1, 3, 2, 3, 3, 3,  // 1000 1001 1010 | 1011 | 1100 | 1101 1110 1111
};

// ---- Table D-4, p. D-10 (PDF p. 65): the 3-bit code of the joint pitch/UV field ----
// Index = the 3-bit code as printed (MSB first). Value = U/V pattern of frames 1..3 as
// bits f1 f2 f3 (f1 = MSB, 1 = voiced); code 0 = at most one voiced frame, resolved by
// the 9-bit code and the mode protection bits (Table D-9).
constexpr int8_t kModeShared = -1;
constexpr int8_t kMode3Bit[8] = {
    kModeShared,  // 000: UUU, UUV, UVU, VUU
    0b110,        // 001: VVU
    0b101,        // 010: VUV
    0b111,        // 011: VVV, codebook A
    0b011,        // 100: UVV
    0b111,        // 101: VVV, codebook B
    0b111,        // 110: VVV, codebook C
    0b111,        // 111: VVV, codebook D
};

// ---- Table D-8, pp. D-13 to D-14 (PDF pp. 68-69): aperiodic flag, 1 bit per superframe ----
// For each U/V pattern (index f1 f2 f3 as above): the aperiodic flags of frames 1..3
// (bits f1 f2 f3, 1 = "J", set) decoded for new flag = 0 and for new flag = 1.
struct ApDecode { uint8_t flag0, flag1; };
constexpr ApDecode kAperiodic[8] = {
    {0b111, 0b111},  // UUU
    {0b110, 0b111},  // UUV
    {0b101, 0b111},  // UVU
    {0b100, 0b110},  // UVV
    {0b011, 0b111},  // VUU
    {0b010, 0b010},  // VUV
    {0b001, 0b011},  // VVU
    {0b000, 0b111},  // VVV
};

// ---- Table D-9, pp. D-14 to D-15 (PDF pp. 69-70): mode protection bits ----
// For each U/V pattern: the 2-bit "bandpass voicing 1" and "bandpass voicing 2" patterns
// and the 4-bit LSF pattern; -1 = not used for protection.
struct ModeProtect { int8_t bp1, bp2, lsf; };
constexpr ModeProtect kModeProtect[8] = {
    {0b00, 0b00, 0b0000},  // UUU
    {0b00, 0b01, -1},      // UUV
    {0b00, 0b10, -1},      // UVU
    {0b11, -1, -1},        // UVV
    {0b00, 0b11, -1},      // VUU
    {0b10, -1, -1},        // VUV
    {0b01, -1, 0b0101},    // VVU
    {-1, -1, -1},          // VVV
};

// ---- Tables D-9a and D-9b, pp. D-15 to D-16 (PDF pp. 70-71): 1200 bit/s bit order ----
// Frame bit k (1..81, bit 1 sent first; RFC 8130 Table 2 numbers them B_01..B_81).
// Both tables: B_01 sync, B_02..B_13 Pitch&UV0..11, then LSP0... .
// D-9b (modes 1-4): LSP0..42 at B_14..B_56, Gain0..9 at B_57..B_66, BP0..5 at
// B_67..B_72, Jitter at B_73, FS0..7 at B_74..B_81.
// D-9a (mode 5, UUU): LSP0..26 at B_14..B_40, Gain0..9 at B_41..B_50; the table
// places no other bits (the 30 protection bits of Table D-1 are not located).
constexpr int kSync1200 = 1;
constexpr int kPitchUV1200 = 2;      // 12 bits
constexpr int kLsp1200 = 14;         // 43 bits (modes 1-4), 27 (mode 5)
constexpr int kGain1200 = 57;        // 10 bits (modes 1-4)
constexpr int kGain1200Mode5 = 41;   // 10 bits (mode 5)
constexpr int kBp1200 = 67;          // 6 bits
constexpr int kJitter1200 = 73;      // 1 bit
constexpr int kFs1200 = 74;          // 8 bits
// MEASURED (see top), not printed:
//  - the 3-bit code of Table D-4 is Pitch&UV2 Pitch&UV1 Pitch&UV0 read MSB first
//    (Pitch&UV0 is its LSB), and the 9-bit code is Pitch&UV3..11;
//  - the voiced frames take the BP pairs (BP1,BP0), (BP3,BP2), (BP5,BP4) in frame
//    order, each read MSB first as the Table D-2 index; a pair no voiced frame uses
//    carries the mode protection bits, "bandpass voicing 1" in (BP5,BP4) and
//    "bandpass voicing 2" in (BP3,BP2).
constexpr int kMode3BitLsb = 0;   // Pitch&UV index of the 3-bit code's LSB
constexpr int kPitch9BitLsb = 3;  // Pitch&UV index of the 9-bit code's LSB (inference)

}  // namespace s4591
}  // namespace melp
