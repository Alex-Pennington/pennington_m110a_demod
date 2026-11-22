// melp_synth: MELP parameter-domain speech synthesiser.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Written from the published descriptions only: NATO STANAG 4591 Ed. 1 Annex A (A.5.2,
// A.5.3) and Annex D (D.4.8); the MELP 600 paper (Harris, MILCOM 2001); RFC 8130. Section
// numbers below are STANAG 4591's. No code from any MELP/MELPe implementation was read or
// used. Where the standard leaves a choice open ("empirically derived", or a table this
// file does not copy) the choice made here is marked CHOICE and described in
// docs/melp-synth.md.
#include "melp_synth/melp_synth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace melp {

namespace {

constexpr double kFs = 8000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kShapeTaps = 65;  // CHOICE: band-pass shaping filter length
constexpr int kDispTaps = 65;   // A.5.3.8: 65th-order FIR ("65 coefficients", Table A-2)

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Polynomial product helper: c = a * b (coefficients in z^-1).
std::vector<double> polymul(const std::vector<double>& a, const std::vector<double>& b) {
  std::vector<double> c(a.size() + b.size() - 1, 0.0);
  for (size_t i = 0; i < a.size(); ++i)
    for (size_t j = 0; j < b.size(); ++j) c[i + j] += a[i] * b[j];
  return c;
}

// Windowed-sinc low-pass, cutoff fc (Hz), kShapeTaps taps, Hamming window, unit DC gain
// before windowing normalisation is not applied so that the band filters telescope.
std::vector<double> lowpass(double fc) {
  std::vector<double> h(kShapeTaps);
  const int c = kShapeTaps / 2;
  const double w = 2.0 * fc / kFs;  // normalised to Nyquist
  for (int n = 0; n < kShapeTaps; ++n) {
    const int m = n - c;
    const double s = m == 0 ? w : std::sin(kPi * w * m) / (kPi * m);
    const double win = 0.54 - 0.46 * std::cos(2.0 * kPi * n / (kShapeTaps - 1));
    h[n] = s * win;
  }
  return h;
}

// CHOICE: the five shaping filters are differences of windowed-sinc low-passes at the band
// edges of A.5.2.4 (500, 1000, 2000, 3000 Hz). Their sum is exactly a delayed unit
// impulse, so fully voiced bands pass the pulse unaltered and all bands together pass
// pulse + noise in complementary shares.
const std::vector<std::vector<double>>& shaping_filters() {
  static const std::vector<std::vector<double>> h = [] {
    const double edges[4] = {500, 1000, 2000, 3000};
    std::vector<std::vector<double>> lp;
    for (double e : edges) lp.push_back(lowpass(e));
    std::vector<double> delta(kShapeTaps, 0.0);
    delta[kShapeTaps / 2] = 1.0;
    lp.push_back(delta);
    std::vector<std::vector<double>> bands(kBands, std::vector<double>(kShapeTaps));
    for (int n = 0; n < kShapeTaps; ++n) {
      bands[0][n] = lp[0][n];
      for (int b = 1; b < kBands; ++b) bands[b][n] = lp[b][n] - lp[b - 1][n];
    }
    return bands;
  }();
  return h;
}

// Full step-down: reflection coefficients k[1..p] of A(z) = 1 + sum a[i] z^-i.
// Returns false if |k| >= 1 somewhere (unstable).
bool reflection_coeffs(const double* a, double* k) {
  double cur[kOrder + 1], nxt[kOrder + 1];
  for (int i = 0; i <= kOrder; ++i) cur[i] = a[i];
  bool ok = true;
  for (int m = kOrder; m >= 1; --m) {
    const double km = cur[m];
    k[m] = km;
    const double den = 1.0 - km * km;
    if (std::fabs(km) >= 1.0 || den <= 1e-12) { ok = false; break; }
    for (int i = 1; i < m; ++i) nxt[i] = (cur[i] - km * cur[m - i]) / den;
    for (int i = 1; i < m; ++i) cur[i] = nxt[i];
  }
  return ok;
}

// 2nd-order Butterworth biquad by the bilinear transform.
struct Biquad {
  double b0, b1, b2, a1, a2;
};
Biquad butter2(double fc, bool highpass) {
  const double K = std::tan(kPi * fc / kFs);
  const double q = std::sqrt(2.0);
  const double norm = 1.0 / (1.0 + q * K + K * K);
  Biquad f;
  if (highpass) {
    f.b0 = norm; f.b1 = -2 * norm; f.b2 = norm;
  } else {
    f.b0 = K * K * norm; f.b1 = 2 * f.b0; f.b2 = f.b0;
  }
  f.a1 = 2 * (K * K - 1) * norm;
  f.a2 = (1 - q * K + K * K) * norm;
  return f;
}
double biquad_step(const Biquad& f, double x, double* xs, double* ys) {
  const double y = f.b0 * x + f.b1 * xs[0] + f.b2 * xs[1] - f.a1 * ys[0] - f.a2 * ys[1];
  xs[1] = xs[0]; xs[0] = x;
  ys[1] = ys[0]; ys[0] = y;
  return y;
}

}  // namespace

