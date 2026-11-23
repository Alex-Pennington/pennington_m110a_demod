/**
 * @file test_late_start.cpp
 * @brief AUTO decode when the transmission starts late in the buffer.
 *
 * A capture from a scanning receiver holds seconds of channel before the
 * preamble. For 600S, 1200S, 2400S and 2400L, this modem's own TX (54-byte
 * payload) is put after 0, 0.2, 0.5, 1, 3 and 10 s of
 *   - digital silence, the signal clean, and
 *   - white noise at 10 dB SNR in 3 kHz over the whole buffer (test/tx/tx_loopback.py's measure),
 * with 0.5 s of the same after it. Each must decode with Mode::AUTO to the transmitted mode and the
 * exact payload with success. Noise alone (30 s, two seeds) must not decode with success.
 *
 * Run: test_late_start            (exit 0 when every case passes)
 */

#include "api/modem.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a::api;

static const std::string PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
static const float FS = 48000.0f;

static DecodeResult decode_auto(const Samples& s) {
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

/// lead_s of silence or noise, the signal, 0.5 s more. snr_db < 0: no noise.
static Samples place(const Samples& sig, float lead_s, float snr_db, unsigned seed) {
    const size_t lead = static_cast<size_t>(lead_s * FS), tail = static_cast<size_t>(0.5f * FS);
    Samples s(lead + sig.size() + tail, 0.0f);
    std::copy(sig.begin(), sig.end(), s.begin() + lead);
    if (snr_db >= 0.0f) {
        double ps = 0;
        for (float v : sig) ps += static_cast<double>(v) * v;
        ps /= sig.size();
        // real white noise spreads over 0..FS/2; the SNR counts the 3 kHz of it in band
        const double sigma = std::sqrt(ps / std::pow(10.0, snr_db / 10.0) * (FS / 2) / 3000.0);
        std::mt19937 rng(seed);
        std::normal_distribution<float> n(0.0f, static_cast<float>(sigma));
        for (auto& v : s) v += n(rng);
    }
    return s;
}

int main() {
    struct ModeCase { Mode mode; const char* tag; };
    const ModeCase modes[] = {{Mode::M600_SHORT, "600S"}, {Mode::M1200_SHORT, "1200S"},
                              {Mode::M2400_SHORT, "2400S"}, {Mode::M2400_LONG, "2400L"}};
    const float leads[] = {0.0f, 0.2f, 0.5f, 1.0f, 3.0f, 10.0f};
    int passed = 0, failed = 0;

    for (const auto& m : modes) {
        TxConfig tcfg;
        tcfg.mode = m.mode;
        tcfg.sample_rate = FS;
        auto enc = ModemTX(tcfg).encode(std::vector<uint8_t>(PAYLOAD.begin(), PAYLOAD.end()));
        if (!enc.ok()) {
            printf("%-5s encode failed ... FAIL\n", m.tag);
            failed++;
            continue;
        }
        for (float snr : {-1.0f, 10.0f}) {
            for (float lead : leads) {
                DecodeResult r = decode_auto(place(enc.value(), lead, snr, 7));
                const bool exact = r.data.size() >= PAYLOAD.size() &&
                                   std::equal(PAYLOAD.begin(), PAYLOAD.end(), r.data.begin());
                const bool ok = r.success && r.mode == m.mode && exact;
                printf("%-5s %-7s lead %5.1f s: mode %-5s success=%d payload %s ... %s\n", m.tag,
                       snr < 0 ? "silence" : "10dB", lead, mode_name(r.mode).c_str(),
                       r.success ? 1 : 0, exact ? "exact" : "WRONG", ok ? "PASS" : "FAIL");
                if (!ok && r.error) printf("      %s\n", r.error->message.c_str());
                (ok ? passed : failed)++;
            }
        }
    }

    // noise alone must not decode
    for (unsigned seed : {1u, 2u}) {
        Samples noise(static_cast<size_t>(30 * FS));
        std::mt19937 rng(seed);
        std::normal_distribution<float> n(0.0f, 0.25f);
        for (auto& v : noise) v = n(rng);
        DecodeResult r = decode_auto(noise);
        const bool ok = !r.success;
        printf("noise 30 s seed %u: success=%d ... %s\n", seed, r.success ? 1 : 0, ok ? "PASS" : "FAIL");
        (ok ? passed : failed)++;
    }

    printf("\n%d/%d late-start cases passed\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
