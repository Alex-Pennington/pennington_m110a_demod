// melp_synth: synthesise 8 kHz 16-bit PCM from a MELP parameter file.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Written from NATO STANAG 4591 Ed. 1 Annex A (A.5.3) and the Harris MELP 600 paper
// (MILCOM 2001); see melp_synth.h. Usage: melp_synth --help.
#include <cstdio>
#include <cstring>
#include <string>

#include "melp_synth/melp_synth.h"

static void usage() {
  std::fprintf(stderr,
      "usage: melp_synth [options] <params.txt> <out.wav|out.raw>\n"
      "  --frame-ms 22.5|25     override the file's frame length\n"
      "  --excitation bandpass|harmonic   (default bandpass)\n"
      "  --postfilter           apply the optional postfilter (A.5.3.10)\n"
      "  --no-noise-att  --no-ase  --no-dispersion   disable a stage\n"
      "  --seed N               noise/jitter generator seed (default 1)\n"
      "Output ending in .raw is headerless s16le; anything else is WAV.\n"
      "Parameter file format: see src/melp_synth/melp_synth.h or docs/melp-synth.md.\n");
}

int main(int argc, char** argv) {
  melp::Options opt;
  double frame_ms = 0;
  const char* in = nullptr;
  const char* out = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) { usage(); return 0; }
    else if (!std::strcmp(a, "--frame-ms")) { const char* v = next(); if (!v) { usage(); return 2; } frame_ms = std::atof(v); }
    else if (!std::strcmp(a, "--excitation")) {
      const char* v = next();
      if (v && !std::strcmp(v, "bandpass")) opt.excitation = melp::Excitation::Bandpass;
      else if (v && !std::strcmp(v, "harmonic")) opt.excitation = melp::Excitation::Harmonic;
      else { usage(); return 2; }
    }
    else if (!std::strcmp(a, "--postfilter")) opt.postfilter = true;
    else if (!std::strcmp(a, "--no-noise-att")) opt.noise_attenuation = false;
    else if (!std::strcmp(a, "--no-ase")) opt.spectral_enhancement = false;
    else if (!std::strcmp(a, "--no-dispersion")) opt.pulse_dispersion = false;
    else if (!std::strcmp(a, "--seed")) { const char* v = next(); if (!v) { usage(); return 2; } opt.seed = uint32_t(std::strtoul(v, nullptr, 10)); }
    else if (a[0] == '-' && a[1]) { std::fprintf(stderr, "unknown option %s\n", a); usage(); return 2; }
    else if (!in) in = a;
    else if (!out) out = a;
    else { usage(); return 2; }
  }
  if (!in || !out) { usage(); return 2; }

  melp::ParamFile pf;
  std::string err;
  if (!melp::read_params(in, pf, err)) { std::fprintf(stderr, "melp_synth: %s\n", err.c_str()); return 1; }
  opt.frame_len = frame_ms > 0 ? int(frame_ms * 8.0 + 0.5) : pf.frame_len;
  if (opt.frame_len != 180 && opt.frame_len != 200) {
    std::fprintf(stderr, "melp_synth: frame length must be 22.5 or 25 ms\n");
    return 1;
  }
  melp::Synthesizer syn(opt);
  std::vector<int16_t> pcm;
  for (const auto& f : pf.frames) syn.synth_frame(f, pcm);

  const std::string o(out);
  bool ok;
  if (o.size() > 4 && o.compare(o.size() - 4, 4, ".raw") == 0) {
    FILE* f = std::fopen(out, "wb");
    ok = f && std::fwrite(pcm.data(), 2, pcm.size(), f) == pcm.size();
    if (f) std::fclose(f);
  } else {
    ok = melp::write_wav(out, pcm);
  }
  if (!ok) { std::fprintf(stderr, "melp_synth: cannot write %s\n", out); return 1; }
  std::fprintf(stderr, "melp_synth: %zu frames, %d samples/frame, %zu samples\n",
               pf.frames.size(), opt.frame_len, pcm.size());
  return 0;
}