// ---- LSF and LPC helpers -------------------------------------------------------------

void lsf_to_lpc(const double* lsf_hz, double* a) {
  // A(z) = (P(z) + Q(z)) / 2 with P(z) = A(z) + z^-11 A(1/z), Q(z) = A(z) - z^-11 A(1/z).
  // P has the root z = -1 and the odd-numbered LSFs, Q the root z = +1 and the even ones.
  std::vector<double> P = {1.0, 1.0}, Q = {1.0, -1.0};
  for (int i = 0; i < kOrder; ++i) {
    const double w = 2.0 * kPi * lsf_hz[i] / kFs;
    const std::vector<double> sec = {1.0, -2.0 * std::cos(w), 1.0};
    if (i % 2 == 0) P = polymul(P, sec); else Q = polymul(Q, sec);
  }
  for (int i = 0; i <= kOrder; ++i) a[i] = 0.5 * (P[i] + Q[i]);
  a[0] = 1.0;
}

void lsf_order_and_separate(double* f) {
  // A.5.2.14, in Hz, 0-based indices (f[0] is the standard's f1).
  for (int pass = 0; pass < 10; ++pass) {
    bool swapped = false;
    for (int i = 0; i + 1 < kOrder; ++i)
      if (f[i] > f[i + 1]) { std::swap(f[i], f[i + 1]); swapped = true; }
    if (!swapped) break;
  }
  const double dmin = 50.0;
  for (int pass = 0; pass < 10; ++pass) {
    for (int i = 0; i + 1 < kOrder; ++i) {
      const double d = f[i + 1] - f[i];
      if (d < dmin) {
        double s1 = (dmin - d) / 2, s2 = (dmin - d) / 2;
        if (i == 0 && f[i] < dmin) {
          s1 = f[i] / 2;
        } else if (i > 0) {
          const double tmp = f[i] - f[i - 1];
          if (tmp < dmin) s1 = 0;
          else if (tmp < 2 * dmin) s1 = (tmp - dmin) / 2;
        }
        if (i == kOrder - 2 && f[i + 1] > 4000 - dmin) {
          s2 = (4000 - f[i + 1]) / 2;
        } else if (i < kOrder - 2) {
          const double tmp = f[i + 2] - f[i + 1];
          if (tmp < dmin) s2 = 0;
          else if (tmp < 2 * dmin) s2 = (tmp - dmin) / 2;
        }
        f[i] -= s1;  // the printed pseudo code shows only the f[i+1] update; the
        f[i + 1] += s2;  // symmetric f[i] update is implied by s1 (CHOICE)
      }
    }
  }
}

double first_reflection(const double* a) {
  double k[kOrder + 1] = {};
  reflection_coeffs(a, k);
  return k[1];
}

double cutoff_frequency(const Frame& f) {
  // Table A-1, expressed as the rule its rows follow: three or more of bands 2..5 voiced
  // -> 4000 Hz; otherwise band 2 unvoiced -> 500 Hz; band 2 and 3 voiced -> 2000 Hz;
  // band 2 voiced only (with at most one of 4, 5) -> 1000 Hz.
  if (f.pitch <= 0 || f.vbp[0] < 0.5) return 0.0;
  const bool v2 = f.vbp[1] >= 0.5, v3 = f.vbp[2] >= 0.5, v4 = f.vbp[3] >= 0.5,
             v5 = f.vbp[4] >= 0.5;
  const int ones = v2 + v3 + v4 + v5;
  if (ones >= 3) return 4000.0;
  if (!v2) return 500.0;
  if (v3) return 2000.0;
  return 1000.0;
}

