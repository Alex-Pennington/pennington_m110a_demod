// m110d_rx: decode one MIL-STD-188-110D Appendix D (3 kHz) transmission from a WAV file.
//
// Usage: m110d_rx [options] <file.wav>
//   --pn restart|continuous|auto   preamble PN alignment (default auto)
//   --bpsk-scr-shifts N            data scrambler iterations per BPSK symbol (default 3)
//   --code d9|alt|auto             K=7 tap reading: Figure D-9 (default), time-reversed, or best
//   --psk-scr-shifts N             data scrambler iterations per QPSK/8PSK symbol (default 3)
//   --qam-scale X                  QAM constellation scale vs the mini-probe (default 1; 0 = per-frame RMS)
//   --selftest                     check generated tables against the standard and exit
//   -v                             diagnostics on stderr
//
// Output is one key=value per line on stdout; test/110d/score.py parses it.
#include "m110d/appd_rx.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace m110d;

static std::string escape(const std::vector<uint8_t>& b) {
    std::string s;
    char buf[8];
    for (uint8_t c : b) {
        if (c >= 32 && c < 127 && c != '\\') s += (char)c;
        else { std::snprintf(buf, sizeof buf, "\\x%02x", c); s += buf; }
    }
    return s;
}

int main(int argc, char** argv) {
    RxOptions opt;
    std::string path;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--pn") {
            std::string v = next();
            opt.pn_mode = v == "restart" ? 1 : v == "continuous" ? 0 : -1;
        } else if (a == "--bpsk-scr-shifts") {
            opt.bpsk_scr_shifts = std::atoi(next().c_str());
        } else if (a == "--code") {
            std::string v = next();
            opt.code_reversed = v == "d9" ? 0 : v == "alt" ? 1 : -1;
        } else if (a == "--psk-scr-shifts") {
            opt.psk_scr_shifts = std::atoi(next().c_str());
        } else if (a == "--qam-scale") {
            opt.qam_scale = std::atof(next().c_str());
        } else if (a == "--selftest") {
            int bad = tables_selftest() + walsh_selftest() + tables_selftest_qam();
            std::printf("selftest_mismatches=%d\n", bad);
            return bad ? 1 : 0;
        } else if (a == "-v") {
            opt.verbose = true;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        } else {
            path = a;
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: m110d_rx [--pn restart|continuous|auto] [--bpsk-scr-shifts N] "
                             "[--code d9|alt|auto] [-v] <file.wav>\n");
        return 2;
    }
    std::vector<double> audio;
    int fs = 0;
    std::string err;
    if (!read_wav(path, audio, fs, err)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        return 2;
    }
    AppDRx rx(opt);
    RxResult r = rx.run(audio, fs);

    std::printf("file=%s\n", path.c_str());
    std::printf("acquired=%d\n", r.acquired ? 1 : 0);
    std::printf("acq_metric=%.3f\n", r.acq_metric);
    std::printf("acq_time_s=%.4f\n", r.acq_time);
    if (r.acquired) {
        std::printf("preamble=%s\n", r.preamble_shape.c_str());
        std::printf("pn=%s\n", r.pn_restart == 1 ? "restart" : "continuous");
        std::printf("freq_offset_hz=%.2f\n", r.freq_offset_hz);
        std::printf("clock_ppm=%.1f\n", r.clock_ppm);
        std::printf("superframes=%d\n", r.superframes_seen);
        std::printf("counts=");
        for (size_t i = 0; i < r.counts.size(); i++) std::printf("%s%d", i ? "," : "", r.counts[i]);
        std::printf("\n");
        std::printf("dcd=%d\n", r.count_parity_ok && r.wid.checksum_ok ? 1 : 0);
        std::printf("wid_raw=0x%03x\n", r.wid.raw);
        std::printf("wid_checksum=%s\n", r.wid.checksum_ok ? "ok" : "bad");
        std::printf("wid=%d\n", r.wid.waveform);
        std::printf("interleaver=%s\n", interleaver_name(r.wid.interleaver));
        std::printf("constraint_length=%d\n", r.wid.constraint);
        std::printf("pn_chip_errors=fixed:%d/%d,count:%d/%d,wid:%d/%d\n", r.chip_err_fixed, r.chips_fixed,
                    r.chip_err_count, r.chips_count, r.chip_err_wid, r.chips_wid);
        std::printf("fixed_symbol_chip_errors=");
        for (size_t i = 0; i < r.fixed_sym_err.size(); i++) std::printf("%s%d", i ? "," : "", r.fixed_sym_err[i]);
        std::printf("\n");
    }
    std::printf("payload=%s\n", r.payload_attempted ? "decoded" : "skipped");
    if (!r.payload_skip_reason.empty()) std::printf("payload_note=%s\n", r.payload_skip_reason.c_str());
    if (r.payload_attempted) {
        std::printf("blocks=%d\n", r.blocks_decoded);
        std::printf("code=%s\n", r.code_reversed == 1 ? "alt" : "d9");
        std::printf("reencode_agree=%.4f\n", r.reencode_agree);
        std::printf("probe_quality=%.3f\n", r.probe_quality);
        if (r.bits_per_sym > 1) {  // WID 6-13 demapper (appd_qam.h)
            std::printf("bits_per_symbol=%d\n", r.bits_per_sym);
            std::printf("evm_db=%.1f\n", r.qam.evm_db());
            std::printf("qam_scale=%.4f\n", r.qam.mean_scale());
            int unused = 0;
            double worst = 0;
            for (size_t i = 0; i < r.qam.hits.size(); i++) {
                unused += r.qam.hits[i] == 0;
                worst = std::max(worst, r.qam.max_err[i]);
            }
            std::printf("points_unused=%d/%zu\n", unused, r.qam.hits.size());
            std::printf("point_max_err=%.4f\n", worst);
        }
        std::printf("boundary_probe_after_frames=");
        for (size_t i = 0; i < r.boundary_frames.size(); i++) std::printf("%s%d", i ? "," : "", r.boundary_frames[i]);
        std::printf("\n");
        std::printf("eom=%d\n", r.eom ? 1 : 0);
        std::printf("bytes=%zu\n", r.bytes.size());
        std::printf("text=%s\n", escape(r.bytes).c_str());
        if (r.walsh_alt_blocks >= 0) std::printf("walsh_alt_last=%d/%d\n", r.walsh_alt_blocks, r.blocks_decoded);
    }
    return 0;
}
