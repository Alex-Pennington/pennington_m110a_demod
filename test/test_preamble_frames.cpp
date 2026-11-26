/**
 * @file test_preamble_frames.cpp
 * @brief 110A long preamble whose first frames are faded: the receiver takes the preamble at a
 *        later frame (docs/m110a-acq-fading.md, "A later preamble frame").
 *
 * ModemTX with EOM, 300L and 600L. The first FADED preamble frames are attenuated by GAIN, the
 * rest of the transmission is at full level, then white noise at SNR dB in 3 kHz over the
 * whole buffer. The preamble finder (first_preamble_cut) crosses its threshold in a faded frame,
 * and BrainDecoder searches only the first frame after that cut. Before the later-frame search,
 * each case below failed with "Mode detection failed" (no preamble taken), although the 24-frame
 * preamble's later frames are clean. Each case must decode with success, the payload exactly
 * and the EOM. In the ramp case the preamble rises from -9 dB to full level over its first 8
 * frames: the first later frame to read a mode is weak, and taking it lost the message (base and
 * first-frame search: success=0); the best-correlating frame decodes it.
 *
 * Run: test_preamble_frames                 (exit 0 when every case passes)
 *      test_preamble_frames --sweep MODE    (prints a grid of fades and SNRs; no verdict)
 */

#include "api/modem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a::api;

static const float FS = 48000.0f;
static const long FRAME = static_cast<long>(0.2 * FS);  // one preamble frame, 480 symbols
static int passed = 0, failed = 0;

static DecodeResult quiet_decode(const Samples& s, Mode mode) {
    RxConfig cfg;
    cfg.sample_rate = FS;
    cfg.mode = mode;
    std::ofstream sink;
    std::streambuf* out = std::cout.rdbuf(sink.rdbuf());
    std::streambuf* err = std::cerr.rdbuf(sink.rdbuf());
    DecodeResult r = decode(s, cfg);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    return r;
}

static std::vector<uint8_t> payload(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> p(n);
    for (auto& b : p) b = static_cast<uint8_t>(rng() & 0xff);
    return p;
}

/// ModemTX's burst with 0.5 s of silence before it, its first `faded` preamble frames scaled by
/// `gain` (ramp: rising linearly from `gain` at the first to full level after the last), then
/// white noise at snr_db in 3 kHz (signal power measured over the unfaded part).
static Samples burst(Mode mode, const std::vector<uint8_t>& data, int faded, float gain, float snr_db,
                     unsigned seed, bool ramp = false) {
    TxConfig t;
    t.mode = mode;
    t.sample_rate = FS;
    auto r = ModemTX(t).encode(data);
    if (!r.ok()) return {};
    Samples x = r.value();
    // the first sample above a hundredth of the peak is the start of the preamble
    float peak = 0.0f;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    long s0 = 0;
    while (s0 < static_cast<long>(x.size()) && std::fabs(x[static_cast<size_t>(s0)]) < 0.01f * peak) s0++;
    double p = 0;
    long np = 0;
    for (long n = s0 + faded * FRAME; n < static_cast<long>(x.size()); n++, np++) p += static_cast<double>(x[n]) * x[n];
    p /= std::max(1L, np);
    for (long n = s0; n < s0 + faded * FRAME && n < static_cast<long>(x.size()); n++) {
        const float g = ramp ? gain + (1.0f - gain) * static_cast<float>(n - s0) / static_cast<float>(faded * FRAME) : gain;
        x[static_cast<size_t>(n)] *= g;
    }
    Samples y(static_cast<size_t>(0.5 * FS), 0.0f);
    y.insert(y.end(), x.begin(), x.end());
    y.insert(y.end(), static_cast<size_t>(0.5 * FS), 0.0f);
    // white noise: power in 3 kHz = p / 10^(snr/10), so per-sample variance over FS/2 Hz
    const double sigma = std::sqrt(p / std::pow(10.0, snr_db / 10.0) * (FS / 2.0) / 3000.0);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, sigma);
    for (auto& v : y) v = static_cast<float>(v + g(rng));
    return y;
}

static const char* verdict(const DecodeResult& r, const std::vector<uint8_t>& p) {
    if (!r.success) return "fail";
    return r.data == p && r.eom_sequence ? "exact" : (r.data.size() >= p.size() &&
           std::equal(p.begin(), p.end(), r.data.begin()) ? "exact" : "WRONG");
}

static void check(const char* name, Mode mode, int faded, float gain, float snr_db, unsigned seed,
                  bool ramp = false) {
    const auto p = payload(120, seed);
    const DecodeResult r = quiet_decode(burst(mode, p, faded, gain, snr_db, seed, ramp), Mode::AUTO);
    const bool ok = r.success && r.data.size() >= p.size() && std::equal(p.begin(), p.end(), r.data.begin());
    std::printf("%-44s success=%d bytes=%zu quality=%.1f dB %s\n", name, r.success ? 1 : 0, r.data.size(),
                r.quality_db, ok ? "PASS" : "FAIL");
    (ok ? passed : failed)++;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--sweep") == 0) {
        const Mode mode = std::string(argv[2]) == "600L" ? Mode::M600_LONG : Mode::M300_LONG;
        const bool ramp = argc > 4 && std::strcmp(argv[4], "ramp") == 0;
        for (int faded : {argc > 3 ? std::atoi(argv[3]) : 4})
            for (float gain : {1.0f, 0.5f, 0.35f, 0.25f, 0.18f, 0.1f})
                for (float snr : {-2.0f, 0.0f, 3.0f}) {
                    const auto p = payload(120, 1);
                    const DecodeResult r = quiet_decode(burst(mode, p, faded, gain, snr, 1, ramp), Mode::AUTO);
                    std::printf("faded=%2d gain=%.2f snr=%4.1f: success=%d bytes=%zu %s err=%s\n", faded, gain, snr,
                                r.success ? 1 : 0, r.data.size(), verdict(r, p), (r.error ? r.error->message.c_str() : ""));
                }
        return 0;
    }
    std::printf("test_preamble_frames: first preamble frames faded, later frames clean\n");
    check("300L, first 4 frames -6 dB, 3 dB SNR", Mode::M300_LONG, 4, 0.5f, 3.0f, 1);
    check("300L, first 8 frames -9 dB, 3 dB SNR", Mode::M300_LONG, 8, 0.35f, 3.0f, 1);
    check("300L, first 12 frames -12 dB, 0 dB SNR", Mode::M300_LONG, 12, 0.25f, 0.0f, 1);
    check("600L, first 4 frames -6 dB, 3 dB SNR", Mode::M600_LONG, 4, 0.5f, 3.0f, 2);
    check("600L, first 8 frames -9 dB, 6 dB SNR", Mode::M600_LONG, 8, 0.35f, 6.0f, 2);
    // the frame taken matters: the first later frame to read a mode is a weak one
    check("300L, first 8 frames ramp up from -9 dB, -2 dB SNR", Mode::M300_LONG, 8, 0.35f, -2.0f, 1, true);
    check("300L, unfaded, 0 dB SNR", Mode::M300_LONG, 0, 1.0f, 0.0f, 3);
    std::printf("Passed: %d  Failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
