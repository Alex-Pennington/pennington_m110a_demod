// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file harris_score.cpp
 * @brief Score MIL-STD-188-110A waveform candidates on one recorded message.
 *
 *   harris_score WAV START_S END_S [--eq L MU_TRAIN MU_DATA] [--dump CAND OUT.bin]
 *
 * WAV is 48 kHz 16-bit audio; START_S..END_S brackets one message (m110a_rx's
 * start_s, and the next message's start or the end of the file). It finds the
 * preamble with BrainDecoder and takes the data symbols after the last frame.
 *
 * Without --eq, carrier phase is tracked on the probes and interpolated across the
 * data. With --eq, a T/2 fractionally spaced NLMS equalizer (2L+1 taps) with a
 * second-order decision-directed PLL is trained on the three known preamble frames,
 * then on the probes and decision-directed on the data; the carrier is first
 * corrected by the slope of the tracked probe phases.
 *
 * For frame structures 20/20 and 32/16 it reports the probe symbol error rate and
 * the probe segments that carry a Walsh D pattern instead of zeros (110C 5.3.2.3.7:
 * D1 then D2 in the two known segments before each new interleaver block). For each
 * candidate (600 BPSK, 1200 QPSK, 2400 8PSK x bypassed, short, long interleave) it
 * soft-demaps, deinterleaves, Viterbi-decodes (K=7, rate 1/2), re-encodes the decoded
 * bits and reports the coded-bit disagreement per interleaver block: the hard-decision
 * Viterbi path metric of the block as a fraction of its coded bits. It also reports
 * where the EOM 4B65A5B2 sits in the decoded bit stream.
 *
 * --dump CAND OUT writes CAND's decoded bits up to the EOM, in the order they leave
 * the Viterbi decoder, packed LSB first (bit k -> byte k/8, bit k%8), the packing of
 * m110a_rx hex=. A final "summary" line gives the dumped candidate's figures.
 *
 * test/harris/score.py drives it over the Harris test set (docs/harris-melpe.md).
 */
#include "m110a/brain_decoder.h"
#include "m110a/brain_preamble.h"
#include "modem/scrambler_fixed.h"
#include "modem/gray_code.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace m110a;
typedef std::complex<float> cf;

static const cf PSK8[8] = {{1,0},{0.70710678f,0.70710678f},{0,1},{-0.70710678f,0.70710678f},
                           {-1,0},{-0.70710678f,-0.70710678f},{0,-1},{0.70710678f,-0.70710678f}};

static bool read_wav(const char* path, std::vector<float>& out, int& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[12];
    f.read(riff, 12);
    if (memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;
    int ch = 0, bits = 0; rate = 0;
    while (f) {
        char id[4]; uint32_t sz;
        f.read(id, 4); f.read(reinterpret_cast<char*>(&sz), 4);
        if (!f) break;
        if (!memcmp(id, "fmt ", 4)) {
            std::vector<char> b(sz); f.read(b.data(), sz);
            ch = *reinterpret_cast<uint16_t*>(&b[2]);
            rate = *reinterpret_cast<uint32_t*>(&b[4]);
            bits = *reinterpret_cast<uint16_t*>(&b[14]);
        } else if (!memcmp(id, "data", 4)) {
            if (bits != 16 || ch < 1) return false;
            std::vector<int16_t> raw(sz / 2);
            f.read(reinterpret_cast<char*>(raw.data()), sz);
            for (size_t i = 0; i < raw.size(); i += ch) out.push_back(raw[i] / 32768.0f);
            return true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    return false;
}

struct Structure { int U, K; const char* name; };
struct Cand { const char* name; int bps_sym; Structure st; InterleaverParams il; };

// Walsh pattern value (0 or 4) of D at position k of a 16-symbol known segment
static int dpat(int d, int k) { return k < 16 ? brain::psymbol[d][k % 8] : 0; }

struct Tracked {
    std::vector<cf> z;          // data symbols, derotated and descrambled, in order
    std::vector<float> probe_phase;
    int probe_errors = 0, probe_total = 0, d_probes = 0;
};

static float wrap(float a) { while (a > M_PI) a -= 2 * M_PI; while (a < -M_PI) a += 2 * M_PI; return a; }

// Probe analysis + phase tracking for one frame structure.
static Tracked track(const std::vector<cf>& s, const Structure& st, bool report, int max_frames) {
    const int L = st.U + st.K;
    DataScramblerFixed scr;
    std::vector<int> sq(160);
    for (int i = 0; i < 160; i++) sq[i] = scr.next();
    int nf = std::min<int>(s.size() / L, max_frames);
    std::vector<cf> c(nf);
    for (int f = 0; f < nf; f++) {
        cf a(0, 0);
        for (int i = 0; i < st.K; i++) {
            int n = f * L + st.U + i;
            a += s[n] * std::conj(PSK8[sq[n % 160]]);
        }
        c[f] = a;
    }
    // amplitude reference: median |c|/K
    std::vector<float> m(nf);
    for (int f = 0; f < nf; f++) m[f] = std::abs(c[f]) / st.K;
    std::vector<float> ms = m;
    std::nth_element(ms.begin(), ms.begin() + ms.size() / 2, ms.end());
    float med = ms.empty() ? 1 : ms[ms.size() / 2];
    // phase: unwrap over frames whose probe looks like zeros; interpolate the rest
    std::vector<float> ph(nf, 0);
    std::vector<bool> good(nf);
    float last = 0;
    for (int f = 0; f < nf; f++) {
        good[f] = m[f] > 0.6f * med;
        if (good[f]) { last = last + wrap(std::arg(c[f]) - last); ph[f] = last; }
    }
    for (int f = 0; f < nf; f++) if (!good[f]) {
        int a = f - 1, b = f + 1;
        while (a >= 0 && !good[a]) a--;
        while (b < nf && !good[b]) b++;
        if (a >= 0 && b < nf) ph[f] = ph[a] + (ph[b] - ph[a]) * (f - a) / float(b - a);
        else if (a >= 0) ph[f] = ph[a]; else if (b < nf) ph[f] = ph[b];
    }
    Tracked t;
    t.probe_phase = ph;
    int perr = 0, ptot = 0, nd = 0;
    std::vector<std::string> dlist;
    for (int f = 0; f < nf; f++) {
        cf rot = std::polar(1.0f, -ph[f]);
        // D pattern test on this probe segment
        float best = -1e9; int bd = 0; float pw = 0;
        for (int i = 0; i < st.K; i++) pw += std::norm(s[f * L + st.U + i]);
        for (int d = 0; d < 8; d++) {
            cf a(0, 0);
            for (int i = 0; i < st.K; i++) {
                int n = f * L + st.U + i;
                a += s[n] * rot * std::conj(PSK8[(sq[n % 160] + dpat(d, i)) & 7]);
            }
            float v = a.real() / std::sqrt(pw * st.K + 1e-12f);
            if (v > best) { best = v; bd = d; }
        }
        if (bd != 0) {
            nd++;
            if (report && dlist.size() < 40) {
                char b[96];
                snprintf(b, sizeof b, "f%d@%d:D%d(%.2f)", f, f * L + st.U, bd, best);
                dlist.push_back(b);
            }
        } else {
            for (int i = 0; i < st.K; i++) {
                int n = f * L + st.U + i;
                cf z = s[n] * rot * std::conj(PSK8[sq[n % 160]]);
                int pos = ((int)std::lround(std::arg(z) * 4 / M_PI) + 8) % 8;
                ptot++; if (pos != 0) perr++;
            }
        }
        // data symbols of frame f: phase midway between the previous probe and this one
        float p0 = f > 0 ? ph[f - 1] : ph[f];
        for (int i = 0; i < st.U; i++) {
            int n = f * L + i;
            float p = p0 + (ph[f] - p0) * (i + st.K / 2.0f + 0.5f) / float(L);
            t.z.push_back(s[n] * std::polar(1.0f, -p) * std::conj(PSK8[sq[n % 160]]));
        }
    }
    t.probe_errors = perr; t.probe_total = ptot; t.d_probes = nd;
    if (report) {
        printf("  structure %s: frames %d, probe |corr| median %.3f, zero-probe symbol errors %d/%d (%.2f%%), D-pattern probes %d\n",
               st.name, nf, med, perr, ptot, ptot ? 100.0 * perr / ptot : 0.0, nd);
        if (!dlist.empty()) {
            printf("    D probes (frame@symbol:Dd(corr)):");
            for (auto& x : dlist) printf(" %s", x.c_str());
            printf("\n");
        }
    }
    return t;
}

// labels: position -> bits (MSB first), for 1, 2 or 3 bits per symbol
static int label(int bps, int pos) {
    if (bps == 1) return pos == 0 ? 0 : 1;          // positions 0, 4
    if (bps == 2) return INV_MGD2[pos / 2];          // positions 0, 2, 4, 6
    return INV_MGD3[pos];
}

static std::vector<soft_bit_t> demap(const std::vector<cf>& z, int bps) {
    const int step = bps == 1 ? 4 : bps == 2 ? 2 : 1;
    // amplitude: mean |z|
    double am = 0; for (auto& v : z) am += std::abs(v); am /= std::max<size_t>(1, z.size());
    std::vector<soft_bit_t> out;
    out.reserve(z.size() * bps);
    for (auto v : z) {
        cf u = v / float(am + 1e-12);
        float d0[3] = {1e9f, 1e9f, 1e9f}, d1[3] = {1e9f, 1e9f, 1e9f};
        for (int pos = 0; pos < 8; pos += step) {
            float d = std::norm(u - PSK8[pos]);
            int lab = label(bps, pos);
            for (int b = 0; b < bps; b++) {
                int bit = (lab >> (bps - 1 - b)) & 1;
                if (bit) d1[b] = std::min(d1[b], d); else d0[b] = std::min(d0[b], d);
            }
        }
        for (int b = 0; b < bps; b++) {
            float llr = (d1[b] - d0[b]) * 40.0f;   // + = bit 0
            out.push_back((soft_bit_t)std::max(-127.0f, std::min(127.0f, llr)));
        }
    }
    return out;
}

// EOM 4B65A5B2, most significant digit first
static long find_eom(const std::vector<uint8_t>& bits) {
    const uint32_t eom = 0x4B65A5B2u;
    for (size_t p = 0; p + 32 <= bits.size(); p++) {
        uint32_t v = 0;
        for (int i = 0; i < 32; i++) v = (v << 1) | bits[p + i];
        if (v == eom) return (long)p;
    }
    return -1;
}

struct Score { int blocks, clean; double med_ber, first_ber, msg_ber; int msg_blocks; long eom; std::vector<uint8_t> bits; std::vector<double> ber; };

static Score run(const Tracked& t, const Cand& c) {
    Score sc{};
    auto soft = demap(t.z, c.bps_sym);
    MultiModeInterleaver il(c.il);
    const int B = il.block_size();
    int nb = soft.size() / B;
    std::vector<soft_bit_t> di;
    for (int b = 0; b < nb; b++) {
        std::vector<soft_bit_t> blk(soft.begin() + b * B, soft.begin() + (b + 1) * B);
        auto d = il.deinterleave(blk);
        di.insert(di.end(), d.begin(), d.end());
    }
    ViterbiDecoder v;
    std::vector<uint8_t> bits;
    v.decode_block(di, bits, true);
    bits.resize(di.size() / 2);
    ConvEncoder enc;
    std::vector<uint8_t> re;
    enc.encode(bits, re, false);
    for (int b = 0; b < nb; b++) {
        int dis = 0;
        for (int i = b * B; i < (b + 1) * B; i++) {
            int hard = di[i] < 0 ? 1 : 0;
            if (hard != re[i]) dis++;
        }
        sc.ber.push_back(double(dis) / B);
    }
    sc.blocks = nb;
    for (double x : sc.ber) if (x < 0.10) sc.clean++;
    std::vector<double> s2 = sc.ber;
    if (!s2.empty()) {
        std::nth_element(s2.begin(), s2.begin() + s2.size() / 2, s2.end());
        sc.med_ber = s2[s2.size() / 2];
        sc.first_ber = sc.ber[0];
    }
    sc.eom = find_eom(bits);
    // mean disagreement over the blocks wholly before the EOM (the message itself)
    sc.msg_blocks = sc.eom >= 0 ? int(std::min<long>(nb, (sc.eom * 2) / B)) : 0;
    double acc = 0;
    for (int b = 0; b < sc.msg_blocks; b++) acc += sc.ber[b];
    sc.msg_ber = sc.msg_blocks ? acc / sc.msg_blocks : -1;
    sc.bits = bits;
    return sc;
}

// Residual carrier offset from the slope of the tracked probe phases (Hz).
static double probe_slope_hz(const Tracked& t, const Structure& st) {
    int n = t.probe_phase.size();
    if (n < 4) return 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int f = 0; f < n; f++) { sx += f; sy += t.probe_phase[f]; sxx += double(f) * f; sxy += f * t.probe_phase[f]; }
    double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);   // rad per frame
    return slope / (2 * M_PI * (st.U + st.K) / 2400.0);
}

// Known preamble frame (BrainPreambleEncoder::encode_frame with explicit D1, D2)
static std::vector<int> preamble_frame(int d1, int d2, int countdown) {
    std::vector<int> s;
    int k = 0;
    for (int seg = 0; seg < 9; seg++)
        for (int i = 0; i < 32; i++, k++) s.push_back((brain::psymbol[brain::p_c_seq[seg]][i % 8] + brain::pscramble[k % 32]) % 8);
    for (int d : {d1, d2})
        for (int i = 0; i < 32; i++) s.push_back((brain::psymbol[d][i % 8] + brain::pscramble[i]) % 8);
    int cd[3] = {((countdown >> 4) & 3) + 4, ((countdown >> 2) & 3) + 4, (countdown & 3) + 4};
    k = 0;
    for (int seg = 0; seg < 3; seg++)
        for (int i = 0; i < 32; i++, k++) s.push_back((brain::psymbol[cd[seg]][i % 8] + brain::pscramble[k % 32]) % 8);
    for (int i = 0; i < 32; i++) s.push_back((brain::psymbol[0][i % 8] + brain::pscramble[i]) % 8);
    return s;
}

// T/2 fractionally spaced NLMS equalizer: trained on the 3 known preamble frames, then on
// probes (zeros, or D1/D2 in the last two probe segments of each 1440-symbol block) and
// decision-directed on data. Returns descrambled data symbols like track().
static Tracked fse(const std::vector<float>& seg, double foff, int start, int d1, int d2,
                   const Structure& st, int bps, int L, float mu_t, float mu_d, float& probe_evm_db) {
    const int sps = 20;
    std::vector<cf> bb(seg.size());
    double ph = 0, inc = 2 * M_PI * (1800.0 + foff) / 48000.0;
    for (size_t i = 0; i < seg.size(); i++) { bb[i] = cf(seg[i] * std::cos(ph), -seg[i] * std::sin(ph)); ph += inc; }
    auto taps = generate_srrc_taps(0.35f, 6, float(sps));
    int half = taps.size() / 2;
    auto filt = [&](long n) {
        cf a(0, 0);
        for (size_t j = 0; j < taps.size(); j++) { long k = n - half + j; if (k >= 0 && k < (long)bb.size()) a += bb[k] * taps[j]; }
        return a;
    };
    const int N = 2 * L + 1;
    std::vector<cf> w(N, cf(0, 0));
    w[L] = cf(1, 0);
    auto input = [&](long n, std::vector<cf>& x) {
        for (int k = 0; k < N; k++) x[k] = filt(start + n * sps + (k - L) * (sps / 2));
    };
    DataScramblerFixed scr;
    std::vector<int> sq(160);
    for (int i = 0; i < 160; i++) sq[i] = scr.next();
    std::vector<cf> x(N);
    // Second-order decision-directed PLL after the equalizer: yr = y * exp(-j theta).
    float theta = 0, nu = 0;
    const float pll_a = 0.03f, pll_b = 0.0005f;
    auto adapt = [&](const cf& ref, const cf& yr, float mu) {
        float e_ph = std::arg(yr * std::conj(ref));
        nu += pll_b * e_ph;
        theta += pll_a * e_ph + nu;
        cf e = (ref - yr) * std::polar(1.0f, theta);
        float p = 1e-6f;
        for (auto& v : x) p += std::norm(v);
        for (int k = 0; k < N; k++) w[k] += mu / p * e * std::conj(x[k]);
    };
    auto out = [&]() { cf y(0, 0); for (int k = 0; k < N; k++) y += w[k] * x[k]; return y * std::polar(1.0f, -theta); };
    // preamble: frames countdown 2, 1, 0 -> 1440 known symbols; two passes
    std::vector<int> pre;
    for (int c = 2; c >= 0; c--) { auto f = preamble_frame(d1, d2, c); pre.insert(pre.end(), f.begin(), f.end()); }
    // scale: initial w gives |y| ~ |filtered|; normalise target amplitude to the preamble rms
    double pr = 0;
    for (int n = 0; n < 288; n++) { input(n, x); pr += std::norm(x[L]); }
    float amp = std::sqrt(pr / 288);
    for (auto& v : w) v /= amp;
    for (int pass = 0; pass < 1; pass++)
        for (int n = 0; n < (int)pre.size(); n++) { input(n, x); adapt(PSK8[pre[n]], out(), mu_t); }
    const long data0 = 1440;
    const long nsym = (long(seg.size()) - start) / sps - data0 - 8;
    const int Lf = st.U + st.K;
    const int blk_frames = 1440 / Lf;
    const int step = bps == 1 ? 4 : bps == 2 ? 2 : 1;
    Tracked t;
    double pe = 0, pp = 0;
    for (long n = 0; n < nsym; n++) {
        input(data0 + n, x);
        cf y = out();
        int f = n / Lf, i = n % Lf;
        int s = sq[n % 160];
        if (i >= st.U) {
            int k = i - st.U, fb = f % blk_frames, d = 0;
            if (fb == blk_frames - 2) d = dpat(d1, k);
            else if (fb == blk_frames - 1) d = dpat(d2, k);
            cf ref = PSK8[(s + d) & 7];
            pe += std::norm(ref - y); pp += 1;
            adapt(ref, y, mu_t);
        } else {
            cf z = y * std::conj(PSK8[s]);
            int pos = ((int)std::lround(std::arg(z) * 4 / M_PI / step) * step + 8) % 8;
            t.z.push_back(z);
            adapt(PSK8[(pos + s) & 7], y, mu_d);
        }
    }
    probe_evm_db = pp > 0 ? float(10 * std::log10(pp / pe)) : 0;
    return t;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: harris_score WAV START_S END_S [--eq L MU_TRAIN MU_DATA] [--dump CAND OUT] [--frames N]\n"); return 2; }
    std::vector<float> x; int rate;
    if (!read_wav(argv[1], x, rate) || rate != 48000) { fprintf(stderr, "bad wav\n"); return 2; }
    double t0 = atof(argv[2]), t1 = atof(argv[3]);
    std::string dump_cand, dump_out;
    int max_frames = 1 << 30;
    int eq_L = 0; float eq_mu_t = 0.02f, eq_mu_d = 0.005f;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 2 < argc) { dump_cand = argv[i + 1]; dump_out = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--eq") && i + 3 < argc) {
            eq_L = atoi(argv[i + 1]); eq_mu_t = atof(argv[i + 2]); eq_mu_d = atof(argv[i + 3]); i += 3;
        }
    }
    long a0 = std::max<long>(0, long((t0 - 0.04) * rate)), a1 = std::min<long>(x.size(), long(t1 * rate));
    std::vector<float> seg(x.begin() + a0, x.begin() + a1);
    double rms = 0; for (float v : seg) rms += v * v; rms = std::sqrt(rms / seg.size());
    for (auto& v : seg) v *= float((7000.0 / 32768.0) / (rms + 1e-12));

    BrainDecoderConfig cfg;
    cfg.sample_rate = 48000;
    cfg.preamble_symbols = 1440;
    cfg.freq_search_step = 0.5f;
    BrainDecoder dec(cfg);
    std::streambuf* old = std::cout.rdbuf();
    std::ostringstream sink; std::cout.rdbuf(sink.rdbuf());
    auto r = dec.decode(seg);
    std::cout.rdbuf(old);
    printf("%s [%.3f, %.3f]: preamble corr %.3f, foff %+.2f Hz, D1=%d D2=%d countdown %d, data symbols %zu\n",
           argv[1], t0, t1, r.correlation, r.freq_offset_hz, r.d1, r.d2, r.countdown, r.data_symbols.size());
    if (!r.preamble_found) return 1;

    const Structure S20{20, 20, "20/20"}, S32{32, 16, "32/16"};
    Tracked t20 = track(r.data_symbols, S20, true, max_frames);
    Tracked t32 = track(r.data_symbols, S32, true, max_frames);
    if (eq_L > 0) {
        bool is32 = (r.d1 == 7 && r.d2 == 7);
        const Structure& st = is32 ? S32 : S20;
        double foff = r.freq_offset_hz + probe_slope_hz(is32 ? t32 : t20, st);
        int bps = is32 ? 3 : (r.d2 == 6 ? 2 : 1);
        float evm;
        Tracked te = fse(seg, foff, r.start_sample, r.d1, r.d2, st, bps, eq_L, eq_mu_t, eq_mu_d, evm);
        printf("  FSE: carrier %+.3f Hz (preamble %+.2f + probe slope), %d T/2 taps, mu %.3f/%.3f, probe SNR %.1f dB, data symbols %zu\n",
               foff, r.freq_offset_hz, 2 * eq_L + 1, eq_mu_t, eq_mu_d, evm, te.z.size());
        (is32 ? t32 : t20) = te;
    }

    const InterleaverParams BYP600{40, 18, 0, 0, 0}, SH600{40, 18, 9, 1, 36}, LG600{40, 144, 9, 127, 288};
    const InterleaverParams BYP1200{40, 36, 0, 0, 0}, SH1200{40, 36, 9, 19, 36}, LG1200{40, 288, 9, 271, 288};
    const InterleaverParams BYP2400{40, 72, 0, 0, 0}, SH2400{40, 72, 9, 55, 30}, LG2400{40, 576, 9, 559, 240};
    const Cand cands[] = {
        {"600-bypass", 1, S20, BYP600}, {"600-short", 1, S20, SH600}, {"600-long", 1, S20, LG600},
        {"1200-bypass", 2, S20, BYP1200}, {"1200-short", 2, S20, SH1200}, {"1200-long", 2, S20, LG1200},
        {"2400-bypass", 3, S32, BYP2400}, {"2400-short", 3, S32, SH2400}, {"2400-long", 3, S32, LG2400},
    };
    printf("  %-12s %6s %6s %9s %9s %8s %s\n", "candidate", "blocks", "clean", "ber_blk0", "ber_med", "eom_bit", "per-block coded-bit disagreement");
    for (const auto& c : cands) {
        const Tracked& t = c.st.U == 20 ? t20 : t32;
        Score sc = run(t, c);
        printf("  %-12s %6d %6d %9.4f %9.4f %8ld ", c.name, sc.blocks, sc.clean, sc.first_ber, sc.med_ber, sc.eom);
        for (size_t i = 0; i < sc.ber.size() && i < 24; i++) printf(" %.3f", sc.ber[i]);
        printf("\n");
        if (dump_cand == c.name) {
            // bits as they leave the Viterbi decoder, packed LSB first (as m110a_rx hex=), up to the EOM
            size_t n = sc.eom >= 0 ? size_t(sc.eom) : sc.bits.size();
            std::vector<uint8_t> bytes((n + 7) / 8, 0);
            for (size_t i = 0; i < n; i++) if (sc.bits[i]) bytes[i / 8] |= 1 << (i % 8);
            std::ofstream o(dump_out, std::ios::binary);
            o.write(reinterpret_cast<char*>(bytes.data()), bytes.size());
            printf("  wrote %zu bits (%zu bytes) to %s\n", n, bytes.size(), dump_out.c_str());
            printf("summary cand=%s d1=%d d2=%d bits=%zu eom_bit=%ld msg_blocks=%d msg_ber=%.5f "
                   "probe_errors=%d probe_total=%d d_probes=%d\n",
                   c.name, r.d1, r.d2, n, sc.eom, sc.msg_blocks, sc.msg_ber,
                   t.probe_errors, t.probe_total, t.d_probes);
        }
    }
    return 0;
}
