// Fast checks on the printed STANAG 4591 tables (stanag4591_tables.h) and the 1200 bit/s
// codebook-free field decoder (melp1200.h). Uses no private tool or data.
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <cstring>

#include "melp_synth/melp1200.h"
#include "melp_synth/stanag4591_tables.h"

using namespace melp;
using namespace melp::s4591;

static int fails = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } \
  } while (0)

static int weight(int v) { int n = 0; for (; v; v >>= 1) n += v & 1; return n; }

// Builds a 1200 bit/s superframe from field values (B_01..B_81).
struct Builder {
  uint8_t b[81] = {};
  void put(int first_bit, int n, int v) {
    for (int i = 0; i < n; ++i) b[first_bit - 1 + i] = (v >> i) & 1;
  }
};

int main() {
  // Table 1: 8 unvoiced codes (weight 0, 1), 21 erasures (weight 2), indices 0..98 once
  // each, increasing with the code.
  int nu = 0, ne = 0, seen[99] = {}, last = -1;
  for (int c = 0; c < 128; ++c) {
    int v = kPitchCode[c];
    if (weight(c) <= 1) { CHECK(v == kUnvoiced); ++nu; }
    else if (weight(c) == 2) { CHECK(v == kErasure); ++ne; }
    else { CHECK(v >= 0 && v < 99); if (v >= 0 && v < 99) ++seen[v]; CHECK(v > last); last = v; }
  }
  CHECK(nu == 8 && ne == 21);
  for (int i = 0; i < 99; ++i) CHECK(seen[i] == 1);
  // spot values read off the page
  CHECK(kPitchCode[0x07] == 0 && kPitchCode[0x23] == 16 && kPitchCode[0x61] == 68 &&
        kPitchCode[0x7F] == 98);

  // uniform quantisers (s5.3.7)
  CHECK(std::fabs(std::pow(10.0, kPitch.value(0)) - 20) < 1e-9);
  CHECK(std::fabs(std::pow(10.0, kPitch.value(98)) - 160) < 1e-9);
  CHECK(std::fabs(kGain2.value(31) - 77.0) < 1e-12 && std::fabs(kGain2.value(1) - (10 + 67.0 / 31)) < 1e-12);

  // Table 3: field widths and each (field, subscript) exactly once
  int width[11] = {}, seen3[11][8] = {};
  for (const auto& e : kOrder2400) { ++width[e.field]; CHECK(e.sub < 8); ++seen3[e.field][e.sub]; }
  const int want[11] = {5, 3, 7, 7, 6, 6, 6, 4, 8, 1, 1};  // G2 G1 P LSF1-4 BP FM AF SYNC
  for (int f = 0; f < 11; ++f) {
    CHECK(width[f] == want[f]);
    for (int s = 0; s < want[f]; ++s) CHECK(seen3[f][s] == 1);
  }
  CHECK(kOrder2400[53].field == SYNC);
  // unvoiced FEC sits exactly on the 13 BP, FM and AF positions
  int nfec = 0;
  for (const auto& e : kFec2400UV) {
    Field2400 f = kOrder2400[e.bit - 1].field;
    CHECK(f == BP || f == FM || f == AF);
    ++nfec;
  }
  CHECK(nfec == 13);

  // Table D-2: every pattern maps to a codeword, the codewords map to themselves
  int per[4] = {};
  for (int p = 0; p < 16; ++p) ++per[kBpPatternToIndex[p]];
  CHECK(per[0] == 7 && per[1] == 3 && per[2] == 1 && per[3] == 5);
  for (int i = 0; i < 4; ++i) {
    int p = kBpCodeword[i][0] << 3 | kBpCodeword[i][1] << 2 | kBpCodeword[i][2] << 1 | kBpCodeword[i][3];
    CHECK(kBpPatternToIndex[p] == i);
  }
  CHECK(kBpPatternToIndex[0b1001] == 1);  // the example printed under the table

  // Table D-4: each pattern with two or three voiced frames has a code
  for (int c = 1; c < 8; ++c) CHECK(weight(kMode3Bit[c]) >= 2);
  CHECK(kMode3Bit[0] == kModeShared);

  // Table D-8: the flag decodes as printed for a few rows
  CHECK(kAperiodic[0b111].flag0 == 0 && kAperiodic[0b111].flag1 == 0b111);
  CHECK(kAperiodic[0b101].flag0 == kAperiodic[0b101].flag1);

  // Decoder on built superframes
  {
    Builder s;  // VVV, codebook B (101), cutoffs 1000/2000/4000, jitter set
    s.put(kSync1200, 1, 1);
    s.put(kPitchUV1200, 3, 0b101);
    s.put(kPitchUV1200 + 3, 9, 0x155);
    s.put(kBp1200, 2, 1); s.put(kBp1200 + 2, 2, 2); s.put(kBp1200 + 4, 2, 3);
    s.put(kJitter1200, 1, 1);
    s.put(kGain1200, 10, 0x2a5);
    s.put(kFs1200, 8, 0x81);
    Superframe1200 d = decode_superframe_1200(s.b);
    CHECK(d.sync == 1 && d.mode3 == 0b101 && d.pitch9 == 0x155 && d.uv == 0b111);
    CHECK(d.bp_index[0] == 1 && d.bp_index[1] == 2 && d.bp_index[2] == 3);
    CHECK(d.aperiodic == 0b111 && d.gain_index == 0x2a5 && d.fs_index == 0x81 && d.protection_ok);
    Frame fr[3];
    superframe_to_frames(d, Fill1200(), fr);
    CHECK(fr[0].pitch > 0 && fr[0].vbp[1] == 1 && fr[0].vbp[2] == 0);
    CHECK(fr[2].vbp[4] == 1);
  }
  {
    Builder s;  // UVU: 000, 9-bit code of weight 3, protection 2 = "10" in (BP3,BP2)
    s.put(kPitchUV1200 + 3, 9, 0b000010101);
    s.put(kBp1200, 2, 2);       // the voiced frame: 2000 Hz
    s.put(kBp1200 + 2, 2, 0b10);
    Superframe1200 d = decode_superframe_1200(s.b);
    CHECK(d.uv == 0b010 && d.uv_from_protection && d.protection_ok);
    CHECK(d.bp_index[0] == -1 && d.bp_index[1] == 2 && d.bp_index[2] == -1);
  }
  {
    Builder s;  // UUU: 000 and a 9-bit code with one bit set
    s.put(kPitchUV1200 + 3, 9, 0x010);
    s.put(kGain1200Mode5, 10, 0x3ff);
    Superframe1200 d = decode_superframe_1200(s.b);
    CHECK(d.uv == 0 && d.gain_index == 0x3ff && d.fs_index == -1);
  }
  {
    Builder s;  // VVU with wrong protection bits in (BP5,BP4)
    s.put(kPitchUV1200, 3, 0b001);
    s.put(kBp1200 + 4, 2, 0b10);
    Superframe1200 d = decode_superframe_1200(s.b);
    CHECK(d.uv == 0b110 && !d.protection_ok);
  }

  std::printf(fails ? "test_stanag4591_tables: %d failures\n" : "test_stanag4591_tables: all passed\n", fails);
  return fails ? 1 : 0;
}
