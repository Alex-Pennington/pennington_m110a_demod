// m110d_chan: impair a test WAV with the MIL-STD-188-110D Appendix E channel and write a new WAV.
//
// Usage: m110d_chan [options] <in.wav> <out.wav>
//        m110d_chan --selftest
//        m110d_chan --list-presets
//
// The chain is described in src/channel/appe_channel.h. The output is one key=value per line.

#include "../src/channel/appe_channel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

bool read_wav(const std::string& path, std::vector<double>& out, int& fs, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    char hdr[12];
    if (!f.read(hdr, 12) || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
        err = "not a RIFF/WAVE file"; return false;
    }
    int ch = 0, bits = 0, fmt = 0;
    fs = 0;
    while (f) {
        char id[4]; uint32_t sz = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&sz), 4)) break;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (sz < 16) { err = "short fmt chunk"; return false; }
            std::vector<char> b(sz);
            f.read(b.data(), sz);
            if (sz & 1) f.seekg(1, std::ios::cur);
            uint16_t af, nc, bps; uint32_t sr;
            std::memcpy(&af, &b[0], 2); std::memcpy(&nc, &b[2], 2);
            std::memcpy(&sr, &b[4], 4); std::memcpy(&bps, &b[14], 2);
            fmt = af; ch = nc; fs = (int)sr; bits = bps;
        } else if (!std::memcmp(id, "data", 4)) {
            if (fmt != 1 || bits != 16 || ch < 1 || fs <= 0) { err = "only 16-bit PCM supported"; return false; }
            size_t n = sz / (2 * (size_t)ch);
            std::vector<int16_t> pcm(n * ch);
            f.read(reinterpret_cast<char*>(pcm.data()), (std::streamsize)(n * ch * 2));
            out.resize(n);
            for (size_t i = 0; i < n; i++) out[i] = pcm[i * ch] / 32768.0;  // channel 0
            return true;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    err = "no data chunk";
    return false;
}

bool write_wav(const std::string& path, const std::vector<double>& x, int fs, size_t& clipped) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t data = (uint32_t)(x.size() * 2), riff = 36 + data, sr = (uint32_t)fs, br = sr * 2, fmtsz = 16;
    uint16_t af = 1, nc = 1, ba = 2, bps = 16;
    f.write("RIFF", 4); f.write((char*)&riff, 4); f.write("WAVE", 4);
    f.write("fmt ", 4); f.write((char*)&fmtsz, 4); f.write((char*)&af, 2); f.write((char*)&nc, 2);
    f.write((char*)&sr, 4); f.write((char*)&br, 4); f.write((char*)&ba, 2); f.write((char*)&bps, 2);
    f.write("data", 4); f.write((char*)&data, 4);
    std::vector<int16_t> pcm(x.size());
    clipped = 0;
    for (size_t i = 0; i < x.size(); i++) {
        double v = std::lround(x[i] * 32768.0);
        if (v > 32767) { v = 32767; clipped++; }
        if (v < -32768) { v = -32768; clipped++; }
        pcm[i] = (int16_t)v;
    }
    f.write((char*)pcm.data(), (std::streamsize)(pcm.size() * 2));
    return (bool)f;
}

// Path sets. Sources are given per preset; see docs/110d-appd.md "Channel testing".
struct Preset {
    const char* name;
    const char* source;
    std::vector<appe::Path> paths;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = {
        {"awgn", "Table D-LXIV: single non-fading path", {}},
        {"poor", "110D D.6.1 / Table D-LXIV notes (ITU-R F.1487 mid-latitude disturbed): 2 equal paths, 2 ms, 1 Hz",
         {{0, 0, 1.0, 0}, {2.0, 0, 1.0, 0}}},
        {"moderate", "ITU-R F.1487 mid-latitude moderate (not filed in this repo): 2 equal paths, 1 ms, 0.5 Hz",
         {{0, 0, 0.5, 0}, {1.0, 0, 0.5, 0}}},
        {"good", "ITU-R F.1487 mid-latitude quiet (not filed in this repo): 2 equal paths, 0.5 ms, 0.1 Hz",
         {{0, 0, 0.1, 0}, {0.5, 0, 0.1, 0}}},
        {"static-wid2", "Table D-LXV 3 kHz WID 2: static, equal power, 0 / 3.0 / 9.0 ms",
         {{0, 0, 0, 0}, {3.0, 0, 0, 0}, {9.0, 0, 0, 0}}},
        {"static-wid10", "Table D-LXV 3 kHz WID 10: static, equal power, 0 / 2.0 / 4.5 ms",
         {{0, 0, 0, 0}, {2.0, 0, 0, 0}, {4.5, 0, 0, 0}}},
        {"static-wid12", "Table D-LXV 3 kHz WID 12: static, equal power, 0 / 1.5 ms",
         {{0, 0, 0, 0}, {1.5, 0, 0, 0}}},
    };
    return p;
}

