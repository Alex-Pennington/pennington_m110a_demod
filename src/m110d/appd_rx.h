// MIL-STD-188-110D Appendix D, 3 kHz single serial tone: receiver.
//
// Scope: preamble acquisition (D.5.2.1), down-count and Waveform ID decode
// (D.5.2.1.3.1, D.5.2.1.3.2) for every waveform 0-13, and payload decode for every
// waveform 0-13 with the K=7 code.
//
// WID 1-13 share one path: D.5.2 frame structure with D.5.2.2 mini-probes, D.5.1.3
// scrambling, D.5.3.3 block interleaver, D.5.3.2.4 repetition/puncturing, D.5.3.2.3
// full-tail-biting K=7 code, D.5.4.3 / D.5.4.5.1 EOM. WID 1-5 (BPSK) use the D.5.1.2.1.1
// transcoding here; WID 6-13 (QPSK, 8PSK, 16/32/64/256QAM) are demapped by appd_qam.h.
// WID 0 (Walsh, no frames or probes) is decoded by appd_walsh.h.
//
// Synchronisation: the preamble is searched in time and over 25 Hz frequency bins (+-112 Hz);
// the clock ratio is fitted from the fixed-subsection timing of every super-frame; WID 1-13
// track residual frequency and symbol timing on the D.5.2.2 mini-probes, WID 0 with a
// decision-directed carrier and timing loop. Channel estimation is one complex gain per
// mini-probe, interpolated across the frame: there is no equaliser. An equaliser would replace
// the per-symbol `h` in decode_payload, with the tracked `tau` and frequency ramp kept.
//
// Not decoded: K=9.
#ifndef M110D_APPD_RX_H
#define M110D_APPD_RX_H

#include "m110d/appd_tables.h"
#include "m110d/appd_qam.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace m110d {

// ---------------------------------------------------------------------------------------------
// WAV input: PCM 16-bit, any channel count (channel 0 used), chunk-walking reader.
// ---------------------------------------------------------------------------------------------
inline bool read_wav(const std::string& path, std::vector<double>& out, int& fs, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    char hdr[12];
    if (!f.read(hdr, 12) || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
        err = "not a RIFF/WAVE file"; return false;
    }
    int ch = 0, bits = 0, fmt = 0;
    fs = 0;
    while (f) {
        char id[4]; uint32_t sz = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&sz), 4)) break;
        if (!std::memcmp(id, "fmt ", 4)) {
            std::vector<char> b(sz);
            f.read(b.data(), sz);
            uint16_t af, nc, bps; uint32_t sr;
            std::memcpy(&af, &b[0], 2); std::memcpy(&nc, &b[2], 2);
            std::memcpy(&sr, &b[4], 4); std::memcpy(&bps, &b[14], 2);
            fmt = af; ch = nc; fs = (int)sr; bits = bps;
        } else if (!std::memcmp(id, "data", 4)) {
            if (fmt != 1 || bits != 16 || ch < 1) { err = "only 16-bit PCM supported"; return false; }
            size_t n = sz / (2 * ch);
            std::vector<int16_t> pcm(n * ch);
            f.read(reinterpret_cast<char*>(pcm.data()), (std::streamsize)(n * ch * 2));
            out.resize(n);
            for (size_t i = 0; i < n; i++) out[i] = pcm[i * ch] / 32768.0;
            return true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    err = "no data chunk";
    return false;
}

// ---------------------------------------------------------------------------------------------
// Front end: mix the 1800 Hz sub-carrier (Table D-I) to 0 Hz and apply the D.5.1.5 SRRC
// matched filter. The in-phase carrier is cos and the quadrature carrier -sin (D.5.1), so
// s(t) = Re{x(t) e^{j w t}} and x = 2 * LPF{s(t) e^{-j w t}}.
// ---------------------------------------------------------------------------------------------
inline std::vector<double> srrc_taps(int sps, int span, double beta) {
    int n = 2 * span * sps + 1;
    std::vector<double> h(n);
    double e = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)(i - span * sps) / sps;
        double v;
        if (std::fabs(t) < 1e-12) v = 1.0 - beta + 4 * beta / M_PI;
        else if (std::fabs(std::fabs(4 * beta * t) - 1.0) < 1e-9)
            v = beta / std::sqrt(2.0) * ((1 + 2 / M_PI) * std::sin(M_PI / (4 * beta)) +
                                         (1 - 2 / M_PI) * std::cos(M_PI / (4 * beta)));
        else
            v = (std::sin(M_PI * t * (1 - beta)) + 4 * beta * t * std::cos(M_PI * t * (1 + beta))) /
                (M_PI * t * (1 - (4 * beta * t) * (4 * beta * t)));
        h[i] = v;
        e += v * v;
    }
    for (auto& v : h) v /= std::sqrt(e);
    return h;
}

inline std::vector<cplx> to_baseband(const std::vector<double>& x, int fs, int sps) {
    size_t n = x.size();
    std::vector<cplx> mix(n);
    double w = 2 * M_PI * kCarrierHz / fs;
    for (size_t i = 0; i < n; i++) mix[i] = 2.0 * x[i] * std::polar(1.0, -w * (double)(i % (size_t)fs));
    std::vector<double> h = srrc_taps(sps, 8, kRolloff);
    int L = (int)h.size(), half = L / 2;
    std::vector<cplx> y(n);
    for (size_t i = 0; i < n; i++) {
        cplx acc = 0;
        int k0 = std::max(0, (int)i + half - (int)n + 1), k1 = std::min(L - 1, (int)i + half);
        for (int k = k0; k <= k1; k++) acc += h[k] * mix[i + half - k];
        y[i] = acc;
    }
    return y;
}