const std::vector<double>& dispersion_filter() {
  // CHOICE (A.5.3.8 describes the filter as "derived from a spectrally-flattened triangle
  // pulse"; its Table A-2 is not copied): an asymmetric triangle, rising over 45 samples
  // and falling over 20 (slow glottal opening, fast closure), length 65; its 65-point
  // DFT is set to unit magnitude, phase kept, and transformed back. The result has unit
  // energy and spreads a pulse over the period without changing its spectrum.
  static const std::vector<double> h = [] {
    const int L = kDispTaps, R = 45;
    std::vector<double> t(L);
    for (int n = 0; n < L; ++n)
      t[n] = n <= R ? double(n) / R : double(L - 1 - n) / (L - 1 - R);
    std::vector<double> re(L), im(L);
    for (int k = 0; k < L; ++k) {
      double sr = 0, si = 0;
      for (int n = 0; n < L; ++n) {
        sr += t[n] * std::cos(2 * kPi * k * n / L);
        si -= t[n] * std::sin(2 * kPi * k * n / L);
      }
      const double m = std::hypot(sr, si);
      re[k] = m > 1e-12 ? sr / m : 1.0;
      im[k] = m > 1e-12 ? si / m : 0.0;
    }
    std::vector<double> out(L);
    for (int n = 0; n < L; ++n) {
      double s = 0;
      for (int k = 0; k < L; ++k)
        s += re[k] * std::cos(2 * kPi * k * n / L) - im[k] * std::sin(2 * kPi * k * n / L);
      out[n] = s / L;
    }
    double e = 0;
    for (double v : out) e += v * v;
    for (double& v : out) v /= std::sqrt(e);
    return out;
  }();
  return h;
}

// ---- synthesiser ----------------------------------------------------------------------

struct Synthesizer::Interp {
  double gain;              // dB
  double pitch;             // samples
  double jitter;            // 0..0.25
  double vbp[kBands];
  double fourier[kFourier];
  double cutoff;            // Hz
  double lsf[kOrder];
  double tilt;
};

Synthesizer::Synthesizer(const Options& opt) : opt_(opt), N_(opt.frame_len), rng_(opt.seed) {
  if (rng_ == 0) rng_ = 0x9E3779B9u;
  disp_hist_.assign(kDispTaps - 1, 0.0);
}

double Synthesizer::rand_uniform() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return (rng_ / 4294967295.0) * 2.0 - 1.0;
}

Frame Synthesizer::normalise(const Frame& in) const {
  Frame f = in;
  lsf_order_and_separate(f.lsf);
  if (f.pitch <= 0) {
    // A.5.3.1 unvoiced defaults: pitch 50, jitter 25 %, voicing 0, Fourier magnitudes 1.
    f.pitch = 0;
    for (double& v : f.vbp) v = 0;
    f.aperiodic = true;
    for (double& m : f.fourier) m = 1.0;
    f.have_fourier = true;
  } else {
    f.pitch = clampd(f.pitch, kMinPitch, kMaxPitch);
    f.vbp[0] = 1.0;  // A.5.3.1: Vbp1 = 1 in voiced mode
    for (double& v : f.vbp) v = clampd(v, 0, 1);
    // A.5.3.1 exception: 0001 for bands 2..5 -> band 5 unvoiced
    if (f.vbp[1] < 0.5 && f.vbp[2] < 0.5 && f.vbp[3] < 0.5 && f.vbp[4] >= 0.5) f.vbp[4] = 0;
    if (!f.have_fourier) {
      for (double& m : f.fourier) m = 1.0;
      f.have_fourier = true;
    }
  }
  return f;
}

