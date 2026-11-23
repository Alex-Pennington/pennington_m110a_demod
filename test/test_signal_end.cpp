/**
 * @file test_signal_end.cpp
 * @brief The bytes follow the signal, and success is never reported for noise
 *        or for a forced mode the signal does not carry.
 *
 * This modem's own TX (ModemTX, no EOM: the data, a 6-bit encoder tail, zeros to the end
 * of its interleaver block; payloads are one byte short of whole blocks so the tail fits),
 * then white noise at the signal's RMS:
 *   - whole blocks, then 5 s of noise: AUTO and the forced mode must give success, the
 *     payload, and at most the one padding byte of the last block, no bytes from the noise;
 *   - 600S cut 1.5 blocks before its end (a burst that stops without EOM), then 5 s of
 *     noise: success and exactly the 2 whole blocks the signal covers (90 bytes); the
 *     half block's bytes are not reported;
 *   - 300L stopping 1.2 s into its first block, then noise: no whole block, success=0, no bytes;
 *   - 300L, 2 blocks, the signal off for 1 s mid-message under noise: the whole
 *     message, exact (a fade is not the end of the signal);
 *   - a 300L transmission from its last 3 preamble frames on (what m110a_rx hands
 *     ModemRX for a forced short mode, counting back from the last frame it saw), forced
 *     to 150S, 300S and 600S, which share 300L's frame and probes: success=0;
 *   - 30 s of noise alone, AUTO and every forced mode: success=0.
 *
 * Run: test_signal_end            (exit 0 when every case passes)
 */

#include "api/modem.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a::api;

static const float FS = 48000.0f;
static int passed = 0, failed = 0;

static DecodeResult quiet_decode(const Samples& s, Mode mode) {
    RxConfig cfg;
    cfg.sample_rate = FS;
    cfg.mode = mode;
    cfg.equalizer = Equalizer::DFE;
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
    t.include_eom = false;
    auto r = ModemTX(t).encode(data);
    return r.ok() ? r.value() : Samples();
}

/// s, then tail_s of white noise at level (RMS)
static Samples with_noise(Samples s, float tail_s, float level, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, level);
    for (size_t i = 0, k = static_cast<size_t>(tail_s * FS); i < k; i++) s.push_back(n(rng));
    return s;
}

static bool starts_with(const std::vector<uint8_t>& d, const std::vector<uint8_t>& p) {
    return d.size() >= p.size() && std::equal(p.begin(), p.end(), d.begin());
}

static void check(const char* what, const DecodeResult& r, bool pass) {
    printf("%-44s success=%d q=%.1f/%.1f bytes=%zu eom=%d ... %s\n", what, r.success ? 1 : 0,
           r.quality_db, r.quality_threshold_db, r.data.size(), r.eom_sequence ? 1 : 0,
           pass ? "PASS" : "FAIL");
    fflush(stdout);
    (pass ? passed : failed)++;
}

