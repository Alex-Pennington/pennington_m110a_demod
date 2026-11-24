// m75_fading_check - validate the MIL-STD-188-110D Appendix E fading model
// (src/channel/appe_channel.h, the model m110d_chan runs) at the Doppler spreads the 75 bps
// STANAG 4415 measurements use (0.5 to 50 Hz), and the two-path channel built from it.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Per Doppler spread d (two-sigma width; sigma = d/2), on appe::fading_gains interpolated to
// a sample rate of at least 96 d:
//   - mean power (unit by design)
//   - spectrum: 2 sigma from the second moment, and the share of power within +/- sigma and
//     +/- 2 sigma (Gaussian: 0.683, 0.954; hfchansim's Butterworth shape differs)
//   - envelope: P(|g|^2 < 0.1) and P(|g|^2 < 0.01) against the Rayleigh (exponential power)
//     values 0.0952 and 0.00995
//   - autocorrelation at the lag where a Gaussian spectrum puts it at 0.5
//   - two paths drawn as appe::apply draws them (streams 100, 101): correlation coefficient
// Then the whole chain at 48 kHz (appe::apply): white noise through two equal paths 10 ms
// apart with fading d: the input/output cross-correlation peaks at lags 0 and 480 samples,
// each with half the power, and nothing at other lags.
//
// Usage: m75_fading_check [--seconds S] [--doppler 0.5,2,5,10,20,50]
//   --seconds: record length per spread in units of 1/d seconds x 1000 (default 1.0: 1000
//   correlation times per spread). Exit status 1 when a check fails.

#include "channel/appe_channel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using appe::cd;

namespace {

int fails = 0;

void check(const char* what, double d, double got, double lo, double hi) {
    const bool ok = got >= lo && got <= hi;
    fails += !ok;
    printf("d=%5.1f Hz  %-42s %10.4f  [%.4f, %.4f]  %s\n", d, what, got, lo, hi, ok ? "PASS" : "FAIL");
}

}  // namespace