// ---------------------------------------------------------------------------------------------
// Preamble chip sequences (D.5.2.1.1, D.5.2.1.4). `restart` selects the reading where the
// scrambling sequence index restarts at 0 for each Walsh channel symbol; otherwise the index
// runs continuously across the section (the README's STD-AMB-001 variant).
// ---------------------------------------------------------------------------------------------
inline int pn_index(int sym, int chip, bool restart) {
    return restart ? chip : (sym * kWalshLen + chip) % 256;
}

inline std::vector<int> walsh_section(const int* pn, const int* dibits, int nsym, bool restart) {
    std::vector<int> c(nsym * kWalshLen);
    for (int s = 0; s < nsym; s++)
        for (int j = 0; j < kWalshLen; j++)
            c[s * kWalshLen + j] = (pn[pn_index(s, j, restart)] + kWalsh4[dibits[s]][j % 4]) % 8;
    return c;
}

// D.5.2.1.3.1: down-count di-bits c3..c0 carry b7..b0, b7 MSB; parity b7 b6 b5.
inline bool count_from_bits(int b, int& count) {
    auto bit = [&](int i) { return (b >> i) & 1; };
    count = b & 31;
    return bit(7) == (bit(1) ^ bit(2) ^ bit(3)) && bit(6) == (bit(2) ^ bit(3) ^ bit(4)) &&
           bit(5) == (bit(0) ^ bit(1) ^ bit(2));
}

// D.5.2.1.3.2 with Tables D-XV, D-XVI, D-XVII.
struct WidField {
    int raw = 0;
    int waveform = -1;
    int interleaver = -1;
    int constraint = 0;
    bool checksum_ok = false;
};

inline WidField wid_from_bits(int d) {
    auto bit = [&](int i) { return (d >> i) & 1; };
    WidField w;
    w.raw = d;
    w.waveform = (d >> 6) & 15;
    w.interleaver = (d >> 4) & 3;
    w.constraint = bit(3) ? 9 : 7;
    w.checksum_ok = bit(2) == (bit(9) ^ bit(8) ^ bit(7)) && bit(1) == (bit(7) ^ bit(6) ^ bit(5)) &&
                    bit(0) == (bit(5) ^ bit(4) ^ bit(3));
    return w;
}

// ---------------------------------------------------------------------------------------------
// Full-tail-biting K=7 rate-1/2 code (D.5.3.2.1, D.5.3.2.3) with repetition and puncturing
// (D.5.3.2.4, Table D-L).
//
// Tap sets are given as delays (0 = the bit just shifted in). Figure D-9 shifts the input into
// the x^6 cell, so cell x^k holds the bit with delay 6-k:
//   T1 = x^6+x^4+x^3+x+1 -> delays {0,2,3,5,6};  T2 = x^6+x^5+x^4+x^3+1 -> delays {0,1,2,3,6}.
// D.5.3.2.1 says this is the 110D section 5.3.2 serial-tone code; it is the code this repo's
// 110A path uses (src/common/constants.h:79-80, 0x5B / 0x79 with the new bit at bit 6), and
// it is the reading that decodes the interop test set (docs/110d-appd.md). `alt` selects the
// time-reversed reading, kept only as a diagnostic.
// ---------------------------------------------------------------------------------------------
struct ConvCode {
    int t1[5], t2[5];
    static ConvCode make(bool alt) {
        ConvCode c;
        const int a[5] = {0, 2, 3, 5, 6};
        const int b[5] = {0, 1, 2, 3, 6};
        for (int i = 0; i < 5; i++) {
            c.t1[i] = alt ? 6 - a[i] : a[i];
            c.t2[i] = alt ? 6 - b[i] : b[i];
        }
        return c;
    }
    // state bit i = bit with delay i+1; returns T1 in bit 0, T2 in bit 1
    int out(int state, int b) const {
        int reg = b | (state << 1);  // bit d = delay d
        int o1 = 0, o2 = 0;
        for (int i = 0; i < 5; i++) { o1 ^= (reg >> t1[i]) & 1; o2 ^= (reg >> t2[i]) & 1; }
        return o1 | (o2 << 1);
    }
};

// Undo repetition + puncturing: punctured soft bits -> per-input-bit soft (T1, T2) sums.
// Pairs are numbered over the repeated stream; the pattern column advances per pair and
// starts at the first pair of the block (D.5.3.2.4).
inline bool depuncture(const std::vector<double>& in, int n_input, const WaveformParams& p,
                       std::vector<double>& s1, std::vector<double>& s2) {
    int P = (int)std::strlen(p.punct_t1);
    s1.assign(n_input, 0.0);
    s2.assign(n_input, 0.0);
    size_t pos = 0;
    for (int pair = 0; pair < n_input * p.repeats; pair++) {
        int k = pair / p.repeats, col = pair % P;
        if (p.punct_t1[col] == '1') { if (pos >= in.size()) return false; s1[k] += in[pos++]; }
        if (p.punct_t2[col] == '1') { if (pos >= in.size()) return false; s2[k] += in[pos++]; }
    }
    return pos == in.size();
}

