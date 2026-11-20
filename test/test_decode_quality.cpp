// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file test_decode_quality.cpp
 * @brief DecodeResult::success must not be reported for wrong bytes.
 *
 * 110A carries no CRC, so success rests on DecodeResult::quality_db (probe
 * SNR as the codec sees it) clearing quality_threshold_db for the mode.
 *
 * - Input level: each MS-DMT reference recording, scaled from native level
 *   (about 7000 RMS) down to 1/20 of it, must decode exactly or report failure.
 *   Before level normalisation the DFE never trained below about 2500 RMS and
 *   the decoder returned garbage with success=1.
 * - Normal level: the same recordings and this modem's own TX, at native level,
 *   must decode exactly with success=1 (all supported modes but 2400S, which
 *   does not decode exactly yet).
 * - Noise: recordings with white noise added (and 0.1 s of noise lead-in)
 *   where the payload comes out wrong must report success=0. At least one such
 *   decode must occur, so the gate is exercised and cannot pass vacuously.
 *
 * Build (from the repo root):
 *   g++ -std=c++17 -O2 -I. -Isrc -D_USE_MATH_DEFINES -o test/test_decode_quality \
 *       test/test_decode_quality.cpp api/modem_tx.cpp api/modem_rx.cpp
 * Run from the repo root, or pass the reference directory as argv[1].
 */

#include "api/modem.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a::api;
namespace fs = std::filesystem;

static int tests_passed = 0;
static int tests_failed = 0;

static const std::string PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

struct ModeTag { Mode mode; const char* tag; };
static const ModeTag EXACT_MODES[] = {
    {Mode::M150_SHORT, "150S"}, {Mode::M150_LONG, "150L"},
    {Mode::M300_SHORT, "300S"}, {Mode::M300_LONG, "300L"},
    {Mode::M600_SHORT, "600S"}, {Mode::M600_LONG, "600L"},
    {Mode::M1200_SHORT, "1200S"}, {Mode::M1200_LONG, "1200L"},
    {Mode::M2400_LONG, "2400L"},
};

static bool load_ref(const fs::path& dir, const std::string& tag, Samples& out) {
    std::string prefix = "tx_" + tag + "_";
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string n = e.path().filename().string();
        if (n.rfind(prefix, 0) != 0 || e.path().extension() != ".pcm") continue;
        std::ifstream f(e.path(), std::ios::binary | std::ios::ate);
        size_t sz = f.tellg();
        f.seekg(0);
        std::vector<int16_t> raw(sz / 2);
        f.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
        out.resize(raw.size());
        for (size_t i = 0; i < raw.size(); i++) out[i] = raw[i] / 32768.0f;
        return true;
    }
    return false;
}

// Quantise to int16 at the given scale, as a recording at that level would be.
static Samples at_level(const Samples& s, float scale) {
    Samples o(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        float v = std::round(s[i] * scale * 32768.0f);
        v = std::max(-32768.0f, std::min(32767.0f, v));
        o[i] = v / 32768.0f;
    }
    return o;
}

static float rms(const Samples& s) {
    double acc = 0;
    for (float x : s) acc += double(x) * x;
    return s.empty() ? 0.0f : float(std::sqrt(acc / s.size()));
}

static bool exact(const DecodeResult& r) {
    return r.data.size() >= PAYLOAD.size() &&
           std::equal(PAYLOAD.begin(), PAYLOAD.end(), r.data.begin());
}