int main(int argc, char** argv) {
    double scale = 1.0;
    std::vector<double> ds = {0.5, 2, 5, 10, 20, 50};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) scale = atof(argv[++i]);
        else if (!strcmp(argv[i], "--doppler") && i + 1 < argc) {
            ds.clear();
            std::stringstream s(argv[++i]);
            std::string v;
            while (std::getline(s, v, ',')) ds.push_back(atof(v.c_str()));
        } else { fprintf(stderr, "usage: m75_fading_check [--seconds S] [--doppler LIST]\n"); return 2; }
    }
    uint64_t seed = 5;
    for (double d : ds) {
        seed++;
        const double sigma = d / 2;
        // sample rate: a power-of-two multiple of 4 d, at least 96 d
        int fs = 1;
        while (fs < 96 * d) fs <<= 1;
        if (fs < 64) fs = 64;
        const double T = 1000.0 / d * scale;  // 1000 correlation times
        const size_t n = static_cast<size_t>(T * fs);
        appe::Rng r1(seed, 100), r2(seed, 101);
        const std::vector<cd> g = appe::fading_gains(n, fs, d, 0, r1);
        const std::vector<cd> h = appe::fading_gains(n, fs, d, 0, r2);
        // N_eff: about T * d independent samples; tolerances are 4 standard errors
        const double neff = T * d;
        double p = 0, p01 = 0, p001 = 0;
        cd x12 = 0;
        double ph = 0;
        for (size_t i = 0; i < n; i++) {
            const double e = std::norm(g[i]);
            p += e;
            ph += std::norm(h[i]);
            p01 += e < 0.1;
            p001 += e < 0.01;
            x12 += g[i] * std::conj(h[i]);
        }
        p /= n; ph /= n; p01 /= n; p001 /= n;
        const double rho12 = std::abs(x12) / n / std::sqrt(p * ph);
        const double se = 1.0 / std::sqrt(neff);
        check("mean power (unit)", d, p, 1 - 4 * se, 1 + 4 * se);
        check("P(|g|^2 < 0.1), Rayleigh 0.0952", d, p01, 0.0952 - 4 * std::sqrt(0.0952 / neff), 0.0952 + 4 * std::sqrt(0.0952 / neff));
        check("P(|g|^2 < 0.01), Rayleigh 0.00995", d, p001, 0.00995 - 4 * std::sqrt(0.00995 / neff),
              0.00995 + 4 * std::sqrt(0.00995 / neff));
        check("|corr(path 1, path 2)|", d, rho12, 0, 4 * se);
        // autocorrelation at the half lag: exp(-2 pi^2 sigma^2 tau^2) = 0.5
        const double tau = std::sqrt(std::log(2.0) / (2 * M_PI * M_PI * sigma * sigma));
        const size_t lag = static_cast<size_t>(std::lround(tau * fs));
        cd ac = 0;
        for (size_t i = 0; i + lag < n; i++) ac += g[i + lag] * std::conj(g[i]);
        const double rho = std::real(ac) / (n - lag) / p;
        const double rho_want = std::exp(-2 * M_PI * M_PI * sigma * sigma * std::pow(lag / static_cast<double>(fs), 2));
        check("autocorr at Gaussian half lag, minus theory", d, rho - rho_want, -4 * se, 4 * se);
        // Welch spectrum, Hann, segments of 64 / d seconds (resolution d / 64)
        size_t seg = 1;
        while (seg < static_cast<size_t>(64.0 / d * fs)) seg <<= 1;
        std::vector<double> psd(seg, 0.0);
        int nseg = 0;
        for (size_t s0 = 0; s0 + seg <= n; s0 += seg / 2) {
            std::vector<cd> a(g.begin() + s0, g.begin() + s0 + seg);
            for (size_t i = 0; i < seg; i++) a[i] *= 0.5 - 0.5 * std::cos(2 * M_PI * i / seg);
            appe::fft(a, false);
            for (size_t k = 0; k < seg; k++) psd[k] += std::norm(a[k]);
            nseg++;
        }
        double s0 = 0, s2 = 0, in1 = 0, in2 = 0;
        for (size_t k = 0; k < seg; k++) {
            const double f = static_cast<double>(k < seg / 2 ? static_cast<long>(k) : static_cast<long>(k) - static_cast<long>(seg)) * fs / seg;
            s0 += psd[k];
            s2 += f * f * psd[k];
            if (std::fabs(f) <= sigma) in1 += psd[k];
            if (std::fabs(f) <= 2 * sigma) in2 += psd[k];
        }
        check("2 sigma of the spectrum / d", d, 2 * std::sqrt(s2 / s0) / d, 0.95, 1.05);
        check("power within +/- sigma, Gaussian 0.683", d, in1 / s0, 0.683 - 0.03, 0.683 + 0.03);
        check("power within +/- 2 sigma, Gaussian 0.954", d, in2 / s0, 0.954 - 0.015, 0.954 + 0.015);
    }

    // The chain at 48 kHz: two equal paths 10 ms apart, white noise in. Lags are even: the
    // analytic signal of white noise is uncorrelated at even lags (|2 / pi L|^2 at odd ones)
    for (double d : {0.5, 50.0}) {
        const int fs = 48000;
        const double secs = (d < 1 ? 240.0 : 60.0) * std::max(0.25, scale);
        const size_t n = static_cast<size_t>(secs * fs);
        appe::Rng rn(9, 3);
        std::vector<double> x(n);
        for (auto& v : x) v = 0.1 * rn.normal();
        appe::Config c;
        c.paths = {appe::Path{0, 0, d, 0}, appe::Path{10, 0, d, 0}};
        c.seed = 4;
        appe::Stats st;
        const std::vector<double> y = appe::apply(x, fs, c, st);
        // cross-correlation power per lag, accumulated over 50 ms blocks (fading changes the
        // phase from block to block): E|sum y x(lag)|^2 is the path power at that lag
        const std::vector<cd> xa = appe::analytic(x), ya = appe::analytic(y);
        const size_t blk = fs / 20;
        auto lag_power = [&](long L) {
            double acc = 0;
            int nb = 0;
            for (size_t b0 = 1000; b0 + blk + 1000 < n; b0 += blk) {
                cd s = 0;
                for (size_t i = b0; i < b0 + blk; i++) s += ya[i] * std::conj(xa[i - L]);
                acc += std::norm(s);
                nb++;
            }
            return acc / nb;
        };
        const double p0 = lag_power(0), p480 = lag_power(480), p240 = lag_power(240), p482 = lag_power(482);
        const double ref = p0 + p480;
        // each path's share: independent fading over secs x d correlation times
        const double tol = 4.0 * 0.5 / std::sqrt(secs * d);
        check("chain: power at lag 0 / total", d, p0 / ref, 0.5 - tol, 0.5 + tol);
        check("chain: power at lag 480 (10 ms) / total", d, p480 / ref, 0.5 - tol, 0.5 + tol);
        check("chain: power at lag 240 / total", d, p240 / ref, 0, 0.02);
        check("chain: power at lag 482 / total", d, p482 / ref, 0, 0.02);
        printf("chain: delay samples %d, %d\n", st.delay_samples[0], st.delay_samples[1]);
    }
    printf("fading check: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