// Tail-biting soft Viterbi by wrap-around: decode the circular pair sequence extended by W
// pairs at each end, keep the middle. Soft input: positive = bit 0. Returns v[] where pair j
// was produced with v[j] as the newest register bit.
inline std::vector<int> viterbi_tb(const std::vector<double>& s1, const std::vector<double>& s2,
                                   const ConvCode& code, double* metric_out = nullptr) {
    const int N = (int)s1.size();
    const int W = std::min(N, 96);
    const int T = N + 2 * W;
    const int S = 64;
    std::vector<double> pm(S, 0.0), nm(S);
    std::vector<uint8_t> dec((size_t)T * S);
    int outs[S][2];
    for (int s = 0; s < S; s++) for (int b = 0; b < 2; b++) outs[s][b] = code.out(s, b);
    for (int e = 0; e < T; e++) {
        int j = ((e - W) % N + N) % N;
        double l1 = s1[j], l2 = s2[j];
        std::fill(nm.begin(), nm.end(), -1e300);
        for (int s = 0; s < S; s++) {
            for (int b = 0; b < 2; b++) {
                int o = outs[s][b];
                double m = pm[s] + ((o & 1) ? -l1 : l1) + ((o & 2) ? -l2 : l2);
                int ns = ((s << 1) | b) & 63;
                if (m > nm[ns]) { nm[ns] = m; dec[(size_t)e * S + ns] = (uint8_t)s; }
            }
        }
        double mx = *std::max_element(nm.begin(), nm.end());
        for (int s = 0; s < S; s++) pm[s] = nm[s] - mx;
    }
    int st = (int)(std::max_element(pm.begin(), pm.end()) - pm.begin());
    std::vector<int> bits(T);
    for (int e = T - 1; e >= 0; e--) {
        bits[e] = st & 1;
        st = dec[(size_t)e * S + st];
    }
    std::vector<int> v(N);
    for (int j = 0; j < N; j++) v[j] = bits[W + j];
    if (metric_out) {
        // fraction of soft decisions the decoded path re-encodes to the same sign
        int agree = 0, tot = 0;
        for (int j = 0; j < N; j++) {
            int state = 0;
            for (int d = 1; d <= 6; d++) state |= v[((j - d) % N + N) % N] << (d - 1);
            int o = code.out(state, v[j]);
            if (s1[j] != 0) { tot++; agree += ((o & 1) ? -1 : 1) * s1[j] > 0; }
            if (s2[j] != 0) { tot++; agree += ((o & 2) ? -1 : 1) * s2[j] > 0; }
        }
        *metric_out = tot ? (double)agree / tot : 0;
    }
    return v;
}

// ---------------------------------------------------------------------------------------------
// Receiver
// ---------------------------------------------------------------------------------------------
struct RxOptions {
    int pn_mode = -1;          // -1 auto, 1 restart-per-Walsh-symbol, 0 continuous
    int bpsk_scr_shifts = 3;   // scrambler iterations per BPSK symbol (D.5.1.3), measured
    int code_reversed = 0;     // 0 Figure D-9 reading (default), 1 time-reversed (diagnostic), -1 pick best
    int psk_scr_shifts = 3;    // scrambler iterations per QPSK/8PSK symbol (D.5.1.3), see appd_qam.h
    double qam_scale = 1.0;    // QAM constellation scale vs mini-probe; 0 = per-frame RMS match (appd_qam.h)
    bool verbose = false;
};

struct RxResult {
    bool acquired = false;
    double acq_metric = 0;
    double acq_time = 0;
    std::string preamble_shape;  // nine-fixed / single-fixed
    int pn_restart = -1;
    double freq_offset_hz = 0;
    double clock_ppm = 0;  // receive clock error from the preamble timing fit
    int superframes_seen = 0;
    std::vector<int> counts;
    bool count_parity_ok = false;
    WidField wid;
    int wid_votes = 0;
    bool payload_attempted = false;
    std::string payload_skip_reason;
    int blocks_decoded = 0;
    int code_reversed = -1;
    double reencode_agree = 0;
    double probe_quality = 0;  // mean normalised probe correlation over the data
    bool eom = false;
    long eom_bit = -1;
    std::vector<uint8_t> bytes;  // decoded payload up to EOM (or all decoded bits)
    // Chip-level check of the preamble scrambling tables (D-XVIII, D-XIX, D-XX) against the
    // acquired super-frame: hard 8PSK decisions vs expected chips, per section.
    int chips_fixed = 0, chip_err_fixed = 0;
    int chips_count = 0, chip_err_count = 0;
    int chips_wid = 0, chip_err_wid = 0;
    std::vector<int> fixed_sym_err;  // chip errors per fixed Walsh symbol
    std::vector<int> boundary_frames;  // data frames followed by the cyclically shifted probe
    int walsh_alt_blocks = -1;  // WID 0: blocks whose last symbol matched the alternate Walsh set
    int bits_per_sym = 0;
    QamStats qam;  // WID 6-13 demapper statistics (appd_qam.h)
};

}  // namespace m110d

#include "m110d/appd_walsh.h"  // WID 0 decoder (D.5.1.2.3, D.5.1.4)

namespace m110d {

class AppDRx {
public:
    explicit AppDRx(RxOptions o) : opt_(o) {}

    RxResult run(const std::vector<double>& audio, int fs) {
        RxResult r;
        sps_ = fs / (int)kSymbolRate;
        fs_ = fs;
        if (sps_ * (int)kSymbolRate != fs) { r.payload_skip_reason = "sample rate not a multiple of 2400"; return r; }
        y_ = to_baseband(audio, fs, sps_);
        if (!acquire(r)) return r;
        if (!read_preamble(r)) return r;
        if (r.wid.waveform == 0) {  // WID 0: Walsh data, no frames or probes (Tables D-XI, D-XII)
            decode_walsh({[this](double n, double tau) { return sample(n, tau); }, data_start_sym_, h_last_,
                          (long)(((double)y_.size() - spos(0)) / (sps_ * rate_)), sps_, opt_.verbose}, r);
            return r;
        }
        decode_payload(r);
        return r;
    }

private:
    RxOptions opt_;
    int sps_ = 0, fs_ = 0;
    std::vector<cplx> y_;
    long t0_ = 0;       // sample index of the first chip of the acquired super-frame
    int sf_len_ = 0;    // super-frame length in symbols
    int nfixed_ = 0;    // Walsh symbols in the fixed subsection
    double fo_ = 0;     // frequency offset, Hz
    double rate_ = 1;   // receive samples per nominal sample (clock ratio, 1 + ppm*1e-6)
    double t0f_ = 0;    // fractional correction to t0_, samples
    cplx h0_ = 1.0;     // channel gain from the fixed subsection
    cplx h_last_ = 1.0;  // fixed-subsection gain of the last super-frame read

