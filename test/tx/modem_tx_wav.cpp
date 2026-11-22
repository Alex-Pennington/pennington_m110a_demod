/**
 * modem_tx_wav - standard 110A modes through ModemTX::encode, to a 48 kHz WAV.
 *
 * Test tool for test/tx/tx_loopback.py (ctest tx_loopback_*). Transmit code only:
 * it writes a file and keys nothing.
 *
 * Usage: modem_tx_wav --mode M [--rect] [--leading] [--lead S] [--tail S] IN.bin OUT.wav
 *   --mode M     150S 150L 300S 300L 600S 600L 1200S 1200L 2400S 2400L 4800S
 *   --rect       TxConfig::use_pulse_shaping = false (default: true, RRC)
 *   --leading    TxConfig::include_leading_symbols = true (default: false, as
 *                m110a_server sends)
 *   --lead/--tail  seconds of silence before/after the signal (default 0.5)
 * The signal is scaled to 0.2136 RMS (7000/32768, the MS-DMT reference level).
 */

#include "api/modem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace m110a::api;

namespace {

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "modem_tx_wav: %s\n", msg);
    fprintf(stderr, "usage: modem_tx_wav --mode M [--rect] [--leading] [--lead S] [--tail S] IN.bin OUT.wav\n");
    exit(2);
}

bool write_wav16(const std::string& path, const std::vector<float>& x, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    const uint32_t data = static_cast<uint32_t>(x.size() * 2);
    f.write("RIFF", 4); u32(36 + data); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(data);
    for (float v : x) {
        long s = std::lround(v * 32767.0f);
        s = std::max(-32768L, std::min(32767L, s));
        u16(static_cast<uint16_t>(static_cast<int16_t>(s)));
    }
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, Mode> modes = {
        {"150S", Mode::M150_SHORT}, {"150L", Mode::M150_LONG}, {"300S", Mode::M300_SHORT},
        {"300L", Mode::M300_LONG}, {"600S", Mode::M600_SHORT}, {"600L", Mode::M600_LONG},
        {"1200S", Mode::M1200_SHORT}, {"1200L", Mode::M1200_LONG}, {"2400S", Mode::M2400_SHORT},
        {"2400L", Mode::M2400_LONG}, {"4800S", Mode::M4800_SHORT}};
    std::string mode_s, in, out;
    bool rect = false, leading = false;
    double lead = 0.5, tail = 0.5;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { if (++i >= argc) usage("missing value"); return argv[i]; };
        if (a == "--mode") mode_s = val();
        else if (a == "--rect") rect = true;
        else if (a == "--leading") leading = true;
        else if (a == "--lead") lead = atof(val().c_str());
        else if (a == "--tail") tail = atof(val().c_str());
        else if (a[0] == '-') usage("unknown option");
        else if (in.empty()) in = a;
        else if (out.empty()) out = a;
        else usage("too many arguments");
    }
    if (in.empty() || out.empty()) usage(nullptr);
    auto m = modes.find(mode_s);
    if (m == modes.end()) usage("unknown mode");
    std::ifstream f(in, std::ios::binary);
    if (!f) usage("cannot read IN.bin");
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    TxConfig cfg;
    cfg.mode = m->second;
    cfg.sample_rate = 48000;
    cfg.include_leading_symbols = leading;
    cfg.use_pulse_shaping = !rect;
    ModemTX tx(cfg);
    auto r = tx.encode(data);
    if (!r.ok()) {
        fprintf(stderr, "modem_tx_wav: encode failed: %s\n", r.error().message.c_str());
        return 1;
    }
    std::vector<float> sig = r.value();
    double e = 0;
    for (float v : sig) e += double(v) * v;
    const double rms = std::sqrt(e / std::max<size_t>(1, sig.size()));
    if (rms <= 0) { fprintf(stderr, "modem_tx_wav: silent output\n"); return 1; }
    const float g = static_cast<float>((7000.0 / 32768.0) / rms);
    std::vector<float> wav(static_cast<size_t>(lead * 48000), 0.0f);
    for (float v : sig) wav.push_back(v * g);
    wav.resize(wav.size() + static_cast<size_t>(tail * 48000), 0.0f);
    if (!write_wav16(out, wav, 48000)) { fprintf(stderr, "modem_tx_wav: cannot write %s\n", out.c_str()); return 1; }
    printf("mode=%s rrc=%d bytes=%zu samples=%zu raw_rms=%.4f\n", mode_s.c_str(), rect ? 0 : 1,
           data.size(), sig.size(), rms);
    return 0;
}
