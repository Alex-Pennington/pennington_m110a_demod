// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_mode_detect.cpp
 * @brief AUTO mode detection must identify every supported mode from the preamble.
 *
 * For each of 150S/L, 300S/L, 600S/L, 1200S/L, 2400S/L, decode with Mode::AUTO:
 * - the MS-DMT reference recording (release/examples/refrence_pcm/tx_<mode>_*.pcm,
 *   48 kHz int16 LE, written by N2CKH's MS-DMT, a different modem)
 * - this modem's own TX output for the same payload
 *
 * Each must report the transmitted mode. Every mode except 2400S must also return
 * the exact payload; 2400S is checked for mode only, because it does not decode
 * exactly with the mode given explicitly either.
 *
 * Build (from the repo root):
 *   g++ -std=c++17 -O2 -I. -Isrc -D_USE_MATH_DEFINES -o test/test_mode_detect \
 *       test/test_mode_detect.cpp api/modem_tx.cpp api/modem_rx.cpp
 * Run from the repo root, or pass the reference directory as argv[1].
 */

#include "api/modem.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace m110a::api;
namespace fs = std::filesystem;

static int tests_passed = 0;
static int tests_failed = 0;

static const std::string PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

struct Case { Mode mode; const char* tag; bool expect_exact; };
static const Case CASES[] = {
    {Mode::M150_SHORT,  "150S",  true}, {Mode::M150_LONG,  "150L",  true},
    {Mode::M300_SHORT,  "300S",  true}, {Mode::M300_LONG,  "300L",  true},
    {Mode::M600_SHORT,  "600S",  true}, {Mode::M600_LONG,  "600L",  true},
    {Mode::M1200_SHORT, "1200S", true}, {Mode::M1200_LONG, "1200L", true},
    {Mode::M2400_SHORT, "2400S", false}, {Mode::M2400_LONG, "2400L", true},
};

static bool load_pcm(const fs::path& p, Samples& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    size_t sz = f.tellg();
    f.seekg(0);
    std::vector<int16_t> raw(sz / 2);
    f.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
    out.resize(raw.size());
    for (size_t i = 0; i < raw.size(); i++) out[i] = raw[i] / 32768.0f;
    return true;
}

static fs::path find_ref(const fs::path& dir, const std::string& tag) {
    std::string prefix = "tx_" + tag + "_";
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string n = e.path().filename().string();
        if (n.rfind(prefix, 0) == 0 && e.path().extension() == ".pcm") return e.path();
    }
    return {};
}

static bool payload_exact(const DecodeResult& r) {
    return r.data.size() >= PAYLOAD.size() &&
           std::equal(PAYLOAD.begin(), PAYLOAD.end(), r.data.begin());
}

// Decode with Mode::AUTO, silencing the decoder's own trace output.
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

static void check(const std::string& source, const Case& c, const Samples& s) {
    DecodeResult r = decode_auto(s);
    bool mode_ok = r.mode == c.mode;
    bool data_ok = !c.expect_exact || (r.success && payload_exact(r));
    std::cout << "AUTO " << source << " " << c.tag << ": detected " << mode_name(r.mode)
              << (c.expect_exact ? (data_ok ? ", payload exact" : ", payload WRONG") : "")
              << " ... " << ((mode_ok && data_ok) ? "PASS" : "FAIL") << std::endl;
    if (mode_ok && data_ok) tests_passed++; else tests_failed++;
}

int main(int argc, char** argv) {
    fs::path ref_dir = argc > 1 ? fs::path(argv[1]) : fs::path("release/examples/refrence_pcm");
    if (!fs::is_directory(ref_dir)) {
        std::cerr << "reference directory not found: " << ref_dir << std::endl;
        return 2;
    }

    for (const auto& c : CASES) {
        // MS-DMT reference recording
        fs::path p = find_ref(ref_dir, c.tag);
        Samples s;
        if (p.empty() || !load_pcm(p, s)) {
            std::cout << "AUTO ms-dmt " << c.tag << ": reference file missing ... FAIL" << std::endl;
            tests_failed++;
        } else {
            check("ms-dmt", c, s);
        }

        // This modem's own TX
        TxConfig tcfg;
        tcfg.mode = c.mode;
        tcfg.sample_rate = 48000;
        ModemTX tx(tcfg);
        auto enc = tx.encode(std::vector<uint8_t>(PAYLOAD.begin(), PAYLOAD.end()));
        if (!enc.ok()) {
            std::cout << "AUTO own-tx " << c.tag << ": encode failed ... FAIL" << std::endl;
            tests_failed++;
        } else {
            check("own-tx", c, enc.value());
        }
    }

    std::cout << "\n" << tests_passed << "/" << (tests_passed + tests_failed)
              << " AUTO mode detection cases passed" << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
