/**
 * @file test_preamble_countdown.cpp
 * @brief The receiver must start data after the LAST preamble frame, whichever
 *        frame its preamble search locks onto.
 *
 * find_preamble() takes the first correlation above 0.80 in a ~500-symbol
 * window. With a short lead-in and noise, frame 0 can correlate below that
 * and frame 1 (480 symbols later) wins. Data used to start a fixed preamble
 * length after the found frame, i.e. one frame late: 1200L decoded as
 * ")THE LAZY DOG 1234567890..." and 600S as garbage.
 *
 * Each MS-DMT reference transmission is given 0, 5, 10 and 20 ms of lead-in
 * and white Gaussian noise of the signal's RMS over 0-24 kHz (about 7 dB in
 * 3 kHz), then decoded with the mode given. The payload must come out whole.
 *
 * usage: test_preamble_countdown REF_DIR
 */

#include "api/modem.h"

#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace m110a::api;

static const std::string PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

static std::string find_ref(const std::string& dir, const std::string& mode) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string want = "tx_" + mode + "_", found;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.rfind(want, 0) == 0 && n.size() > 4 && n.substr(n.size() - 4) == ".pcm") found = dir + "/" + n;
    }
    closedir(d);
    return found;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_preamble_countdown REF_DIR\n"); return 2; }
    const std::string dir = argv[1];
    const struct { const char* name; Mode mode; } modes[] = {
        {"150S", Mode::M150_SHORT}, {"150L", Mode::M150_LONG},
        {"300S", Mode::M300_SHORT}, {"300L", Mode::M300_LONG},
        {"600S", Mode::M600_SHORT}, {"600L", Mode::M600_LONG},
        {"1200S", Mode::M1200_SHORT}, {"1200L", Mode::M1200_LONG},
    };
    const int leads_ms[] = {0, 5, 10, 20};
    int failed = 0, run = 0;
    for (const auto& m : modes) {
        std::string path = find_ref(dir, m.name);
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { printf("%-6s missing reference in %s\n", m.name, dir.c_str()); failed++; continue; }
        size_t sz = f.tellg();
        f.seekg(0);
        std::vector<int16_t> raw(sz / 2);
        f.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
        double p = 0;
        for (int16_t v : raw) p += double(v) * v;
        const double rms = std::sqrt(p / raw.size());

        for (int lead : leads_ms) {
            const size_t nl = static_cast<size_t>(lead) * 48;
            std::mt19937 rng(7 + lead);
            std::normal_distribution<double> noise(0.0, rms);
            std::vector<double> y(nl + raw.size());
            double py = 0;
            for (size_t i = 0; i < y.size(); i++) {
                y[i] = (i >= nl ? raw[i - nl] : 0.0) + noise(rng);
                py += y[i] * y[i];
            }
            const double g = 7000.0 / std::sqrt(py / y.size()) / 32768.0;
            Samples s(y.size());
            for (size_t i = 0; i < y.size(); i++) s[i] = static_cast<float>(y[i] * g);

            RxConfig cfg;
            cfg.mode = m.mode;
            cfg.equalizer = Equalizer::DFE;
            std::stringstream sink;
            auto* o = std::cout.rdbuf(sink.rdbuf());
            auto* e = std::cerr.rdbuf(sink.rdbuf());
            DecodeResult r = decode(s, cfg);
            std::cout.rdbuf(o);
            std::cerr.rdbuf(e);

            std::string text(r.data.begin(), r.data.end());
            bool ok = text.rfind(PAYLOAD, 0) == 0;
            std::string shown;
            for (char c : text.substr(0, 40)) shown += (c >= 32 && c < 127) ? c : '.';
            printf("%-6s lead=%2d ms  %s  \"%s\"\n", m.name, lead, ok ? "PASS" : "FAIL", shown.c_str());
            run++;
            if (!ok) failed++;
        }
    }
    printf("=== %d/%d passed ===\n", run - failed, run);
    return failed ? 1 : 0;
}
