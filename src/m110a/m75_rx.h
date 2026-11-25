// m75_rx.h - MIL-STD-188-110A serial tone 75 bps (75S, 75L) receiver.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// The waveform is MIL-STD-188-110D 5.3.2 at 75 bps (src/m110a/m75_codec.h). This file is
// the demodulator; the standard leaves its design open. Design (docs/m110a-75bps.md):
//
//  - m75::acquire (src/m110a/m75_acq.h) finds the end of the preamble, the carrier
//    offset and D1/D2 by integrating whole preamble frames, well below the SNR where the
//    shared BrainDecoder acquisition gives out. The data starts at the end of the preamble.
//  - Each set of 32 symbols carries one of four orthogonal Walsh words, scrambled. The
//    matched-filter output is descrambled and correlated with the four words at every
//    tap of a delay window (a quarter symbol apart, +/-6 ms about the preamble timing);
//    each word's metric is its correlation energy summed over the taps, weighted by the
//    channel's power-delay profile less the noise floor. That is a non-coherent RAKE:
//    the phase may wander from set to set (fading, residual offset) and every path's
//    energy counts.
//  - The profile starts from the last preamble frame (known symbols) and follows the
//    winning word's tap energies; the noise floor follows the losing words'. The window
//    re-centres on the profile, so clock drift over a long message is followed.
//  - Soft bits per set come from the four metrics through Table IX (max-log), then
//    deinterleave and K=7 Viterbi (m75::decode_soft). The message ends at the EOM, or
//    without it at the last interleaver block the signal covers.
//  - success: the decoded bits, coded again, agree with the hard channel decisions on
//    all but a fraction p of the message's coded bits, quality_db = 10 log10((1-p)/p) at
//    least 11.25 dB, and on the worst interleaver block at least 6.5 dB (a fade longer than
//    the 0.6 s interleaver decodes wrong while the message as a whole still agrees well).

#ifndef M110A_M75_RX_H
#define M110A_M75_RX_H

#include "api/modem_rx.h"
#include "dsp/fir_filter.h"
#include "m110a/brain_preamble.h"
#include "m110a/m75_acq.h"
#include "m110a/m75_codec.h"
#include "m110a/m75_excise.h"
#include "m110a/mode_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace m110a {
namespace m75 {

/// Receiver constants. Chosen and checked on the AWGN and Watterson sweeps of
/// test/m75/m75_sweep.py (docs/m110a-75bps.md).
struct RxTuning {
    double window_ms = 12.0;        // delay window either side of the preamble timing (FFI: spreads to 12 ms)
    double profile_alpha = 0.03;    // per-set update of the tap energy profile (the mean
                                    // power-delay profile weighs square-law combining)
    double weight_floor = 2.0;      // tap weight = profile - weight_floor x noise floor, >= 0
    double noise_beta = 0.02;       // per-set update of the noise floor
    double soft_scale = 16.0;       // soft bit = scale * (sqrt M0 - sqrt M1) / sqrt(noise)
    double min_quality_db = 11.25;  // success: quality_db = 10 log10((1 - p) / p) at least this
    double min_block_quality_db = 6.5;  // ... and the same over the worst interleaver block
    double signal_ratio = 2.0;      // a set carries signal when (Mwin - Mlose) / noise >= this
    bool excise = true;             // narrowband interference excision (m75_excise.h) in front
    double alt_profile_alpha = 0.01;  // a second decode with this profile update when the first
                                      // is not clean; the better kept (0: none)
    double max_drift_ppm = 0.0;     // window re-centring at most this clock error (0: no limit)
    double alt_max_drift_ppm = 200.0;  // ... in the second decode: under slow fading the profile's
                                       // centroid swings between the paths, which is not drift
    double centre_alpha = 0.02;     // per-set update of the profile the window centres on
    double alt_centre_alpha = 0.002;  // ... in the second decode (about 13 s: the mean profile)
    double alt_window_ms = 16.0;    // the second decode's delay window, either side
    bool per_tap_noise = false;     // tap weights over the losing words' energy at the tap
    bool alt_per_tap_noise = true;  // ... in the second decode
};

/// The tuning ModemRX's 75 bps path uses. A test tool may change it (m75_ber --no-excise);
/// ModemRX itself never does.
inline RxTuning& default_tuning() {
    static RxTuning t;
    return t;
}

/// Success needs quality_db at or above this
inline float quality_threshold_db(const RxTuning& t) { return static_cast<float>(t.min_quality_db); }

/**
 * Per-set Walsh demodulator over matched-filter output y (complex baseband, one sample
 * per input sample, already shifted to the carrier found).
 */
class WalshRake {
public:
    WalshRake(const std::vector<std::complex<float>>& y, int sps, long data_start, ModeId mode,
              const RxTuning& t)
        : y_(y), sps_(sps), data_start_(data_start), per_block_(sets_per_block(mode)), t_(t) {
        step_ = sps_ % 4 == 0 ? sps_ / 4 : (sps_ % 2 == 0 ? sps_ / 2 : 1);
        half_ = static_cast<int>(std::ceil(t_.window_ms * 1e-3 * 2400.0 * sps_ / step_));
        ntaps_ = 2 * half_ + 1;
        prof_.assign(ntaps_, 0.0);
        slow_.assign(ntaps_, 0.0);
        const DataScramblerFixed scr;
        for (int i = 0; i < SCRAMBLER_PERIOD; i++) scr_[i] = std::conj(psk8(scr.at(i)));
    }

