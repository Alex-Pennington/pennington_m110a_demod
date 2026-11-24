// m75_excise.h - narrowband interference excision in front of the 75 bps receiver.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// STANAG 4415 asks a 75 bps receiver to work through narrowband interference: up to 4
// tones, a swept CW, FSK and SSB voice, 20 to 40 dB above the wanted signal (secondary
// sources: docs/m110a-75bps.md). The 75 bps signal spreads its power over about 2.4 kHz,
// so an interferer that is strong in a few frequency bins can be cut out there at a small
// cost to the wanted signal. Design:
//
//  - Short-time Fourier transform: Hann analysis window of about 40 ms (a power of two,
//    2048 points at 48 kHz, 23 Hz bins), hop half a window, overlap-add synthesis without
//    a second window (a periodic Hann at 50% overlap sums to 1, so untouched frames come
//    back as they went in).
//  - Floor: per frame, the median bin power over carrier +/- 1000 Hz (the flat part of the
//    RRC spectrum) pooled over the frames 8 either side, divided by ln 2 (the median of an
//    exponential is ln 2 times its mean). Pooled over 0.4 s it follows fading and level
//    changes but is not moved by interference in fewer than half the bins.
//  - Detection: a frame is hit when any bin within carrier +/- 1650 Hz (the signal's band)
//    holds more than `detect_db` (20 dB) over its floor. Noise and the 75 bps signal give
//    exponential bin powers: exceeding 100 times the mean has probability e^-100, so a
//    signal with no narrowband interference is never touched, and the receiver sees the
//    input itself, sample for sample.
//  - Weighting: in a hit frame and the frames either side, each in-band bin above `keep`
//    (3) times the floor is scaled in amplitude by keep x floor / power. That is about the
//    weight a combiner gives a bin whose interference outweighs the signal (weight in
//    proportion to S / (S + I) on amplitude): a tone 50 dB up is left at -50 dB, a bin a
//    little over the floor loses little. Tones, sweeps and FSK are cut where they are;
//    the harmonics of a voice are cut harmonic by harmonic.

#ifndef M110A_M75_EXCISE_H
#define M110A_M75_EXCISE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace m110a {
namespace m75 {

struct ExciseTuning {
    double detect_db = 20.0;     // a frame is hit when a bin holds this much over the floor
    double keep = 3.0;           // in hit frames, a bin of power p above keep x floor is scaled to (keep x floor)^2 / p
    double band_hz = 1650.0;     // excision band: carrier +/- this
    double floor_band_hz = 1000.0;  // floor estimated over carrier +/- this
    int floor_frames = 8;        // floor pooled over this many frames either side
    double window_s = 0.04;      // analysis window, rounded up to a power of two
};

struct ExciseStats {
    long frames = 0, hit_frames = 0;
    double cut_share = 0.0;      // share of in-band bin-frames scaled down
};

namespace detail {

inline void fft_inplace(std::vector<std::complex<float>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / static_cast<double>(len) * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; j++) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * std::complex<float>(w);
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& v : a) v /= static_cast<float>(n);
}

}  // namespace detail

/**
 * Excise narrowband interference from rf (real passband, sample rate fs, 75 bps carrier at
 * `carrier`). Returns true and fills `out` when any frame was hit; returns false and leaves
 * `out` untouched when nothing was found (the caller then uses rf as it is).
 */
