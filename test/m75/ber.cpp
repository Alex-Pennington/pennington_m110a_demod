// m75_ber - bit error rate of the 75 bps receiver (75S, 75L) through ModemRX, in process.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Per seed: payload "DE AAM402 " + seeded bytes -> ModemTX (48 kHz) -> silence before
// (0.5 to 2.5 s, seeded) and 1 s after -> MIL-STD-188-110D Appendix E channel
// (src/channel/appe_channel.h, the model m110d_chan runs: Watterson paths with Gaussian
// Doppler of two-sigma width, noise in 3 kHz against the signal's average power) ->
// interference (below) -> api::ModemRX::decode, forced to the mode or AUTO -> the decoded
// bytes against the payload, bit by bit. Bytes not delivered count as wrong bits; success
// and eom are reported, and do not change the bit count.
//
// Interference, each at a signal-to-interference ratio in dB (signal power over the keyed
// part, interferer power averaged over the whole capture):
//   --tone F:SIR        a CW tone at F Hz (repeat for several)
//   --sweep SIR         a swept CW, 0 to 3000 Hz at 150 Hz/s, sawtooth, seeded start
//   --fsk SIR           FSK 75 Bd, 1575 / 2425 Hz, random keying, phase continuous
//   --voice SIR         speech-like: voiced syllables (pitch 90-180 Hz, harmonics through
//                       three moving formants), unvoiced hiss, syllable and phrase pauses,
//                       300-3400 Hz (test/m75/ber.cpp speech_like)
//   --cochannel SIR     a second 75L transmission (other payload), carrier offset within
//                       +/-3 Hz, already on air: its preamble ends before the capture starts
// Output: one line per seed, then a total line.
//
// Usage: m75_ber --mode 75S|75L [--rx-mode AUTO|75S|75L] [--bytes N] [--seeds A:B]
//                [--snr DB] [--path DELAY_MS:GAIN_DB:DOPPLER_HZ ...] [--foff HZ]
//                [--tone F:SIR ...] [--sweep SIR] [--fsk SIR] [--voice SIR] [--cochannel SIR]
//                [--co-foff HZ] [--co-skip S]
//                [--no-excise] [--tune FIELD=VALUE ...] [--save SEED:OUT.f32] [-v]

#include "api/modem.h"
#include "channel/appe_channel.h"
#include "m110a/m75_rx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace m110a;

namespace {

constexpr int FS = 48000;
constexpr double PI = 3.14159265358979323846;

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "m75_ber: %s\n", msg);
    fprintf(stderr,
            "usage: m75_ber --mode 75S|75L [--rx-mode AUTO|75S|75L] [--bytes N] [--seeds A:B] [--snr DB]\n"
            "               [--path D:G:F ...] [--foff HZ] [--tone F:SIR ...] [--sweep SIR] [--fsk SIR]\n"
            "               [--voice SIR] [--cochannel SIR] [--no-excise] [--save SEED:OUT.f32]\n");
    exit(2);
}

api::Mode mode_of(const std::string& s) {
    if (s == "75S") return api::Mode::M75_SHORT;
    if (s == "75L") return api::Mode::M75_LONG;
    if (s == "AUTO") return api::Mode::AUTO;
    usage("mode must be 75S, 75L or AUTO");
}

std::vector<double> transmit(const std::vector<uint8_t>& data, api::Mode m) {
    api::TxConfig cfg;
    cfg.mode = m;
    cfg.sample_rate = FS;
    api::ModemTX tx(cfg);
    auto r = tx.encode(data);
    if (!r.ok()) { fprintf(stderr, "m75_ber: encode failed: %s\n", r.error().message.c_str()); exit(1); }
    std::vector<double> x(r.value().begin(), r.value().end());
    double e = 0;
    for (double v : x) e += v * v;
    const double g = (7000.0 / 32768.0) / std::sqrt(e / std::max<size_t>(1, x.size()));
    for (auto& v : x) v *= g;
    return x;
}