    /// Start the profile and noise floor from the 480 known symbols of the last preamble
    /// frame, which ends where the data starts.
    void init_from_preamble(const std::vector<std::complex<float>>& last_frame) {
        const long f0 = data_start_ - static_cast<long>(last_frame.size()) * sps_;
        const int chunks = static_cast<int>(last_frame.size()) / SET_SYMBOLS;
        double noise = 0.0;
        long nn = 0;
        for (int j = 0; j < ntaps_; j++) {
            double e = 0.0;
            int n = 0;
            for (int q = 0; q < chunks; q++) {
                std::complex<double> c(0, 0), w(0, 0);
                bool ok = true;
                for (int i = 0; i < SET_SYMBOLS && ok; i++) {
                    const long idx = f0 + tap_offset(j) + static_cast<long>(q * SET_SYMBOLS + i) * sps_;
                    if (idx < 0 || idx >= static_cast<long>(y_.size())) { ok = false; break; }
                    const std::complex<double> u = std::complex<double>(y_[idx]) *
                                                   std::conj(std::complex<double>(last_frame[q * SET_SYMBOLS + i]));
                    c += u;
                    w += (i & 1) ? -u : u;  // an orthogonal word: noise and leakage only
                }
                if (!ok) continue;
                e += std::norm(c);
                noise += std::norm(w);
                n++;
                nn++;
            }
            prof_[j] = n ? e / n : 0.0;
        }
        noise_ = nn ? noise / nn : 1e-12;
        lose_.assign(ntaps_, noise_);
        for (int j = 0; j < ntaps_; j++) slow_[j] = std::max(0.0, prof_[j] - noise_);
        centre0_ = centroid();
    }

    /// True when set k lies inside the buffer
    bool available(long k) const {
        const long last = base(k) + tap_offset(ntaps_ - 1) + static_cast<long>(SET_SYMBOLS - 1) * sps_;
        return base(k) + tap_offset(0) >= 0 && last < static_cast<long>(y_.size());
    }

    struct SetOut {
        double m[4];      // weighted metrics of the four words
        double noise_m;   // noise expectation of a metric (noise floor x sum of weights)
        int win;
        double ratio;     // (M_win - mean M_lose) / noise_m
        double lose_m;    // mean of the three losing metrics
        double kappa;     // sum w^2 / (sum w * max w): the weighted taps' share of a peak tap
    };