void Synthesizer::excitation_period(const Interp& p, int T, std::vector<double>& e) {
  e.assign(T, 0.0);
  const int L = T / 2;
  const int c = T / 2;  // pulse centre: keeps the pulse off the period boundary
  auto mag = [&](int k) { return k <= kFourier ? p.fourier[k - 1] : 1.0; };

  if (opt_.excitation == Excitation::Harmonic) {
    // A.5.3.4 / D.4.8. CHOICE: FL = 0.9 F, FH = 1.03 F (inside the published ranges
    // [0.85F, 0.98F] and [F, 1.05F]); mixed-band gain g = 1 - 0.1 log2(F / 250), i.e.
    // 0.9, 0.8, 0.7, 0.6 at 500, 1000, 2000, 4000 Hz ("the higher F, the smaller g").
    const double F = p.cutoff;
    const double VL = 0.90 * F * T / kFs, VH = 1.03 * F * T / kFs;
    const double g = F > 0 ? clampd(1.0 - 0.1 * std::log2(F / 250.0), 0.3, 1.0) : 1.0;
    const double phi0 = -2.0 * kPi * c / T;
    for (int k = 1; k <= L; ++k) {
      const double M = mag(k);
      double A, ph;
      const double rnd = 2.0 * kPi * rand_uniform();
      if (k < VL) {
        A = M; ph = k * phi0;
      } else if (k <= VH && VH > VL) {
        const double u = (k - VL) / (VH - VL);
        A = u * g * M + (VH - k) / (VH - VL) * M;
        ph = k * phi0 - u * rnd;
      } else {
        A = g * M; ph = rnd;
      }
      const double w = (T % 2 == 0 && k == L) ? 1.0 : 2.0;
      for (int n = 0; n < T; ++n) e[n] += w * A * std::cos(2.0 * kPi * k * n / T + ph);
    }
    const double s = 1.0 / std::sqrt(2.0 * std::max(L, 1));
    for (double& v : e) v *= s;
    return;
  }

  // Bandpass mixed excitation: one pitch pulse (inverse DFT of the Fourier magnitudes,
  // zero phase about the centre, unit RMS over the period) and white uniform noise of unit
  // RMS, each through the shaping filters weighted by the voicing strengths:
  //   pulse filter = sum_b vbp_b h_b, noise filter = sum_b (1 - vbp_b) h_b.
  std::vector<double> pulse(T, 0.0);
  for (int k = 1; k <= L; ++k) {
    const double w = (T % 2 == 0 && k == L) ? 1.0 : 2.0;
    const double M = mag(k);
    for (int n = 0; n < T; ++n) pulse[n] += w * M * std::cos(2.0 * kPi * k * (n - c) / T);
  }
  double pe = 0;
  for (double v : pulse) pe += v * v;
  const double ps = pe > 0 ? std::sqrt(T / pe) : 0.0;
  for (double& v : pulse) v *= ps;

  const auto& H = shaping_filters();
  double hp[kShapeTaps], hn[kShapeTaps];
  for (int j = 0; j < kShapeTaps; ++j) {
    hp[j] = hn[j] = 0;
    for (int b = 0; b < kBands; ++b) {
      hp[j] += p.vbp[b] * H[b][j];
      hn[j] += (1.0 - p.vbp[b]) * H[b][j];
    }
  }
  const double ns = std::sqrt(3.0);
  for (int n = 0; n < T; ++n) {
    const double xn = ns * rand_uniform();
    // shift histories: hist[0] is the newest past sample
    double yp = hp[0] * pulse[n], yn = hn[0] * xn;
    for (int j = 1; j < kShapeTaps; ++j) {
      yp += hp[j] * pulse_hist_[j - 1];
      yn += hn[j] * noise_hist_[j - 1];
    }
    for (int j = kShapeTaps - 2; j > 0; --j) {
      pulse_hist_[j] = pulse_hist_[j - 1];
      noise_hist_[j] = noise_hist_[j - 1];
    }
    pulse_hist_[0] = pulse[n];
    noise_hist_[0] = xn;
    e[n] = yp + yn;
  }
}

