/**
 * @file test_dfe_context.cpp
 * @brief The 110A DFE enters the data, and leaves each block-boundary probe, with its
 *        delay lines holding the symbols that precede the next output.
 *
 * The feed-forward taps span the symbols -5 to +5 around the one decided (lookahead 5),
 * and the feedback taps the 5 decisions before it. A decision on data symbol n < 5 reads
 * the last preamble symbols through both. When the lines held other preamble symbols, or
 * stale decisions, SER on data symbols 0-4 rose from 0.0% to 0.6% (flat channel, 18 dB),
 * and the same after every block-boundary probe.
 *
 * Symbol level, 2400S framing (32 data + 16 probes, 30-frame interleaver blocks), a
 * 3-frame preamble from BrainPreambleEncoder, data and probes through one continuous
 * channel at the receiver's nominal level (0.55):
 *   1. A fresh DFE with lookahead 5 passes the symbol it decides: unity center tap on x[n].
 *   2. Flat channel, Es/N0 18 dB, 1000 seeds, LMS: SER on data symbols 0-4 of frame 0
 *      at most 0.1%; on symbols 0-4 of the frames after a block-boundary probe at most
 *      0.1%, with the zero probe and with D1/D2 Walsh patterns on those probes.
 *   3. The same with NLMS (no 20x pretraining step there).
 *   4. Without a preamble (probe training only), frame 0's data decides at most 0.5% SER
 *      at 18 dB.
 *   5. After preamble training on a flat channel (20x LMS step, or the least-squares fit
 *      frame_dfe_equalize uses), DFE::is_converged() is true.
 *   6. BrainDecoder on this modem's own 2400S TX: pre_data_symbols holds the last
 *      preamble frame's zero segment, each symbol nearest PSK8[pscramble[i]].
 *   7. Decode quality (the probe figure the success gate reads): within 0.3 dB of the
 *      a-priori MMSE figure 10 log10(1 + Es/N0) on a flat channel at 3 and 7 dB, and
 *      above 15 dB on a precursor-ISI channel at 18 dB.
 *
 * Run: test_dfe_context            (exit 0 when every check passes)
 */

#include "api/modem.h"
#include "equalizer/frame_dfe.h"
#include "m110a/brain_decoder.h"
#include "m110a/brain_preamble.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace m110a;

static int passed = 0, failed = 0;

static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    (ok ? passed : failed)++;
}

static const complex_t P8[8] = {
    {1.0f, 0.0f}, {0.70710678f, 0.70710678f}, {0.0f, 1.0f}, {-0.70710678f, 0.70710678f},
    {-1.0f, 0.0f}, {-0.70710678f, -0.70710678f}, {0.0f, -1.0f}, {0.70710678f, -0.70710678f}};

/// Deterministic generator: xorshift32 and Box-Muller, the same on every platform.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float gauss() {
        float u1 = uniform(), u2 = uniform();
        if (u1 < 1e-7f) u1 = 1e-7f;
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
    }
};

struct Burst {
    std::vector<complex_t> preamble;   // first preamble frame, as received (480)
    std::vector<complex_t> pre_data;   // last 32 preamble symbols, as received
    std::vector<complex_t> data;       // data + probes, as received
    std::vector<int> sym;              // transmitted data tribit per data-stream symbol, -1 on probes
};

static const int U = 32, K = 16, PL = U + K, BF = 30;

/// One 2400S burst of `frames` frames through a flat AWGN channel at es_n0_db.
/// walsh: the two probes before each new block carry D patterns instead of zeros.
static Burst make_burst(uint32_t seed, int frames, float es_n0_db, bool walsh, bool isi = false) {
    static const int W[2][8] = {{0, 0, 4, 4, 4, 4, 0, 0}, {0, 0, 0, 0, 4, 4, 4, 4}};
    Rng g(seed);
    BrainPreambleEncoder enc;
    std::vector<complex_t> tx = enc.encode(10, false);  // 2400S, 3 frames
    const size_t P = tx.size();
    Burst b;
    DataScramblerFixed scr;
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < U; i++) {
            int t = static_cast<int>(g.next() % 8);
            b.sym.push_back(t);
            tx.push_back(P8[t]);
            scr.next();
        }
        const int wb = (f % BF) - (BF - 2);
        for (int i = 0; i < K; i++) {
            int t = scr.next();
            if (walsh && wb >= 0) t += W[wb][i % 8];
            b.sym.push_back(-1);
            tx.push_back(P8[t & 7]);
        }
    }
    const float sigma = std::sqrt(0.5f * std::pow(10.0f, -es_n0_db / 10.0f));
    std::vector<complex_t> rx(tx.size());
    for (size_t i = 0; i < tx.size(); i++) {
        complex_t a = tx[i];
        if (isi) {  // precursor 0.38, postcursors 0.20 and 0.22
            if (i + 1 < tx.size()) a += 0.38f * tx[i + 1];
            if (i >= 1) a += 0.20f * tx[i - 1];
            if (i >= 2) a += 0.22f * tx[i - 2];
        }
        rx[i] = 0.55f * (a + complex_t(g.gauss(), g.gauss()) * sigma);
    }
    b.preamble.assign(rx.begin(), rx.begin() + 480);
    b.pre_data.assign(rx.begin() + P - 32, rx.begin() + P);
    b.data.assign(rx.begin() + P, rx.end());
    return b;
}