    /// Demodulate set k (the sets must come in order: the profile follows them)
    SetOut demod(long k) {
        const bool ex = exceptional_set(k, per_block_);
        std::vector<double> wgt(ntaps_);
        double wsum = 0.0;
        // the floor under a tap: the noise floor, or (per_tap_noise) the losing words' energy
        // at that tap, which also holds any other Walsh-word signal there (a co-channel 75 bps
        // signal whose scrambler lines up with ours at that delay)
        for (int j = 0; j < ntaps_; j++) {
            const double f = t_.per_tap_noise ? std::max(noise_, lose_[j]) : noise_;
            wgt[j] = std::max(0.0, prof_[j] - t_.weight_floor * f);
            wsum += wgt[j];
        }
        if (wsum <= 0.0) {  // no profile above the floor: weigh every tap alike
            for (int j = 0; j < ntaps_; j++) wgt[j] = 1.0;
            wsum = ntaps_;
        }
        // signs of the four words (tribit 0 -> +1, 4 -> -1)
        int sg[4][SET_SYMBOLS];
        for (int v = 0; v < 4; v++)
            for (int i = 0; i < SET_SYMBOLS; i++) sg[v][i] = tribit(v, ex, i) ? -1 : 1;
        std::vector<double> e(4 * ntaps_);
        const long b = base(k);
        const int s0 = static_cast<int>((k * SET_SYMBOLS) % SCRAMBLER_PERIOD);
        for (int j = 0; j < ntaps_; j++) {
            std::complex<float> u[SET_SYMBOLS];
            const long t0 = b + tap_offset(j);
            for (int i = 0; i < SET_SYMBOLS; i++)
                u[i] = y_[t0 + static_cast<long>(i) * sps_] * scr_[(s0 + i) % SCRAMBLER_PERIOD];
            for (int v = 0; v < 4; v++) {
                std::complex<float> c(0, 0);
                for (int i = 0; i < SET_SYMBOLS; i++) c += sg[v][i] > 0 ? u[i] : -u[i];
                e[v * ntaps_ + j] = std::norm(c);
            }
        }
        SetOut o{};
        for (int v = 0; v < 4; v++) {
            double m = 0.0;
            for (int j = 0; j < ntaps_; j++) m += wgt[j] * e[v * ntaps_ + j];
            o.m[v] = m;
        }
        o.win = static_cast<int>(std::max_element(o.m, o.m + 4) - o.m);
        o.noise_m = noise_ * wsum;
        double lose = 0.0;
        for (int v = 0; v < 4; v++) if (v != o.win) lose += o.m[v];
        lose /= 3.0;
        o.lose_m = lose;
        double w2 = 0.0, wmax = 0.0;
        for (int j = 0; j < ntaps_; j++) { w2 += wgt[j] * wgt[j]; wmax = std::max(wmax, wgt[j]); }
        o.kappa = wmax > 0.0 ? w2 / (wsum * wmax) : 0.0;
        o.ratio = o.noise_m > 0.0 ? (o.m[o.win] - lose) / o.noise_m : 0.0;

        // follow the channel: winner's tap energies into the profile, losers' into the floor
        double fl = 0.0;
        for (int j = 0; j < ntaps_; j++) {
            prof_[j] += t_.profile_alpha * (e[o.win * ntaps_ + j] - prof_[j]);
            for (int v = 0; v < 4; v++) if (v != o.win) fl += e[v * ntaps_ + j];
        }
        noise_ += t_.noise_beta * (fl / (3.0 * ntaps_) - noise_);
        if (t_.per_tap_noise)
            for (int j = 0; j < ntaps_; j++) {
                double l = 0.0;
                for (int v = 0; v < 4; v++) if (v != o.win) l += e[v * ntaps_ + j];
                lose_[j] += t_.noise_beta * (l / 3.0 - lose_[j]);
            }
        for (int j = 0; j < ntaps_; j++) slow_[j] += t_.centre_alpha * (std::max(0.0, prof_[j] - noise_) - slow_[j]);
        recentre();
        return o;
    }

    double noise() const { return noise_; }
    long shift() const { return shift_; }

private:
    const std::vector<std::complex<float>>& y_;
    int sps_;
    long data_start_;
    int per_block_;
    RxTuning t_;
    int step_ = 1, half_ = 0, ntaps_ = 1;
    std::vector<double> prof_, slow_;
    double noise_ = 1e-12;
    std::vector<double> lose_;  // per tap: the losing words' mean energy
    double centre0_ = 0.0;
    long shift_ = 0;
    long sets_ = 0, last_move_ = 0;
    std::complex<float> scr_[SCRAMBLER_PERIOD];

    long tap_offset(int j) const { return static_cast<long>(j - half_) * step_; }
    long base(long k) const { return data_start_ + k * SET_SYMBOLS * sps_ + shift_; }

    double centroid() const {
        double s = 0.0, w = 0.0;
        for (int j = 0; j < ntaps_; j++) { s += j * slow_[j]; w += slow_[j]; }
        return w > 0.0 ? s / w : half_;
    }