void Synthesizer::synth_frame(const Frame& in, std::vector<int16_t>& out) {
  Frame cur = normalise(in);
  const double half = N_ / 2.0;

  // A.5.3.2 noise attenuation; the estimator's step sizes are 3 dB/s up and 12 dB/s down
  // at two updates per frame, so they scale with the frame length.
  if (opt_.noise_attenuation) {
    const double up = 3.0 * (N_ / kFs) / 2.0, down = 12.0 * (N_ / kFs) / 2.0;
    for (int i = 0; i < 2; ++i) {
      double& G = cur.gain[i];
      if (G > gn_ + up) gn_ += up;
      else if (G < gn_ - down) gn_ -= down;
      else gn_ = G;
      gn_ = clampd(gn_, 10.0, 80.0);
      const double gn = std::min(gn_, 20.0);
      const double arg = 1.0 - std::pow(10.0, 0.1 * (gn + 3.0 - G));
      double att = arg > 0 ? -10.0 * std::log10(arg) : 6.0;
      att = clampd(att, 0.0, 6.0);
      G -= att;
    }
  }

  double a_cur[kOrder + 1];
  lsf_to_lpc(cur.lsf, a_cur);
  const double tilt_cur = std::max(0.5 * first_reflection(a_cur), 0.0);

  if (first_) {
    prev_ = cur;
    g2p_ = cur.gain[0];
    prev_tilt_ = tilt_cur;
    for (int i = 0; i < kOrder; ++i) pf_prev_lsf_[i] = cur.lsf[i];
    first_ = false;
  }
  const double pitch_prev = prev_.pitch > 0 ? prev_.pitch : 50.0;
  const double pitch_cur = cur.pitch > 0 ? cur.pitch : 50.0;
  const double jit_prev = prev_.aperiodic ? 0.25 : 0.0, jit_cur = cur.aperiodic ? 0.25 : 0.0;
  const double F_prev = cutoff_frequency(prev_), F_cur = cutoff_frequency(cur);
  // A.5.3.3 exceptions
  const bool onset_pitch = cur.gain[0] > g2p_ + 6.0 && pitch_cur < pitch_prev / 2.0;
  const bool gain_onset = std::fabs(cur.gain[1] - g2p_) > 6.0;

  std::vector<double> buf = pending_;
  int t0 = t0_;
  std::vector<double> e, s;
  while (t0 < N_) {
    Interp p;
    p.gain = t0 < half ? g2p_ + (cur.gain[0] - g2p_) * t0 / half
                       : cur.gain[0] + (cur.gain[1] - cur.gain[0]) * (t0 - half) / half;
    const double w = double(t0) / N_;
    double wg = w;
    if (gain_onset) wg = clampd((p.gain - g2p_) / (cur.gain[1] - g2p_), 0.0, 1.0);
    p.pitch = onset_pitch ? pitch_cur : pitch_prev + wg * (pitch_cur - pitch_prev);
    p.jitter = jit_prev + w * (jit_cur - jit_prev);
    for (int b = 0; b < kBands; ++b) p.vbp[b] = prev_.vbp[b] + w * (cur.vbp[b] - prev_.vbp[b]);
    for (int k = 0; k < kFourier; ++k)
      p.fourier[k] = prev_.fourier[k] + w * (cur.fourier[k] - prev_.fourier[k]);
    p.cutoff = F_prev + w * (F_cur - F_prev);
    for (int i = 0; i < kOrder; ++i) p.lsf[i] = prev_.lsf[i] + wg * (cur.lsf[i] - prev_.lsf[i]);
    p.tilt = prev_tilt_ + wg * (tilt_cur - prev_tilt_);

    // A.5.3.4: T = interpolated pitch + jitter * pitch * U(-1, 1), rounded, 20..160
    int T = int(std::lround(p.pitch * (1.0 + p.jitter * rand_uniform())));
    T = std::max(kMinPitch, std::min(kMaxPitch, T));

    excitation_period(p, T, e);

    double a[kOrder + 1];
    lsf_to_lpc(p.lsf, a);

    // A.5.3.5 adaptive spectral enhancement: A(alpha z^-1) / A(beta z^-1) * (1 + mu z^-1)
    if (opt_.spectral_enhancement) {
      const double prob = clampd((p.gain - gn_ - 12.0) / 18.0, 0.0, 1.0);
      const double al = 0.5 * prob, be = 0.8 * prob, mu = p.tilt * prob;
      double an[kOrder + 1], ad[kOrder + 1];
      double fa = 1, fb = 1;
      for (int i = 0; i <= kOrder; ++i) { an[i] = a[i] * fa; ad[i] = a[i] * fb; fa *= al; fb *= be; }
      for (int n = 0; n < T; ++n) {
        double y = e[n];
        for (int i = 1; i <= kOrder; ++i) y += an[i] * ase_x_[i - 1] - ad[i] * ase_y_[i - 1];
        for (int i = kOrder - 1; i > 0; --i) { ase_x_[i] = ase_x_[i - 1]; ase_y_[i] = ase_y_[i - 1]; }
        ase_x_[0] = e[n];
        ase_y_[0] = y;
        const double z = y + mu * tilt_x_;
        tilt_x_ = y;
        e[n] = z;
      }
    }

    // A.5.3.6 LPC synthesis 1/A(z), direct form
    s.assign(T, 0.0);
    for (int n = 0; n < T; ++n) {
      double y = e[n];
      for (int i = 1; i <= kOrder; ++i) y -= a[i] * syn_y_[i - 1];
      for (int i = kOrder - 1; i > 0; --i) syn_y_[i] = syn_y_[i - 1];
      syn_y_[0] = y;
      s[n] = y;
    }

    // A.5.3.7 gain: scale to the interpolated gain's RMS, ramped over 10 samples
    double ss = 0;
    for (double v : s) ss += v * v;
    const double rms = std::sqrt(ss / T);
    const double scale = rms > 1e-9 ? std::pow(10.0, p.gain / 20.0) / rms : 0.0;
    if (prev_scale_ <= 0) prev_scale_ = scale;
    for (int n = 0; n < T; ++n) {
      const double g = n < 10 ? prev_scale_ + (scale - prev_scale_) * (n + 1) / 10.0 : scale;
      s[n] *= g;
    }
    prev_scale_ = scale;

    // A.5.3.8 pulse dispersion
    if (opt_.pulse_dispersion) {
      const auto& h = dispersion_filter();
      for (int n = 0; n < T; ++n) {
        double y = h[0] * s[n];
        for (int j = 1; j < kDispTaps; ++j) y += h[j] * disp_hist_[j - 1];
        for (int j = kDispTaps - 2; j > 0; --j) disp_hist_[j] = disp_hist_[j - 1];
        disp_hist_[0] = s[n];
        s[n] = y;
      }
    }

    buf.insert(buf.end(), s.begin(), s.end());
    t0 += T;  // A.5.3.9 loop control
  }

  std::vector<double> frame(buf.begin(), buf.begin() + N_);
  pending_.assign(buf.begin() + N_, buf.end());
  t0_ = t0 - N_;

  if (opt_.postfilter) postfilter_frame(cur, frame);

  for (double v : frame) {
    const long q = std::lround(v);
    out.push_back(int16_t(q > 32767 ? 32767 : (q < -32768 ? -32768 : q)));
  }

  prev_ = cur;
  g2p_ = cur.gain[1];
  prev_tilt_ = tilt_cur;
}

