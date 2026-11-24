// stream_vs_whole - decode one audio buffer with ModemRX::decode() and with the streaming
// receiver (start / push_samples / finish), and compare (docs/m110a-streaming-rx.md).
//
// usage: stream_vs_whole [--chunk N] [--mode M] [--eq dfe|none] [--expect FILE] AUDIO
//   AUDIO  48 kHz mono: .wav (16-bit PCM), .f32 (float32), anything else raw s16le
//   --chunk samples per push_samples call (default 480, 10 ms)
//   --mode  force the mode (as m110a_rx's --mode names it); default AUTO
//   --expect the payload: report each decode exact or not
//
// Prints one line of key=value fields: the whole-burst and streaming verdicts and bytes
// (sha of each, same=1 when identical), the streaming blocks (count, bytes, last, the
// worst block's quality), when each block was reported (in input seconds, against the
// end of the block's symbols), and the CPU time of each decode. Exit 0 when the bytes and
// the verdict agree, 1 when they differ, 2 on a usage or input error.

#include "api/modem.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

using namespace m110a::api;

namespace {

std::vector<float> load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<float> x;
    auto ends = [&](const char* e) {
        return path.size() >= strlen(e) && path.compare(path.size() - strlen(e), strlen(e), e) == 0;
    };
    if (ends(".f32")) {
        x.resize(b.size() / 4);
        memcpy(x.data(), b.data(), x.size() * 4);
        return x;
    }
    size_t off = 0;
    if (ends(".wav") && b.size() >= 12 && !memcmp(b.data(), "RIFF", 4)) {
        size_t p = 12;
        while (p + 8 <= b.size()) {
            uint32_t sz;
            memcpy(&sz, &b[p + 4], 4);
            if (!memcmp(&b[p], "data", 4)) { off = p + 8; break; }
            p += 8 + sz + (sz & 1);
        }
    }
    const size_t n = (b.size() - off) / 2;
    x.resize(n);
    for (size_t i = 0; i < n; i++) {
        int16_t v;
        memcpy(&v, &b[off + 2 * i], 2);
        x[i] = v / 32768.0f;
    }
    return x;
}

std::string hash(const std::vector<uint8_t>& d) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    for (uint8_t c : d) { h ^= c; h *= 1099511628211ull; }
    char s[17];
    snprintf(s, sizeof s, "%016llx", (unsigned long long)h);
    return s;
}

double cpu_now() { return double(std::clock()) / CLOCKS_PER_SEC; }