double mean_power(const std::vector<double>& x) {
    double e = 0;
    for (double v : x) e += v * v;
    return x.empty() ? 0 : e / x.size();
}

void add_scaled(std::vector<double>& y, const std::vector<double>& i, double ps, double sir_db) {
    const double pi_ = mean_power(i);
    if (pi_ <= 0) return;
    const double g = std::sqrt(ps / std::pow(10.0, sir_db / 10.0) / pi_);
    for (size_t k = 0; k < y.size() && k < i.size(); k++) y[k] += g * i[k];
}

std::vector<double> tone(size_t n, double f, appe::Rng& r) {
    std::vector<double> x(n);
    const double ph0 = 2 * PI * r.uniform();
    for (size_t k = 0; k < n; k++) x[k] = std::cos(2 * PI * f * k / FS + ph0);
    return x;
}

std::vector<double> sweep(size_t n, appe::Rng& r) {
    std::vector<double> x(n);
    double f = 3000.0 * r.uniform(), ph = 2 * PI * r.uniform();
    for (size_t k = 0; k < n; k++) {
        x[k] = std::cos(ph);
        ph = std::fmod(ph + 2 * PI * f / FS, 2 * PI);
        f += 150.0 / FS;
        if (f >= 3000.0) f -= 3000.0;
    }
    return x;
}

std::vector<double> fsk(size_t n, appe::Rng& r) {
    std::vector<double> x(n);
    const double sym = FS / 75.0;
    double ph = 2 * PI * r.uniform(), next = sym * r.uniform();
    bool mark = r.uniform() < 0.5;
    for (size_t k = 0; k < n; k++) {
        if (k >= next) { mark = r.uniform() < 0.5; next += sym; }
        x[k] = std::cos(ph);
        ph = std::fmod(ph + 2 * PI * (mark ? 1575.0 : 2425.0) / FS, 2 * PI);
    }
    return x;
}

// Speech-like test signal (not recorded speech): syllables 100-300 ms with 30-200 ms gaps,
// phrases of 1-3 s with 0.3-0.8 s pauses; 80% of syllables voiced (a harmonic series on a
// pitch of 90-180 Hz gliding +/-15%, each harmonic weighted by three formant resonances,
// F1 300-800, F2 900-2300, F3 2300-3000 Hz, bandwidths 80/120/200 Hz), 20% unvoiced (noise
// weighted toward 2-3.4 kHz); 20 ms raised-cosine edges; harmonics and noise kept in
// 300-3400 Hz (an SSB voice channel).
std::vector<double> speech_like(size_t n, appe::Rng& r) {
    std::vector<double> x(n, 0.0);
    auto U = [&](double a, double b) { return a + (b - a) * r.uniform(); };
    auto formant = [](double f, double fc, double bw) { return 1.0 / (1.0 + std::pow((f - fc) / (bw / 2), 2)); };
    size_t t = static_cast<size_t>(U(0, 0.5) * FS);
    double phrase_end = t + U(1, 3) * FS;
    std::vector<double> hp(60, 0.0);
    while (t < n) {
        const size_t len = static_cast<size_t>(U(0.1, 0.3) * FS);
        const bool voiced = r.uniform() < 0.8;
        const double f0a = U(90, 180), f0b = f0a * U(0.85, 1.15);
        const double F1 = U(300, 800), F2 = U(900, 2300), F3 = U(2300, 3000);
        const size_t edge = static_cast<size_t>(0.02 * FS);
        // unvoiced: noise through a 2-pole resonator near 2.8 kHz, crude but band-limited below
        double n1 = 0, n2 = 0;
        const double rr = 0.97, w = 2 * PI * 2800.0 / FS;
        for (size_t k = 0; k < len && t + k < n; k++) {
            double env = 1.0;
            if (k < edge) env = 0.5 - 0.5 * std::cos(PI * k / edge);
            if (len - k < edge) env = 0.5 - 0.5 * std::cos(PI * (len - k) / edge);
            double s = 0;
            if (voiced) {
                const double f0 = f0a + (f0b - f0a) * k / len;
                for (int h = 1; h * f0 < 3400.0 && h < 60; h++) {
                    const double f = h * f0;
                    hp[h] = std::fmod(hp[h] + 2 * PI * f / FS, 2 * PI);
                    if (f < 300.0) continue;
                    const double a = formant(f, F1, 80) + 0.6 * formant(f, F2, 120) + 0.3 * formant(f, F3, 200);
                    s += a * std::cos(hp[h]) / std::sqrt(h);
                }
            } else {
                const double v = r.normal() + 2 * rr * std::cos(w) * n1 - rr * rr * n2;
                n2 = n1;
                n1 = v;
                s = 0.05 * v;
            }
            x[t + k] = env * s;
        }
        t += len + static_cast<size_t>(U(0.03, 0.2) * FS);
        if (t > phrase_end) {
            t += static_cast<size_t>(U(0.3, 0.8) * FS);
            phrase_end = t + U(1, 3) * FS;
        }
    }
    return x;
}

