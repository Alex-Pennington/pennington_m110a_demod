/**
 * @file test_grid_step.cpp
 * @brief A timing step mid-message: the receiver moves its data grid and decodes exact.
 *
 * An on-air 300L burst moved 36.8 symbols (15.3 ms) 1.2 s into its data: an audio
 * discontinuity at the sender or in the recording chain. The data grid stayed where the
 * preamble put it and the receiver lost every probe after that (BrainDecoder::grid_steps).
 *
 * This modem's own TX (ModemTX, no EOM), payloads one byte short of whole blocks, then
 * 2 s of white noise at the signal's RMS. 15 ms (720 samples, 36 symbols) at 48 kHz:
 *   - inserted as silence,
 *   - inserted as a replay of the 15 ms before it (a sound-card underrun),
 *   - deleted (36 symbols lost; the FEC carries them),
 * 1.2 s into 300L's data and 1.0 s into 2400S's. AUTO and the forced mode must give
 * success and the payload exact, with at most the one padding byte after it.
 *
 * Run: test_grid_step            (exit 0 when every case passes)
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

int main() {
    struct Case { Mode mode; const char* tag; size_t bytes; size_t preamble_symbols; float at_s; };
    // 300L: 2 blocks of 180 B (4.8 s each), 24 preamble frames; 2400S: 4 blocks of 180 B
    // (0.6 s each), 3 frames. ModemTX sends 288 leading symbols before the preamble.
    const Case cases[] = {{Mode::M300_LONG, "300L", 359, 288 + 24 * 480, 1.2f},
                          {Mode::M2400_SHORT, "2400S", 719, 288 + 3 * 480, 1.0f}};
    const size_t step = static_cast<size_t>(0.015f * FS);
    enum Kind { SILENCE, REPLAY, DELETE };
    const char* kind_name[] = {"15 ms of silence inserted", "15 ms replayed", "15 ms deleted"};
    char what[128];
    for (const auto& c : cases) {
        const auto data = payload(c.bytes, 31);
        const Samples sig = tx(c.mode, data);
        const float level = rms(sig);
        const size_t at = c.preamble_symbols * 20 + static_cast<size_t>(c.at_s * FS);
        for (Kind k : {SILENCE, REPLAY, DELETE}) {
            Samples s(sig.begin(), sig.begin() + at);
            if (k == SILENCE) s.insert(s.end(), step, 0.0f);
            if (k == REPLAY) s.insert(s.end(), sig.begin() + at - step, sig.begin() + at);
            s.insert(s.end(), sig.begin() + at + (k == DELETE ? step : 0), sig.end());
            std::mt19937 rng(37);
            std::normal_distribution<float> n(0.0f, level);
            for (size_t i = 0, e = static_cast<size_t>(2 * FS); i < e; i++) s.push_back(n(rng));
            for (Mode m : {Mode::AUTO, c.mode}) {
                DecodeResult r = quiet_decode(s, m);
                const bool exact = r.data.size() >= data.size() && r.data.size() <= data.size() + 1 &&
                                   std::equal(data.begin(), data.end(), r.data.begin());
                const bool pass = r.success && exact;
                snprintf(what, sizeof what, "%s %zu B, %s at data +%.1f s, %s", c.tag, c.bytes,
                         kind_name[k], c.at_s, m == Mode::AUTO ? "AUTO" : "forced");
                printf("%-62s success=%d bytes=%zu ... %s\n", what, r.success ? 1 : 0, r.data.size(),
                       pass ? "PASS" : "FAIL");
                fflush(stdout);
                (pass ? passed : failed)++;
            }
        }
    }
    printf("\n%d/%d grid-step cases passed\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