void Synthesizer::postfilter_frame(const Frame& cur, std::vector<double>& x) {
  // A.5.3.10: four subframes, LSFs interpolated per subframe; H = A(gamma z)/A(beta z) *
  // (1 - mu z^-1), gamma 0.56, beta 0.75; mu 0 if prod(1 - r_i^2) > 0.3 else 0.2; gain
  // ratio of absolute sums, ramped; then 2nd-order Butterworth low-pass 3800 Hz and
  // high-pass 60 Hz.
  static const Biquad lp = butter2(3800.0, false), hp = butter2(60.0, true);
  const int sub = N_ / 4;
  for (int j = 0; j < 4; ++j) {
    const double w = (j + 1) / 4.0;
    double lsf[kOrder], a[kOrder + 1], r[kOrder + 1] = {};
    for (int i = 0; i < kOrder; ++i) lsf[i] = pf_prev_lsf_[i] + w * (cur.lsf[i] - pf_prev_lsf_[i]);
    lsf_to_lpc(lsf, a);
    reflection_coeffs(a, r);
    double t = 1;
    for (int i = 1; i <= kOrder; ++i) t *= 1.0 - r[i] * r[i];
    const double mu = t > 0.3 ? 0.0 : 0.2;
    double an[kOrder + 1], ad[kOrder + 1], fg = 1, fb = 1;
    for (int i = 0; i <= kOrder; ++i) { an[i] = a[i] * fg; ad[i] = a[i] * fb; fg *= 0.56; fb *= 0.75; }
    const int n0 = j * sub, n1 = (j == 3) ? N_ : n0 + sub;
    std::vector<double> y(n1 - n0);
    double ein = 0, eout = 0;
    for (int n = n0; n < n1; ++n) {
      double v = x[n];
      for (int i = 1; i <= kOrder; ++i) v += an[i] * pf_x_[i - 1] - ad[i] * pf_y_[i - 1];
      for (int i = kOrder - 1; i > 0; --i) { pf_x_[i] = pf_x_[i - 1]; pf_y_[i] = pf_y_[i - 1]; }
      pf_x_[0] = x[n];
      pf_y_[0] = v;
      const double z = v - mu * pf_tilt_x_;
      pf_tilt_x_ = v;
      y[n - n0] = z;
      ein += std::fabs(x[n]);
      eout += std::fabs(z);
    }
    const double g = eout >= 64 ? ein / eout : pf_gprev_;
    const int len = n1 - n0;
    for (int n = 0; n < len; ++n) {
      const double gg = (1.0 - double(n) / len) * pf_gprev_ + double(n) / len * g;
      double v = gg * y[n];
      v = biquad_step(lp, v, lp_x_, lp_y_);
      v = biquad_step(hp, v, hp_x_, hp_y_);
      x[n0 + n] = v;
    }
    pf_gprev_ = g;
  }
  for (int i = 0; i < kOrder; ++i) pf_prev_lsf_[i] = cur.lsf[i];
}

