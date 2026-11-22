// MIL-STD-188-110D Appendix D receiver: input stage.
//
// Reads a WAV from a file or from stdin and brings it to a sample rate the demodulator
// (appd_rx.h) runs at. That rate must be a whole multiple of the 2400 Bd symbol rate.
//
//   WAV    PCM 16/24/32-bit or IEEE float 32-bit, WAVE_FORMAT_EXTENSIBLE accepted, any
//          channel count (channel 0 used). A data chunk whose length field is 0 or
//          0xFFFFFFFF, or longer than what follows it (a streamed WAV), is read to EOF.
//   Rate   a multiple of 2400 Hz is passed through unchanged. Any other rate from 6840 Hz
//          up is resampled to kInternalRate (48000 Hz) by a rational polyphase FIR.
//
// Resampler: L/M = 48000/fs reduced by their gcd. Kaiser-windowed sinc prototype at L*fs,
// gain L. With fmin = min(fs, 48000):
//   passband   0 .. 0.4375*fmin   (0 .. 3500 Hz for 8 kHz input), ripple below 0.001 dB
//   stopband   0.5*fmin ..         (4000 Hz up for 8 kHz input), at least 100 dB down
//   cutoff     0.46875*fmin, Kaiser beta 10.06, length from Kaiser's formula, rounded up
//              to 2*H*L+1 so the group delay is a whole number of input samples' worth and
//              the output is time-aligned with the input.
// The 110D signal occupies 1800 +/- 1620 Hz (180 .. 3420 Hz), inside the passband.
// resampler_report() measures the designed filter's response; `m110d_rx --resampler-report`
// prints it.
#ifndef M110D_APPD_INPUT_H
#define M110D_APPD_INPUT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

namespace m110d {

constexpr int kInternalRate = 48000;
constexpr double kResampPass = 0.4375;   // passband edge, fraction of min(fs_in, fs_out)
constexpr double kResampStop = 0.5;      // stopband edge, fraction of min(fs_in, fs_out)
constexpr double kResampAttenDb = 100.0; // stopband attenuation
constexpr int kMinInputRate = 6840;      // 2 x 3420 Hz, the top of the 110D band

// ---------------------------------------------------------------------------------------------
// WAV parsing from a byte buffer.
// ---------------------------------------------------------------------------------------------
inline bool read_all(FILE* f, std::vector<uint8_t>& buf) {
    uint8_t tmp[1 << 16];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    return !std::ferror(f);
}

inline uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

inline bool parse_wav(const std::vector<uint8_t>& b, std::vector<double>& out, int& fs, std::string& err) {
    if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) {
        err = "not a RIFF/WAVE file"; return false;
    }
    // The RIFF length field is not used, so 0 and 0xFFFFFFFF (streamed WAV) are accepted.
    int ch = 0, bits = 0, fmt = 0;
    bool have_fmt = false;
    fs = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const uint8_t* id = &b[pos];
        uint32_t sz = le32(&b[pos + 4]);
        pos += 8;
        size_t rest = b.size() - pos;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (sz < 16 || sz > rest) { err = "short fmt chunk"; return false; }
            fmt = le16(&b[pos]); ch = le16(&b[pos + 2]); fs = (int)le32(&b[pos + 4]); bits = le16(&b[pos + 14]);
            if (fmt == 0xFFFE && sz >= 40) fmt = le16(&b[pos + 24]);  // WAVE_FORMAT_EXTENSIBLE sub-format
            have_fmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            if (!have_fmt) { err = "data chunk before fmt chunk"; return false; }
            bool pcm = fmt == 1 && (bits == 16 || bits == 24 || bits == 32);
            bool flt = fmt == 3 && bits == 32;
            if (!(pcm || flt) || ch < 1) { err = "only PCM 16/24/32-bit or float 32-bit supported"; return false; }
            size_t len = (sz == 0 || sz == 0xFFFFFFFFu || sz > rest) ? rest : sz;  // streamed: to EOF
            size_t bps = (size_t)bits / 8, frame = bps * (size_t)ch, n = len / frame;
            out.resize(n);
            for (size_t i = 0; i < n; i++) {
                const uint8_t* p = &b[pos + i * frame];
                double v;
                if (flt) { float f; std::memcpy(&f, p, 4); v = f; }
                else if (bits == 16) v = (int16_t)le16(p) / 32768.0;
                else if (bits == 24) v = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | (uint32_t)p[2] << 24)) / 2147483648.0;
                else v = (int32_t)le32(p) / 2147483648.0;
                out[i] = v;
            }
            return true;
        }
        if (sz > rest) break;
        pos += sz + (sz & 1);
    }
    err = "no data chunk";
    return false;
}

// path "-" reads stdin.
inline bool read_wav_any(const std::string& path, std::vector<double>& out, int& fs, std::string& err) {
    std::vector<uint8_t> buf;
    if (path == "-") {
        if (!read_all(stdin, buf)) { err = "read error on stdin"; return false; }
    } else {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { err = "cannot open"; return false; }
        bool ok = read_all(f, buf);
        std::fclose(f);
        if (!ok) { err = "read error"; return false; }
    }
    return parse_wav(buf, out, fs, err);
}

