// melp1200_fields: decode the codebook-free fields of STANAG 4591 1200 bit/s superframes.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Input: 11-byte superframes, B_01 in the LSB of byte 0 (RFC 8130 Figure 3; the
// reference coder's -b 54 packing). Output: one line per superframe on stdout, and a
// melp_synth parameter file in which the fields the codebooks would give (pitch, gains,
// LSFs, Fourier magnitudes) are fixed stand-ins. See docs/melp-synth.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "melp_synth/melp1200.h"

static void usage() {
  std::fprintf(stderr,
      "usage: melp1200_fields [--pitch SAMPLES] [--gain DB] <in.bit> [out.params]\n"
      "  stdout: sf sync mode3 pitch9 uv prot cutoff1..3 aperiodic gain_index fs_index\n"
      "  out.params: melp_synth frames; pitch, gains and LSFs are stand-ins\n");
}

int main(int argc, char** argv) {
  melp::Fill1200 fill;
  const char* in = nullptr;
  const char* out = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) { usage(); return 0; }
    if (!std::strcmp(a, "--pitch") && i + 1 < argc) fill.pitch = std::atof(argv[++i]);
    else if (!std::strcmp(a, "--gain") && i + 1 < argc) fill.gain_db = std::atof(argv[++i]);
    else if (a[0] == '-' && a[1]) { usage(); return 2; }
    else if (!in) in = a;
    else if (!out) out = a;
    else { usage(); return 2; }
  }
  if (!in) { usage(); return 2; }
  FILE* f = std::fopen(in, "rb");
  if (!f) { std::fprintf(stderr, "melp1200_fields: cannot read %s\n", in); return 1; }
  std::vector<uint8_t> bytes;
  for (int c; (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
  std::fclose(f);
  auto sfs = melp::unpack_1200(bytes);

  static const char* kUV[8] = {"UUU", "UUV", "UVU", "UVV", "VUU", "VUV", "VVU", "VVV"};
  static const int kCut[4] = {500, 1000, 2000, 4000};
  FILE* o = out ? std::fopen(out, "w") : nullptr;
  if (out && !o) { std::fprintf(stderr, "melp1200_fields: cannot write %s\n", out); return 1; }
  if (o) std::fprintf(o, "# melp1200_fields %s: pitch %.1f, gain %.1f dB and LSFs are stand-ins\n"
                         "frame_ms 22.5\n", in, fill.pitch, fill.gain_db);
  for (size_t i = 0; i < sfs.size(); ++i) {
    const auto& s = sfs[i];
    std::printf("%zu %d %d%d%d %03x %s %s", i, s.sync, (s.mode3 >> 2) & 1, (s.mode3 >> 1) & 1,
                s.mode3 & 1, s.pitch9, kUV[s.uv], s.protection_ok ? "ok" : "bad");
    for (int k = 0; k < 3; ++k) std::printf(" %d", s.bp_index[k] < 0 ? 0 : kCut[s.bp_index[k]]);
    std::printf(" %d%d%d %d %d\n", (s.aperiodic >> 2) & 1, (s.aperiodic >> 1) & 1,
                s.aperiodic & 1, s.gain_index, s.fs_index);
    if (o) {
      melp::Frame fr[3];
      melp::superframe_to_frames(s, fill, fr);
      for (const auto& x : fr) {
        std::fprintf(o, "frame pitch=%g g1=%g g2=%g vbp=%g,%g,%g,%g,%g lsf=", x.pitch,
                     x.gain[0], x.gain[1], x.vbp[0], x.vbp[1], x.vbp[2], x.vbp[3], x.vbp[4]);
        for (int k = 0; k < melp::kOrder; ++k) std::fprintf(o, k ? ",%.1f" : "%.1f", x.lsf[k]);
        std::fprintf(o, " ap=%d\n", x.aperiodic ? 1 : 0);
      }
    }
  }
  if (o) std::fclose(o);
  std::fprintf(stderr, "melp1200_fields: %zu superframes\n", sfs.size());
  return 0;
}
