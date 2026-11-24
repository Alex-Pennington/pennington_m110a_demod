// m75_acq.h - preamble acquisition for 75 bps below the reach of the shared 110A
// acquisition.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// BrainDecoder (the preamble finder every mode uses) takes a normalised correlation of
// one frame's 288 common symbols and needs it above 0.7, and ModemRX's AUTO pass needs
// 0.5. That metric is at most sqrt(S / (S + N)) per symbol, so neither gate opens much
// below 0 dB SNR in 3 kHz, where 75 bps still decodes (docs/m110a-75bps.md). This
// acquisition integrates the whole preamble instead:
//
//  - MIL-STD-188-110D 5.3.2.3.7.2: a frame is 15 segments of 32 symbols, channel symbols
//    0 1 3 0 1 3 1 2 0 D1 D2 C1 C2 C3 0, each segment mapped by Table XIII and scrambled by
//    the 32-symbol sync sequence from its first symbol. So a segment's 32 symbols depend
//    only on its channel symbol: the matched-filter output is correlated with the 8
//    possible segments once, at every half-symbol start.
//  - The 13 segments of a frame other than D1 and D2 are known once the frame's place in
//    the countdown is assumed. Each segment scores the share of its 8 correlation energies
//    that falls on the expected word (non-coherent: no carrier phase needed, fading
//    allowed; scale-free: a segment in a quiet stretch weighs as much as one in a loud one).
//    The shares are summed over the last 3 frames (short) or the last 24 (long) before an
//    assumed end of preamble; their excess over 1/8 each, in standard deviations of
//    noise, is the significance z that picks the end of preamble, the frame count and the
//    carrier offset (7.5 Hz grid, then 0.5 Hz).
//  - D1 and D2 are then read as a pair by the same shares summed over those frames:
//    every code Table XI defines (and the Harris 5,6 / 5,7) is scored, e1[D1] + e2[D2],
//    and the best taken. The mode is the one that code names; it is never taken from the
//    metric.

#ifndef M110A_M75_ACQ_H
#define M110A_M75_ACQ_H

