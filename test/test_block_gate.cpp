/**
 * @file test_block_gate.cpp
 * @brief success reads every interleaver block of the message, not only its first
 *        1440 data symbols.
 *
 * ModemTX (with EOM), white noise at a fixed SNR in 3 kHz, then decode with the DFE:
 *   - clean: 1200S (3 blocks) and 300L (2 blocks): success, the payload exact, one
 *     block_quality_db entry per block, each at or above block_quality_threshold_db, and
 *     the worst within 1.5 dB of quality_db (a flat channel scores as itself);
 *   - burst: the same messages with a noise burst over nearly all of their second
 *     interleaver block only (in 24 kHz 10 dB above the signal for 1200S, 20 dB for 300L;
 *     about 1 and 11 dB in 3 kHz). The first 1440 data symbols are clean, so quality_db
 *     still clears quality_threshold_db (the gate before the whole-message check passed
 *     these); the bytes are wrong; the second block scores below
 *     block_quality_threshold_db, and success is 0. The first block still clears it.
 *
 * The payload starts "DE AAM402".
 * Run: test_block_gate            (exit 0 when every case passes)
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

static void check(bool ok, const std::string& what) {
    printf("  %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    (ok ? passed : failed)++;
}

static DecodeResult quiet_decode(const Samples& s) {
    RxConfig cfg;
    cfg.sample_rate = FS;
    cfg.mode = Mode::AUTO;
    cfg.equalizer = Equalizer::DFE;
    std::ofstream sink;
    std::streambuf* out = std::cout.rdbuf(sink.rdbuf());
    std::streambuf* err = std::cerr.rdbuf(sink.rdbuf());
    DecodeResult r = decode(s, cfg);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    return r;
}

static std::vector<uint8_t> payload(size_t n, unsigned seed) {
    const std::string head = "DE AAM402 block gate test ";
    std::mt19937 rng(seed);
    std::vector<uint8_t> p(head.begin(), head.end());
    while (p.size() < n) p.push_back(static_cast<uint8_t>(rng() & 0xff));
    return p;
}

/// TX, 0.5 s of silence each side, white noise at snr_db in 3 kHz (signal power measured
/// where it is on), and a burst of noise burst_db above the signal from b0 to b1 s after
/// the preamble starts (b1 <= b0: none).
static Samples make(Mode mode, const std::vector<uint8_t>& data, float snr_db, float b0, float b1,
                    float burst_db, unsigned seed) {
    TxConfig t;
    t.mode = mode;
    t.sample_rate = FS;
    auto r = ModemTX(t).encode(data);
    if (!r.ok()) return {};
    const Samples& s = r.value();
    double ps = 0;
    size_t on = 0;
    for (float v : s) if (v != 0.0f) { ps += static_cast<double>(v) * v; on++; }
    ps /= std::max<size_t>(1, on);
    const float sigma = static_cast<float>(std::sqrt(ps / std::pow(10.0, snr_db / 10.0) * (FS / 2) / 3000.0));
    const float sigma_b = static_cast<float>(std::sqrt(ps * std::pow(10.0, burst_db / 10.0)));
    const size_t lead = static_cast<size_t>(0.5f * FS);
    Samples x(lead + s.size() + lead, 0.0f);
    std::copy(s.begin(), s.end(), x.begin() + lead);
    // the preamble follows ModemTX's leading symbols (2 + 8 at 2400 baud)
    const size_t p0 = lead + static_cast<size_t>(10.0f / 2400.0f * FS);
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, 1.0f);
    for (size_t i = 0; i < x.size(); i++) {
        x[i] += sigma * n(rng);
        const float t_s = (static_cast<float>(i) - static_cast<float>(p0)) / FS;
        if (b1 > b0 && t_s >= b0 && t_s < b1) x[i] += sigma_b * n(rng);
    }
    return x;
}

static std::string blocks(const DecodeResult& r) {
    std::string s;
    char b[16];
    for (float q : r.block_quality_db) { snprintf(b, sizeof b, "%s%.1f", s.empty() ? "" : ",", q); s += b; }
    return s;
}

struct Case { const char* name; Mode mode; size_t bytes; size_t nblocks; float snr; float b0, b1, burst_db; };

int main() {
    // Second interleaver block: 1.2-1.8 s after the preamble start for a short mode
    // (preamble 0.6 s, blocks 0.6 s), 9.6-14.4 s for a long one (4.8 s each).
    const Case cases[] = {
        {"1200S", Mode::M1200_SHORT, 200, 3, 12.0f, 1.22f, 1.75f, 10.0f},
        {"300L", Mode::M300_LONG, 300, 2, 6.0f, 9.7f, 14.2f, 20.0f},
    };
    for (const Case& c : cases) {
        const auto data = payload(c.bytes, 402);
        for (int burst = 0; burst < 2; burst++) {
            const Samples x = make(c.mode, data, c.snr, c.b0, burst ? c.b1 : c.b0, c.burst_db, 7);
            const DecodeResult r = quiet_decode(x);
            const bool exact = r.data == data;
            printf("%s %s: success=%d exact=%d quality_db=%.1f (threshold %.1f) blocks=%s (threshold %.1f) err=\"%s\"\n",
                   c.name, burst ? "burst in block 2" : "clean", r.success, exact, r.quality_db,
                   r.quality_threshold_db, blocks(r).c_str(), r.block_quality_threshold_db,
                   r.error ? r.error->message.c_str() : "");
            check(r.block_quality_db.size() == c.nblocks, "one block_quality_db entry per interleaver block");
            if (r.block_quality_db.size() != c.nblocks) continue;
            if (!burst) {
                check(r.success && exact && r.eom_sequence, "success, exact, EOM");
                check(r.block_quality_min_db >= r.block_quality_threshold_db, "every block clears the block threshold");
                check(std::fabs(r.block_quality_min_db - r.quality_db) <= 1.5f,
                      "a flat channel: worst block within 1.5 dB of quality_db");
            } else {
                check(r.quality_db >= r.quality_threshold_db, "the first 1440 symbols are clean: quality_db clears its threshold");
                check(!exact, "the bytes are wrong");
                check(r.block_quality_db[1] < r.block_quality_threshold_db, "block 2 scores below the block threshold");
                check(r.block_quality_db[0] >= r.block_quality_threshold_db, "block 1 clears it");
                check(!r.success, "success=0");
            }
        }
    }
    printf("test_block_gate: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