inline bool excise(const std::vector<float>& rf, float fs, float carrier, std::vector<float>& out,
                   ExciseStats* stats = nullptr, const ExciseTuning& t = ExciseTuning()) {
    size_t N = 1;
    while (N < static_cast<size_t>(t.window_s * fs)) N <<= 1;
    const size_t H = N / 2;
    if (rf.size() < N) return false;
    const double df = fs / static_cast<double>(N);
    const long k_lo = std::max(1L, static_cast<long>(std::floor((carrier - t.band_hz) / df)));
    const long k_hi = std::min(static_cast<long>(N / 2) - 1, static_cast<long>(std::ceil((carrier + t.band_hz) / df)));
    const long f_lo = std::max(k_lo, static_cast<long>(std::floor((carrier - t.floor_band_hz) / df)));
    const long f_hi = std::min(k_hi, static_cast<long>(std::ceil((carrier + t.floor_band_hz) / df)));
    if (k_hi <= k_lo || f_hi <= f_lo) return false;
    const long nb = k_hi - k_lo + 1;

    std::vector<float> win(N);
    for (size_t i = 0; i < N; i++) win[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * i / N));  // periodic Hann
    // frames start at -H so every sample is covered by two frames
    const long nf = static_cast<long>((rf.size() + H) / H) + 1;
    std::vector<std::complex<float>> spec(static_cast<size_t>(nf) * nb);
    std::vector<float> pw(static_cast<size_t>(nf) * nb);
    std::vector<std::complex<float>> buf(N);
    auto frame = [&](long f) {
        const long s0 = f * static_cast<long>(H) - static_cast<long>(H);
        for (size_t i = 0; i < N; i++) {
            const long s = s0 + static_cast<long>(i);
            buf[i] = (s >= 0 && s < static_cast<long>(rf.size())) ? std::complex<float>(rf[s] * win[i], 0.0f)
                                                                  : std::complex<float>(0.0f, 0.0f);
        }
        detail::fft_inplace(buf, false);
    };
    for (long f = 0; f < nf; f++) {
        frame(f);
        for (long k = 0; k < nb; k++) {
            spec[f * nb + k] = buf[k_lo + k];
            pw[f * nb + k] = std::norm(buf[k_lo + k]);
        }
    }
    // floor per frame: pooled median over the floor band, frames f +/- floor_frames
    std::vector<double> fl(nf);
    std::vector<float> pool;
    const long fb0 = f_lo - k_lo, fb1 = f_hi - k_lo;
    for (long f = 0; f < nf; f++) {
        pool.clear();
        for (long g = std::max(0L, f - t.floor_frames); g <= std::min(nf - 1, f + t.floor_frames); g++)
            for (long k = fb0; k <= fb1; k++) pool.push_back(pw[g * nb + k]);
        std::nth_element(pool.begin(), pool.begin() + pool.size() / 2, pool.end());
        fl[f] = pool[pool.size() / 2] / std::log(2.0);
    }
    const double det = std::pow(10.0, t.detect_db / 10.0);
    std::vector<char> hit(nf, 0);
    long nhit = 0;
    for (long f = 0; f < nf; f++) {
        if (fl[f] <= 0.0) continue;
        for (long k = 0; k < nb && !hit[f]; k++)
            if (pw[f * nb + k] > det * fl[f]) hit[f] = 1;
        nhit += hit[f];
    }
    if (stats) { stats->frames = nf; stats->hit_frames = nhit; stats->cut_share = 0.0; }
    if (nhit == 0) return false;

    // weight the bins of hit frames and their neighbours; resynthesise every frame
    out.assign(rf.size(), 0.0f);
    long cut = 0, inb = 0;
    for (long f = 0; f < nf; f++) {
        const bool w = hit[f] || (f > 0 && hit[f - 1]) || (f + 1 < nf && hit[f + 1]);
        frame(f);
        if (w) {
            const double th = t.keep * fl[f];
            for (long k = 0; k < nb; k++) {
                const double p = pw[f * nb + k];
                inb++;
                if (p > th && p > 0.0) {
                    const float g = static_cast<float>(th / p);
                    buf[k_lo + k] *= g;
                    buf[N - (k_lo + k)] *= g;  // the conjugate bin, so the frame stays real
                    cut++;
                }
            }
        }
        detail::fft_inplace(buf, true);
        const long s0 = f * static_cast<long>(H) - static_cast<long>(H);
        for (size_t i = 0; i < N; i++) {
            const long s = s0 + static_cast<long>(i);
            if (s >= 0 && s < static_cast<long>(out.size())) out[s] += buf[i].real();
        }
    }
    if (stats) stats->cut_share = inb ? static_cast<double>(cut) / inb : 0.0;
    return true;
}

}  // namespace m75
}  // namespace m110a

#endif  // M110A_M75_EXCISE_H
