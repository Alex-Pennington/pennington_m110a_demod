// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_table_xi_modes.cpp
 * @brief TX/RX loopback for the D1/D2 codes added after the Harris recordings.
 *
 * Table XI codes (MIL-STD-188-110C/D):
 *   - 2400 digital voice, D1,D2 = 7,7: own TX -> Mode::AUTO must report 2400V
 *     and return the exact payload; so must 2400S given explicitly (2400V is
 *     2400S's waveform)
 *   - 75 bps short 7,5 and long 5,5: own TX preamble (the 75 bps data path is not
 *     implemented) -> Mode::AUTO must read the D1/D2 and report 75S / 75L
 * Harris application-specific codes (Table XI reserves 5,6 and 5,7 and does not
 * define them; docs/harris-melpe.md):
 *   - 5,7 = 600 bps short, 5,6 = 1200 bps short: own TX -> Mode::AUTO must report
 *     600H / 1200H and return the exact payload
 * And the old codes must not move: 600S (6,6) and 1200S (6,5) still read as such.
 */

#include "api/modem.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a::api;

static int passed = 0, failed = 0;
static const std::string PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

static DecodeResult decode_auto(const Samples& s) {
    RxConfig cfg;
    cfg.sample_rate = 48000;
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

static void report(const std::string& what, bool ok) {
    std::cout << what << " ... " << (ok ? "PASS" : "FAIL") << std::endl;
    (ok ? passed : failed)++;
}

// Full message: own TX, AUTO RX, mode and (when need_exact) the exact payload.
static void loopback(Mode m, int d1, int d2, bool need_exact = true) {
    TxConfig t;
    t.mode = m;
    t.sample_rate = 48000;
    ModemTX tx(t);
    auto enc = tx.encode(std::vector<uint8_t>(PAYLOAD.begin(), PAYLOAD.end()));
    if (!enc.ok()) { report("own-tx " + mode_name(m) + ": encode failed", false); return; }
    DecodeResult r = decode_auto(enc.value());
    bool exact = r.success && r.data.size() >= PAYLOAD.size() &&
                 std::equal(PAYLOAD.begin(), PAYLOAD.end(), r.data.begin());
    size_t same = 0;
    for (size_t i = 0; i < PAYLOAD.size() && i < r.data.size(); i++) same += r.data[i] == (uint8_t)PAYLOAD[i];
    char buf[200];
    snprintf(buf, sizeof buf, "own-tx %s: read D1,D2 %d,%d (want %d,%d), detected %s, payload %zu/%zu bytes%s",
             mode_name(m).c_str(), r.d1, r.d2, d1, d2, mode_name(r.mode).c_str(),
             same, PAYLOAD.size(), need_exact ? (exact ? " exact" : " WRONG") : " (not required)");
    report(buf, r.mode == m && r.d1 == d1 && r.d2 == d2 && (exact || !need_exact));
}

// Preamble only (75 bps): AUTO must name the mode from D1/D2.
static void preamble_only(Mode m, int d1, int d2) {
    TxConfig t;
    t.mode = m;
    t.sample_rate = 48000;
    ModemTX tx(t);
    auto pre = tx.generate_preamble();
    if (!pre.ok()) { report("preamble " + mode_name(m) + ": generate failed", false); return; }
    Samples s = pre.value();
    // follow it with 1 s of low-level noise so the decoder has samples after the preamble
    std::mt19937 rng(1);
    std::normal_distribution<float> n(0.0f, 0.01f);
    for (int i = 0; i < 48000; i++) s.push_back(n(rng));
    DecodeResult r = decode_auto(s);
    char buf[160];
    snprintf(buf, sizeof buf, "preamble %s: read D1,D2 %d,%d (want %d,%d), detected %s",
             mode_name(m).c_str(), r.d1, r.d2, d1, d2, mode_name(r.mode).c_str());
    report(buf, r.mode == m && r.d1 == d1 && r.d2 == d2);
}

int main() {
    loopback(Mode::M2400_VOICE, 7, 7);
    loopback(Mode::M2400_SHORT, 6, 4);
    loopback(Mode::M600_HARRIS, 5, 7);
    loopback(Mode::M1200_HARRIS, 5, 6);
    loopback(Mode::M600_SHORT, 6, 6);
    loopback(Mode::M1200_SHORT, 6, 5);
    preamble_only(Mode::M75_SHORT, 7, 5);
    preamble_only(Mode::M75_LONG, 5, 5);
    std::cout << "\n" << passed << "/" << (passed + failed) << " Table XI / Harris code cases passed"
              << std::endl;
    return failed == 0 ? 0 : 1;
}
