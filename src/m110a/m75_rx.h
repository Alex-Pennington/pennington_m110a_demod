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
//    all but a fraction p of the message's coded bits. quality_db = 10 log10((1-p)/p).

#ifndef M110A_M75_RX_H
#define M110A_M75_RX_H

#include "api/modem_rx.h"
#include "dsp/fir_filter.h"
#include "m110a/brain_preamble.h"
#include "m110a/m75_acq.h"
#include "m110a/m75_codec.h"
#include "m110a/mode_config.h"

#include <algorithm>
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
    double window_ms = 6.0;         // delay window either side of the preamble timing
    double profile_alpha = 0.03;    // per-set update of the tap energy profile (the mean
                                    // power-delay profile weighs square-law combining)
    double weight_floor = 2.0;      // tap weight = profile - weight_floor x noise floor, >= 0
    double noise_beta = 0.02;       // per-set update of the noise floor
    double soft_scale = 16.0;       // soft bit = scale * (sqrt M0 - sqrt M1) / sqrt(noise)
    double min_quality_db = 11.25;  // success: quality_db = 10 log10((1 - p) / p) at least this
    double signal_ratio = 2.0;      // a set carries signal when (Mwin - Mlose) / noise >= this
};

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
        double excess;    // winner's tap energy over the losers' mean, summed over the window
    };

    /// Demodulate set k (the sets must come in order: the profile follows them)
    SetOut demod(long k) {
        const bool ex = exceptional_set(k, per_block_);
        std::vector<double> wgt(ntaps_);
        double wsum = 0.0;
        for (int j = 0; j < ntaps_; j++) { wgt[j] = std::max(0.0, prof_[j] - t_.weight_floor * noise_); wsum += wgt[j]; }
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
        o.ratio = o.noise_m > 0.0 ? (o.m[o.win] - lose) / o.noise_m : 0.0;

        // follow the channel: winner's tap energies into the profile, losers' into the floor
        double fl = 0.0, ew = 0.0;
        for (int j = 0; j < ntaps_; j++) {
            prof_[j] += t_.profile_alpha * (e[o.win * ntaps_ + j] - prof_[j]);
            ew += e[o.win * ntaps_ + j];
            for (int v = 0; v < 4; v++) if (v != o.win) fl += e[v * ntaps_ + j];
        }
        o.excess = ew - fl / 3.0;
        noise_ += t_.noise_beta * (fl / (3.0 * ntaps_) - noise_);
        for (int j = 0; j < ntaps_; j++) slow_[j] += 0.02 * (std::max(0.0, prof_[j] - noise_) - slow_[j]);
        recentre();
        return o;
    }

    double noise() const { return noise_; }
    /// Sum over the window's taps of p(tap)^2, p the raised-cosine pulse at unit peak: a
    /// symbol's energy as the taps see it, about (sps / step)(1 - alpha / 4)
    double taps_per_symbol() const { return static_cast<double>(sps_) / step_ * (1.0 - 0.35 / 4.0); }
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
    double centre0_ = 0.0;
    long shift_ = 0;
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
        const double c = centroid();
        int d = 0;
        if (c - centre0_ >= 1.0) d = 1;
        else if (c - centre0_ <= -1.0) d = -1;
        if (d == 0) return;
        shift_ += d * step_;
        auto mv = [&](std::vector<double>& p) {
            if (d > 0) { std::rotate(p.begin(), p.begin() + 1, p.end()); p.back() = 0.0; }
            else { std::rotate(p.rbegin(), p.rbegin() + 1, p.rend()); p.front() = 0.0; }
        };
        mv(prof_);
        mv(slow_);
        prof_[d > 0 ? ntaps_ - 1 : 0] = noise_;
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
    double snr_db = 0.0;      // estimated, 3 kHz noise bandwidth
    std::string error;
};

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
/// asks whether a 75 bps preamble is there (find_low_snr) and then decodes the same buffer.
inline Acquisition acquire_cached(const std::vector<float>& rf, float fs, float carrier, float frange) {
    struct Key { const float* p = nullptr; size_t n = 0; float fs = 0, fc = 0, fr = 0, a = 0, b = 0; };
    thread_local Key key;
    thread_local Acquisition last;
    Key k;
    k.p = rf.data(); k.n = rf.size(); k.fs = fs; k.fc = carrier; k.fr = frange;
    k.a = rf.empty() ? 0.0f : rf[rf.size() / 3];
    k.b = rf.empty() ? 0.0f : rf[rf.size() - 1];
    if (k.p == key.p && k.n == key.n && k.fs == key.fs && k.fc == key.fc && k.fr == key.fr && k.a == key.a &&
        k.b == key.b)
        return last;
    last = acquire(rf, fs, carrier, frange);
    key = k;
    return last;
}

