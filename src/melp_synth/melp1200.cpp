// melp1200: the codebook-free fields of a STANAG 4591 1200 bit/s superframe.
// Copyright (C) the pennington_m110a_demod authors. SPDX-License-Identifier: GPL-3.0-only
// Sources: STANAG 4591 Ed. 1 Annex D, D.3.3, D.3.7, D.3.9, D.3.10, D.4.1 and the tables
// in stanag4591_tables.h. No MELP or MELPe implementation was read.
#include "melp_synth/melp1200.h"

#include "melp_synth/stanag4591_tables.h"

namespace melp {

namespace {
int field(const uint8_t* bits, int first_bit, int n) {  // LSB = first bit
  int v = 0;
  for (int i = 0; i < n; ++i) v |= (bits[first_bit - 1 + i] & 1) << i;
  return v;
}
int bp_pair(const uint8_t* bits, int pair) {  // (BP2p+1, BP2p), MSB first
  return field(bits, s4591::kBp1200 + 2 * pair, 2);
}
int popcount9(int v) {
  int n = 0;
  for (; v; v >>= 1) n += v & 1;
  return n;
}
}  // namespace

Superframe1200 decode_superframe_1200(const uint8_t* bits) {
  using namespace s4591;
  Superframe1200 s;
  s.sync = bits[kSync1200 - 1] & 1;
  int puv = field(bits, kPitchUV1200, 12);
  s.mode3 = (puv >> kMode3BitLsb) & 7;
  s.pitch9 = (puv >> kPitch9BitLsb) & 0x1ff;
  int bp_pairs[3] = {bp_pair(bits, 0), bp_pair(bits, 1), bp_pair(bits, 2)};

  if (kMode3Bit[s.mode3] != kModeShared) {
    s.uv = kMode3Bit[s.mode3];
  } else if (popcount9(s.pitch9) <= 1) {
    s.uv = 0;  // [1] D.4.1: all-zero or one bit set -> UUU
  } else {
    // Table D-9: "bandpass voicing 2" tells UUV / UVU / VUU apart
    s.uv_from_protection = true;
    switch (bp_pairs[1]) {
      case 0b01: s.uv = 0b001; break;
      case 0b10: s.uv = 0b010; break;
      case 0b11: s.uv = 0b100; break;
      default: s.uv = 0; s.protection_ok = false; break;
    }
  }

  // voiced frames take the BP pairs in frame order; the spare pairs are protection
  int used = 0;
  for (int f = 0; f < 3; ++f)
    if (s.voiced(f)) s.bp_index[f] = bp_pairs[used++];
  const ModeProtect& mp = kModeProtect[s.uv];
  if (s.uv != 0) {  // UUU has no BP field (Table D-9a)
    if (mp.bp1 >= 0 && used <= 2 && bp_pairs[2] != mp.bp1) s.protection_ok = false;
    if (mp.bp2 >= 0 && used <= 1 && bp_pairs[1] != mp.bp2) s.protection_ok = false;
  }

  if (s.uv == 0) {
    s.lsp_bits = field(bits, kLsp1200, 27);
    s.gain_index = field(bits, kGain1200Mode5, 10);
    s.aperiodic = kAperiodic[0].flag0;
  } else {
    s.lsp_bits = 0;  // 43 bits do not fit an int; the codebooks are absent anyway
    s.gain_index = field(bits, kGain1200, 10);
    s.fs_index = field(bits, kFs1200, 8);
    int j = bits[kJitter1200 - 1] & 1;
    s.aperiodic = j ? kAperiodic[s.uv].flag1 : kAperiodic[s.uv].flag0;
  }
  return s;
}

std::vector<Superframe1200> unpack_1200(const std::vector<uint8_t>& bytes) {
  std::vector<Superframe1200> out;
  uint8_t bits[81];
  for (size_t off = 0; off + 11 <= bytes.size(); off += 11) {
    for (int k = 0; k < 81; ++k) bits[k] = (bytes[off + k / 8] >> (k % 8)) & 1;
    out.push_back(decode_superframe_1200(bits));
  }
  return out;
}

void superframe_to_frames(const Superframe1200& s, const Fill1200& fill, Frame out[3]) {
  for (int f = 0; f < 3; ++f) {
    Frame& fr = out[f];
    fr = Frame();
    fr.gain[0] = fr.gain[1] = fill.gain_db;
    for (int i = 0; i < kOrder; ++i)
      fr.lsf[i] = fill.have_lsf ? fill.lsf[i] : 4000.0 * (i + 1) / (kOrder + 1);
    fr.aperiodic = (s.aperiodic >> (2 - f)) & 1;
    if (s.voiced(f)) {
      fr.pitch = fill.pitch;
      fr.vbp[0] = 1;
      const uint8_t* cw = s4591::kBpCodeword[s.bp_index[f] & 3];
      for (int b = 0; b < 4; ++b) fr.vbp[b + 1] = cw[b];
    }
  }
}

}  // namespace melp