int main() {
    char what[96];

    // 1. Whole interleaver blocks, then noise: the bytes are the payload, no more.
    struct Whole { Mode mode; const char* tag; size_t bytes; };
    const Whole whole[] = {{Mode::M150_SHORT, "150S", 44},  {Mode::M600_SHORT, "600S", 89},
                           {Mode::M1200_SHORT, "1200S", 179}, {Mode::M2400_SHORT, "2400S", 359},
                           {Mode::M300_LONG, "300L", 179}};
    for (const auto& w : whole) {
        const auto data = payload(w.bytes, 7);
        const Samples sig = tx(w.mode, data);
        const Samples s = with_noise(sig, 5.0f, rms(sig), 11);
        for (Mode m : {Mode::AUTO, w.mode}) {
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "%s %zu B + 5 s noise, %s", w.tag, w.bytes,
                     m == Mode::AUTO ? "AUTO" : "forced");
            check(what, r, r.success && starts_with(r.data, data) && r.data.size() <= data.size() + 1);
        }
    }

    // 2. A burst cut mid-block: the whole blocks the signal covers, no more.
    {
        const auto data = payload(179, 9);  // 600S: 4 blocks of 45 bytes, 0.6 s each
        Samples sig = tx(Mode::M600_SHORT, data);
        const float level = rms(sig);
        sig.resize(sig.size() - static_cast<size_t>(0.9f * FS));  // 1.5 blocks short
        const Samples s = with_noise(sig, 5.0f, level, 13);
        for (Mode m : {Mode::AUTO, Mode::M600_SHORT}) {
            DecodeResult r = quiet_decode(s, m);
            const bool head = r.data.size() >= 90 &&
                              std::equal(data.begin(), data.begin() + 90, r.data.begin());
            snprintf(what, sizeof what, "600S cut at 2.5 blocks + 5 s noise, %s",
                     m == Mode::AUTO ? "AUTO" : "forced");
            check(what, r, r.success && head && r.data.size() == 90);
        }
    }

    // 2a. The signal stops 1.2 s into 300L's first 4.8 s block (the on-air slice's shape):
    // no whole block, so success=0 and no bytes.
    {
        const auto data = payload(179, 19);
        Samples sig = tx(Mode::M300_LONG, data);
        const float level = rms(sig);
        sig.resize(static_cast<size_t>(288 + 24 * 480) * 20 + static_cast<size_t>(1.2f * FS));
        const Samples s = with_noise(sig, 10.0f, level, 29);
        for (Mode m : {Mode::AUTO, Mode::M300_LONG}) {
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "300L stops 1.2 s into block 1, %s",
                     m == Mode::AUTO ? "AUTO" : "forced");
            check(what, r, !r.success && r.data.empty());
        }
    }

    // 2b. A deep fade mid-message: 300L, 2 blocks (9.6 s of data), the signal off for 1 s
    // from 3 s into the data, noise throughout and 5 s of it after. The signal end is the
    // last frame that carries signal, so the fade must not cut the message.
    {
        const auto data = payload(359, 21);
        Samples sig = tx(Mode::M300_LONG, data);
        const float level = rms(sig);
        const size_t data0 = static_cast<size_t>(288 + 24 * 480) * 20;
        std::fill(sig.begin() + data0 + static_cast<size_t>(3 * FS),
                  sig.begin() + data0 + static_cast<size_t>(4 * FS), 0.0f);
        Samples s = with_noise(sig, 5.0f, 0.0f, 0);
        std::mt19937 rng(23);
        std::normal_distribution<float> n(0.0f, 0.3f * level);
        for (auto& v : s) v += n(rng);
        for (Mode m : {Mode::AUTO, Mode::M300_LONG}) {
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "300L 359 B, 1 s fade mid-message, %s",
                     m == Mode::AUTO ? "AUTO" : "forced");
            check(what, r, r.success && starts_with(r.data, data) && r.data.size() <= data.size() + 1);
        }
    }

    // 3. Forced short modes on a 300L transmission from its last 3 preamble frames on.
    {
        const auto data = payload(180, 5);
        const Samples sig = tx(Mode::M300_LONG, data);
        // 288 leading symbols, then 21 of the 24 preamble frames of 480 symbols, 20 samples each
        const size_t cut = static_cast<size_t>(288 + 21 * 480) * 20 - 96 * 20;
        const Samples s = with_noise(Samples(sig.begin() + cut, sig.end()), 2.0f, rms(sig), 17);
        for (Mode m : {Mode::M150_SHORT, Mode::M300_SHORT, Mode::M600_SHORT}) {
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "300L last 3 preamble frames, forced %s", mode_name(m).c_str());
            check(what, r, !r.success);
        }
    }

    // 4. Noise alone, AUTO and every forced mode.
    {
        std::mt19937 rng(3);
        std::normal_distribution<float> n(0.0f, 0.2f);
        Samples s(static_cast<size_t>(30 * FS));
        for (auto& v : s) v = n(rng);
        for (int i = 0; i <= static_cast<int>(Mode::M1200_HARRIS); i++) {
            const Mode m = static_cast<Mode>(i);
            if (m == Mode::M75_SHORT || m == Mode::M75_LONG || m == Mode::M4800_LONG) continue;
            DecodeResult r = quiet_decode(s, m);
            snprintf(what, sizeof what, "30 s noise, %s", mode_name(m).c_str());
            check(what, r, !r.success);
        }
    }

    printf("\n%d/%d signal-end cases passed\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