    // Move the window one tap when the slow profile's centroid has moved a tap or more
    // (clock drift); the profiles move with it, so the channel stays where it was.
    void recentre() {
        sets_++;
        const double c = centroid();
        int d = 0;
        if (c - centre0_ >= 1.0) d = 1;
        else if (c - centre0_ <= -1.0) d = -1;
        if (d == 0) return;
        // at most max_drift_ppm: one tap (step_ samples) per step_ / (ppm 1e-6 x 32 sps) sets
        if (t_.max_drift_ppm > 0.0 &&
            static_cast<double>(sets_ - last_move_) * t_.max_drift_ppm * 1e-6 * SET_SYMBOLS * sps_ < step_)
            return;
        last_move_ = sets_;
        shift_ += d * step_;
        auto mv = [&](std::vector<double>& p) {
            if (d > 0) { std::rotate(p.begin(), p.begin() + 1, p.end()); p.back() = 0.0; }
            else { std::rotate(p.rbegin(), p.rbegin() + 1, p.rend()); p.front() = 0.0; }
        };
        mv(prof_);
        mv(slow_);
        mv(lose_);
        prof_[d > 0 ? ntaps_ - 1 : 0] = noise_;
        lose_[d > 0 ? ntaps_ - 1 : 0] = noise_;
    }
};

/// Matched-filter output of rf at carrier `fc` (the offset found included), the same
/// filter and alignment as BrainDecoder (RRC alpha 0.35, 6 symbols each side, centred).
/// Computed only from sample lo on (the last preamble frame and the data).
inline std::vector<std::complex<float>> matched_filter(const std::vector<float>& rf, float fs, double fc,
                                                       int sps, long lo) {
    const auto taps = generate_srrc_taps(0.35f, 6, static_cast<float>(sps));
    const long half = static_cast<long>(taps.size() / 2);
    const long n = static_cast<long>(rf.size());
    std::vector<std::complex<float>> bb(rf.size());
    const double w = 2.0 * M_PI * fc / fs;
    for (long i = 0; i < n; i++) {
        const double ph = std::fmod(w * static_cast<double>(i), 2.0 * M_PI);
        bb[i] = std::complex<float>(static_cast<float>(rf[i] * std::cos(ph)), static_cast<float>(-rf[i] * std::sin(ph)));
    }
    std::vector<std::complex<float>> y(rf.size(), std::complex<float>(0, 0));
    for (long i = std::max(0L, lo); i < n; i++) {
        std::complex<float> acc(0, 0);
        const long j0 = std::max(0L, half - i), j1 = std::min<long>(static_cast<long>(taps.size()), n - i + half);
        for (long j = j0; j < j1; j++) acc += bb[i - half + j] * taps[j];
        y[i] = acc;
    }
    return y;
}

/// Internal result of a 75 bps receive
struct RxOut {
    bool preamble = false;
    Acquisition acq;
    std::vector<uint8_t> data;
    long eom_bit = -1;
    long sets = 0, signal_sets = 0, blocks = 0;
    double p = 0.5;           // channel bit disagreement over the message
    double p_block = 0.5;     // the same over the worst interleaver block of it
    double snr_db = 0.0;      // estimated, 3 kHz noise bandwidth
    long excised_frames = 0;  // excision frames hit (0: the input went in untouched)
    std::string error;
    std::vector<soft_bit_t> code;  // the soft bits the message was scored over, in code order
    std::vector<int> re;           // the decoded bits coded again, the same span
};

/// The last RxOut decode() produced on this thread (for test tools: m75_ber --trace)
inline RxOut& last_rx() {
    thread_local RxOut r;
    return r;
}

/// Max-log soft bits of one set (+ = 0, the Viterbi convention), Table IX inverted
inline void soft_bits(const WalshRake::SetOut& o, double scale, soft_bit_t& first, soft_bit_t& last) {
    double b0[2] = {-1, -1}, b1[2] = {-1, -1};  // best sqrt metric with bit = 0 / = 1
    for (int v = 0; v < 4; v++) {
        int f, l;
        mgd_inverse(v, f, l);
        const double s = std::sqrt(std::max(0.0, o.m[v]));
        double* bf = f ? &b1[0] : &b0[0];
        double* bl = l ? &b1[1] : &b0[1];
        *bf = std::max(*bf, s);
        *bl = std::max(*bl, s);
    }
    const double den = std::sqrt(std::max(1e-30, o.noise_m));
    auto clip = [](double x) { return static_cast<soft_bit_t>(std::max(-127.0, std::min(127.0, std::round(x)))); };
    first = clip(scale * (b0[0] - b1[0]) / den);
    last = clip(scale * (b0[1] - b1[1]) / den);
}

/// Acquisition of the last buffer acquire_cached() was asked about: ModemRX's AUTO pass
/// asks whether a 75 bps preamble is there (find_low_snr) and then decodes the same audio.
/// Keyed on the audio itself (length, settings and 9 samples spread over it).
inline Acquisition acquire_cached(const std::vector<float>& rf, float fs, float carrier, float frange) {
    using Key = std::array<double, 13>;
    thread_local Key key{};
    thread_local Acquisition last;
    Key k{};
    k[0] = static_cast<double>(rf.size()); k[1] = fs; k[2] = carrier; k[3] = frange;
    for (int i = 0; i < 9; i++) k[4 + i] = rf.empty() ? 0.0 : rf[(rf.size() - 1) * i / 8];
    if (k == key) return last;
    last = acquire(rf, fs, carrier, frange);
    key = k;
    return last;
}

/// rf with narrowband interference excised (m75_excise.h), or rf itself when the excision
/// found none: then the receiver sees its input sample for sample. Cached like
/// acquire_cached, since the AUTO pass and the decode look at the same audio. `hit` (if
/// given) receives the excision's statistics.
inline const std::vector<float>& excised(const std::vector<float>& rf, float fs, float carrier,
                                         ExciseStats* hit = nullptr) {
    using Key = std::array<double, 12>;
    thread_local Key key{};
    thread_local bool cut = false;
    thread_local std::vector<float> out;
    thread_local ExciseStats st;
    Key k{};
    k[0] = static_cast<double>(rf.size()); k[1] = fs; k[2] = carrier;
    for (int i = 0; i < 9; i++) k[3 + i] = rf.empty() ? 0.0 : rf[(rf.size() - 1) * i / 8];
    if (!(k == key)) {
        st = ExciseStats();
        cut = excise(rf, fs, carrier, out, &st);
        if (!cut) { out.clear(); out.shrink_to_fit(); }
        key = k;
    }
    if (hit) *hit = st;
    return cut ? out : rf;
}

/// The audio ModemRX hands the 75 bps path: `cut` (its buffer from its first preamble cut
/// on) with up to 5 s of `buffer` before it put back. At low SNR first_preamble_cut fires
/// on whichever preamble frame first crosses its threshold, which can be inside a 75 bps
/// preamble (24 frames, 4.8 s for 75L); acquisition wants all of it.
inline std::vector<float> lead_in(const std::vector<float>& buffer, const std::vector<float>& cut, float fs) {
    const size_t at = buffer.size() >= cut.size() ? buffer.size() - cut.size() : 0;
    const size_t back = std::min(at, static_cast<size_t>(5.0 * fs));
    return std::vector<float>(buffer.begin() + static_cast<long>(at - back), buffer.end());
}

/// D1/D2 read clearly: the best defined code's energy at least `ratio` times the
/// runner-up's. 1.25 for refusing a forced mode; 1.1 for taking the mode in AUTO, where a
/// wrong reading cannot pass the decode gate (a 150S read as 75S decodes to garbage).
inline bool clear_d(const Acquisition& a, double ratio = 1.25) { return a.pair_ratio >= ratio; }

/// Demodulate and decode the data after the preamble from matched-filter output y: fills
/// r's sets, signal_sets, blocks, eom_bit, data, p, p_block and snr_db; returns the
/// window shift the RAKE ended at.
inline long demod_decode(const std::vector<std::complex<float>>& y, int sps, long data_start, ModeId mode,
                         const std::vector<std::complex<float>>& last, const RxTuning& t, RxOut& r) {
    WalshRake rake(y, sps, data_start, mode, t);
    rake.init_from_preamble(last);

    std::vector<soft_bit_t> soft;
    std::vector<double> ratio, win_ex, lose, kappa;
    for (long k = 0; rake.available(k); k++) {
        const auto o = rake.demod(k);
        soft_bit_t a, b;
        soft_bits(o, t.soft_scale, a, b);
        soft.push_back(a);
        soft.push_back(b);
        ratio.push_back(o.ratio);
        win_ex.push_back(o.m[o.win] - o.lose_m);
        lose.push_back(o.lose_m);
        kappa.push_back(o.kappa);
    }
    r.sets = static_cast<long>(ratio.size());

    // Last set with signal: searched from the end, a set whose 9-set mean ratio clears the
    // threshold (a fade inside the message does not end it)
    const int S = sets_per_block(mode);
    long last_sig = -1;
    for (long k = r.sets; k-- > 0;) {
        double s = 0.0;
        int n = 0;
        for (long q = std::max(0L, k - 8); q <= k; q++) { s += ratio[q]; n++; }
        if (ratio[k] >= t.signal_ratio && s / n >= t.signal_ratio) { last_sig = k; break; }
    }
    r.signal_sets = last_sig + 1;

    // Decode every block the signal reaches into, and look for the EOM
    const long avail_blocks = r.sets / S;
    long blocks = std::min(avail_blocks, (r.signal_sets + S - 1) / S);
    std::vector<soft_bit_t> code;
    std::vector<uint8_t> info = decode_soft(std::vector<soft_bit_t>(soft.begin(), soft.begin() + blocks * S * 2),
                                            mode, &code);
    r.eom_bit = find_eom(info);
    if (r.eom_bit < 0) r.eom_bit = find_eom_tolerant(info);
    size_t info_end;  // information bits the message is scored over
    if (r.eom_bit >= 0) {
        r.data = bits_to_bytes(info, static_cast<size_t>(r.eom_bit));
        info_end = std::min(info.size(), static_cast<size_t>(r.eom_bit) + 32 + FLUSH_BITS);
    } else {
        // Without the EOM: the whole blocks the signal covers, to within S/9 sets of the end
        // (a partial block cannot be trusted: its missing part is spread over all of it)
        blocks = std::min(avail_blocks, (r.signal_sets + S / 9) / S);
        info = decode_soft(std::vector<soft_bit_t>(soft.begin(), soft.begin() + blocks * S * 2), mode, &code);
        info_end = info.size() > 6 ? info.size() - 6 : 0;
        r.data = bits_to_bytes(info, info_end);
    }
    r.blocks = blocks;

    // Agreement of the decoded bits, coded again, with the hard channel bits
    const auto re = reencode(std::vector<uint8_t>(info.begin(), info.begin() + info_end));
    double dis = 0.0;
    const size_t n = std::min(re.size(), code.size());
    for (size_t i = 0; i < n; i++) {
        const int hard = code[i] < 0 ? 1 : 0;
        dis += code[i] == 0 ? 0.5 : (hard != re[i] ? 1.0 : 0.0);
    }
    r.p = n ? dis / n : 0.5;
    r.code.assign(code.begin(), code.begin() + static_cast<long>(n));
    r.re.assign(re.begin(), re.begin() + static_cast<long>(n));
    // ... and block by block (code order is block by block after deinterleaving): a fade
    // longer than the interleaver leaves one block's errors to the code, which may decode
    // them wrong while the whole message still agrees well
    const size_t B = static_cast<size_t>(2 * S);
    r.p_block = 0.0;
    for (size_t b0 = 0; b0 < n; b0 += B) {
        const size_t b1 = std::min(n, b0 + B);
        if (b1 - b0 < B / 4) continue;
        double d = 0.0;
        for (size_t i = b0; i < b1; i++) {
            const int hard = code[i] < 0 ? 1 : 0;
            d += code[i] == 0 ? 0.5 : (hard != re[i] ? 1.0 : 0.0);
        }
        r.p_block = std::max(r.p_block, d / (b1 - b0));
    }
    if (n == 0) r.p_block = 0.5;

    // SNR: the weights sit on the channel's taps, where the losing words see noise (and
    // multipath leakage) only: with w_j proportional to a tap's signal energy, M_win - M_lose
    // = 32^2 S sum w_j g_j and M_lose = 32 sigma^2 sum w_j (g_j the tap's share of the peak
    // energy), so S / sigma^2 = (M_win - M_lose) / M_lose / (32 kappa), kappa = sum w g / sum w.
    // S / sigma^2 is Es/N0 at the matched filter; the standard counts noise in 3 kHz.
    if (r.signal_sets > 0) {
        double a = 0.0, b = 0.0, kp = 0.0;
        for (long k = 0; k < r.signal_sets; k++) { a += win_ex[k]; b += lose[k]; kp += kappa[k]; }
        kp /= r.signal_sets;
        const double es_n0 = b > 0.0 && kp > 0.0 ? a / b / (SET_SYMBOLS * kp) : 1e4;
        r.snr_db = es_n0 > 0 ? std::min(40.0, 10.0 * std::log10(es_n0 * 2400.0 / 3000.0)) : -99.0;
    }
    return rake.shift();
}

/// Demodulate and decode the data of a preamble r.acq found in `in`: fills r as
/// demod_decode does, from the decode kept; alt: the second decode was kept. Returns the
/// window shift.
///
/// Two profile time constants: the RAKE's weights follow the channel's power-delay
/// profile, at t.profile_alpha per set and, when that decode is not clean, again at
/// t.alt_profile_alpha (slower: under slow fading the fast profile chases each fade and
/// decision errors in a fade pull the weights off the paths), with the window's
/// re-centring limited to t.alt_max_drift_ppm and centred on a profile averaged over about
/// 13 s, in a window of +/- t.alt_window_ms (the profile's centroid swings between the
/// paths as they fade, which the first decode follows as drift: a path 10 ms off can
/// leave its +/- 12 ms window), and each tap weighed over its own losing words' energy
/// (a co-channel 75 bps signal lined up with our scrambler at some delay fills the losing
/// words there, where a path of ours leaves them at the noise). The decode whose channel
/// bits agree better with it (lower p) is kept; a tie keeps the first.
inline long receive_data(const std::vector<float>& in, ModeId mode, float fs, float carrier, const RxTuning& t,
                         RxOut& r, bool& alt) {
    const int sps = static_cast<int>(std::lround(fs / 2400.0f));
    const long data_start = r.acq.data_start;
    const long lo = data_start - static_cast<long>(brain::P_FRAME_LENGTH + 64) * sps;
    const auto y = matched_filter(in, fs, carrier + r.acq.freq_offset_hz, sps, lo);

    // the last preamble frame as sent (countdown 0)
    auto pre_syms = BrainPreambleEncoder().encode(mode == ModeId::M75NS ? 0 : 1, mode == ModeId::M75NL);
    std::vector<std::complex<float>> last(pre_syms.end() - brain::P_FRAME_LENGTH, pre_syms.end());

    long shift = demod_decode(y, sps, data_start, mode, last, t, r);
    alt = false;
    if (t.alt_profile_alpha > 0.0 && r.p > 0.0) {
        RxTuning t2 = t;
        t2.profile_alpha = t.alt_profile_alpha;
        t2.max_drift_ppm = t.alt_max_drift_ppm;
        t2.centre_alpha = t.alt_centre_alpha;
        t2.window_ms = t.alt_window_ms;
        t2.per_tap_noise = t.alt_per_tap_noise;
        RxOut r2 = r;
        const long shift2 = demod_decode(y, sps, data_start, mode, last, t2, r2);
        if (r2.p < r.p) { r = r2; shift = shift2; alt = true; }
    }
    return shift;
}

/**
 * Receive one 75 bps message from rf (a real passband buffer holding its preamble within
 * its first 15 s). mode: the mode to decode as.
 */
inline RxOut receive(const std::vector<float>& rf, ModeId mode, float fs, float carrier, float frange,
                     const RxTuning& t = RxTuning()) {
    RxOut r;
    ExciseStats ex;
    const std::vector<float>& in = t.excise ? excised(rf, fs, carrier, &ex) : rf;
    r.excised_frames = ex.hit_frames;
    r.acq = acquire_cached(in, fs, carrier, frange);
    long shift = 0;
    bool alt = false, cm = false;
    if (r.acq.found) {
        r.preamble = true;
        shift = receive_data(in, mode, fs, carrier, t, r, alt);
    }
    // A strong constant-envelope interferer (FSK, CW, a swept CW) that the bin weighting
    // found but could not clear: decode again after constant-modulus cancellation of the
    // input (m75_excise.h cm_cancel), and keep the decode that agrees better (lower p).
    std::vector<float> cmo;
    if (t.excise && ex.hit_frames > 0 && (!r.preamble || r.p > 0.0) && cm_cancel(rf, fs, carrier, cmo)) {
        RxOut r2;
        r2.excised_frames = r.excised_frames;
        r2.acq = acquire(cmo, fs, carrier, frange);
        if (r2.acq.found) {
            r2.preamble = true;
            bool alt2 = false;
            const long shift2 = receive_data(cmo, mode, fs, carrier, t, r2, alt2);
            if (!r.preamble || r2.p < r.p) { r = r2; shift = shift2; alt = alt2; cm = true; }
        }
    }
    if (!r.preamble) {
        char buf[120];
        snprintf(buf, sizeof buf, "75 bps: no preamble found (significance %.1f)", r.acq.z);
        r.error = buf;
        return r;
    }
    const long data_start = r.acq.data_start;
    char buf[320];
    snprintf(buf, sizeof buf,
             "[RX M75] acq z=%.1f frames=%d foff=%+.1f Hz D1,D2=%d,%d (pair ratio %.2f) data_start=%ld "
             "sets=%ld signal_sets=%ld blocks=%ld eom_bit=%ld p=%.4f p_block=%.4f snr=%.1f dB shift=%ld",
             r.acq.z, r.acq.frames, r.acq.freq_offset_hz, r.acq.d1, r.acq.d2, r.acq.pair_ratio, data_start, r.sets, r.signal_sets, r.blocks, r.eom_bit, r.p, r.p_block, r.snr_db,
             shift);
    std::cout << buf;
    if (alt) std::cout << " profile_alpha=" << t.alt_profile_alpha;
    if (cm) std::cout << " cm_cancel=1";
    if (r.excised_frames > 0) std::cout << " excised_frames=" << r.excised_frames << " cut=" << ex.cut_share;
    std::cout << std::endl;
    return r;
}

/// In a forced mode, the Table XI data mode a clearly read D1/D2 names instead, or ""
/// (as ModemRXImpl::preamble_contradicts does for the other modes)
inline std::string preamble_contradicts(const Acquisition& a, ModeId forced) {
    static const struct { int d1, d2; const char* name; } KNOWN[] = {
        {7, 5, "75S"}, {5, 5, "75L"}, {7, 4, "150S"}, {5, 4, "150L"}, {6, 7, "300S"}, {4, 7, "300L"},
        {6, 6, "600S"}, {4, 6, "600L"}, {6, 5, "1200S"}, {4, 5, "1200L"}, {6, 4, "2400S"}, {4, 4, "2400L"},
        {7, 6, "4800S"}};
    if (!clear_d(a)) return "";
    for (const auto& k : KNOWN)
        if (k.d1 == a.d1 && k.d2 == a.d2) {
            const bool same = (forced == ModeId::M75NS && k.d1 == 7 && k.d2 == 5) ||
                              (forced == ModeId::M75NL && k.d1 == 5 && k.d2 == 5);
            return same ? "" : k.name;
        }
    return "";
}

/// ModemRX's AUTO pass, when the shared acquisition found no preamble: is there a 75 bps
/// one, found and with D1/D2 read clearly as 7,5 or 5,5? Its mode in `mode`.
inline bool find_low_snr(const std::vector<float>& rf, const api::RxConfig& cfg, ModeId& mode) {
    const std::vector<float>& in = default_tuning().excise ? excised(rf, cfg.sample_rate, cfg.carrier_freq) : rf;
    const Acquisition a = acquire_cached(in, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range);
    if (!a.found || !a.is_75 || !clear_d(a, 1.1)) return false;
    mode = a.mode;
    return true;
}

/**
 * ModemRX's 75 bps path: fill `res` (which carries what the AUTO pass read) from a
 * receive of `rf` in `mode`. forced: the mode was given, not read from the preamble.
 */
inline api::DecodeResult decode(const std::vector<float>& rf, const api::RxConfig& cfg, ModeId mode,
                                api::DecodeResult res, bool forced, const RxTuning& t = default_tuning()) {
    // AUTO: the mode D1/D2 name, read over the whole preamble, when they name a 75 bps one
    if (!forced) {
        const std::vector<float>& in = t.excise ? excised(rf, cfg.sample_rate, cfg.carrier_freq) : rf;
        const Acquisition a = acquire_cached(in, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range);
        if (a.found && a.is_75 && clear_d(a, 1.1)) mode = a.mode;
    }
    const RxOut& r = last_rx() = receive(rf, mode, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range, t);
    res.mode = mode == ModeId::M75NS ? api::Mode::M75_SHORT : api::Mode::M75_LONG;
    if (r.preamble) {  // as the 75 bps acquisition read them: the frame locked onto is the last
        res.d1 = r.acq.d1;
        res.d2 = r.acq.d2;
        res.countdown = 0;
    }
    if (!r.preamble) {
        res.success = false;
        res.error = api::Error(api::ErrorCode::RX_NO_SIGNAL, r.error);
        return res;
    }
    res.data = r.data;
    res.eom_sequence = r.eom_bit >= 0;
    res.eom_detected = res.eom_sequence;
    res.freq_offset_hz = r.acq.freq_offset_hz;
    res.snr_db = static_cast<float>(r.snr_db);
    res.ber_estimate = static_cast<float>(r.p);
    res.quality_db = static_cast<float>(10.0 * std::log10((1.0 - r.p) / std::max(r.p, 1e-4)));
    const double q_block = 10.0 * std::log10((1.0 - r.p_block) / std::max(r.p_block, 1e-4));
    res.quality_threshold_db = quality_threshold_db(t);
    res.success = res.quality_db >= res.quality_threshold_db && q_block >= t.min_block_quality_db &&
                  (r.eom_bit >= 0 || r.blocks > 0);
    if (!res.success) {
        char buf[200];
        if (r.eom_bit < 0 && r.blocks == 0)
            snprintf(buf, sizeof buf, "Signal covers 0 whole 75 bps interleaver blocks; no bytes reported");
        else if (res.quality_db >= res.quality_threshold_db)
            snprintf(buf, sizeof buf,
                     "Worst interleaver block quality %.1f dB below %.1f dB (channel bits %.1f%% off the decoded "
                     "path there); bytes not trusted",
                     q_block, t.min_block_quality_db, 100.0 * r.p_block);
        else
            snprintf(buf, sizeof buf,
                     "Decode quality %.1f dB below %.1f dB (channel bits %.1f%% off the decoded path); bytes not trusted",
                     res.quality_db, res.quality_threshold_db, 100.0 * r.p);
        res.error = api::Error(api::ErrorCode::RX_DECODE_FAILED, buf);
    }
    const std::string other = forced ? preamble_contradicts(r.acq, mode) : "";
    if (res.success && !other.empty()) {
        res.success = false;
        res.error = api::Error(api::ErrorCode::RX_DECODE_FAILED,
                               "Preamble D1/D2 read " + other + ", not the forced mode; bytes not trusted");
    }
    return res;
}

}  // namespace m75
}  // namespace m110a

#endif  // M110A_M75_RX_H