// ---------------------------------------------------------------------------------------------
// Rational polyphase resampler.
// ---------------------------------------------------------------------------------------------
inline double bessel_i0(double x) {
    double s = 1, t = 1, q = x * x / 4;
    for (int k = 1; k < 200 && t > 1e-17 * s; k++) { t *= q / ((double)k * k); s += t; }
    return s;
}

struct Resampler {
    int fs_in = 0, fs_out = 0, L = 1, M = 1, H = 0;  // H: half-length in input samples
    std::vector<double> h;                         // prototype, length 2*H*L+1, at L*fs_in

    bool design(int in, int out, std::string& err) {
        fs_in = in; fs_out = out;
        int g = std::gcd(in, out);
        L = out / g; M = in / g;
        if (L > 4096 || M > 4096) { err = "sample-rate ratio too awkward to resample"; return false; }
        double fmin = std::min(in, out), fh = (double)L * in;
        double fp = kResampPass * fmin, fsb = kResampStop * fmin, fc = 0.5 * (fp + fsb);
        double A = kResampAttenDb, beta = 0.1102 * (A - 8.7);
        double dw = 2 * M_PI * (fsb - fp) / fh;
        int n = (int)std::ceil((A - 7.95) / (2.285 * dw)) + 1;
        H = (n + 2 * L - 1) / (2 * L);
        int N = 2 * H * L + 1, c = H * L;
        h.assign(N, 0.0);
        double i0b = bessel_i0(beta);
        for (int k = 0; k < N; k++) {
            double t = (double)(k - c), r = t / c;
            double sinc = t == 0 ? 2 * fc / fh : std::sin(2 * M_PI * fc / fh * t) / (M_PI * t);
            h[k] = L * sinc * bessel_i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0b;
        }
        return true;
    }

    std::vector<double> run(const std::vector<double>& x) const {
        size_t nin = x.size(), nout = (size_t)(((long long)nin * L + M - 1) / M);
        std::vector<double> y(nout);
        long c = (long)H * L;
        for (size_t m = 0; m < nout; m++) {
            long t = (long)m * M + c;  // index in the upsampled domain, delay-compensated
            long i0 = t / L, ph = t - i0 * L;
            double acc = 0;
            for (long k = ph, i = i0; k < (long)h.size(); k += L, i--)
                if (i >= 0 && i < (long)nin) acc += h[k] * x[i];
            y[m] = acc;
        }
        return y;
    }

    // Response of the prototype at frequency f (Hz), relative to gain L.
    double gain_at(double f) const {
        double fh = (double)L * fs_in, re = 0, im = 0;
        for (size_t k = 0; k < h.size(); k++) {
            double a = -2 * M_PI * f / fh * (double)k;
            re += h[k] * std::cos(a); im += h[k] * std::sin(a);
        }
        return std::hypot(re, im) / L;
    }
};

// Bring audio to a rate appd_rx.h can run at. Returns false with err on an unusable rate.
inline bool to_internal_rate(std::vector<double>& audio, int& fs, std::string& err, bool verbose = false) {
    if (fs > 0 && fs % 2400 == 0) return true;
    if (fs < kMinInputRate) { err = "sample rate " + std::to_string(fs) + " Hz too low for the 110D band"; return false; }
    Resampler rs;
    if (!rs.design(fs, kInternalRate, err)) return false;
    if (verbose)
        std::fprintf(stderr, "input: %d Hz resampled to %d Hz (L=%d M=%d, %zu taps)\n", fs, kInternalRate, rs.L, rs.M,
                     rs.h.size());
    audio = rs.run(audio);
    fs = kInternalRate;
    return true;
}

// Measured response of the resampler for input rate fs: worst passband deviation (dB) and
// least stopband attenuation (dB) on a fine grid, the stopband scanned to the prototype's
// Nyquist frequency.
inline void resampler_report(int fs) {
    Resampler rs;
    std::string err;
    if (!rs.design(fs, kInternalRate, err)) { std::printf("error=%s\n", err.c_str()); return; }
    double fmin = std::min(fs, kInternalRate), fp = kResampPass * fmin, fsb = kResampStop * fmin;
    double fnyq = 0.5 * rs.L * fs, pdev = 0, smax = 0;
    for (int i = 0; i <= 2000; i++) {
        double g = rs.gain_at(fp * i / 2000.0);
        pdev = std::max(pdev, std::fabs(20 * std::log10(g)));
    }
    int ns = 20000;
    for (int i = 0; i <= ns; i++) smax = std::max(smax, rs.gain_at(fsb + (fnyq - fsb) * i / ns));
    std::printf("fs_in=%d\nfs_out=%d\nL=%d\nM=%d\ntaps=%zu\n", fs, kInternalRate, rs.L, rs.M, rs.h.size());
    std::printf("passband_hz=0-%.0f\npassband_dev_db=%.6f\n", fp, pdev);
    std::printf("stopband_hz=%.0f-%.0f\nstopband_atten_db=%.1f\n", fsb, fnyq, -20 * std::log10(smax));
    std::printf("gain_3420hz_db=%.4f\n", 20 * std::log10(rs.gain_at(3420)));
}

}  // namespace m110d

#endif
