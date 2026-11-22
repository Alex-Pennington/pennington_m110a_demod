// melp1200: the codebook-free fields of a STANAG 4591 1200 bit/s superframe.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Decodes from an 81-bit superframe what STANAG 4591 Annex D lets a decoder recover
// without the unprinted codebooks (stanag4591_tables.h): the sync bit, the U/V pattern
// of the three frames, the band-pass cutoff of each voiced frame, and the aperiodic
// flags. Pitch, gains, LSFs and Fourier magnitudes need the codebooks and are returned
// as raw indices only.
#pragma once

#include <cstdint>
#include <vector>

#include "melp_synth/melp_synth.h"

namespace melp {

struct Superframe1200 {
  int sync = 0;
  int mode3 = 0;            // Table D-4 3-bit code
  int pitch9 = 0;           // the 9-bit pitch code (needs the unprinted codebooks)
  int uv = 0;               // U/V of frames 1..3 as bits f1 f2 f3, 1 = voiced
  bool uv_from_protection = false;  // code 000: the pattern came from the Table D-9 bits
  bool protection_ok = true;        // the Table D-9 bits agree with the pattern
  int bp_index[3] = {-1, -1, -1};   // Table D-2 index per voiced frame, -1 if unvoiced
  int aperiodic = 0;        // frames 1..3, bits f1 f2 f3 (Table D-8)
  int lsp_bits = 0, gain_index = 0, fs_index = -1;  // raw, codebook needed
  int voiced_count() const { return ((uv >> 2) & 1) + ((uv >> 1) & 1) + (uv & 1); }
  bool voiced(int frame) const { return (uv >> (2 - frame)) & 1; }
};

// bits[0..80] = B_01..B_81 (0/1).
Superframe1200 decode_superframe_1200(const uint8_t* bits);

// The reference-coder / RFC 8130 Figure 3 packing: 11 bytes, B_01 in the LSB of byte 0.
std::vector<Superframe1200> unpack_1200(const std::vector<uint8_t>& bytes);

// What the synthesiser can be given without the codebooks. The fields the bits do not
// determine are filled with the stand-ins in `fill`, and the result says so.
struct Fill1200 {
  double pitch = 80;   // samples
  double gain_db = 50; // G1 = G2
  // LSFs: evenly spaced 10 values over 0-4 kHz (a flat envelope) unless given.
  bool have_lsf = false;
  double lsf[kOrder] = {};
};
void superframe_to_frames(const Superframe1200& s, const Fill1200& fill, Frame out[3]);

}  // namespace melp