    // y_ at fractional sample index n (linear interpolation), frequency-corrected.
    cplx at(double n) const {
        if (n < 0 || n > (double)(y_.size() - 1)) return 0.0;
        long i = (long)n;
        double f = n - (double)i;
        cplx v = f > 0 && i + 1 < (long)y_.size() ? y_[i] * (1 - f) + y_[i + 1] * f : y_[i];
        if (fo_ == 0) return v;
        return v * std::polar(1.0, -2 * M_PI * fo_ * n / fs_);
    }

    // Sample index of symbol position sym_pos (from t0_) under the clock ratio, plus tau samples.
    double spos(double sym_pos, double tau = 0) const { return (double)t0_ + t0f_ + sym_pos * sps_ * rate_ + tau; }

    cplx sample(double sym_pos, double tau = 0) const { return at(spos(sym_pos, tau)); }

    // Normalised non-coherent correlation of chips, 32-chip coherent parts, starting at
    // sample t (fractional), frequency-corrected by fo_.
    double corr_at(double t, const std::vector<int>& chips, std::vector<cplx>* parts = nullptr,
                   int part_len = kWalshLen) const {
        int N = (int)chips.size();
        if (t < 0 || t + (double)(N - 1) * sps_ * rate_ >= (double)y_.size()) return 0;
        double num = 0, e = 0;
        if (parts) parts->clear();
        for (int c = 0; c < N; c += part_len) {
            cplx acc = 0;
            for (int j = c; j < std::min(N, c + part_len); j++) {
                cplx v = at(t + (double)j * sps_ * rate_);
                acc += v * std::conj(psk8(chips[j]));
                e += std::norm(v);
            }
            num += std::abs(acc);
            if (parts) parts->push_back(acc);
        }
        return e > 0 ? num / std::sqrt(e * N) : 0;
    }

    // Preamble search over time and frequency. Each frequency bin applies a phase ramp to the
    // chip reference, so a 32-chip part stays coherent at the bin's offset; bins are kAcqBinHz
    // apart over +-kAcqBins*kAcqBinHz. Time is searched on a grid of sps/4 samples, then the
    // best cell is refined at every sample over the neighbouring bins.
    static constexpr double kAcqBinHz = 25.0;
    static constexpr int kAcqBins = 4;  // bins -100..+100 Hz: coverage +-112.5 Hz

    long search(const std::vector<int>& chips, double& best, double& best_f) const {
        const int N = (int)chips.size(), NB = 2 * kAcqBins + 1;
        std::vector<std::vector<cplx>> ref(NB, std::vector<cplx>(N));
        for (int b = 0; b < NB; b++) {
            double f = (b - kAcqBins) * kAcqBinHz;
            for (int j = 0; j < N; j++)
                ref[b][j] = std::conj(psk8(chips[j])) * std::polar(1.0, -2 * M_PI * f * (j % kWalshLen) / kSymbolRate);
        }
        const long span = (long)(N - 1) * sps_;
        auto eval = [&](long t, int b0, int b1, double& m, int& bb) {
            double e = 0;
            for (int j = 0; j < N; j++) e += std::norm(y_[t + (long)j * sps_]);
            m = 0; bb = b0;
            if (e <= 0) return;
            for (int b = b0; b <= b1; b++) {
                double num = 0;
                for (int c = 0; c < N; c += kWalshLen) {
                    cplx acc = 0;
                    for (int j = c; j < std::min(N, c + kWalshLen); j++) acc += y_[t + (long)j * sps_] * ref[b][j];
                    num += std::abs(acc);
                }
                double v = num / std::sqrt(e * N);
                if (v > m) { m = v; bb = b; }
            }
        };
        best = 0;
        best_f = 0;
        long bt = -1;
        int bbin = kAcqBins;
        const long step = std::max(1, sps_ / 4);
        for (long t = 0; t + span < (long)y_.size(); t += step) {
            double m; int b;
            eval(t, 0, NB - 1, m, b);
            if (m > best) { best = m; bt = t; bbin = b; }
        }
        if (bt < 0) return bt;
        long c = bt;
        int cb = bbin;
        for (long t = std::max(0L, c - step); t <= c + step && t + span < (long)y_.size(); t++) {
            double m; int b;
            eval(t, std::max(0, cb - 1), std::min(NB - 1, cb + 1), m, b);
            if (m > best) { best = m; bt = t; bbin = b; }
        }
        best_f = (bbin - kAcqBins) * kAcqBinHz;
        return bt;
    }

    // Frequency offset at sample t: the bin offset f0 plus the residual from the phase step
    // between 8-chip parts (unambiguous to +-150 Hz), then between 32-chip parts (+-37.5 Hz)
    // once the 8-chip residual is removed. Returns f0 when there is only one 32-chip part.
    double estimate_freq(long t, const std::vector<int>& chips, double f0) {
        auto step = [&](int len) {
            std::vector<cplx> parts;
            corr_at((double)t, chips, &parts, len);
            cplx d = 0;
            for (size_t k = 1; k < parts.size(); k++) d += parts[k] * std::conj(parts[k - 1]);
            return parts.size() > 1 ? std::arg(d) / (2 * M_PI * len / kSymbolRate) : 0.0;
        };
        fo_ = f0;
        fo_ += step(8);
        if (chips.size() > (size_t)kWalshLen) fo_ += step(kWalshLen);
        return fo_;
    }

