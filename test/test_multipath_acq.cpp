/**
 * @file test_multipath_acq.cpp
 * @brief 110A acquisition through a frequency-selective channel, the forced-mode bypass,
 *        no acquisition on a tone, and MLSE_ADAPTIVE's quality on noise
 *        (docs/m110a-acq-fading.md).
 *
 * ModemTX with EOM through a static channel of three equal-power paths 0, 2 and 4 symbols
 * apart (0.83 and 1.67 ms; notches across the band), then white noise, 12 dB in 3 kHz. The
 * single-path preamble correlation of that channel is about 0.58, under BrainDecoder's 0.70
 * gate, so before the multipath search every case below failed at the preamble:
 *   - 600L, 300S, 1200S, AUTO: success, the payload exactly, the EOM;
 *   - 600L forced: the same;
 *   - a CW tone at 1500 Hz (300 Hz, four of the common segment's 75 Hz lines, from the
 *     carrier) in noise, AUTO and forced 600L: success=0;
 *   - MLSE_ADAPTIVE, 600S in white noise at 0 dB, no channel, seeds 1-2: no success with wrong
 *     bytes, and a quality figure under 20 dB. Scored on its output's own probes, which are the
 *     known symbols, every decode read 82.5 dB, and these two were successes with wrong bytes.
 *
 * Run: test_multipath_acq          (exit 0 when every case passes)
 */

#include "api/modem.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using namespace m110a::api;

static const float FS = 48000.0f;
static int passed = 0, failed = 0;

static DecodeResult quiet_decode(const Samples& s, Mode mode, Equalizer eq = Equalizer::DFE) {
    RxConfig cfg;
    cfg.sample_rate = FS;
    cfg.mode = mode;
    cfg.equalizer = eq;
    std::ofstream sink;
    std::streambuf* out = std::cout.rdbuf(sink.rdbuf());
    std::streambuf* err = std::cerr.rdbuf(sink.rdbuf());
    DecodeResult r = decode(s, cfg);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    return r;
}

static float rms(const Samples& s) {
    double acc = 0;
    for (float x : s) acc += static_cast<double>(x) * x;
    return s.empty() ? 0.0f : static_cast<float>(std::sqrt(acc / s.size()));
}

static std::vector<uint8_t> payload(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> p(n);
    for (auto& b : p) b = static_cast<uint8_t>(rng() & 0xff);
    return p;
}

static Samples tx(Mode mode, const std::vector<uint8_t>& data) {
    TxConfig t;
    t.mode = mode;
    t.sample_rate = FS;
    auto r = ModemTX(t).encode(data);
    return r.ok() ? r.value() : Samples();
}

/// Three equal-power paths at 0, 40 and 80 samples (0, 2, 4 symbols), phases 0, 100, 220 degrees,
/// applied to the real passband audio through its analytic signal (a delay and a carrier phase).
static Samples channel(const Samples& x) {
    // analytic signal by a 255-tap Hilbert FIR
    const int H = 127;
    std::vector<float> h(2 * H + 1, 0.0f);
    for (int k = -H; k <= H; k++)
        if (k % 2) h[k + H] = static_cast<float>(2.0 / (M_PI * k) * (0.54 + 0.46 * std::cos(M_PI * k / H)));
    std::vector<std::complex<float>> a(x.size());
    for (size_t n = 0; n < x.size(); n++) {
        float q = 0.0f;
        for (int k = -H; k <= H; k++) {
            const long i = static_cast<long>(n) - k;
            if (i >= 0 && i < static_cast<long>(x.size())) q += h[k + H] * x[static_cast<size_t>(i)];
        }
        a[n] = {x[n], q};
    }
    const int delay[3] = {0, 40, 80};
    const float phase[3] = {0.0f, 100.0f, 220.0f};
    Samples y(x.size() + 80, 0.0f);
    for (int p = 0; p < 3; p++) {
        const std::complex<float> g = std::polar(1.0f / std::sqrt(3.0f), phase[p] * static_cast<float>(M_PI) / 180.0f);
        for (size_t n = 0; n < x.size(); n++) y[n + delay[p]] += (g * a[n]).real();
    }
    return y;
}

/// White noise at snr_db in 3 kHz against the RMS of s, added; lead and tail of noise alone
static Samples noisy(const Samples& s, float snr_db, float lead_s, float tail_s, unsigned seed) {
    const float sigma = rms(s) * std::sqrt(8.0f / std::pow(10.0f, snr_db / 10.0f));
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, sigma);
    Samples y;
    for (size_t i = 0, k = static_cast<size_t>(lead_s * FS); i < k; i++) y.push_back(n(rng));
    for (float v : s) y.push_back(v + n(rng));
    for (size_t i = 0, k = static_cast<size_t>(tail_s * FS); i < k; i++) y.push_back(n(rng));
    return y;
}

static void check(const char* what, const DecodeResult& r, bool pass) {
    printf("%-52s success=%d mode=%s q=%.1f/%.1f bytes=%zu eom=%d ... %s\n", what, r.success ? 1 : 0,
           mode_name(r.mode).c_str(), r.quality_db, r.quality_threshold_db, r.data.size(),
           r.eom_sequence ? 1 : 0, pass ? "PASS" : "FAIL");
    fflush(stdout);
    (pass ? passed : failed)++;
}

int main() {
    char what[96];

    // 1. Through the channel: AUTO, and the forced mode for 600L
    struct Case { Mode mode; const char* tag; size_t bytes; };
    const Case cases[] = {{Mode::M600_LONG, "600L", 120}, {Mode::M300_SHORT, "300S", 60},
                          {Mode::M1200_SHORT, "1200S", 120}};
    for (const auto& c : cases) {
        const auto data = payload(c.bytes, 3);
        const Samples s = noisy(channel(tx(c.mode, data)), 12.0f, 0.3f, 1.0f, 5);
        for (Mode m : {Mode::AUTO, c.mode}) {
            if (m != Mode::AUTO && c.mode != Mode::M600_LONG) continue;
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "%s 3 paths 0/2/4 sym, 12 dB, %s", c.tag, m == Mode::AUTO ? "AUTO" : "forced");
            check(what, r, r.success && r.mode == c.mode && r.data == data && r.eom_sequence);
        }
    }

    // 2. A tone 300 Hz below the carrier, in noise: no success
    {
        Samples s(static_cast<size_t>(8.0f * FS));
        for (size_t n = 0; n < s.size(); n++) s[n] = 0.3f * std::sin(2.0f * static_cast<float>(M_PI) * 1500.0f * n / FS);
        s = noisy(s, 20.0f, 0.0f, 0.0f, 9);
        for (Mode m : {Mode::AUTO, Mode::M600_LONG}) {
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "1500 Hz tone in noise, %s", m == Mode::AUTO ? "AUTO" : "forced 600L");
            check(what, r, !r.success);
        }
    }

    // 3. MLSE_ADAPTIVE at the edge of 600S: success only with the right bytes, an honest quality
    for (unsigned seed = 1; seed <= 2; seed++) {
        const auto data = payload(89, seed);
        const Samples s = noisy(tx(Mode::M600_SHORT, data), 0.0f, 0.3f, 1.0f, 20 + seed);
        DecodeResult r = quiet_decode(s, Mode::AUTO, Equalizer::MLSE_ADAPTIVE);
        snprintf(what, sizeof what, "600S 0 dB AWGN seed %u, MLSE_ADAPTIVE", seed);
        check(what, r, (!r.success || r.data == data) && r.quality_db < 20.0f);
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
