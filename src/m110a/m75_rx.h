// m75_rx.h - MIL-STD-188-110A serial tone 75 bps (75S, 75L) receiver.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// The waveform is MIL-STD-188-110D 5.3.2 at 75 bps (src/m110a/m75_codec.h). This file is
// the demodulator; the standard leaves its design open. Design (docs/m110a-75bps.md):
//
//  - BrainDecoder finds the preamble, reads D1/D2 and the countdown, and measures the
//    carrier offset and phase line, as for every other mode. The data starts after the
//    last preamble frame.
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
#include "m110a/brain_decoder.h"
#include "m110a/brain_preamble.h"
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
    double window_ms = 6.0;       // delay window either side of the preamble timing
    double profile_alpha = 0.25;  // per-set update of the tap energy profile
    double noise_beta = 0.02;     // per-set update of the noise floor
    double soft_scale = 16.0;     // soft bit = scale * (sqrt M0 - sqrt M1) / sqrt(noise)
    double max_p = 0.20;          // success: largest channel-bit disagreement p
    double signal_ratio = 2.0;    // a set carries signal when (Mwin - Mlose) / noise >= this
};

/// Channel bit disagreement p for a quality_db: success needs quality_db at or above this
inline float quality_threshold_db(const RxTuning& t) {
    return static_cast<float>(10.0 * std::log10((1.0 - t.max_p) / t.max_p));
}

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
    };

    /// Demodulate set k (the sets must come in order: the profile follows them)
    SetOut demod(long k) {
        const bool ex = exceptional_set(k, per_block_);
        std::vector<double> wgt(ntaps_);
        double wsum = 0.0;
        for (int j = 0; j < ntaps_; j++) { wgt[j] = std::max(0.0, prof_[j] - noise_); wsum += wgt[j]; }
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
        double fl = 0.0;
        for (int j = 0; j < ntaps_; j++) {
            prof_[j] += t_.profile_alpha * (e[o.win * ntaps_ + j] - prof_[j]);
            for (int v = 0; v < 4; v++) if (v != o.win) fl += e[v * ntaps_ + j];
        }
        noise_ += t_.noise_beta * (fl / (3.0 * ntaps_) - noise_);
        for (int j = 0; j < ntaps_; j++) slow_[j] += 0.02 * (std::max(0.0, prof_[j] - noise_) - slow_[j]);
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

/// Matched-filter output of rf at carrier + foff, the same filter and alignment as
/// BrainDecoder (RRC alpha 0.35, 6 symbols each side, centred), derotated by the
/// preamble's phase line. Computed only where the data and the last preamble frame
/// lie, from sample lo on.
inline std::vector<std::complex<float>> matched_filter(const std::vector<float>& rf, float fs, float carrier,
                                                       const BrainDecodeResult& pre, int sps, long lo) {
    const auto taps = generate_srrc_taps(0.35f, 6, static_cast<float>(sps));
    const long half = static_cast<long>(taps.size() / 2);
    const long n = static_cast<long>(rf.size());
    std::vector<std::complex<float>> bb(rf.size());
    const double w = 2.0 * M_PI * (carrier + pre.freq_offset_hz) / fs;
    for (long i = 0; i < n; i++) {
        const double ph = std::fmod(w * static_cast<double>(i), 2.0 * M_PI);
        bb[i] = std::complex<float>(static_cast<float>(rf[i] * std::cos(ph)), static_cast<float>(-rf[i] * std::sin(ph)));
    }
    std::vector<std::complex<float>> y(rf.size(), std::complex<float>(0, 0));
    for (long i = std::max(0L, lo); i < n; i++) {
        std::complex<float> acc(0, 0);
        const long j0 = std::max(0L, half - i), j1 = std::min<long>(static_cast<long>(taps.size()), n - i + half);
        for (long j = j0; j < j1; j++) acc += bb[i - half + j] * taps[j];
        const double a = pre.phase_offset - pre.phase_rate * static_cast<double>(i - pre.start_sample) / sps;
        y[i] = acc * std::complex<float>(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    }
    return y;
}

/// Internal result of a 75 bps receive
struct RxOut {
    bool preamble = false;
    BrainDecodeResult pre;
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

/**
 * Receive one 75 bps message from rf (a real passband buffer starting near the preamble).
 */
inline RxOut receive(const std::vector<float>& rf, ModeId mode, const BrainDecoderConfig& base_cfg,
                     const RxTuning& t = RxTuning()) {
    RxOut r;
    const auto& mc = ModeDatabase::get(mode);
    BrainDecoderConfig dc = base_cfg;
    dc.baud_rate = 2400.0f;
    dc.unknown_data_len = SET_SYMBOLS;
    dc.known_data_len = 0;
    dc.preamble_symbols = mc.preamble_symbols();
    BrainDecoder dec(dc);
    r.pre = dec.decode(rf);
    if (!r.pre.preamble_found) { r.error = "75 bps: no preamble found"; return r; }
    r.preamble = true;

    const int sps = static_cast<int>(dc.sample_rate / 2400.0f);
    int frames_after = dc.preamble_symbols / brain::P_FRAME_LENGTH;
    if (r.pre.countdown >= 0) frames_after = r.pre.countdown + 1;
    const long data_start = r.pre.start_sample + static_cast<long>(frames_after) * brain::P_FRAME_LENGTH * sps;
    const long lo = data_start - static_cast<long>(brain::P_FRAME_LENGTH + 64) * sps;
    const auto y = matched_filter(rf, dc.sample_rate, dc.carrier_freq, r.pre, sps, lo);

    // the last preamble frame as sent (countdown 0)
    auto pre_syms = BrainPreambleEncoder().encode(mode == ModeId::M75NS ? 0 : 1, mode == ModeId::M75NL);
    std::vector<std::complex<float>> last(pre_syms.end() - brain::P_FRAME_LENGTH, pre_syms.end());

    WalshRake rake(y, sps, data_start, mode, t);
    rake.init_from_preamble(last);

    std::vector<soft_bit_t> soft;
    std::vector<double> ratio;
    for (long k = 0; rake.available(k); k++) {
        const auto o = rake.demod(k);
        soft_bit_t a, b;
        soft_bits(o, t.soft_scale, a, b);
        soft.push_back(a);
        soft.push_back(b);
        ratio.push_back(o.ratio);
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

    // SNR: signal power from the matched-filter output over the signal sets, noise from
    // the floor (noise-only energy of a 32-symbol correlation is 32 sigma^2)
    if (r.signal_sets > 0) {
        double pw = 0.0;
        long m = 0;
        for (long i = data_start; i < data_start + r.signal_sets * SET_SYMBOLS * sps &&
                                  i < static_cast<long>(y.size()); i += sps, m++)
            pw += std::norm(y[i]);
        const double s2 = rake.noise() / SET_SYMBOLS;
        const double s = m && s2 > 0 ? pw / m / s2 - 1.0 : 0.0;
        // matched-filter noise bandwidth 2400 Hz; the standard counts noise in 3 kHz
        r.snr_db = s > 0 ? 10.0 * std::log10(s * 2400.0 / 3000.0) : -99.0;
    }
    char buf[256];
    snprintf(buf, sizeof buf,
             "[RX M75] data_start=%ld sets=%ld signal_sets=%ld blocks=%ld eom_bit=%ld p=%.4f snr=%.1f dB shift=%ld",
             data_start, r.sets, r.signal_sets, r.blocks, r.eom_bit, r.p, r.snr_db, rake.shift());
    std::cout << buf << std::endl;
    return r;
}

/// The standard 110A mode a clearly read D1/D2 names when it is not `forced`, or ""
/// (as ModemRXImpl::preamble_contradicts does for the other modes)
inline std::string preamble_contradicts(const BrainDecodeResult& b, ModeId forced) {
    static const char* const KNOWN[] = {"M75NS", "M75NL", "M150S", "M150L", "M300S", "M300L", "M600S",
                                        "M600L", "M1200S", "M1200L", "M2400S", "M2400L", "M4800S"};
    if (std::find(std::begin(KNOWN), std::end(KNOWN), b.mode_name) == std::end(KNOWN)) return "";
    if (b.d1_corr < 1.5f * b.d1_corr2 || b.d2_corr < 1.5f * b.d2_corr2) return "";
    const std::string want = forced == ModeId::M75NS ? "M75NS" : "M75NL";
    return b.mode_name == want ? "" : b.mode_name.substr(1);
}

/**
 * ModemRX's 75 bps path: fill `res` (which carries what the AUTO pass read) from a
 * receive of `rf` in `mode`. forced: the mode was given, not read from the preamble.
 */
inline api::DecodeResult decode(const std::vector<float>& rf, const api::RxConfig& cfg, ModeId mode,
                                api::DecodeResult res, bool forced, const RxTuning& t = RxTuning()) {
    BrainDecoderConfig dc;
    dc.sample_rate = cfg.sample_rate;
    dc.carrier_freq = cfg.carrier_freq;
    dc.freq_search_range = cfg.freq_search_range;
    dc.freq_search_step = 0.5f;
    dc.use_fft_coarse_afc = cfg.use_fft_coarse_afc;
    dc.coarse_search_range = cfg.coarse_search_range;
    dc.fine_search_range = cfg.fine_search_range;
    const RxOut r = receive(rf, mode, dc, t);
    res.mode = mode == ModeId::M75NS ? api::Mode::M75_SHORT : api::Mode::M75_LONG;
    if (res.d1 < 0 && r.preamble) {
        res.d1 = r.pre.d1;
        res.d2 = r.pre.d2;
        res.countdown = r.pre.countdown;
    }
    if (!r.preamble) {
        res.success = false;
        res.error = api::Error(api::ErrorCode::RX_NO_SIGNAL, r.error);
        return res;
    }
    res.data = r.data;
    res.eom_sequence = r.eom_bit >= 0;
    res.eom_detected = res.eom_sequence;
    res.freq_offset_hz = r.pre.freq_offset_hz;
    res.snr_db = static_cast<float>(r.snr_db);
    res.ber_estimate = static_cast<float>(r.p);
    res.quality_db = static_cast<float>(10.0 * std::log10((1.0 - r.p) / std::max(r.p, 1e-4)));
    res.quality_threshold_db = quality_threshold_db(t);
    res.success = r.p <= t.max_p && (r.eom_bit >= 0 || r.blocks > 0);
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
    const std::string other = forced ? preamble_contradicts(r.pre, mode) : "";
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