void usage() {
    std::fprintf(stderr,
        "usage: m110d_chan [options] <in.wav> <out.wav>\n"
        "       m110d_chan --selftest | --list-presets\n"
        "  --seed N              random seed (default 1)\n"
        "  --snr DB              SNR in --noise-bw against the input's average burst power (default: no noise)\n"
        "  --noise-bw HZ         bandwidth the SNR is stated in (default 3000)\n"
        "  --preset NAME         path set: awgn, good, moderate, poor, static-wid2, static-wid10, static-wid12\n"
        "  --path D:G[:F[:S]]    add a path: delay ms, power dB, two-sigma fading Hz (0 = static), shift Hz\n"
        "                        (repeatable; replaces the preset's paths)\n"
        "  --doppler HZ          set the fading bandwidth of every path\n"
        "  --foff HZ             constant frequency offset\n"
        "  --fdrift HZ_PER_S     linear frequency drift\n"
        "  --sweep A:R[:S]       triangle sweep between -A and +A Hz at R Hz/s, starting at S Hz rising (D.6.5)\n"
        "  --ppm PPM             receiver sample-clock error (positive: more samples, lower tones)\n"
        "  --ppm-drift PPM_PER_S drift of that error\n"
        "  --bandpass LO:HI[:N]  receiver audio filter after the noise: Butterworth edges, -3 dB at LO and\n"
        "                        HI Hz, order N per edge (even, default 8); LO or HI 0 leaves that edge out\n"
        "  --gain DB             output gain before 16-bit quantisation (default: 0, lowered to keep the\n"
        "                        peak at -1 dBFS if it would clip)\n");
}

bool parse_path(const std::string& s, appe::Path& p) {
    double v[4] = {0, 0, 0, 0};
    int n = 0;
    size_t a = 0;
    while (n < 4) {
        size_t b = s.find(':', a);
        std::string tok = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        char* end = nullptr;
        v[n++] = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str() || *end) return false;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    if (n < 2) return false;
    p = {v[0], v[1], v[2], v[3]};
    return p.delay_ms >= 0 && p.doppler_hz >= 0;
}

// ---------------------------------------------------------------------------------------------
// Self-test after E.7: SNR calibration on a non-fading path (E.7.2), fading power (E.7.3),
// Doppler spectrum width (E.7.4), shift without image, clock re-sampling.
// ---------------------------------------------------------------------------------------------
double band_power(const std::vector<double>& x, int fs, double lo, double hi) {
    size_t n = appe::next_pow2(x.size());
    std::vector<appe::cd> a(n, 0.0);
    for (size_t i = 0; i < x.size(); i++) a[i] = x[i];
    appe::fft(a, false);
    double acc = 0;
    for (size_t k = 1; k < n / 2; k++) {
        double f = (double)k * fs / n;
        if (f >= lo && f < hi) acc += 2 * std::norm(a[k]);
    }
    return acc / ((double)n * (double)x.size());
}