    bool acquire(RxResult& r) {
        struct Cand { const char* shape; bool restart; std::vector<int> chips; int nfix; };
        std::vector<Cand> cands;
        for (int rs = 1; rs >= 0; rs--) {
            if (opt_.pn_mode >= 0 && rs != opt_.pn_mode) continue;
            cands.push_back({"nine-fixed", rs == 1, walsh_section(kFixedPN, kFixedNine, 9, rs == 1), 9});
        }
        double best = 0, bf = 0;
        long bt = -1;
        const Cand* bc = nullptr;
        for (auto& c : cands) {
            double m, f;
            long t = search(c.chips, m, f);
            if (opt_.verbose) std::fprintf(stderr, "acq %s pn=%s metric=%.3f t=%ld bin=%+.0f Hz\n", c.shape,
                                           c.restart ? "restart" : "continuous", m, t, f);
            if (m > best) { best = m; bt = t; bc = &c; bf = f; }
        }
        // Single-symbol fixed subsection (D.5.2.1.3): tried when the nine-symbol shape is not
        // present. The single fixed symbol is followed by count and WID; search on the fixed
        // symbol plus the count of a final super-frame (count 0) is not assumed: fixed only.
        if (best < 0.7) {
            for (int rs = 1; rs >= 0; rs--) {
                if (opt_.pn_mode >= 0 && rs != opt_.pn_mode) continue;
                std::vector<int> ch = walsh_section(kFixedPN, kFixedSingle, 1, rs == 1);
                double m, f;
                long t = search(ch, m, f);
                if (opt_.verbose) std::fprintf(stderr, "acq single-fixed pn=%s metric=%.3f t=%ld bin=%+.0f Hz\n",
                                               rs ? "restart" : "continuous", m, t, f);
                if (m > best) {
                    best = m; bt = t; bf = f;
                    static Cand single[2];
                    single[rs] = {"single-fixed", rs == 1, ch, 1};
                    bc = &single[rs];
                }
            }
        }
        r.acq_metric = best;
        if (!bc || best < 0.5) { r.payload_skip_reason = "no preamble found"; return false; }
        r.acquired = true;
        r.preamble_shape = bc->shape;
        r.pn_restart = bc->restart ? 1 : 0;
        nfixed_ = bc->nfix;
        sf_len_ = (nfixed_ + kCountSyms + kWidSyms) * kWalshLen;
        t0_ = bt;
        fixed_chips_ = bc->chips;
        restart_ = bc->restart;
        r.acq_time = (double)bt / fs_;
        estimate_freq(bt, bc->chips, bf);
        r.freq_offset_hz = fo_;
        return true;
    }

    std::vector<int> fixed_chips_;
    bool restart_ = true;

    cplx fixed_gain(long sf_start_sym) const {
        cplx acc = 0;
        double e = 0;
        for (size_t j = 0; j < fixed_chips_.size(); j++) {
            cplx v = sample((double)(sf_start_sym + (long)j));
            acc += v * std::conj(psk8(fixed_chips_[j]));
            e += 1;
        }
        return acc / e;
    }

    // Coherent 4-ary Walsh decision on one channel symbol; returns per-dibit scores.
    void walsh_scores(long start_sym, int sym_in_section, const int* pn, cplx h, double sc[4]) const {
        for (int d = 0; d < 4; d++) {
            cplx acc = 0;
            for (int j = 0; j < kWalshLen; j++) {
                int chip = (pn[pn_index(sym_in_section, j, restart_)] + kWalsh4[d][j % 4]) % 8;
                acc += sample((double)(start_sym + j)) * std::conj(psk8(chip));
            }
            sc[d] = std::real(acc * std::conj(h)) / (std::abs(h) + 1e-12);
        }
    }

    long data_start_sym_ = 0;  // symbol offset (from t0_) of the first mini-probe

    // Peak position, relative to index c, of a least-squares parabola through v[c-4..c+4]
    // (fewer at the ends). The correlation peak is about sps samples wide, so a wide fit is
    // steadier than three points. Clamped to +-1 sample.
    static double peak_offset(const std::vector<double>& v, int c) {
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, t0 = 0, t1 = 0, t2 = 0;
        for (int d = -4; d <= 4; d++) {
            int i = c + d;
            if (i < 0 || i >= (int)v.size()) continue;
            double x = d, x2 = x * x;
            s0 += 1; s1 += x; s2 += x2; s3 += x2 * x; s4 += x2 * x2;
            t0 += v[i]; t1 += x * v[i]; t2 += x2 * v[i];
        }
        // normal equations for v = a + b x + c2 x^2 (Cramer's rule)
        double D = s0 * (s2 * s4 - s3 * s3) - s1 * (s1 * s4 - s3 * s2) + s2 * (s1 * s3 - s2 * s2);
        if (std::fabs(D) < 1e-12) return 0;
        double Db = s0 * (t1 * s4 - s3 * t2) - t0 * (s1 * s4 - s3 * s2) + s2 * (s1 * t2 - t1 * s2);
        double Dc = s0 * (s2 * t2 - t1 * s3) - s1 * (s1 * t2 - t1 * s2) + t0 * (s1 * s3 - s2 * s2);
        double b = Db / D, c2 = Dc / D;
        if (c2 >= 0) return 0;
        return std::max(-1.0, std::min(1.0, -b / (2 * c2)));
    }