/// D1/D2 read clearly: the best defined code's energy at least `ratio` times the
/// runner-up's. 1.25 for refusing a forced mode; 1.1 for taking the mode in AUTO, where a
/// wrong reading cannot pass the decode gate (a 150S read as 75S decodes to garbage).
inline bool clear_d(const Acquisition& a, double ratio = 1.25) { return a.pair_ratio >= ratio; }

/**
 * Receive one 75 bps message from rf (a real passband buffer holding its preamble within
 * its first 15 s). mode: the mode to decode as.
 */
inline RxOut receive(const std::vector<float>& rf, ModeId mode, float fs, float carrier, float frange,
                     const RxTuning& t = RxTuning()) {
    RxOut r;
    r.acq = acquire_cached(rf, fs, carrier, frange);
    if (!r.acq.found) {
        char buf[120];
        snprintf(buf, sizeof buf, "75 bps: no preamble found (significance %.1f)", r.acq.z);
        r.error = buf;
        return r;
    }
    r.preamble = true;
    const int sps = static_cast<int>(std::lround(fs / 2400.0f));
    const long data_start = r.acq.data_start;
    const long lo = data_start - static_cast<long>(brain::P_FRAME_LENGTH + 64) * sps;
    const auto y = matched_filter(rf, fs, carrier + r.acq.freq_offset_hz, sps, lo);

    // the last preamble frame as sent (countdown 0)
    auto pre_syms = BrainPreambleEncoder().encode(mode == ModeId::M75NS ? 0 : 1, mode == ModeId::M75NL);
    std::vector<std::complex<float>> last(pre_syms.end() - brain::P_FRAME_LENGTH, pre_syms.end());

    WalshRake rake(y, sps, data_start, mode, t);
    rake.init_from_preamble(last);

    std::vector<soft_bit_t> soft;
    std::vector<double> ratio, excess;
    for (long k = 0; rake.available(k); k++) {
        const auto o = rake.demod(k);
        soft_bit_t a, b;
        soft_bits(o, t.soft_scale, a, b);
        soft.push_back(a);
        soft.push_back(b);
        ratio.push_back(o.ratio);
        excess.push_back(o.excess);
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

    // SNR: a set's winning word exceeds the losers' mean by 32^2 S sum_j p(tap_j)^2 over the
    // window, S the symbol energy at the matched filter's peak; the noise floor per tap is
    // 32 sigma^2. S / sigma^2 is Es/N0 (matched filter); the standard counts noise in 3 kHz.
    if (r.signal_sets > 0 && rake.noise() > 0.0) {
        double ex = 0.0;
        for (long k = 0; k < r.signal_sets; k++) ex += excess[k];
        ex /= r.signal_sets;
        const double es_n0 = ex / (static_cast<double>(SET_SYMBOLS) * rake.taps_per_symbol() * rake.noise());
        r.snr_db = es_n0 > 0 ? 10.0 * std::log10(es_n0 * 2400.0 / 3000.0) : -99.0;
    }
    char buf[320];
    snprintf(buf, sizeof buf,
             "[RX M75] acq z=%.1f frames=%d foff=%+.1f Hz D1,D2=%d,%d (pair ratio %.2f) data_start=%ld "
             "sets=%ld signal_sets=%ld blocks=%ld eom_bit=%ld p=%.4f snr=%.1f dB shift=%ld",
             r.acq.z, r.acq.frames, r.acq.freq_offset_hz, r.acq.d1, r.acq.d2, r.acq.pair_ratio, data_start, r.sets, r.signal_sets, r.blocks, r.eom_bit, r.p, r.snr_db,
             rake.shift());
    std::cout << buf << std::endl;
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
    const Acquisition a = acquire_cached(rf, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range);
    if (!a.found || !a.is_75 || !clear_d(a, 1.1)) return false;
    mode = a.mode;
    return true;
}

/**
 * ModemRX's 75 bps path: fill `res` (which carries what the AUTO pass read) from a
 * receive of `rf` in `mode`. forced: the mode was given, not read from the preamble.
 */
inline api::DecodeResult decode(const std::vector<float>& rf, const api::RxConfig& cfg, ModeId mode,
                                api::DecodeResult res, bool forced, const RxTuning& t = RxTuning()) {
    // AUTO: the mode D1/D2 name, read over the whole preamble, when they name a 75 bps one
    if (!forced) {
        const Acquisition a = acquire_cached(rf, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range);
        if (a.found && a.is_75 && clear_d(a, 1.1)) mode = a.mode;
    }
    const RxOut r = receive(rf, mode, cfg.sample_rate, cfg.carrier_freq, cfg.coarse_search_range, t);
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
    res.quality_threshold_db = quality_threshold_db(t);
    res.success = res.quality_db >= res.quality_threshold_db && (r.eom_bit >= 0 || r.blocks > 0);
    if (!res.success) {
        char buf[200];
        if (r.eom_bit < 0 && r.blocks == 0)
            snprintf(buf, sizeof buf, "Signal covers 0 whole 75 bps interleaver blocks; no bytes reported");
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