static DecodeResult quiet_decode(const Samples& s, Mode mode) {
    RxConfig cfg;
    cfg.sample_rate = 48000;
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

static void report(const std::string& what, const DecodeResult& r, bool pass) {
    char q[96];
    snprintf(q, sizeof(q), "success=%d exact=%d quality=%.1f/%.1f dB",
             r.success ? 1 : 0, exact(r) ? 1 : 0, r.quality_db, r.quality_threshold_db);
    std::cout << what << ": " << q << " ... " << (pass ? "PASS" : "FAIL") << std::endl;
    if (pass) tests_passed++; else tests_failed++;
}

int main(int argc, char** argv) {
    fs::path ref_dir = argc > 1 ? fs::path(argv[1]) : fs::path("release/examples/refrence_pcm");
    if (!fs::is_directory(ref_dir)) {
        std::cerr << "reference directory not found: " << ref_dir << std::endl;
        return 2;
    }

    // 1. Input level: never success with wrong bytes; native level decodes exactly.
    std::cout << "--- input level (MS-DMT reference, explicit mode, DFE) ---" << std::endl;
    const float scales[] = {1.0f, 0.35f, 0.2f, 0.1f, 0.05f};
    for (const auto& m : {EXACT_MODES[0], EXACT_MODES[4], EXACT_MODES[7], EXACT_MODES[8]}) {
        Samples ref;
        if (!load_ref(ref_dir, m.tag, ref)) {
            std::cout << m.tag << ": reference file missing ... FAIL" << std::endl;
            tests_failed++;
            continue;
        }
        for (float sc : scales) {
            Samples s = at_level(ref, sc);
            DecodeResult r = quiet_decode(s, m.mode);
            bool pass = !(r.success && !exact(r));
            if (sc == 1.0f) pass = r.success && exact(r);
            char what[64];
            snprintf(what, sizeof(what), "%s at %.0f RMS", m.tag, rms(s) * 32768.0f);
            report(what, r, pass);
        }
    }

    // 2. Normal level, every mode that decodes exactly: success and exact bytes.
    std::cout << "--- normal level: MS-DMT reference and own TX ---" << std::endl;
    for (const auto& m : EXACT_MODES) {
        Samples ref;
        if (load_ref(ref_dir, m.tag, ref)) {
            DecodeResult r = quiet_decode(ref, m.mode);
            report(std::string("ms-dmt ") + m.tag, r, r.success && exact(r));
        } else {
            std::cout << "ms-dmt " << m.tag << ": reference file missing ... FAIL" << std::endl;
            tests_failed++;
        }
        TxConfig tcfg;
        tcfg.mode = m.mode;
        tcfg.sample_rate = 48000;
        ModemTX tx(tcfg);
        auto enc = tx.encode(std::vector<uint8_t>(PAYLOAD.begin(), PAYLOAD.end()));
        if (!enc.ok()) {
            std::cout << "own-tx " << m.tag << ": encode failed ... FAIL" << std::endl;
            tests_failed++;
            continue;
        }
        DecodeResult r = quiet_decode(enc.value(), m.mode);
        report(std::string("own-tx ") + m.tag, r, r.success && exact(r));
    }

    // 3. Noise: wrong bytes must not be reported as success.
    std::cout << "--- noise: wrong bytes must not pass ---" << std::endl;
    int wrong_decodes = 0;
    for (const auto& m : {EXACT_MODES[8], EXACT_MODES[7]}) {   // 2400L, 1200L
        Samples ref;
        if (!load_ref(ref_dir, m.tag, ref)) continue;
        float sig = rms(ref);
        for (float nsr : {1.0f, 1.5f, 2.0f}) {     // noise RMS relative to signal RMS, 0-24 kHz
            for (unsigned seed : {1u, 2u}) {
                // 0.1 s of noise before the signal and 0.5 s after, as in the
                // AWGN sweeps. With almost no lead-in the preamble search can
                // lock onto the second preamble frame and drop the start of the
                // payload, a separate defect this test does not cover.
                const size_t lead = 4800, tail = 24000;
                std::mt19937 rng(seed);
                std::normal_distribution<float> nd(0.0f, sig * nsr);
                Samples s(lead + ref.size() + tail);
                for (size_t i = 0; i < s.size(); i++) {
                    float x = (i >= lead && i < lead + ref.size()) ? ref[i - lead] : 0.0f;
                    s[i] = x + nd(rng);
                }
                DecodeResult r = quiet_decode(s, m.mode);
                if (!exact(r) && !r.data.empty()) wrong_decodes++;
                char what[64];
                snprintf(what, sizeof(what), "%s noise/signal %.1f seed %u", m.tag, nsr, seed);
                report(what, r, !(r.success && !exact(r)));
            }
        }
    }
    bool exercised = wrong_decodes > 0;
    std::cout << "wrong-byte decodes produced: " << wrong_decodes
              << " ... " << (exercised ? "PASS" : "FAIL (gate not exercised)") << std::endl;
    if (exercised) tests_passed++; else tests_failed++;

    std::cout << "\n" << tests_passed << "/" << (tests_passed + tests_failed)
              << " decode quality cases passed" << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