struct Opts {
    api::Mode mode = api::Mode::M75_LONG, rx_mode = api::Mode::M75_LONG;
    bool rx_set = false;
    std::string mode_s = "75L";
    int bytes = 450;
    long seed0 = 1, seed1 = 1;
    double snr = std::numeric_limits<double>::infinity();
    std::vector<appe::Path> paths;
    double foff = 0;
    std::vector<std::pair<double, double>> tones;
    double sweep_sir = NAN, fsk_sir = NAN, voice_sir = NAN, co_sir = NAN;
    double co_foff = NAN, co_skip = NAN;  // pin the co-channel interferer's offset / entry (s)
    bool excise = true, verbose = false;
    long save_seed = -1;
    std::string save_path;
};

}  // namespace

int main(int argc, char** argv) {
    Opts o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { if (++i >= argc) usage("missing value"); return argv[i]; };
        if (a == "--mode") { o.mode_s = val(); o.mode = mode_of(o.mode_s); }
        else if (a == "--rx-mode") { o.rx_mode = mode_of(val()); o.rx_set = true; }
        else if (a == "--bytes") o.bytes = atoi(val().c_str());
        else if (a == "--seeds") {
            const std::string v = val();
            const auto c = v.find(':');
            o.seed0 = atol(v.c_str());
            o.seed1 = c == std::string::npos ? o.seed0 : atol(v.c_str() + c + 1);
        }
        else if (a == "--snr") o.snr = atof(val().c_str());
        else if (a == "--path") {
            appe::Path p;
            if (sscanf(val().c_str(), "%lf:%lf:%lf", &p.delay_ms, &p.gain_db, &p.doppler_hz) != 3) usage("--path D:G:F");
            o.paths.push_back(p);
        }
        else if (a == "--foff") o.foff = atof(val().c_str());
        else if (a == "--tone") {
            double f, s;
            if (sscanf(val().c_str(), "%lf:%lf", &f, &s) != 2) usage("--tone F:SIR");
            o.tones.push_back({f, s});
        }
        else if (a == "--sweep") o.sweep_sir = atof(val().c_str());
        else if (a == "--fsk") o.fsk_sir = atof(val().c_str());
        else if (a == "--voice") o.voice_sir = atof(val().c_str());
        else if (a == "--cochannel") o.co_sir = atof(val().c_str());
        else if (a == "--co-foff") o.co_foff = atof(val().c_str());
        else if (a == "--co-skip") o.co_skip = atof(val().c_str());
        else if (a == "--no-excise") o.excise = false;
        else if (a == "-v") o.verbose = true;
        else if (a == "--tune") {  // RxTuning field=value, for exploring the receiver's constants
            const std::string v = val();
            const auto e = v.find('=');
            if (e == std::string::npos) usage("--tune FIELD=VALUE");
            const std::string k = v.substr(0, e);
            const double x = atof(v.c_str() + e + 1);
            m75::RxTuning& t = m75::default_tuning();
            if (k == "window_ms") t.window_ms = x;
            else if (k == "profile_alpha") t.profile_alpha = x;
            else if (k == "weight_floor") t.weight_floor = x;
            else if (k == "noise_beta") t.noise_beta = x;
            else if (k == "soft_scale") t.soft_scale = x;
            else if (k == "min_quality_db") t.min_quality_db = x;
            else if (k == "min_block_quality_db") t.min_block_quality_db = x;
            else if (k == "alt_profile_alpha") t.alt_profile_alpha = x;
            else if (k == "alt_max_drift_ppm") t.alt_max_drift_ppm = x;
            else if (k == "max_drift_ppm") t.max_drift_ppm = x;
            else if (k == "alt_centre_alpha") t.alt_centre_alpha = x;
            else if (k == "alt_window_ms") t.alt_window_ms = x;
            else if (k == "alt_per_tap_noise") t.alt_per_tap_noise = x != 0;
            else if (k == "ex_detect_db") m75::default_excise_tuning().detect_db = x;
            else if (k == "ex_keep") m75::default_excise_tuning().keep = x;
            else if (k == "ex_window_s") m75::default_excise_tuning().window_s = x;
            else if (k == "ex_floor_frames") m75::default_excise_tuning().floor_frames = static_cast<int>(x);
            else usage("--tune: unknown field");
        }
        else if (a == "--save") {
            const std::string v = val();
            const auto c = v.find(':');
            if (c == std::string::npos) usage("--save SEED:PATH");
            o.save_seed = atol(v.c_str());
            o.save_path = v.substr(c + 1);
        }
        else usage("unknown option");
    }
    if (o.mode == api::Mode::AUTO) usage("--mode is the transmitted mode: 75S or 75L");
    if (!o.rx_set) o.rx_mode = o.mode;
    m75::default_tuning().excise = o.excise;

    long tot_bits = 0, tot_err = 0, n_ok = 0, n_exact = 0, n_eom = 0, n = 0, n_sync = 0;
    for (long seed = o.seed0; seed <= o.seed1; seed++) {
        appe::Rng rng(static_cast<uint64_t>(seed), 7);
        std::vector<uint8_t> data = {'D', 'E', ' ', 'A', 'A', 'M', '4', '0', '2', ' '};
        while (static_cast<int>(data.size()) < o.bytes) data.push_back(static_cast<uint8_t>(rng.next() >> 56));
        data.resize(o.bytes);
        const std::vector<double> sig = transmit(data, o.mode);
        const size_t lead = static_cast<size_t>((0.5 + 2.0 * rng.uniform()) * FS);
        std::vector<double> x(lead, 0.0);
        x.insert(x.end(), sig.begin(), sig.end());
        x.resize(x.size() + FS, 0.0);

        appe::Config c;
        c.paths = o.paths;
        c.snr_db = o.snr;
        c.foff_hz = o.foff;
        c.seed = static_cast<uint64_t>(seed) * 7919 + 13;
        appe::Stats st;
        std::vector<double> y = appe::apply(x, FS, c, st);
        const double ps = st.span.power;

        appe::Rng ir(static_cast<uint64_t>(seed), 31);
        for (const auto& t : o.tones) add_scaled(y, tone(y.size(), t.first, ir), ps, t.second);
        if (!std::isnan(o.sweep_sir)) add_scaled(y, sweep(y.size(), ir), ps, o.sweep_sir);
        if (!std::isnan(o.fsk_sir)) add_scaled(y, fsk(y.size(), ir), ps, o.fsk_sir);
        if (!std::isnan(o.voice_sir)) add_scaled(y, speech_like(y.size(), ir), ps, o.voice_sir);
        if (!std::isnan(o.co_sir)) {
            // a long 75L transmission, entered after its preamble and a seeded 0-5 s of data
            std::vector<uint8_t> d2;
            while (d2.size() < static_cast<size_t>(y.size() / FS * 10 + 200)) d2.push_back(static_cast<uint8_t>(ir.next() >> 56));
            const std::vector<double> s2 = transmit(d2, api::Mode::M75_LONG);
            const double u_skip = ir.uniform();
            const size_t skip = static_cast<size_t>((std::isnan(o.co_skip) ? 4.8 + 5.0 * u_skip : o.co_skip) * FS);
            std::vector<double> part(s2.begin() + std::min(skip, s2.size()), s2.end());
            part.resize(y.size(), 0.0);
            appe::Config c2;
            const double u_foff = ir.uniform();
            c2.foff_hz = std::isnan(o.co_foff) ? 6.0 * u_foff - 3.0 : o.co_foff;
            if (o.verbose) fprintf(stderr, "co-channel: entered %.4f s after its preamble start, offset %+.2f Hz\n",
                                   skip / double(FS), c2.foff_hz);
            appe::Stats st2;
            add_scaled(y, appe::apply(part, FS, c2, st2), ps, o.co_sir);
        }
        if (seed == o.save_seed) {
            std::ofstream f(o.save_path, std::ios::binary);
            for (double v : y) { const float s = static_cast<float>(v); f.write(reinterpret_cast<const char*>(&s), 4); }
        }

        api::RxConfig rc;
        rc.sample_rate = FS;
        rc.mode = o.rx_mode;
        api::Samples in(y.begin(), y.end());
        std::stringstream sink;
        std::streambuf* so = std::cout.rdbuf(sink.rdbuf());
        std::streambuf* se = std::cerr.rdbuf(sink.rdbuf());
        api::DecodeResult r = api::decode(in, rc);
        std::cout.rdbuf(so);
        std::cerr.rdbuf(se);
        if (o.verbose) {  // the receiver's own log lines
            std::string line;
            while (std::getline(sink, line))
                if (line.find("[RX M75]") != std::string::npos) fprintf(stderr, "%s\n", line.c_str());
        }

        long err = 0;
        for (size_t k = 0; k < data.size(); k++) {
            const uint8_t got = k < r.data.size() ? r.data[k] : static_cast<uint8_t>(~data[k]);
            err += __builtin_popcount(static_cast<unsigned>(got ^ data[k]));
        }
        const long bits = static_cast<long>(data.size()) * 8;
        const bool exact = r.success && r.eom_sequence && r.data == data &&
                           api::mode_name(r.mode) == o.mode_s;
        // sync: the decode found the preamble and named the mode (an error-free decode needs it)
        const bool sync = api::mode_name(r.mode) == o.mode_s && r.d1 >= 0;
        tot_bits += bits;
        tot_err += err;
        n_ok += r.success;
        n_exact += exact;
        n_eom += r.eom_sequence;
        n_sync += sync;
        n++;
        printf("seed=%ld bits=%ld err=%ld success=%d eom=%d exact=%d sync=%d mode=%s quality_db=%.1f snr_db=%.1f "
               "foff=%.1f err_msg=\"%s\"\n",
               seed, bits, err, r.success ? 1 : 0, r.eom_sequence ? 1 : 0, exact ? 1 : 0, sync ? 1 : 0,
               api::mode_name(r.mode).c_str(), r.quality_db, r.snr_db, r.freq_offset_hz,
               r.error ? r.error->message.c_str() : "");
        fflush(stdout);
    }
    printf("total messages=%ld bits=%ld err=%ld success=%ld eom=%ld exact=%ld sync=%ld\n", n, tot_bits, tot_err, n_ok,
           n_eom, n_exact, n_sync);
    return 0;
}