#include "common/types.h"
#include "dsp/fir_filter.h"
#include "m110a/brain_preamble.h"
#include "m110a/mode_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace m110a {
namespace m75 {

struct Acquisition {
    bool found = false;      // a preamble at least min_z significant was found
    double z = 0.0;          // its significance
    long data_start = 0;     // end of the preamble = first data sample, input samples
    int frames = 0;          // 3 or 24: which countdown matched best
    float freq_offset_hz = 0.0f;
    int d1 = -1, d2 = -1;    // the defined D1,D2 code read best over the frames found
    double pair_ratio = 0;   // its energy over the runner-up code's
    double d1_e = 0, d1_e2 = 0, d2_e = 0, d2_e2 = 0;  // per digit: best and runner-up energies
    ModeId mode = ModeId::M2400S;  // from D1/D2 when they are 7,5 or 5,5
    bool is_75 = false;
};

struct AcqTuning {
    double min_z = 9.0;           // significance needed (noise alone: z up to 6.6 in 24 buffers of 20 s)
    double max_start_s = 15.0;    // latest start of the first preamble frame searched
    double coarse_step_hz = 7.5;
    double fine_step_hz = 0.5;
};

namespace detail {

// The 15 channel symbols of a preamble frame of mode index mi with this countdown, as
// BrainPreambleEncoder sends it: each 32-symbol segment matched to its Table XIII word.
inline std::array<int, 15> frame_symbols(int mi, int countdown) {
    BrainPreambleEncoder enc;
    const auto f = enc.encode_frame(mi, countdown);
    std::array<int, 15> out{};
    for (int s = 0; s < 15; s++) {
        out[s] = -1;
        for (int ch = 0; ch < 8 && out[s] < 0; ch++) {
            bool same = true;
            for (int i = 0; i < 32 && same; i++) {
                const int t = (brain::psymbol[ch][i % 8] + brain::pscramble[i]) & 7;
                const std::complex<float> r(brain::psk8_i[t], brain::psk8_q[t]);
                same = std::abs(f[s * 32 + i] - r) < 1e-3f;
            }
            if (same) out[s] = ch;
        }
    }
    return out;
}

}  // namespace detail

/**
 * Search rf (real passband at sample rate fs, carrier `carrier`) for a 110A preamble that
 * ends a short (3-frame) or long (24-frame) countdown, carrier offset within +/-frange Hz.
 */
inline Acquisition acquire(const std::vector<float>& rf, float fs, float carrier, float frange,
                           const AcqTuning& t = AcqTuning()) {
    Acquisition a;
    const int sps = static_cast<int>(std::lround(fs / 2400.0f));
    if (sps < 2 || std::fabs(sps * 2400.0f - fs) > 1.0f) return a;
    const int D = sps % 2 == 0 ? sps / 2 : 1;  // decimate to 2 samples per symbol when we can
    const int sd = sps / D;
    const double fsd = fs / D;
    const long nfull = std::min<long>(static_cast<long>(rf.size()),
                                      static_cast<long>((t.max_start_s + 24 * 0.2 + 0.2) * fs));
    const long nd = nfull / D;
    constexpr int SEG = 32, FRAME = 480;
    if (nd < 3 * FRAME * sd) return a;

    // matched filter (RRC 0.35, 6 symbols, centred as BrainDecoder's) at the decimated points
    const auto taps = generate_srrc_taps(0.35f, 6, static_cast<float>(sps));
    const long half = static_cast<long>(taps.size() / 2);
    std::vector<std::complex<float>> bb(nfull);
    const double w = 2.0 * M_PI * carrier / fs;
    for (long i = 0; i < nfull; i++) {
        const double ph = std::fmod(w * i, 2.0 * M_PI);
        bb[i] = std::complex<float>(static_cast<float>(rf[i] * std::cos(ph)), static_cast<float>(-rf[i] * std::sin(ph)));
    }
    std::vector<std::complex<float>> y(nd);
    for (long m = 0; m < nd; m++) {
        const long c = m * D;
        std::complex<float> acc(0, 0);
        const long j0 = std::max(0L, half - c), j1 = std::min<long>(static_cast<long>(taps.size()), nfull - c + half);
        for (long j = j0; j < j1; j++) acc += bb[c - half + j] * taps[j];
        y[m] = acc;
    }
    bb.clear();
    bb.shrink_to_fit();

    // A segment of channel symbol ch is psk8(psymbol[ch][i % 8] + pscramble[i]), i < 32: the
    // sync scrambler times a +/-1 word of period 8. Descramble once, fold the 4 repeats of
    // 8, and an 8-point Walsh-Hadamard transform gives all 8 correlations.
    std::complex<float> descr[SEG];
    for (int i = 0; i < SEG; i++)
        descr[i] = std::complex<float>(brain::psk8_i[brain::pscramble[i]], -brain::psk8_q[brain::pscramble[i]]);
    int sign[8][8];
    for (int ch = 0; ch < 8; ch++)
        for (int k = 0; k < 8; k++) sign[ch][k] = brain::psymbol[ch][k] ? -1 : 1;
    // channel symbols of each frame before the end: frame k = 1 is the last (countdown 0)
    std::vector<std::array<int, 15>> fr(24);
    for (int k = 1; k <= 24; k++) fr[k - 1] = detail::frame_symbols(0, k - 1);  // D1/D2 unused

    const long span = static_cast<long>(SEG - 1) * sd;
    const long ne = nd - span;  // segment starts with a whole segment inside y
    if (ne <= 0) return a;
    std::vector<float> E(8 * static_cast<size_t>(ne)), A(ne);
    std::vector<std::complex<float>> z(nd);
    auto energies = [&](double f, long from, long to) {
        // E[ch][m], A[m] = sum over ch, for segment starts m in [from, to), carrier offset f
        from = std::max(0L, from);
        to = std::min(ne, to);
        if (from >= to) return;
        const double wf = -2.0 * M_PI * f / fsd;
        for (long n = from; n < std::min(nd, to + span + 1); n++) {
            const double ph = std::fmod(wf * static_cast<double>(n), 2.0 * M_PI);
            z[n] = y[n] * std::complex<float>(static_cast<float>(std::cos(ph)), static_cast<float>(std::sin(ph)));
        }
        for (long m = from; m < to; m++) {
            std::complex<float> u[8] = {};
            for (int i = 0; i < SEG; i++) u[i & 7] += z[m + static_cast<long>(i) * sd] * descr[i];
            double all = 0.0;
            float e[8];
            for (int ch = 0; ch < 8; ch++) {
                std::complex<float> c(0, 0);
                for (int k = 0; k < 8; k++) c += sign[ch][k] > 0 ? u[k] : -u[k];
                e[ch] = std::norm(c);
                all += e[ch];
            }
            // each word's share of the segment's energy: scale-free, so a segment in a quiet
            // stretch counts as much as one in a loud one, and no single segment can count for
            // more than its 1 - 1/8
            for (int ch = 0; ch < 8; ch++) E[ch * ne + m] = all > 0.0 ? static_cast<float>(e[ch] / all) : 0.125f;
            A[m] = 1.0f;
        }
    };
    // Per frame start m: energy of the 10 segments every frame shares (common + last zero)
    // and the sum over all 8 words at those places
    std::vector<float> Kc(ne, 0.0f), KcA(ne, 0.0f);
    auto frame_sums = [&](long from, long to) {
        for (long m = std::max(0L, from); m < std::min(ne, to); m++) {
            double k = 0.0, ka = 0.0;
            for (int seg : {0, 1, 2, 3, 4, 5, 6, 7, 8, 14}) {
                const long q = m + static_cast<long>(seg) * SEG * sd;
                if (q >= ne) { k = 0.0; ka = 0.0; break; }
                k += E[fr[0][seg] * ne + q];
                ka += A[q];
            }
            Kc[m] = static_cast<float>(k);
            KcA[m] = static_cast<float>(ka);
        }
    };
    // Significance of the preamble ending at T (decimated) with `frames` frames. Frames
    // that would start before the buffer are left out: the caller may have cut the buffer
    // inside a long preamble (ModemRX's first_preamble_cut, which at low SNR fires on
    // whichever frame first crosses its threshold).
    auto score = [&](long T, int frames) -> double {
        double s = 0.0, wrong = 0.0;
        int inside = 0;
        for (int k = 1; k <= frames; k++) {
            const long f0 = T - static_cast<long>(k) * FRAME * sd;
            if (f0 < 0) break;
            inside++;
            s += Kc[f0];
            wrong += KcA[f0] - Kc[f0];
            for (int seg = 11; seg <= 13; seg++) {  // the count C1 C2 C3 of this frame
                const long q = f0 + static_cast<long>(seg) * SEG * sd;
                if (q >= ne) continue;
                const float e = E[fr[k - 1][seg] * ne + q];
                s += e;
                wrong += A[q] - e;
            }
        }
        (void)wrong;
        // shares of 8 noise-only energies: mean 1/8, variance 7 / (64 x 9)
        const int n = inside * 13;
        return n ? (s - n / 8.0) / std::sqrt(n * 7.0 / 576.0) : 0.0;
    };

    const long tmax_start = static_cast<long>(t.max_start_s * fsd);
    double best = -1e30;
    long bestT = 0;
    int bestF = 3;
    double bestf = 0.0;
    const long frame_d = static_cast<long>(FRAME) * sd;
    for (double f = -frange; f <= frange + 1e-9; f += t.coarse_step_hz) {
        energies(f, 0, ne);
        frame_sums(0, ne);
        for (int frames : {3, 24}) {
            const long T0 = frames * frame_d;
            for (long T = frame_d; T - T0 <= tmax_start && T - frame_d + 14L * SEG * sd < ne; T++) {
                const double z = score(T, frames);
                if (z > best) { best = z; bestT = T; bestF = frames; bestf = f; }
            }
        }
    }
    // fine carrier offset around the coarse one, at the timing found (and a sample either side)
    double fbest = bestf, zf = best;
    long Tf = bestT;
    const long T0 = bestF * frame_d;
    for (double f = bestf - t.coarse_step_hz / 2; f <= bestf + t.coarse_step_hz / 2 + 1e-9; f += t.fine_step_hz) {
        energies(f, std::max(0L, bestT - T0 - 2), bestT + 2);
        frame_sums(std::max(0L, bestT - T0 - 2), bestT + 2);
        for (long T = bestT - 1; T <= bestT + 1; T++) {
            if (T < frame_d || T - frame_d + 14L * SEG * sd >= ne) continue;
            const double z = score(T, bestF);
            if (z > zf) { zf = z; fbest = f; Tf = T; }
        }
    }
    a.z = zf;
    a.frames = bestF;
    a.freq_offset_hz = static_cast<float>(fbest);
    a.data_start = Tf * D;
    a.found = zf >= t.min_z;

    // D1 and D2 over the frames found, at the offset found
    energies(fbest, Tf - bestF * frame_d - 1, Tf + 1);
    double e1[8] = {}, e2[8] = {};
    for (int k = 1; k <= bestF; k++) {
        const long f0 = Tf - static_cast<long>(k) * FRAME * sd;
        if (f0 < 0) break;
        for (int q = 0; q < 8; q++) {
            e1[q] += E[q * ne + f0 + 9L * SEG * sd];
            e2[q] += E[q * ne + f0 + 10L * SEG * sd];
        }
    }
    auto top2 = [](const double* e, int& arg, double& b1, double& b2) {
        arg = 0;
        for (int q = 1; q < 8; q++) if (e[q] > e[arg]) arg = q;
        b1 = e[arg];
        b2 = 0.0;
        for (int q = 0; q < 8; q++) if (q != arg) b2 = std::max(b2, e[q]);
    };
    int arg;
    top2(e1, arg, a.d1_e, a.d1_e2);
    top2(e2, arg, a.d2_e, a.d2_e2);
    static const int CODES[][2] = {{7, 5}, {5, 5}, {7, 4}, {5, 4}, {6, 7}, {4, 7}, {6, 6}, {4, 6}, {6, 5},
                                   {4, 5}, {6, 4}, {4, 4}, {7, 6}, {7, 7}, {5, 6}, {5, 7}};
    double b1 = -1.0, b2 = -1.0;
    for (const auto& c : CODES) {
        const double e = e1[c[0]] + e2[c[1]];
        if (e > b1) { b2 = b1; b1 = e; a.d1 = c[0]; a.d2 = c[1]; }
        else if (e > b2) b2 = e;
    }
    a.pair_ratio = b2 > 0.0 ? b1 / b2 : 0.0;
    if (a.d1 == 7 && a.d2 == 5) { a.mode = ModeId::M75NS; a.is_75 = true; }
    if (a.d1 == 5 && a.d2 == 5) { a.mode = ModeId::M75NL; a.is_75 = true; }
    return a;
}

}  // namespace m75
}  // namespace m110a

#endif  // M110A_M75_ACQ_H