static bool wrong(complex_t y, int t) {
    return std::fabs(std::arg(y * std::conj(P8[t]))) > 0.39269908f;
}

struct Ser { double f0_04, f0_531, after_boundary_04; };

/// SER on data symbols 0-4 and 5-31 of frame 0, and on symbols 0-4 of the frames
/// after a block-boundary probe (frames 29 and 30 of 31).
static Ser run_ser(int seeds, float es_n0_db, bool nlms, bool walsh, bool with_preamble) {
    long e04 = 0, e531 = 0, eb = 0;
    const int frames = BF + 1;
    for (int s = 1; s <= seeds; s++) {
        Burst b = make_burst(static_cast<uint32_t>(s), frames, es_n0_db, walsh);
        FrameDfeOutput r = with_preamble
            ? frame_dfe_equalize(b.data, U, K, BF, b.preamble, nlms, b.pre_data)
            : frame_dfe_equalize(b.data, U, K, BF, {}, nlms, {});
        for (int f = 0; f < frames; f++) {
            for (int i = 0; i < U; i++) {
                const int k = f * PL + i;
                const bool e = wrong(r.symbols[k], b.sym[k]);
                if (f == 0) (i < 5 ? e04 : e531) += e;
                if ((f == BF - 1 || f == BF) && i < 5) eb += e;
            }
        }
    }
    return {e04 / (5.0 * seeds), e531 / (27.0 * seeds), eb / (10.0 * seeds)};
}

/// Mean decode quality (dB) as ModemRX scores the DFE: the probe error of the DFE's
/// output before it adapts to each probe, over the frames in the first 1440 symbols.
static double mean_quality(int seeds, float es_n0_db, bool isi) {
    double sum = 0.0;
    for (int s = 1; s <= seeds; s++) {
        Burst b = make_burst(static_cast<uint32_t>(1000 + s), BF + 1, es_n0_db, false, isi);
        FrameDfeOutput r = frame_dfe_equalize(b.data, U, K, BF, b.preamble, false, b.pre_data, 1440);
        double err = 0.0, ref = 0.0;
        for (const auto& f : r.probe_frames) { err += f[1]; ref += f[2]; }
        sum += 10.0 * std::log10(ref / err);
    }
    return sum / seeds;
}

