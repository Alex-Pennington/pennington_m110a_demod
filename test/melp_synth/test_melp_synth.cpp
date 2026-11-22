// Fast self-contained checks of the MELP parameter-domain synthesiser (src/melp_synth).
// No private tool or data: every input is a fixed parameter sequence built here.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <vector>

#include "melp_synth/melp_synth.h"

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      ++failures;                                         \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__);    \
      std::printf(__VA_ARGS__);                           \
      std::printf("\n");                                  \
    }                                                     \
  } while (0)

static const double kPi = 3.14159265358979323846;

// A vowel-like spectrum (formants near 700, 1200, 2500 Hz) as LSFs.
static const double kLsf[10] = {300, 650, 780, 1150, 1300, 2000, 2450, 2650, 3200, 3600};

static melp::Frame voiced(double pitch, double g, bool ap = false) {
  melp::Frame f;
  f.pitch = pitch;
  f.gain[0] = f.gain[1] = g;
  for (double& v : f.vbp) v = 1;
  for (int i = 0; i < 10; ++i) f.lsf[i] = kLsf[i];
  f.aperiodic = ap;
  return f;
}
static melp::Frame unvoiced(double g) {
  melp::Frame f = voiced(0, g);
  for (double& v : f.vbp) v = 0;
  return f;
}

static double rms_db(const std::vector<int16_t>& x, size_t a, size_t b) {
  double s = 0;
  for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
  return 10 * std::log10(0.01 + s / double(b - a));
}

// Highest normalised autocorrelation over lags lo..hi; lag returned in *lag.
static double ac_peak(const std::vector<int16_t>& x, size_t a, size_t b, int lo, int hi, int* lag) {
  double best = -1;
  *lag = 0;
  const size_t n = b - a - hi;
  for (int L = lo; L <= hi; ++L) {
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = 0; i < n; ++i) {
      const double u = x[a + i], v = x[a + i + L];
      xy += u * v; xx += u * u; yy += v * v;
    }
    const double r = xy / std::sqrt(xx * yy + 1e-9);
    if (r > best) { best = r; *lag = L; }
  }
  return best;
}

// LSFs of A(z) by scanning P(e^jw), Q(e^jw) for sign changes, to check lsf_to_lpc.
static std::vector<double> lsf_by_scan(const double* a) {
  std::vector<double> out;
  auto evalPQ = [&](double w, double& p, double& q) {
    // P(z) = A(z) + z^-11 A(1/z), Q = A - z^-11 A(1/z); evaluate e^{j w 11/2} * P real part
    double pr = 0, qr = 0;
    for (int i = 0; i <= 10; ++i) {
      const double c = std::cos(w * (5.5 - i));
      const double s = std::sin(w * (5.5 - i));
      pr += a[i] * 2 * c;  // A(z) z^{5.5} + its conjugate mirror
      qr += a[i] * 2 * s;
    }
    p = pr;
    q = qr;
  };
  const int steps = 40000;
  double pp, qp;
  evalPQ(1e-6, pp, qp);
  for (int k = 1; k < steps; ++k) {
    const double w = kPi * k / steps;
    double p, q;
    evalPQ(w, p, q);
    if ((p > 0) != (pp > 0)) out.push_back(w * 4000 / kPi);
    if ((q > 0) != (qp > 0)) out.push_back(w * 4000 / kPi);
    pp = p;
    qp = q;
  }
  return out;
}

static void test_lsf_roundtrip() {
  double a[11];
  melp::lsf_to_lpc(kLsf, a);
  std::vector<double> f = lsf_by_scan(a);
  CHECK(f.size() == 10, "scan found %zu LSFs", f.size());
  for (size_t i = 0; i < f.size() && i < 10; ++i)
    CHECK(std::fabs(f[i] - kLsf[i]) < 1.0, "LSF %zu: %.2f vs %.2f", i, f[i], kLsf[i]);
  // the vowel-like spectrum is low-pass: MELPe sign convention puts k1 negative
  CHECK(melp::first_reflection(a) < 0, "k1 = %.3f, expected negative", melp::first_reflection(a));
  double bad[10] = {100, 90, 500, 510, 1000, 2000, 2100, 3000, 3950, 3990};
  melp::lsf_order_and_separate(bad);
  for (int i = 0; i + 1 < 10; ++i)
    CHECK(bad[i + 1] - bad[i] >= 49.9, "separation %d: %.1f", i, bad[i + 1] - bad[i]);
}

static void test_cutoff() {
  melp::Frame f = voiced(80, 50);
  CHECK(melp::cutoff_frequency(f) == 4000, "all voiced -> 4000");
  f.vbp[1] = 0; f.vbp[2] = 0; f.vbp[3] = 0; f.vbp[4] = 0;
  CHECK(melp::cutoff_frequency(f) == 500, "0000 -> 500");
  f.vbp[1] = 1;
  CHECK(melp::cutoff_frequency(f) == 1000, "1000 -> 1000");
  f.vbp[2] = 1;
  CHECK(melp::cutoff_frequency(f) == 2000, "1100 -> 2000");
  f.vbp[1] = 0; f.vbp[3] = 1; f.vbp[4] = 1;
  CHECK(melp::cutoff_frequency(f) == 4000, "0111 -> 4000");
  CHECK(melp::cutoff_frequency(unvoiced(40)) == 0, "unvoiced -> 0");
}

