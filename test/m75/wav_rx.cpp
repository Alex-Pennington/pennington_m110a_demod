// m75_wav_rx - decode a 16-bit mono WAV with ModemRX (api::decode), printing m110a_rx's
// message lines. Test tool for test/m75/m75_sweep.py --wav-rx: it measures the modem
// without m110a_rx's I/Q front end and preamble scanner in front of it.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Usage: m75_wav_rx [--mode AUTO|75S|75L|150S|...] [-v] IN.wav   (-v: decoder output to stderr)
// Output: msg=1 mode=M success=0|1 quality_db=Q quality_threshold_db=T eom=0|1 bytes=N
//         snr_db=S d1=D d2=D err="..."   then   "  hex=..."

#include "api/modem.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace m110a::api;

static bool read_wav16(const std::string& path, std::vector<float>& out, int& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[12];
    f.read(riff, 12);
    if (!f || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;
    int ch = 0, bits = 0;
    rate = 0;
    while (f) {
        char id[4];
        uint32_t sz;
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&sz), 4);
        if (!f) break;
        if (!memcmp(id, "fmt ", 4)) {
            std::vector<char> b(sz);
            f.read(b.data(), sz);
            ch = *reinterpret_cast<uint16_t*>(&b[2]);
            rate = static_cast<int>(*reinterpret_cast<uint32_t*>(&b[4]));
            bits = *reinterpret_cast<uint16_t*>(&b[14]);
        } else if (!memcmp(id, "data", 4)) {
            if (bits != 16 || ch < 1) return false;
            std::vector<int16_t> raw(sz / 2);
            f.read(reinterpret_cast<char*>(raw.data()), sz);
            for (size_t i = 0; i < raw.size(); i += ch) out.push_back(raw[i] / 32768.0f);
            return true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    return false;
}

int main(int argc, char** argv) {
    std::string mode_s = "AUTO", in;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode_s = argv[++i];
        else if (a == "-v") verbose = true;
        else if (in.empty()) in = a;
        else { fprintf(stderr, "usage: m75_wav_rx [--mode M] IN.wav\n"); return 2; }
    }
    Mode mode = Mode::AUTO;
    if (mode_s != "AUTO") {
        bool ok = false;
        for (int i = 0; i <= static_cast<int>(Mode::M1200_HARRIS) && !ok; i++)
            if (mode_name(static_cast<Mode>(i)) == mode_s) { mode = static_cast<Mode>(i); ok = true; }
        if (!ok) { fprintf(stderr, "m75_wav_rx: unknown mode %s\n", mode_s.c_str()); return 2; }
    }
    std::vector<float> x;
    int rate = 0;
    if (in.empty() || !read_wav16(in, x, rate)) { fprintf(stderr, "m75_wav_rx: cannot read %s\n", in.c_str()); return 2; }
    // the level m110a_rx normalises to (7000/32768 RMS over the signal)
    double p = 0;
    size_t n = 0;
    for (float v : x) if (v != 0.0f) { p += double(v) * v; n++; }
    const float g = n && p > 0 ? static_cast<float>(7000.0 / 32768.0 / std::sqrt(p / n)) : 1.0f;
    for (auto& v : x) v *= g;

    RxConfig cfg;
    cfg.sample_rate = static_cast<float>(rate);
    cfg.mode = mode;
    cfg.equalizer = Equalizer::DFE;
    std::stringstream sink;
    std::streambuf* o = std::cout.rdbuf(verbose ? std::cerr.rdbuf() : sink.rdbuf());
    std::streambuf* e = std::cerr.rdbuf(verbose ? std::cerr.rdbuf() : sink.rdbuf());
    DecodeResult r = decode(x, cfg);
    std::cout.rdbuf(o);
    std::cerr.rdbuf(e);
    std::string hex;
    char hx[4];
    for (uint8_t c : r.data) { snprintf(hx, sizeof hx, "%02x", c); hex += hx; }
    printf("msg=1 mode=%s success=%d quality_db=%.1f quality_threshold_db=%.1f eom=%d bytes=%zu snr_db=%.1f "
           "d1=%d d2=%d err=\"%s\"\n  hex=%s\n",
           mode_name(r.mode).c_str(), r.success ? 1 : 0, r.quality_db, r.quality_threshold_db,
           r.eom_sequence ? 1 : 0, r.data.size(), r.snr_db, r.d1, r.d2, r.error ? r.error->message.c_str() : "",
           hex.c_str());
    return 0;
}