Mode mode_of(const std::string& m) {
    static const struct { const char* n; Mode m; } k[] = {
        {"75S", Mode::M75_SHORT}, {"75L", Mode::M75_LONG}, {"150S", Mode::M150_SHORT}, {"150L", Mode::M150_LONG},
        {"300S", Mode::M300_SHORT}, {"300L", Mode::M300_LONG}, {"600S", Mode::M600_SHORT}, {"600L", Mode::M600_LONG},
        {"1200S", Mode::M1200_SHORT}, {"1200L", Mode::M1200_LONG}, {"2400S", Mode::M2400_SHORT},
        {"2400L", Mode::M2400_LONG}, {"4800S", Mode::M4800_SHORT}};
    for (const auto& e : k) if (m == e.n) return e.m;
    fprintf(stderr, "unknown mode %s\n", m.c_str());
    exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    size_t chunk = 480;
    std::string path, expect_path;
    RxConfig cfg;
    cfg.sample_rate = 48000;
    cfg.mode = Mode::AUTO;
    cfg.equalizer = Equalizer::DFE;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { if (i + 1 >= argc) exit(2); return argv[++i]; };
        if (a == "--chunk") chunk = size_t(std::max(1, atoi(val().c_str())));
        else if (a == "--mode") cfg.mode = mode_of(val());
        else if (a == "--eq") cfg.equalizer = val() == "none" ? Equalizer::NONE : Equalizer::DFE;
        else if (a == "--expect") expect_path = val();
        else path = a;
    }
    if (path.empty()) { fprintf(stderr, "usage: stream_vs_whole [--chunk N] [--mode M] [--eq dfe|none] [--expect FILE] AUDIO\n"); return 2; }
    const std::vector<float> x = load(path);
    std::vector<uint8_t> expect;
    if (!expect_path.empty()) {
        std::ifstream f(expect_path, std::ios::binary);
        expect.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    // whole burst
    double c0 = cpu_now();
    DecodeResult w;
    {
        ModemRX rx(cfg);
        w = rx.decode(x);
    }
    const double cpu_whole = cpu_now() - c0;

    // streaming
    c0 = cpu_now();
    ModemRX rx(cfg);
    rx.start();
    std::vector<BlockRecord> blocks;
    std::vector<double> at;  // input seconds when each block was reported
    double sync_at = -1;
    size_t fed = 0;
    bool done_early = false;
    for (size_t i = 0; i < x.size(); i += chunk) {
        const size_t n = std::min(chunk, x.size() - i);
        rx.push_samples(Samples(x.begin() + long(i), x.begin() + long(i + n)));
        fed = i + n;
        StreamSync sy;
        if (rx.take_sync(sy)) sync_at = double(fed) / 48000.0;
        for (auto& b : rx.take_blocks()) { blocks.push_back(std::move(b)); at.push_back(double(fed) / 48000.0); }
        if (rx.is_complete()) { done_early = true; break; }
    }
    DecodeResult s = rx.finish();
    for (auto& b : rx.take_blocks()) { blocks.push_back(std::move(b)); at.push_back(double(fed) / 48000.0); }
    const double cpu_stream = cpu_now() - c0;

    std::vector<uint8_t> cat;
    float worst = 99;
    bool all_pass = true;
    for (const auto& b : blocks) {
        cat.insert(cat.end(), b.data.begin(), b.data.end());
        worst = std::min(worst, b.block_quality_db);
        all_pass = all_pass && b.passes;
    }
    const bool same = w.data == s.data && w.success == s.success && w.eom_detected == s.eom_detected;
    auto exact = [&](const std::vector<uint8_t>& d) { return expect.empty() ? -1 : int(d == expect); };
    std::string errw = w.error ? w.error->message : "", errs = s.error ? s.error->message : "";
    for (auto* e : {&errw, &errs}) for (auto& c : *e) if (c == ' ') c = '_';
    printf("file=%s whole_ok=%d whole_bytes=%zu whole_hash=%s whole_eom=%d whole_q=%.2f whole_bqmin=%.2f "
           "stream_ok=%d stream_bytes=%zu stream_hash=%s stream_eom=%d stream_q=%.2f stream_bqmin=%.2f "
           "same=%d blocks=%zu blocks_bytes=%zu blocks_same=%d blocks_pass=%d blocks_worst=%.2f "
           "whole_exact=%d stream_exact=%d blocks_exact=%d mode=%d sync_at=%.3f ended_early=%d "
           "cpu_whole=%.3f cpu_stream=%.3f input_s=%.3f err_whole=%s err_stream=%s\n",
           path.c_str(), int(w.success), w.data.size(), hash(w.data).c_str(), int(w.eom_detected), w.quality_db,
           w.block_quality_min_db, int(s.success), s.data.size(), hash(s.data).c_str(), int(s.eom_detected),
           s.quality_db, s.block_quality_min_db, int(same), blocks.size(), cat.size(), int(cat == s.data),
           int(all_pass), blocks.empty() ? 0.0f : worst, exact(w.data), exact(s.data), exact(cat), int(s.mode),
           sync_at, int(done_early), cpu_whole, cpu_stream, double(x.size()) / 48000.0,
           errw.empty() ? "-" : errw.c_str(), errs.empty() ? "-" : errs.c_str());
    for (size_t i = 0; i < blocks.size(); i++)
        printf("  block %zu at=%.3f bytes=%zu q=%.2f/%.2f span_q=%.2f pass=%d last=%d eom=%d\n", blocks[i].index, at[i],
               blocks[i].data.size(), blocks[i].block_quality_db, blocks[i].block_quality_threshold_db,
               blocks[i].quality_db, int(blocks[i].passes), int(blocks[i].last), int(blocks[i].eom));
    return same ? 0 : 1;
}