    // Symbol timing over the preamble: fine-time the fixed subsection of every super-frame
    // that correlates, nearest the acquired one first, each predicted from a least-squares
    // line through those already found; the line's slope is the clock ratio and its
    // intercept the fractional start. A peak measured with the wrong chip spacing is biased
    // by the stretch over half the subsection, so every super-frame found is then
    // re-measured under the fitted ratio and the line refitted, twice.
    void fit_clock(RxResult& r) {
        std::vector<std::pair<double, double>> pts;  // (super-frame index, peak sample)
        const int W = std::max(2, (2 * sps_) / 5);      // +-0.4 symbol search
        auto refit = [&]() {
            if (pts.size() < 2) return;
            double n = (double)pts.size(), sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (auto& p : pts) { sx += p.first; sy += p.second; sxx += p.first * p.first; sxy += p.first * p.second; }
            double b = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            double a = (sy - b * sx) / n;
            rate_ = b / ((double)sf_len_ * sps_);
            t0f_ = a - (double)t0_;
        };
        auto measure = [&](long m, double& peak) {
            double tp = spos((double)(m * sf_len_));
            if (tp - W < 0 || spos((double)((m + 1) * sf_len_)) + W >= (double)y_.size()) return false;
            int bk = 0;
            double bq = 0;
            std::vector<double> qv(2 * W + 1);
            for (int d = -W; d <= W; d++) {
                qv[d + W] = corr_at(tp + d, fixed_chips_);
                if (qv[d + W] > bq) { bq = qv[d + W]; bk = d; }
            }
            if (bq < 0.5) return false;
            peak = tp + bk + peak_offset(qv, bk + W);
            return true;
        };
        for (int k = 0; k <= 62; k++) {
            long m = (k + 1) / 2 * (k % 2 ? 1 : -1);  // 0, 1, -1, 2, -2, ...
            double pk;
            if (!measure(m, pk)) continue;
            pts.push_back({(double)m, pk});
            if (pts.size() == 1) t0f_ = pk - (double)t0_ - (double)m * sf_len_ * sps_ * rate_;
            refit();
        }
        for (int pass = 0; pass < 2 && pts.size() >= 2; pass++) {
            std::vector<std::pair<double, double>> again;
            for (auto& p : pts) {
                double pk;
                if (measure((long)p.first, pk)) again.push_back({p.first, pk});
            }
            if (again.size() < 2) break;
            pts = again;
            refit();
        }
        r.clock_ppm = (rate_ - 1) * 1e6;
    }

    bool read_preamble(RxResult& r) {
        fit_clock(r);
        // Walk super-frames backwards and forwards from the acquired one while the fixed
        // subsection correlates; decode count and WID in each.
        double wid_sc[kWidSyms][4] = {};
        int first_c = -1, last_c = -1;
        long last_sf = 0;
        bool parity_all = true;
        std::vector<std::pair<long, int>> seen;
        for (long m = -31; m <= 31; m++) {
            long sf = m * (long)sf_len_;
            double t = spos((double)sf);
            if (t < 0 || spos((double)(sf + sf_len_)) >= (double)y_.size()) continue;
            double q = corr_at(t, fixed_chips_);
            if (q < 0.5) continue;
            cplx h = fixed_gain(sf);
            int b = 0;
            for (int s = 0; s < kCountSyms; s++) {
                double sc[4];
                walsh_scores(sf + (nfixed_ + s) * kWalshLen, s, kCountPN, h, sc);
                int d = (int)(std::max_element(sc, sc + 4) - sc);
                b = (b << 2) | d;
            }
            int cnt;
            bool ok = count_from_bits(b, cnt);
            if (!ok) { parity_all = false; continue; }
            for (int s = 0; s < kWidSyms; s++) {
                double sc[4];
                walsh_scores(sf + (nfixed_ + kCountSyms + s) * kWalshLen, s, kWidPN, h, sc);
                for (int d = 0; d < 4; d++) wid_sc[s][d] += sc[d];
            }
            seen.push_back({sf, cnt});
            if (first_c < 0) first_c = cnt;
            last_c = cnt;
            last_sf = sf;
            h_last_ = h;
            if (m == 0) { h0_ = h; count_bits0_ = b; }
        }
        r.superframes_seen = (int)seen.size();
        for (auto& p : seen) r.counts.push_back(p.second);
        r.count_parity_ok = parity_all && !seen.empty();
        if (seen.empty()) { r.payload_skip_reason = "no valid down-count"; return false; }
        int d = 0;
        for (int s = 0; s < kWidSyms; s++) d = (d << 2) | (int)(std::max_element(wid_sc[s], wid_sc[s] + 4) - wid_sc[s]);
        r.wid = wid_from_bits(d);
        r.wid_votes = (int)seen.size();
        // The super-frame with down-count c is followed by c more super-frames (D.5.2.1.3);
        // the mini-probe that ends the preamble follows the last (D.5.2.2).
        data_start_sym_ = last_sf + (long)(last_c + 1) * sf_len_;
        (void)first_c;
        check_chips(r);
        return true;
    }

    int count_bits0_ = -1;

    int hard_chip(long sym, cplx h) const {
        double a = std::arg(sample((double)sym) * std::conj(h));
        return (((int)std::lround(a / (M_PI / 4))) % 8 + 8) % 8;
    }

