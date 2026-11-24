/**
 * m110a_tx - MIL-STD-188-110A transmitter for the Harris modes and 75 bps: payload bits to a WAV.
 *
 * Transmit code only: it writes a file and keys nothing.
 *
 * Chain: payload bits -> [optional Harris framing] -> ModemTX::encode_bits (110C
 *        message end: EOM 4B65A5B2, 144-bit flush to the interleaver block, D1/D2
 *        in the last two known segments of every block) -> 3-frame short preamble
 *        + data, root-raised-cosine shaped, carrier 1800 Hz -> 48 kHz 16-bit mono.
 *        75S / 75L: the 75 bps Walsh-set waveform, EOM and flush, no probes, after a 3- or
 *        24-frame preamble (src/m110a/m75_codec.h, docs/m110a-75bps.md).
 *
 * Usage: m110a_tx [options] OUT.wav
 *   --mode M        600H (default), 1200H, 2400V, 75S or 75L
 *   --bits FILE     payload as ASCII 0/1 in transmit order (other characters ignored)
 *   --bytes FILE    payload as bytes, bit k = bit k%8 of byte k/8 (LSB first, as
 *                   m110a_rx prints hex=)
 *   --frame         the payload is whole vocoder units (60 bits for 600H, 81 for
 *                   1200H, 54 for 2400V); wrap them as the RT-1694D does: the 54-bit
 *                   header, the units, then for 600H and 1200H the 108-bit trailer
 *                   W, W' and zeros to a multiple of 16 bits, for 2400V 108 ones and
 *                   zeros to a multiple of 8 bits, as in the melp2400 recording
 *                   (docs/harris-600-format.md, docs/harris-tx.md); not for 75S / 75L
 *   --lead S        seconds of silence before the signal (default 2)
 *   --tail S        seconds of silence after it (default 1)
 *   --rms R         RMS of the signal part, full scale 1.0 (default 0.2136, the
 *                   7000/32768 MS-DMT reference level)
 *   --match WAV     take --rms from WAV: RMS of its 0.1 s blocks above half the
 *                   loudest block's RMS
 *   --carrier HZ    carrier frequency (default 1800)
 *   --rect          rectangular pulses instead of root-raised-cosine
 *   --leading       send the 288 Brain-modem leading symbols before the preamble
 *   --symbols OUT   write the 8PSK position (one byte, 0-7) of every symbol from
 *                   the first preamble symbol to the last data/probe symbol
 *   --payload-out OUT  write the payload bits sent (after --frame) as ASCII 0/1
 */

#include "api/modem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace m110a::api;