// ---- parameter files and WAV -------------------------------------------------------------

namespace {
bool parse_list(const std::string& s, double* out, int n) {
  std::stringstream ss(s);
  std::string item;
  int i = 0;
  while (std::getline(ss, item, ',')) {
    if (i >= n) return false;
    char* end = nullptr;
    out[i++] = std::strtod(item.c_str(), &end);
    if (end == item.c_str()) return false;
  }
  return i == n;
}
}  // namespace

bool parse_params(const std::string& text, ParamFile& pf, std::string& err) {
  std::istringstream in(text);
  std::string line;
  int lineno = 0;
  pf.frames.clear();
  while (std::getline(in, line)) {
    ++lineno;
    const auto hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream ls(line);
    std::string kw;
    if (!(ls >> kw)) continue;
    auto fail = [&](const std::string& m) {
      err = "line " + std::to_string(lineno) + ": " + m;
      return false;
    };
    if (kw == "frame_ms") {
      double ms;
      if (!(ls >> ms)) return fail("frame_ms needs a value");
      pf.frame_len = int(std::lround(ms * 8.0));
    } else if (kw == "frame_len") {
      if (!(ls >> pf.frame_len)) return fail("frame_len needs a value");
    } else if (kw == "frame") {
      Frame f;
      bool have_lsf = false, have_g = false;
      std::string tok;
      while (ls >> tok) {
        const auto eq = tok.find('=');
        if (eq == std::string::npos) return fail("expected key=value, got " + tok);
        const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        if (k == "pitch") f.pitch = std::strtod(v.c_str(), nullptr);
        else if (k == "g1") { f.gain[0] = std::strtod(v.c_str(), nullptr); have_g = true; }
        else if (k == "g2") { f.gain[1] = std::strtod(v.c_str(), nullptr); have_g = true; }
        else if (k == "vbp") { if (!parse_list(v, f.vbp, kBands)) return fail("vbp needs 5 values"); }
        else if (k == "lsf") { if (!parse_list(v, f.lsf, kOrder)) return fail("lsf needs 10 values"); have_lsf = true; }
        else if (k == "fm") { if (!parse_list(v, f.fourier, kFourier)) return fail("fm needs 10 values"); f.have_fourier = true; }
        else if (k == "ap") f.aperiodic = std::strtol(v.c_str(), nullptr, 10) != 0;
        else return fail("unknown key " + k);
      }
      if (!have_lsf) return fail("frame without lsf");
      if (!have_g) return fail("frame without g1/g2");
      pf.frames.push_back(f);
    } else {
      return fail("unknown keyword " + kw);
    }
  }
  if (pf.frame_len != 180 && pf.frame_len != 200) {
    err = "frame length must be 180 (22.5 ms) or 200 (25 ms) samples";
    return false;
  }
  return true;
}

bool read_params(const std::string& path, ParamFile& pf, std::string& err) {
  std::ifstream f(path);
  if (!f) { err = "cannot open " + path; return false; }
  std::stringstream ss;
  ss << f.rdbuf();
  return parse_params(ss.str(), pf, err);
}

bool write_wav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); };
  auto u16 = [&](uint16_t v) { unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); };
  const uint32_t data = uint32_t(pcm.size() * 2);
  std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
  std::fwrite("data", 1, 4, f); u32(data);
  for (int16_t v : pcm) u16(uint16_t(v));
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  return ok;
}

}  // namespace melp