    // Compare hard chip decisions in the acquired super-frame with the chips the tables give
    // for the decided di-bits. A transcription error in a used table entry shows as a chip
    // error at that index in every file that uses it.
    void check_chips(RxResult& r) {
        if (count_bits0_ < 0) return;
        cplx h = h0_;
        const int* fixed_dibits = nfixed_ == 9 ? kFixedNine : kFixedSingle;
        r.fixed_sym_err.assign(nfixed_, 0);
        for (int s = 0; s < nfixed_; s++)
            for (int j = 0; j < kWalshLen; j++) {
                int e = (kFixedPN[pn_index(s, j, restart_)] + kWalsh4[fixed_dibits[s]][j % 4]) % 8;
                bool bad = hard_chip((long)s * kWalshLen + j, h) != e;
                r.chips_fixed++; r.chip_err_fixed += bad; r.fixed_sym_err[s] += bad;
            }
        for (int s = 0; s < kCountSyms; s++) {
            int d = (count_bits0_ >> (2 * (kCountSyms - 1 - s))) & 3;
            for (int j = 0; j < kWalshLen; j++) {
                int e = (kCountPN[pn_index(s, j, restart_)] + kWalsh4[d][j % 4]) % 8;
                r.chips_count++;
                r.chip_err_count += hard_chip((long)(nfixed_ + s) * kWalshLen + j, h) != e;
            }
        }
        for (int s = 0; s < kWidSyms; s++) {
            int d = (r.wid.raw >> (2 * (kWidSyms - 1 - s))) & 3;
            for (int j = 0; j < kWalshLen; j++) {
                int e = (kWidPN[pn_index(s, j, restart_)] + kWalsh4[d][j % 4]) % 8;
                r.chips_wid++;
                r.chip_err_wid += hard_chip((long)(nfixed_ + kCountSyms + s) * kWalshLen + j, h) != e;
            }
        }
        if (opt_.verbose && nfixed_ == 9) {
            // which fixedPN starting offset reproduces each fixed symbol
            for (int s = 0; s < 9; s++) {
                int best_o = -1, best_m = -1;
                for (int o = 0; o < 256; o++) {
                    int mt = 0;
                    for (int j = 0; j < kWalshLen; j++)
                        mt += hard_chip((long)s * kWalshLen + j, h) ==
                              (kFixedPN[(o + j) % 256] + kWalsh4[kFixedNine[s]][j % 4]) % 8;
                    if (mt > best_m) { best_m = mt; best_o = o; }
                }
                std::fprintf(stderr, "fixed symbol %d: best fixedPN offset %d (%d/32 chips)\n", s, best_o, best_m);
            }
        }
    }

    // Normalised correlation of a mini-probe at symbol offset pos; returns the complex gain
    // at the probe centre. fr is the residual frequency in cycles per symbol, removed about the
    // centre; tau is the timing offset in samples.
    cplx probe_gain(long pos, const std::vector<cplx>& p, double* q, double fr = 0, double tau = 0) const {
        cplx acc = 0;
        double e = 0;
        const double c = 0.5 * (double)(p.size() - 1);
        for (size_t i = 0; i < p.size(); i++) {
            cplx v = sample((double)(pos + (long)i), tau);
            cplx t = v * std::conj(p[i]);
            if (fr != 0) t *= std::polar(1.0, -2 * M_PI * fr * ((double)i - c));
            acc += t;
            e += std::norm(v);
        }
        if (q) *q = e > 0 ? std::abs(acc) / std::sqrt(e * p.size()) : 0;
        return acc / (double)p.size();
    }

