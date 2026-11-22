// melp_synth: MELP parameter-domain speech synthesiser.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Written from the published descriptions only:
//   [1] NATO STANAG 4591 Edition 1, Annex A, sections A.5.2 (encoder: band edges, LPC
//       and gain conventions) and A.5.3 (decoder: A.5.3.1 parameter defaults, A.5.3.2
//       noise attenuation, A.5.3.3 pitch-synchronous interpolation, A.5.3.4 mixed
//       excitation, A.5.3.5 adaptive spectral enhancement, A.5.3.6 LPC synthesis,
//       A.5.3.7 gain, A.5.3.8 pulse dispersion, A.5.3.9 loop control, A.5.3.10
//       postfilter); Annex D section D.4.8 (harmonic excitation).
//   [2] MIL-STD-3005 MELP model as summarised in [1] Annex A.5.1 and [3].
//   [3] M. W. Chamberlain, Harris, "A 600 bps MELP vocoder for use on HF channels",
//       MILCOM 2001 (25 ms frame).
//   [4] RFC 8130 (MELPe 600/1200/2400 RTP payload): frame sizes.
// No code from any MELP/MELPe implementation was read or used.
//
// One frame of parameters in, N samples of 8 kHz 16-bit PCM out, where N is 180 for the
// standard 22.5 ms frame and 200 for the 25 ms frame of Harris 600.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace melp {

constexpr int kOrder = 10;     // LPC order
constexpr int kBands = 5;      // bandpass voicing bands
constexpr int kFourier = 10;   // Fourier magnitudes carried
constexpr int kMinPitch = 20;  // [1] A.5.3.4: pitch clamp
constexpr int kMaxPitch = 160;

// One frame of decoded MELP parameters, as a decoder holds them before synthesis.
struct Frame {
  double pitch = 0;               // samples, 20..160; 0 = unvoiced frame
  double gain[2] = {10, 10};      // G1, G2 in dB (RMS of 16-bit PCM)
  double vbp[kBands] = {0, 0, 0, 0, 0};  // band voicing strengths 0..1, band 1 = 0-500 Hz
  double lsf[kOrder] = {};        // line spectral frequencies, Hz, ascending
  bool have_fourier = false;      // false: all magnitudes 1
  double fourier[kFourier] = {};  // magnitudes of harmonics 1..10 of the residual
  bool aperiodic = false;         // jitter flag: 25 % pitch jitter when set
};

enum class Excitation {
  Bandpass,  // pulse and noise through five band-pass shaping filters (MELP model)
  Harmonic,  // frequency-domain mixing per [1] A.5.3.4 / D.4.8 (MELPe)
};

struct Options {
  int frame_len = 180;                 // samples per frame: 180 (22.5 ms) or 200 (25 ms)
  Excitation excitation = Excitation::Bandpass;
  bool noise_attenuation = true;       // [1] A.5.3.2
  bool spectral_enhancement = true;    // [1] A.5.3.5
  bool pulse_dispersion = true;        // [1] A.5.3.8
  bool postfilter = false;             // [1] A.5.3.10 (optional in the standard)
  uint32_t seed = 1;                   // noise and jitter generator seed
};

// Helpers exposed for tests.
// LSFs (Hz, ascending) -> predictor A(z) = 1 + sum a[i] z^-i, a[0] = 1, 11 entries.
void lsf_to_lpc(const double* lsf_hz, double* a);
// [1] A.5.2.14: ascending order and 50 Hz minimum separation, in place.
void lsf_order_and_separate(double* lsf_hz);
// First reflection coefficient of A(z) by step-down recursion (MELPe sign: A = 1 + sum a).
double first_reflection(const double* a);
// [1] Table A-1 rule: cutoff frequency (Hz) from voicing of bands 2..5; 0 if unvoiced.
double cutoff_frequency(const Frame& f);
// The derived 65-tap pulse dispersion filter (see melp_synth.cpp).
const std::vector<double>& dispersion_filter();

class Synthesizer {
 public:
  explicit Synthesizer(const Options& opt = Options());
  // Synthesise one frame; appends exactly frame_len samples to out.
  void synth_frame(const Frame& f, std::vector<int16_t>& out);
  const Options& options() const { return opt_; }

 private:
  struct Interp;  // parameters interpolated at one pitch-period start
  Frame normalise(const Frame& in) const;
  double rand_uniform();  // -1..1
  void excitation_period(const Interp& p, int T, std::vector<double>& e);
  void postfilter_frame(const Frame& cur, std::vector<double>& x);

  Options opt_;
  int N_;
  bool first_ = true;
  Frame prev_;
  double g2p_ = 10;       // previous frame's G2 (after attenuation)
  double gn_ = 20;        // background noise estimate, dB ([1] A.5.3.2)
  double prev_scale_ = 0;
  double prev_tilt_ = 0;  // tilt coefficient at the end of the previous frame
  int t0_ = 0;            // start of the next pitch period within the frame
  std::vector<double> pending_;  // synthesised samples past the end of the last frame
  uint32_t rng_;
  // filter memories
  double pulse_hist_[64] = {}, noise_hist_[64] = {};
  double ase_x_[kOrder + 1] = {}, ase_y_[kOrder + 1] = {}, tilt_x_ = 0;
  double syn_y_[kOrder] = {};
  std::vector<double> disp_hist_;
  // postfilter
  double pf_x_[kOrder] = {}, pf_y_[kOrder] = {}, pf_tilt_x_ = 0, pf_gprev_ = 1;
  double lp_x_[2] = {}, lp_y_[2] = {}, hp_x_[2] = {}, hp_y_[2] = {};
  double pf_prev_lsf_[kOrder] = {};
};

// ---- parameter files and WAV ----
// Text format, one item per line, '#' starts a comment:
//   frame_ms 22.5          (or 25; or "frame_len 200"; default 22.5)
//   frame pitch=<samples|0> g1=<dB> g2=<dB> vbp=v1,v2,v3,v4,v5 lsf=f1,...,f10
//         [fm=m1,...,m10] [ap=0|1]
// pitch=0 marks an unvoiced frame. lsf in Hz. vbp entries are 0/1 or strengths 0..1.
struct ParamFile {
  int frame_len = 180;
  std::vector<Frame> frames;
};
bool read_params(const std::string& path, ParamFile& pf, std::string& err);
bool parse_params(const std::string& text, ParamFile& pf, std::string& err);
bool write_wav(const std::string& path, const std::vector<int16_t>& pcm, int rate = 8000);

}  // namespace melp