// 30 frames voiced at pitch P, 30 unvoiced, 30 voiced aperiodic; checks length, energy,
// pitch and the absence of pitch in the unvoiced part.
static void test_sequence(int frame_len, melp::Excitation ex, const char* name) {
  melp::Options opt;
  opt.frame_len = frame_len;
  opt.excitation = ex;
  melp::Synthesizer syn(opt);
  std::vector<int16_t> pcm;
  const double P = 80, G = 50;
  for (int i = 0; i < 30; ++i) syn.synth_frame(voiced(P, G), pcm);
  for (int i = 0; i < 30; ++i) syn.synth_frame(unvoiced(G - 6), pcm);
  for (int i = 0; i < 30; ++i) syn.synth_frame(voiced(53, G, true), pcm);
  const size_t N = size_t(frame_len);
  CHECK(pcm.size() == 90 * N, "%s: %zu samples, expected %zu", name, pcm.size(), 90 * N);

  // energy: steady voiced frames 10..28 at G dB, unvoiced frames 40..58 at G-6 dB
  const double ev = rms_db(pcm, 10 * N, 29 * N), eu = rms_db(pcm, 40 * N, 59 * N);
  CHECK(std::fabs(ev - G) < 1.5, "%s: voiced RMS %.2f dB, commanded %.1f", name, ev, G);
  CHECK(std::fabs(eu - (G - 6)) < 1.5, "%s: unvoiced RMS %.2f dB, commanded %.1f", name, eu, G - 6);

  int lag;
  const double rv = ac_peak(pcm, 10 * N, 29 * N, 30, 150, &lag);
  CHECK(rv > 0.6 && std::abs(lag - int(P)) <= 1, "%s: voiced peak r=%.3f at lag %d, commanded %d",
        name, rv, lag, int(P));
  int lagu;
  const double ru = ac_peak(pcm, 40 * N, 59 * N, 30, 150, &lagu);
  CHECK(ru < 0.35, "%s: unvoiced peak r=%.3f at lag %d (should have no pitch)", name, ru, lagu);
  int laga;
  const double ra = ac_peak(pcm, 70 * N, 89 * N, 30, 150, &laga);
  CHECK(ra < rv, "%s: aperiodic peak r=%.3f not below periodic %.3f", name, ra, rv);
  std::printf("%-22s N=%d  len=%zu  voiced %.2f dB r=%.3f@%d  unvoiced %.2f dB r=%.3f  aperiodic r=%.3f@%d\n",
              name, frame_len, pcm.size(), ev, rv, lag, eu, ru, ra, laga);
}

static void test_determinism() {
  std::vector<int16_t> a, b, c;
  melp::Options o1;
  melp::Options o2;
  o2.seed = 7;
  melp::Synthesizer s1(o1), s2(o1), s3(o2);
  for (int i = 0; i < 10; ++i) {
    s1.synth_frame(unvoiced(40), a);
    s2.synth_frame(unvoiced(40), b);
    s3.synth_frame(unvoiced(40), c);
  }
  CHECK(a == b, "same seed, different output");
  CHECK(a != c, "different seed, same output");
}

static void test_parse() {
  const char* text =
      "# test\n"
      "frame_ms 25\n"
      "frame pitch=60 g1=40 g2=42 vbp=1,1,0,0,0 lsf=300,650,780,1150,1300,2000,2450,2650,3200,3600 ap=1\n"
      "frame pitch=0 g1=30 g2=30 vbp=0,0,0,0,0 lsf=300,650,780,1150,1300,2000,2450,2650,3200,3600 "
      "fm=1,1,1,1,1,1,1,1,1,1\n";
  melp::ParamFile pf;
  std::string err;
  const bool ok = melp::parse_params(text, pf, err);
  CHECK(ok, "parse failed: %s", err.c_str());
  CHECK(pf.frame_len == 200, "frame_len %d", pf.frame_len);
  CHECK(pf.frames.size() == 2, "%zu frames", pf.frames.size());
  if (pf.frames.size() == 2) {
    CHECK(pf.frames[0].pitch == 60 && pf.frames[0].aperiodic && pf.frames[0].gain[1] == 42, "frame 0 fields");
    CHECK(pf.frames[1].have_fourier && pf.frames[1].pitch == 0, "frame 1 fields");
  }
  melp::ParamFile bad;
  CHECK(!melp::parse_params("frame pitch=60 g1=1 g2=1 lsf=1,2,3\n", bad, err), "short lsf accepted");
}

int main() {
  test_lsf_roundtrip();
  test_cutoff();
  test_sequence(180, melp::Excitation::Bandpass, "bandpass 22.5 ms");
  test_sequence(200, melp::Excitation::Bandpass, "bandpass 25 ms");
  test_sequence(180, melp::Excitation::Harmonic, "harmonic 22.5 ms");
  test_sequence(200, melp::Excitation::Harmonic, "harmonic 25 ms");
  test_determinism();
  test_parse();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