    void decode_payload(RxResult& r) {
        const WidField& w = r.wid;
        if (!w.checksum_ok) { r.payload_skip_reason = "WID checksum failed"; return; }
        if (w.constraint != 7) { r.payload_skip_reason = "K=9 not implemented"; return; }
        const WaveformParams* wp = waveform_params(w.waveform);
        const InterleaverParams* ip = interleaver_params(w.waveform, w.interleaver);
        if (!wp || !ip) { r.payload_skip_reason = "waveform/interleaver not defined at 3 kHz"; return; }
        if (wp->bits_per_sym == 0) { r.payload_skip_reason = "Walsh data not decoded"; return; }
        r.bits_per_sym = wp->bits_per_sym;
        r.payload_attempted = true;

        const int U = wp->U, K = wp->K;
        const std::vector<cplx> probe = mini_probe(K, false);
        const std::vector<cplx> probe_b = mini_probe(K, true);
        const int nbits = ip->bits;
        std::vector<double> blk;
        blk.reserve(nbits);
        std::vector<int> outbits;
        long pos = data_start_sym_;
        double q;
        cplx h_prev = probe_gain(pos, probe, &q);
        double qsum = q;
        int qn = 1;
        pos += K;
        int code_rev = opt_.code_reversed;
        double agree_sum = 0;
        // Residual frequency tracking on the mini-probes (D.5.2.2). fr is the residual in cycles
        // per symbol after fo_, dfr its change per frame (drift). Each probe gain is referred to
        // the probe centre; the rotation from one centre to the next is predicted from fr and
        // the measured rotation is unwrapped about that prediction, so it may exceed pi.
        const double Tf = (double)(U + K);  // symbols between probe centres
        double fr = 0, dfr = 0;
        const double kFrA = 0.5, kFrB = 0.15;
        // Symbol timing tracking on the same probes: tau is the timing offset in samples at a
        // probe, dtau its change per frame (residual clock error after rate_). The probe is
        // correlated at tau_pred and +-sps/4 and |gain| is fitted with a parabola; the update
        // is skipped on a probe below quality 0.6 (a fade), and dtau is held to +-1000 ppm.
        double tau = 0, dtau = 0;
        // Low gains: the preamble fit carries the clock ratio, the loop only trims it. Gains of
        // 0.5 / 0.1 cost seeds on the fading channels, where multipath moves the probe peak
        // (measured on files 29 and 41, ITU good, 10 seeds).
        const double kTauA = 0.15, kTauB = 0.01;
        const double dl = std::max(1.0, sps_ / 4.0);
        const double dtau_max = 1e-3 * Tf * sps_;
        for (int frame = 0;; frame++) {
            long next_probe = pos + U;
            if (spos((double)(next_probe + K)) >= (double)y_.size()) break;
            const double fr_pred = fr + dfr;
            const double th_pred = 2 * M_PI * 0.5 * (fr + fr_pred) * Tf;
            const double tau_pred = tau + dtau;
            double qa, qb;
            cplx ha = probe_gain(next_probe, probe, &qa, fr_pred, tau_pred);
            cplx hb = probe_gain(next_probe, probe_b, &qb, fr_pred, tau_pred);
            const bool use_b = qb > qa;
            const std::vector<cplx>& pr = use_b ? probe_b : probe;
            double tau_next = tau_pred;
            if (std::max(qa, qb) >= 0.6) {
                double vl = std::abs(probe_gain(next_probe, pr, nullptr, fr_pred, tau_pred - dl));
                double vc = std::abs(use_b ? hb : ha);
                double vr = std::abs(probe_gain(next_probe, pr, nullptr, fr_pred, tau_pred + dl));
                double den = vl - 2 * vc + vr, x = 0;
                if (den < 0) x = std::max(-1.0, std::min(1.0, 0.5 * (vl - vr) / den)) * dl;
                tau_next = tau_pred + kTauA * x;
                dtau = std::max(-dtau_max, std::min(dtau_max, dtau + kTauB * x));
                if (tau_next != tau_pred) {
                    ha = probe_gain(next_probe, probe, &qa, fr_pred, tau_next);
                    hb = probe_gain(next_probe, probe_b, &qb, fr_pred, tau_next);
                }
            }
            cplx h_next = qa >= qb ? ha : hb;
            if (qb > qa) r.boundary_frames.push_back(frame);
            double qn_ = std::max(qa, qb);
            if (qn_ < 0.5) break;  // signal gone: no mini-probe where one is due
            qsum += qn_;
            qn++;
            // unwrapped rotation from the previous probe centre to this one
            double th = th_pred + std::arg(h_next * std::conj(h_prev) * std::polar(1.0, -th_pred));
            double fr_meas = th / (2 * M_PI * Tf);
            double ef = fr_meas - fr_pred;
            fr = fr_pred + kFrA * ef;
            dfr += kFrB * ef;
            // interpolate: magnitude and the rotation-removed gain linearly, phase ramp on top
            const cplx hn0 = h_next * std::polar(1.0, -th);
            const double c_prev = -0.5 * (K + 1);  // previous probe centre, from data symbol 0
            DataScrambler scr;  // D.5.1.3: initialised at the start of each data frame
            std::vector<cplx> zf;
            for (int i = 0; i < U; i++) {
                double a = ((double)i - c_prev) / Tf;
                cplx h = (h_prev * (1 - a) + hn0 * a) * std::polar(1.0, th * a);
                cplx z = sample((double)(pos + i), tau + (tau_next - tau) * a) * std::conj(h) / (std::norm(h) + 1e-12);
                if (wp->bits_per_sym > 1) { zf.push_back(z); continue; }
                z *= std::conj(psk8(scr.value3()));
                scr.shift(opt_.bpsk_scr_shifts);
                blk.push_back(std::real(z));  // Table D-III: bit 0 -> symbol 0, bit 1 -> symbol 4
            }
            if (wp->bits_per_sym > 1)  // WID 6-13: appd_qam.h
                demap_frame(wp->bits_per_sym, zf, opt_.psk_scr_shifts, opt_.qam_scale, blk, r.qam);
            h_prev = h_next;
            tau = tau_next;
            pos = next_probe + K;
            if ((int)blk.size() == nbits) {
                // D.5.3.3.2 load / D.5.3.3.3 fetch: punctured bit n sits at (n*inc) mod size
                std::vector<double> coded(nbits);
                for (int n = 0; n < nbits; n++) coded[n] = blk[(size_t)(((long)n * ip->increment) % nbits)];
                blk.clear();
                std::vector<double> s1, s2;
                if (!depuncture(coded, ip->input_bits, *wp, s1, s2)) { r.payload_skip_reason = "puncture length mismatch"; return; }
                std::vector<int> v;
                double ag = 0;
                if (code_rev < 0) {
                    double a0, a1;
                    std::vector<int> v0 = viterbi_tb(s1, s2, ConvCode::make(false), &a0);
                    std::vector<int> v1 = viterbi_tb(s1, s2, ConvCode::make(true), &a1);
                    code_rev = a1 > a0 ? 1 : 0;
                    v = code_rev ? v1 : v0;
                    ag = std::max(a0, a1);
                } else {
                    v = viterbi_tb(s1, s2, ConvCode::make(code_rev == 1), &ag);
                }
                agree_sum += ag;
                // D.5.3.2.3: pair j carries input bit (j + K-1) mod N as its newest bit
                int Nin = ip->input_bits;
                for (int i = 0; i < Nin; i++) outbits.push_back(v[((i - 6) % Nin + Nin) % Nin]);
                r.blocks_decoded++;
                if (find_eom(outbits, r)) break;
            }
        }
        r.code_reversed = code_rev;
        r.reencode_agree = r.blocks_decoded ? agree_sum / r.blocks_decoded : 0;
        r.probe_quality = qn ? qsum / qn : 0;
        long nb = r.eom ? r.eom_bit : (long)outbits.size();
        for (long i = 0; i + 8 <= nb; i += 8) {
            int b = 0;
            for (int k = 0; k < 8; k++) b = (b << 1) | outbits[i + k];  // MSB first
            r.bytes.push_back((uint8_t)b);
        }
        if (r.blocks_decoded == 0) r.payload_skip_reason = "no complete interleaver block";
    }

    static bool find_eom(const std::vector<int>& bits, RxResult& r) {
        // D.5.4.5.1: scan all decoded bits for the 32-bit EOM
        if (bits.size() < 32) return false;
        uint32_t sh = 0;
        for (size_t i = 0; i < bits.size(); i++) {
            sh = (sh << 1) | (uint32_t)bits[i];
            if (i >= 31 && sh == kEOM) {
                r.eom = true;
                r.eom_bit = (long)(i - 31);
                return true;
            }
        }
        return false;
    }
};

}  // namespace m110d

#endif