int main() {
    char what[256];

    // 1. Unity center tap on the symbol decided
    {
        DFE::Config c;
        c.lookahead = c.ff_taps / 2;
        c.leak = 0.0f;
        DFE d(c);
        std::vector<complex_t> x(40);
        for (size_t i = 0; i < x.size(); i++) x[i] = P8[(i * 3 + 1) % 8];
        for (int k = 0; k < c.lookahead; k++) d.push(x[k]);
        double err = 0.0;
        for (size_t n = 0; n + c.lookahead < x.size(); n++) {
            complex_t y = d.process(x[n + c.lookahead], complex_t(0, 0), false);
            err += std::norm(y - x[n]);
        }
        std::snprintf(what, sizeof what, "fresh DFE, lookahead 5: y[n] = x[n] (squared error %.2e)", err);
        check(err < 1e-6, what);
    }

    // 2. LMS, flat 18 dB
    for (bool walsh : {false, true}) {
        Ser s = run_ser(1000, 18.0f, false, walsh, true);
        std::snprintf(what, sizeof what,
                      "LMS flat 18 dB, %s boundary probes: SER data 0-4 %.4f%% (<= 0.1%%), 5-31 %.4f%%, "
                      "after boundary 0-4 %.4f%% (<= 0.1%%)",
                      walsh ? "Walsh" : "zero", 100 * s.f0_04, 100 * s.f0_531, 100 * s.after_boundary_04);
        check(s.f0_04 <= 0.001 && s.after_boundary_04 <= 0.001, what);
    }

    // 3. NLMS, flat 18 dB
    {
        Ser s = run_ser(1000, 18.0f, true, true, true);
        std::snprintf(what, sizeof what,
                      "NLMS flat 18 dB: SER data 0-4 %.4f%% (<= 0.1%%), after boundary 0-4 %.4f%% (<= 0.1%%)",
                      100 * s.f0_04, 100 * s.after_boundary_04);
        check(s.f0_04 <= 0.001 && s.after_boundary_04 <= 0.001, what);
    }

    // 4. No preamble: probe training only
    {
        Ser s = run_ser(300, 18.0f, false, false, false);
        std::snprintf(what, sizeof what, "no preamble, flat 18 dB: SER frame 0 data 0-4 %.4f%%, 5-31 %.4f%% (<= 0.5%%)",
                      100 * s.f0_04, 100 * s.f0_531);
        check(s.f0_04 <= 0.005 && s.f0_531 <= 0.005, what);
    }

    // 5. is_converged() after preamble training on a flat channel, by the 20x LMS step and
    //    by the least-squares fit. With the unity tap on x[n+5] the training moved the
    //    cursor to an edge tap, the center tap went to about 0, and is_converged() was
    //    false on every channel.
    {
        Burst b = make_burst(7, 2, 18.0f, false);
        DFE::Config c;
        c.lookahead = c.ff_taps / 2;
        DFE d(c);
        d.set_step(0.1f, 0.05f);
        BrainPreambleEncoder enc;
        std::vector<complex_t> ref = enc.encode_frame(10, 2);
        for (int pass = 0; pass < 2; pass++) {
            d.clear_lines();
            for (int k = 0; k < c.lookahead; k++) d.push(b.preamble[k]);
            for (int n = 0; n < 288; n++) d.process(b.preamble[n + c.lookahead], ref[n], true);
        }
        const int center = c.ff_taps / 2;
        std::snprintf(what, sizeof what, "DFE::is_converged() after 20x-step LMS on a flat channel (|center tap| %.2f)",
                      std::abs(d.ff_taps()[center]));
        check(d.is_converged(), what);
        DFE ls(c);
        const bool fit = ls.fit_least_squares(b.preamble, ref, c.lookahead, 288);
        std::snprintf(what, sizeof what, "DFE::is_converged() after the least-squares fit on a flat channel (|center tap| %.2f)",
                      std::abs(ls.ff_taps()[center]));
        check(fit && ls.is_converged(), what);
    }

    // 6. BrainDecoder: the zero segment before the data
    {
        using namespace m110a::api;
        TxConfig t;
        t.mode = Mode::M2400_SHORT;
        t.sample_rate = 48000.0f;
        t.include_eom = true;
        const std::string text = "DE AAM402 THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789";
        auto enc = ModemTX(t).encode(std::vector<uint8_t>(text.begin(), text.end()));
        BrainDecoderConfig cfg;
        cfg.sample_rate = 48000.0f;
        cfg.unknown_data_len = U;
        cfg.known_data_len = K;
        cfg.preamble_symbols = 1440;
        BrainDecoder dec(cfg);
        std::vector<float> pcm = enc.ok() ? enc.value() : std::vector<float>();
        auto res = dec.decode(pcm);
        int agree = 0;
        if (res.pre_data_symbols.size() == 32) {
            for (int i = 0; i < 32; i++) {
                if (!wrong(res.pre_data_symbols[i], brain::pscramble[i])) agree++;
            }
        }
        std::snprintf(what, sizeof what, "BrainDecoder pre_data_symbols: %zu symbols, %d of 32 on the zero segment",
                      res.pre_data_symbols.size(), agree);
        check(res.pre_data_symbols.size() == 32 && agree == 32, what);
    }

    // 7. Decode quality, the figure the success gate reads. On a flat channel it must
    //    stay within 0.3 dB of the a-priori MMSE figure 10 log10(1 + Es/N0): the gate's
    //    thresholds were calibrated on flat-channel decodes, and a 20x LMS step on the
    //    preamble left tap noise in the quality span that cost 0.3-0.6 dB and put exact
    //    decodes under the gate. On the precursor-ISI channel at 18 dB the two-sided
    //    taps must keep it above 15 dB (causal taps: about 8 dB).
    for (float snr : {3.0f, 7.0f}) {
        const double q = mean_quality(200, snr, false);
        const double mmse = 10.0 * std::log10(1.0 + std::pow(10.0, snr / 10.0));
        std::snprintf(what, sizeof what, "quality, flat %.0f dB: mean %.2f dB, MMSE %.2f dB (>= MMSE - 0.3)",
                      snr, q, mmse);
        check(q >= mmse - 0.3, what);
    }
    {
        const double q = mean_quality(100, 18.0f, true);
        std::snprintf(what, sizeof what, "quality, precursor ISI 18 dB: mean %.2f dB (>= 15)", q);
        check(q >= 15.0, what);
    }

    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