namespace {

// Measured in all nine 600_x and 1200_x streams (docs/harris-600-format.md)
const char* HEADER = "011111011010100110011010100100000100111110000011110010";
const char* TRAILER_W = "101001010010100010100000000100010101000101110011111101";
const char* TRAILER_W2 = "101001010010100010100000000100010101000101110011111100";

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "m110a_tx: %s\n", msg);
    fprintf(stderr,
        "usage: m110a_tx [--mode 600H|1200H|2400V|75S|75L] (--bits FILE | --bytes FILE) [--frame]\n"
        "                [--lead S] [--tail S] [--rms R | --match WAV] [--carrier HZ]\n"
        "                [--rect] [--leading] [--symbols OUT] [--payload-out OUT] OUT.wav\n");
    exit(2);
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool read_wav16(const std::string& path, std::vector<float>& out, int& rate) {
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
            rate = *reinterpret_cast<uint32_t*>(&b[4]);
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

// RMS of the 0.1 s blocks above half the loudest block's RMS: the signal, not the gaps
double active_rms(const std::vector<float>& x, int rate) {
    const size_t B = rate / 10;
    std::vector<double> ms;
    for (size_t i = 0; i + B <= x.size(); i += B) {
        double a = 0;
        for (size_t j = i; j < i + B; j++) a += double(x[j]) * x[j];
        ms.push_back(a / B);
    }
    double mx = 0;
    for (double v : ms) mx = std::max(mx, v);
    double acc = 0;
    int n = 0;
    for (double v : ms) if (v >= 0.25 * mx) { acc += v; n++; }
    return n ? std::sqrt(acc / n) : 0.0;
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
    std::string mode_s = "600H", bits_path, bytes_path, match, symbols_out, payload_out, out;
    double lead = 2.0, tail = 1.0, rms = 7000.0 / 32768.0, carrier = 1800.0;
    bool frame = false, rect = false, leading = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage(("missing value for " + a).c_str());
            return argv[++i];
        };
        if (a == "--mode") mode_s = next();
        else if (a == "--bits") bits_path = next();
        else if (a == "--bytes") bytes_path = next();
        else if (a == "--frame") frame = true;
        else if (a == "--lead") lead = atof(next().c_str());
        else if (a == "--tail") tail = atof(next().c_str());
        else if (a == "--rms") rms = atof(next().c_str());
        else if (a == "--match") match = next();
        else if (a == "--carrier") carrier = atof(next().c_str());
        else if (a == "--rect") rect = true;
        else if (a == "--leading") leading = true;
        else if (a == "--symbols") symbols_out = next();
        else if (a == "--payload-out") payload_out = next();
        else if (!a.empty() && a[0] == '-') usage(("unknown option " + a).c_str());
        else if (out.empty()) out = a;
        else usage("more than one output");
    }
    if (out.empty()) usage("no output WAV");
    if (bits_path.empty() == bytes_path.empty()) usage("give exactly one of --bits and --bytes");

    Mode mode;
    int unit;
    if (mode_s == "600H") { mode = Mode::M600_HARRIS; unit = 60; }
    else if (mode_s == "1200H") { mode = Mode::M1200_HARRIS; unit = 81; }
    else if (mode_s == "2400V") { mode = Mode::M2400_VOICE; unit = 54; }
    else if (mode_s == "75S") { mode = Mode::M75_SHORT; unit = 0; }
    else if (mode_s == "75L") { mode = Mode::M75_LONG; unit = 0; }
    else usage("--mode must be 600H, 1200H, 2400V, 75S or 75L");
    if (frame && unit == 0) usage("--frame is the Harris vocoder framing: not for 75S / 75L");

    std::string raw;
    if (!read_file(bits_path.empty() ? bytes_path : bits_path, raw)) usage("cannot read payload");
    std::vector<uint8_t> bits;
    if (!bits_path.empty()) {
        for (char c : raw) if (c == '0' || c == '1') bits.push_back(c - '0');
    } else {
        for (unsigned char c : raw) for (int k = 0; k < 8; k++) bits.push_back((c >> k) & 1);
    }
    if (frame) {
        if (bits.empty() || bits.size() % unit != 0) {
            fprintf(stderr, "m110a_tx: --frame needs whole %d-bit units, got %zu bits\n", unit, bits.size());
            return 2;
        }
        std::vector<uint8_t> f;
        for (const char* p = HEADER; *p; p++) f.push_back(*p - '0');
        f.insert(f.end(), bits.begin(), bits.end());
        if (mode == Mode::M2400_VOICE) {
            // melp2400, the one 2400V recording: 108 ones, 6 zeros to bit 15288, a
            // multiple of 8 and not of 16 (docs/harris-tx.md)
            f.insert(f.end(), 108, 1);
            while (f.size() % 8 != 0) f.push_back(0);
        } else {
            for (const char* p = TRAILER_W; *p; p++) f.push_back(*p - '0');
            for (const char* p = TRAILER_W2; *p; p++) f.push_back(*p - '0');
            while (f.size() % 16 != 0) f.push_back(0);
        }
        bits.swap(f);
    }
    if (bits.empty()) usage("empty payload");

    if (!match.empty()) {
        std::vector<float> m;
        int r = 0;
        if (!read_wav16(match, m, r)) usage("cannot read --match WAV (16-bit PCM)");
        rms = active_rms(m, r);
    }

    TxConfig cfg;
    cfg.mode = mode;
    cfg.sample_rate = 48000;
    cfg.carrier_freq = static_cast<float>(carrier);
    cfg.include_leading_symbols = leading;
    cfg.use_pulse_shaping = !rect;
    ModemTX tx(cfg);
    std::vector<int> syms;
    auto r = tx.encode_bits(bits, &syms);
    if (!r.ok()) {
        fprintf(stderr, "m110a_tx: encode failed: %s\n", r.error().message.c_str());
        return 1;
    }
    std::vector<float> sig = r.value();
    double cur = active_rms(sig, 48000);
    if (cur <= 0) { fprintf(stderr, "m110a_tx: silent output\n"); return 1; }
    const float g = static_cast<float>(rms / cur);
    std::vector<float> wav(static_cast<size_t>(lead * 48000), 0.0f);
    for (float v : sig) wav.push_back(v * g);
    wav.resize(wav.size() + static_cast<size_t>(tail * 48000), 0.0f);
    if (!write_wav16(out, wav, 48000)) { fprintf(stderr, "m110a_tx: cannot write %s\n", out.c_str()); return 1; }

    if (!symbols_out.empty()) {
        std::ofstream f(symbols_out, std::ios::binary);
        for (int s : syms) f.put(static_cast<char>(s));
    }
    if (!payload_out.empty()) {
        std::ofstream f(payload_out);
        for (uint8_t b : bits) f.put(b ? '1' : '0');
        f.put('\n');
    }
    printf("mode=%s payload_bits=%zu symbols=%zu signal_s=%.3f lead_s=%.3f tail_s=%.3f rms=%.4f out=%s\n",
           mode_s.c_str(), bits.size(), syms.size(), sig.size() / 48000.0, lead, tail, rms, out.c_str());
    return 0;
}