int selftest() {
    int fails = 0;
    auto check = [&](const char* what, double got, double lo, double hi) {
        bool ok = got >= lo && got <= hi;
        fails += !ok;
        std::printf("%-44s %10.4f  [%g, %g]  %s\n", what, got, lo, hi, ok ? "PASS" : "FAIL");
    };
    const int fs = 48000;
    size_t n = (size_t)1 << 21;
    std::vector<double> tone(n);
    for (size_t i = 0; i < n; i++) tone[i] = 0.25 * std::cos(2 * appe::kPi * 1800.0 * i / fs);

    // E.7.2 at band centre and +-25% of 3 kHz: noise measured in 300-3300 Hz.
    for (double fc : {1800.0, 1050.0, 2550.0}) {
        std::vector<double> t(n);
        for (size_t i = 0; i < n; i++) t[i] = 0.25 * std::cos(2 * appe::kPi * fc * i / fs);
        appe::Config c; c.snr_db = 10; c.seed = 7;
        appe::Stats st;
        std::vector<double> y = appe::apply(t, fs, c, st);
        std::vector<double> nz(n);
        for (size_t i = 0; i < n; i++) nz[i] = y[i] - t[i];
        double snr = 10 * std::log10(st.span.power / band_power(nz, fs, 300, 3300));
        char lbl[64]; std::snprintf(lbl, sizeof lbl, "E.7.2 SNR dB, set 10, tone %.0f Hz", fc);
        check(lbl, snr, 9.75, 10.25);
    }
    // Re(analytic) reproduces the input.
    {
        std::vector<appe::cd> a = appe::analytic(tone);
        double e = 0;
        for (size_t i = 0; i < n; i++) e = std::max(e, std::fabs(a[i].real() - tone[i]));
        check("analytic: max |Re(xa) - x|", e, 0, 1e-9);
    }
    // Frequency shift by +30 Hz: image at 1770 Hz suppressed.
    {
        appe::Config c; c.foff_hz = 30;
        appe::Stats st;
        std::vector<double> y = appe::apply(tone, fs, c, st);
        double want = band_power(y, fs, 1825, 1835), img = band_power(y, fs, 1765, 1775);
        check("shift +30 Hz: image rejection dB", 10 * std::log10(want / (img + 1e-30)), 60, 400);
    }
    // Fading gain: unit average power and Gaussian spectrum with sigma = d/2 (E.5.4, E.7.4).
    for (double d : {1.0, 10.0}) {
        int gfs = (int)(d * 400);
        size_t m = (size_t)1 << 21;
        appe::Rng rng(11, 100);
        std::vector<appe::cd> g = appe::fading_gains(m, gfs, d, 0, rng);
        double p = 0;
        for (auto& v : g) p += std::norm(v);
        p /= (double)m;
        char lbl[64]; std::snprintf(lbl, sizeof lbl, "fading d=%g Hz: mean power dB", d);
        check(lbl, 10 * std::log10(p), -0.5, 0.5);
        // spectrum second moment, averaged over 4096-point segments
        size_t seg = 4096;
        std::vector<double> psd(seg, 0.0);
        for (size_t s0 = 0; s0 + seg <= m; s0 += seg) {
            std::vector<appe::cd> a(g.begin() + s0, g.begin() + s0 + seg);
            for (size_t i = 0; i < seg; i++) a[i] *= 0.5 - 0.5 * std::cos(2 * appe::kPi * i / seg);
            appe::fft(a, false);
            for (size_t k = 0; k < seg; k++) psd[k] += std::norm(a[k]);
        }
        double s0 = 0, s2 = 0;
        for (size_t k = 0; k < seg; k++) {
            double f = (double)(k < seg / 2 ? (long)k : (long)k - (long)seg) * gfs / seg;
            s0 += psd[k]; s2 += f * f * psd[k];
        }
        std::snprintf(lbl, sizeof lbl, "fading d=%g Hz: 2*sigma of spectrum, Hz", d);
        check(lbl, 2 * std::sqrt(s2 / s0), 0.9 * d, 1.1 * d);
    }
    // Clock: +100 ppm stretches the burst and lowers an 1800 Hz tone by 100 ppm.
    {
        appe::Config c; c.ppm = 100;
        appe::Stats st;
        std::vector<double> y = appe::apply(tone, fs, c, st);
        check("ppm +100: length ratio - 1, ppm", ((double)y.size() / n - 1) * 1e6, 99, 101);
        std::vector<appe::cd> a = appe::analytic(y);
        size_t i0 = 100000, i1 = y.size() - 100000;
        double dph = std::arg(a[i1] * std::conj(a[i0]));
        double cyc = std::round(1800.0 * (i1 - i0) / fs / (1 + 1e-4) - dph / (2 * appe::kPi));
        double f = (cyc + dph / (2 * appe::kPi)) * fs / (double)(i1 - i0);
        check("ppm +100: tone frequency offset, ppm", (f / 1800.0 - 1) * 1e6, -101, -99);
    }
    // Receiver audio filter, 300-2700 Hz, 8th order per edge: unity in band, -3 dB at the corners,
    // and on a tone through the chain the same gain as the design.
    {
        appe::Config c; c.bp_lo_hz = 300; c.bp_hi_hz = 2700; c.bp_order = 8;
        std::vector<appe::Biquad> f = appe::audio_filter(c, fs);
        auto db = [&](double hz) { return 20 * std::log10(appe::biquads_gain(f, hz, fs)); };
        check("bandpass 300-2700: gain at 1500 Hz, dB", db(1500), -0.05, 0.05);
        check("bandpass 300-2700: gain at 300 Hz, dB", db(300), -3.2, -2.8);
        check("bandpass 300-2700: gain at 2700 Hz, dB", db(2700), -3.2, -2.8);
        check("bandpass 300-2700: gain at 150 Hz, dB", db(150), -400, -46);
        check("bandpass 300-2700: gain at 5400 Hz, dB", db(5400), -400, -46);
        size_t m = (size_t)1 << 18;
        for (double hz : {1500.0, 3000.0}) {
            std::vector<double> t(m);
            for (size_t i = 0; i < m; i++) t[i] = 0.25 * std::cos(2 * appe::kPi * hz * i / fs);
            appe::Stats st;
            std::vector<double> y = appe::apply(t, fs, c, st);
            double pi_ = 0, po = 0;
            for (size_t i = m / 2; i < m; i++) { pi_ += t[i] * t[i]; po += y[i] * y[i]; }
            char lbl[64]; std::snprintf(lbl, sizeof lbl, "bandpass: tone %.0f Hz, chain minus design, dB", hz);
            check(lbl, 10 * std::log10(po / pi_) - db(hz), -0.05, 0.05);
        }
    }
    std::printf("selftest: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    appe::Config c;
    std::string preset = "awgn";
    std::vector<appe::Path> custom;
    double doppler = -1, gain_db = std::numeric_limits<double>::quiet_NaN();
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        auto num = [&]() -> double {
            std::string v = val();
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end) { std::fprintf(stderr, "%s: bad number %s\n", a.c_str(), v.c_str()); std::exit(2); }
            return d;
        };
        if (a == "--selftest") return selftest();
        else if (a == "--list-presets") {
            for (auto& p : presets()) std::printf("%-13s %s\n", p.name, p.source);
            return 0;
        }
        else if (a == "--seed") c.seed = (uint64_t)num();
        else if (a == "--snr") c.snr_db = num();
        else if (a == "--noise-bw") c.noise_bw_hz = num();
        else if (a == "--preset") preset = val();
        else if (a == "--path") {
            appe::Path p;
            std::string v = val();
            if (!parse_path(v, p)) { std::fprintf(stderr, "--path: bad value %s\n", v.c_str()); return 2; }
            custom.push_back(p);
        }
        else if (a == "--doppler") doppler = num();
        else if (a == "--foff") c.foff_hz = num();
        else if (a == "--fdrift") c.fdrift_hz_per_s = num();
        else if (a == "--sweep") {
            std::string v = val();
            double A = 0, R = 0, S = 0;
            if (std::sscanf(v.c_str(), "%lf:%lf:%lf", &A, &R, &S) < 2 || A <= 0 || R <= 0 || std::fabs(S) > A) {
                std::fprintf(stderr, "--sweep: bad value %s\n", v.c_str()); return 2;
            }
            c.sweep_amp_hz = A; c.sweep_rate_hz_per_s = R; c.sweep_start_hz = S;
        }
        else if (a == "--ppm") c.ppm = num();
        else if (a == "--ppm-drift") c.ppm_per_s = num();
        else if (a == "--gain") gain_db = num();
        else if (a == "--bandpass") {
            std::string v = val();
            double lo = 0, hi = 0;
            int ord = 8;
            if (std::sscanf(v.c_str(), "%lf:%lf:%d", &lo, &hi, &ord) < 2 || lo < 0 || hi < 0 || (hi > 0 && hi <= lo) ||
                ord < 2 || ord % 2) {
                std::fprintf(stderr, "--bandpass: bad value %s\n", v.c_str()); return 2;
            }
            c.bp_lo_hz = lo; c.bp_hi_hz = hi; c.bp_order = ord;
        }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
        else pos.push_back(a);
    }
    if (pos.size() != 2) { usage(); return 2; }

    const Preset* pr = nullptr;
    for (auto& p : presets()) if (preset == p.name) pr = &p;
    if (!pr) { std::fprintf(stderr, "unknown preset %s (--list-presets)\n", preset.c_str()); return 2; }
    c.paths = custom.empty() ? pr->paths : custom;
    if (doppler >= 0) {
        if (c.paths.empty()) c.paths.push_back(appe::Path{});
        for (auto& p : c.paths) p.doppler_hz = doppler;
    }

    std::vector<double> x;
    int fs = 0;
    std::string err;
    if (!read_wav(pos[0], x, fs, err)) { std::fprintf(stderr, "%s: %s\n", pos[0].c_str(), err.c_str()); return 1; }
    if (c.noise_bw_hz <= 0 || c.noise_bw_hz > fs / 2.0) { std::fprintf(stderr, "--noise-bw out of range\n"); return 2; }
    if (c.bp_hi_hz >= fs / 2.0) { std::fprintf(stderr, "--bandpass: HI at or above fs/2\n"); return 2; }

    appe::Stats st;
    std::vector<double> y = appe::apply(x, fs, c, st);

    double peak = 0;
    for (double v : y) peak = std::max(peak, std::fabs(v));
    double g;
    if (std::isnan(gain_db)) {
        const double lim = std::pow(10.0, -1.0 / 20);
        g = peak > lim ? lim / peak : 1.0;
    } else {
        g = std::pow(10.0, gain_db / 20);
    }
    for (auto& v : y) v *= g;
    size_t clipped = 0;
    if (!write_wav(pos[1], y, fs, clipped)) { std::fprintf(stderr, "%s: write failed\n", pos[1].c_str()); return 1; }

    std::printf("in=%s\nout=%s\nfs=%d\nseed=%llu\n", pos[0].c_str(), pos[1].c_str(), fs, (unsigned long long)c.seed);
    std::printf("samples_in=%zu\nsamples_out=%zu\n", st.n_in, st.n_out);
    std::printf("active_s=%.3f-%.3f\nsignal_power_dbfs=%.2f\n", (double)st.span.begin / fs, (double)st.span.end / fs,
                10 * std::log10(st.span.power + 1e-30));
    if (std::isfinite(c.snr_db))
        std::printf("snr_db=%.2f\nnoise_bw_hz=%.0f\nnoise_power_dbfs_fullband=%.2f\n", c.snr_db, c.noise_bw_hz,
                    10 * std::log10(st.noise_var));
    else
        std::printf("snr_db=inf\n");
    std::printf("preset=%s\npaths=%zu\n", custom.empty() ? pr->name : "custom", c.paths.size());
    for (size_t i = 0; i < c.paths.size(); i++)
        std::printf("path%zu=delay_ms:%.3f delay_samples:%d gain_db:%.2f doppler_hz:%.3f shift_hz:%.3f\n", i,
                    c.paths[i].delay_ms, i < st.delay_samples.size() ? st.delay_samples[i] : 0, c.paths[i].gain_db,
                    c.paths[i].doppler_hz, c.paths[i].shift_hz);
    std::printf("foff_hz=%.3f\nfdrift_hz_per_s=%.4f\nsweep=%.2f:%.3f:%.2f\nfreq_range_hz=%.2f..%.2f\n", c.foff_hz,
                c.fdrift_hz_per_s, c.sweep_amp_hz, c.sweep_rate_hz_per_s, c.sweep_start_hz, st.fmin_hz, st.fmax_hz);
    std::printf("ppm=%.3f\nppm_per_s=%.4f\n", c.ppm, c.ppm_per_s);
    if (c.bp_lo_hz > 0 || c.bp_hi_hz > 0)
        std::printf("bandpass_hz=%.0f-%.0f\nbandpass_order=%d\n", c.bp_lo_hz, c.bp_hi_hz, c.bp_order);
    std::printf("out_gain_db=%.2f\nclipped=%zu\n", 20 * std::log10(g), clipped);
    return 0;
}
