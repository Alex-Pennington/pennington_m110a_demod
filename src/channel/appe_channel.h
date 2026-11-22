#ifndef M110_APPE_CHANNEL_H
#define M110_APPE_CHANNEL_H

// Real-audio HF channel simulator after MIL-STD-188-110D Appendix E, for 48 kHz (or any rate)
// mono test WAVs. Header-only; no dependency on the rest of the repo.
//
// Signal path, in order:
//   1. sample-clock offset and drift: the input is re-sampled on the receiver's clock
//   2. Hilbert transform to the analytic signal (E.4.2 note, E.6.2: FFT form)
//   3. Watterson multipath: N paths, each an integer-sample delay (E.5.2), a power share, and a
//      complex Gaussian-Doppler fading gain (E.5.4); a path with zero Doppler is static
//   4. frequency offset: constant, linear drift and/or the D.6.5 triangle sweep, applied as a
//      true shift of the analytic signal (no image)
//   5. real part (E.4.2 note)
//   6. white Gaussian noise set for the SNR in a stated bandwidth, against the average power of
//      the input signal (E.5.5)
//   7. optional receiver audio filter (not in Appendix E): a radio's SSB passband, lo..hi Hz,
//      as Butterworth high-pass and low-pass edges of a stated order each, bilinear transform
//      with pre-warped corners (-3 dB at lo and hi). It is minimum-phase like an analogue
//      crystal or LC filter, so the group delay rises toward both edges. It filters signal and
//      noise alike; the SNR of step 6 is still stated before it.
//
// The 110A headers awgn.h and watterson.h are not used: awgn.h sets the SNR against the whole
// Nyquist band and the whole-file power, and watterson.h applies |tap| only (no Doppler phase).
// Both are left unchanged for their 110A callers.

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace appe {

using cd = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------
// Reproducible random numbers: xoshiro256** seeded through SplitMix64, normals by Box-Muller.
// std::normal_distribution is implementation-defined, so it is not used: a seed gives the same
// impaired file with any compiler.
// ---------------------------------------------------------------------------------------------
inline uint64_t splitmix64(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class Rng {
public:
    // `stream` separates independent processes drawn from one user seed (noise, each path).
    Rng(uint64_t seed, uint64_t stream) {
        uint64_t s = seed ^ (stream * 0xD1B54A32D192ED03ull);
        for (auto& v : st_) v = splitmix64(s);
    }
    uint64_t next() {
        uint64_t r = rotl(st_[1] * 5, 7) * 9, t = st_[1] << 17;
        st_[2] ^= st_[0]; st_[3] ^= st_[1]; st_[1] ^= st_[2]; st_[0] ^= st_[3];
        st_[2] ^= t; st_[3] = rotl(st_[3], 45);
        return r;
    }
    double uniform() { return ((next() >> 11) + 1) * (1.0 / 9007199254740993.0); }  // (0,1]
    double normal() {
        if (have_) { have_ = false; return spare_; }
        double u1 = uniform(), u2 = uniform();
        double r = std::sqrt(-2.0 * std::log(u1));
        spare_ = r * std::sin(2 * kPi * u2);
        have_ = true;
        return r * std::cos(2 * kPi * u2);
    }
    // Complex normal with E|z|^2 = 1.
    cd cnormal() { return cd(normal(), normal()) * std::sqrt(0.5); }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t st_[4];
    bool have_ = false;
    double spare_ = 0;
};

// ---------------------------------------------------------------------------------------------
// FFT (radix 2, in place) and the analytic signal.
// ---------------------------------------------------------------------------------------------
inline void fft(std::vector<cd>& a, bool inverse) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * kPi / (double)len * (inverse ? 1 : -1);
        cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t j = 0; j < len / 2; j++) {
                cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& v : a) v /= (double)n;
}

inline size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Analytic signal x + j*H{x} by the FFT method (E.6.2). Zero-padded so the circular wrap of
// the Hilbert kernel lands in padding; Re(result) equals x.
inline std::vector<cd> analytic(const std::vector<double>& x) {
    size_t n = next_pow2(x.size() + 8192);
    std::vector<cd> a(n, 0.0);
    for (size_t i = 0; i < x.size(); i++) a[i] = x[i];
    fft(a, false);
    for (size_t k = 1; k < n / 2; k++) a[k] *= 2.0;
    for (size_t k = n / 2 + 1; k < n; k++) a[k] = 0.0;
    fft(a, true);
    a.resize(x.size());
    return a;
}

// ---------------------------------------------------------------------------------------------
// Average power of the transmission (E.5.5 "average power of the input signal").
// The test WAVs carry leading and trailing silence, so the power is averaged over the active
// span only: from the first to the last 10 ms window whose power is within `thresh_db` of the
// strongest window.
// ---------------------------------------------------------------------------------------------
struct ActiveSpan {
    size_t begin = 0, end = 0;
    double power = 0;
};

inline ActiveSpan active_span(const std::vector<double>& x, int fs, double thresh_db = -30.0) {
    ActiveSpan s;
    size_t w = (size_t)std::max(1, fs / 100);
    size_t nw = x.size() / w;
    if (nw == 0) return s;
    std::vector<double> p(nw, 0.0);
    double pmax = 0;
    for (size_t k = 0; k < nw; k++) {
        for (size_t i = k * w; i < (k + 1) * w; i++) p[k] += x[i] * x[i];
        p[k] /= (double)w;
        pmax = std::max(pmax, p[k]);
    }
    if (pmax <= 0) return s;
    double th = pmax * std::pow(10.0, thresh_db / 10.0);
    size_t a = 0, b = nw;
    while (a < nw && p[a] < th) a++;
    while (b > a && p[b - 1] < th) b--;
    s.begin = a * w;
    s.end = b * w;
    double acc = 0;
    for (size_t i = s.begin; i < s.end; i++) acc += x[i] * x[i];
    s.power = s.end > s.begin ? acc / (double)(s.end - s.begin) : 0;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Sample-clock offset and drift.
//
// ppm(t) = ppm0 + ppm_per_s * t is the receiver's sample clock error relative to the
// transmitter's. A positive ppm means the receiver takes (1 + ppm*1e-6) samples per transmitter
// sample: the burst lasts longer in samples and every tone appears lower by the same fraction
// (a sound-card clock error moves the carrier and the symbol timing together).
// Output sample n is the input evaluated at t_in(n) = sum_{k<n} 1 / (1 + ppm(k)*1e-6), by a
// Kaiser-windowed sinc (32 taps, 1024 phases, linear between phases).
// ---------------------------------------------------------------------------------------------
inline double bessel_i0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 50; k++) {
        t *= (x / (2 * k)) * (x / (2 * k));
        s += t;
        if (t < 1e-17 * s) break;
    }
    return s;
}

inline std::vector<double> resample_clock(const std::vector<double>& x, int fs, double ppm0,
                                          double ppm_per_s) {
    if (ppm0 == 0 && ppm_per_s == 0) return x;
    const int half = 16, phases = 1024;
    const double beta = 9.0;
    std::vector<double> tab((size_t)(phases + 1) * 2 * half);
    for (int ph = 0; ph <= phases; ph++) {
        double frac = (double)ph / phases;
        for (int k = 0; k < 2 * half; k++) {
            double d = (double)(k - half + 1) - frac;  // tap at input index i - half + 1 + k
            double sinc = std::fabs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d);
            double r = d / half;
            double win = std::fabs(r) >= 1 ? 0 : bessel_i0(beta * std::sqrt(1 - r * r)) / bessel_i0(beta);
            tab[(size_t)ph * 2 * half + k] = sinc * win;
        }
    }
    std::vector<double> y;
    y.reserve((size_t)(x.size() * 1.01) + 16);
    double t = 0;
    for (size_t n = 0;; n++) {
        if (t > (double)x.size() - 1) break;
        long i = (long)std::floor(t);
        double frac = t - (double)i;
        double fp = frac * phases;
        int ph = (int)fp;
        double a = fp - ph;
        const double* t0 = &tab[(size_t)ph * 2 * half];
        const double* t1 = &tab[(size_t)(ph + 1) * 2 * half];
        double acc = 0;
        for (int k = 0; k < 2 * half; k++) {
            long j = i - half + 1 + k;
            if (j < 0 || j >= (long)x.size()) continue;
            acc += x[(size_t)j] * ((1 - a) * t0[k] + a * t1[k]);
        }
        y.push_back(acc);
        double ppm = ppm0 + ppm_per_s * (double)n / fs;
        t += 1.0 / (1.0 + ppm * 1e-6);
    }
    return y;
}

// ---------------------------------------------------------------------------------------------
// Fading tap gain (E.5.4, Figure E-2).
//
// Complex white Gaussian noise filtered by f(t) = exp(-pi^2 t^2 d^2), |t| <= tau, where tau puts
// |f(tau)| at 1% of the peak; its power spectrum is Gaussian with standard deviation d/2, so d
// is the two-sigma fading bandwidth ("Doppler spread"). Gains are computed at 128*d (E.5.4: at
// least 32*d) and interpolated to the sample rate (E.5.4 DO) by cubic (Catmull-Rom)
// interpolation. The filter is scaled so the gain has unit average power.
// d == 0 gives a static path of gain 1. `shift_hz` adds a per-path Doppler shift.
// ---------------------------------------------------------------------------------------------
inline std::vector<cd> fading_gains(size_t n, int fs, double d_hz, double shift_hz, Rng& rng) {
    std::vector<cd> g(n, cd(1.0, 0.0));
    if (d_hz > 0) {
        double R = 128.0 * d_hz;
        double tau = std::sqrt(std::log(100.0)) / (kPi * d_hz);
        int L = (int)std::ceil(tau * R);
        std::vector<double> f(2 * L + 1);
        double e = 0;
        for (int k = -L; k <= L; k++) {
            double t = k / R;
            f[k + L] = std::exp(-kPi * kPi * t * t * d_hz * d_hz);
            e += f[k + L] * f[k + L];
        }
        for (auto& v : f) v /= std::sqrt(e);
        size_t m = (size_t)std::ceil((double)n * R / fs) + 4;
        std::vector<cd> w(m + 2 * L);
        for (auto& v : w) v = rng.cnormal();
        std::vector<cd> c(m);
        for (size_t i = 0; i < m; i++) {
            cd acc = 0;
            for (int k = 0; k <= 2 * L; k++) acc += f[k] * w[i + k];
            c[i] = acc;
        }
        for (size_t s = 0; s < n; s++) {
            double tc = (double)s * R / fs + 1.0;  // +1 so the cubic has a sample on the left
            size_t i = (size_t)tc;
            double u = tc - (double)i;
            cd p0 = c[i - 1], p1 = c[i], p2 = c[i + 1], p3 = c[i + 2];
            g[s] = p1 + 0.5 * u * (p2 - p0 + u * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
                                                    u * (3.0 * (p1 - p2) + p3 - p0)));
        }
    }
    if (shift_hz != 0)
        for (size_t s = 0; s < n; s++) g[s] *= std::polar(1.0, 2 * kPi * shift_hz * (double)s / fs);
    return g;
}

// ---------------------------------------------------------------------------------------------
// Channel configuration and the whole chain.
// ---------------------------------------------------------------------------------------------
struct Path {
    double delay_ms = 0;
    double gain_db = 0;     // relative power; all paths are scaled to unit total power (E.5.4 k)
    double doppler_hz = 0;  // two-sigma fading bandwidth; 0 = static
    double shift_hz = 0;    // per-path Doppler shift
};

struct Config {
    std::vector<Path> paths;  // empty: one static path of gain 1 (the AWGN channel)
    double snr_db = std::numeric_limits<double>::infinity();
    double noise_bw_hz = 3000.0;  // bandwidth the SNR is stated in (E.5.5; D.6.1 "specified bandwidth")
    double foff_hz = 0;           // constant frequency offset
    double fdrift_hz_per_s = 0;   // linear frequency drift
    double sweep_amp_hz = 0;      // D.6.5 triangle sweep: limits +-amp
    double sweep_rate_hz_per_s = 0;
    double sweep_start_hz = 0;    // sweep starts here, rising
    double ppm = 0;               // receiver sample-clock error, ppm
    double ppm_per_s = 0;         // its drift
    double active_thresh_db = -30;
    uint64_t seed = 1;
    double bp_lo_hz = 0, bp_hi_hz = 0;  // step 7; 0 = edge not applied
    int bp_order = 8;                   // per edge, even
};

// ---------------------------------------------------------------------------------------------
// Step 7: Butterworth edges as cascaded biquads (direct form I).
// ---------------------------------------------------------------------------------------------
struct Biquad {
    double b0, b1, b2, a1, a2;
};

// order/2 sections, high-pass (hp) or low-pass at fc Hz. Section k has the Butterworth pole-pair
// Q = 1 / (2 cos(pi (2k+1) / (2 order))).
inline std::vector<Biquad> butterworth(int order, double fc, int fs, bool hp) {
    std::vector<Biquad> v;
    const double w = 2 * kPi * fc / fs, cw = std::cos(w), sw = std::sin(w);
    for (int k = 0; k < order / 2; k++) {
        double q = 1.0 / (2 * std::cos(kPi * (2 * k + 1) / (2.0 * order)));
        double al = sw / (2 * q), a0 = 1 + al;
        Biquad b;
        if (hp) { b.b0 = (1 + cw) / 2; b.b1 = -(1 + cw); b.b2 = (1 + cw) / 2; }
        else { b.b0 = (1 - cw) / 2; b.b1 = 1 - cw; b.b2 = (1 - cw) / 2; }
        b.b0 /= a0; b.b1 /= a0; b.b2 /= a0;
        b.a1 = -2 * cw / a0; b.a2 = (1 - al) / a0;
        v.push_back(b);
    }
    return v;
}

inline std::vector<Biquad> audio_filter(const Config& c, int fs) {
    std::vector<Biquad> f;
    if (c.bp_lo_hz > 0) f = butterworth(c.bp_order, c.bp_lo_hz, fs, true);
    if (c.bp_hi_hz > 0) {
        std::vector<Biquad> l = butterworth(c.bp_order, c.bp_hi_hz, fs, false);
        f.insert(f.end(), l.begin(), l.end());
    }
    return f;
}

inline void run_biquads(const std::vector<Biquad>& f, std::vector<double>& y) {
    for (const Biquad& b : f) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& s : y) {
            double o = b.b0 * s + b.b1 * x1 + b.b2 * x2 - b.a1 * y1 - b.a2 * y2;
            x2 = x1; x1 = s; y2 = y1; y1 = o;
            s = o;
        }
    }
}

// |H(f)| of the cascade.
inline double biquads_gain(const std::vector<Biquad>& f, double hz, int fs) {
    std::complex<double> z1 = std::polar(1.0, -2 * kPi * hz / fs), z2 = z1 * z1, h = 1.0;
    for (const Biquad& b : f) h *= (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
    return std::abs(h);
}

struct Stats {
    size_t n_in = 0, n_out = 0;
    ActiveSpan span;                // on the re-sampled input
    double noise_var = 0;           // per-sample noise variance added
    std::vector<int> delay_samples; // per path, after E.5.2 rounding
    double fmin_hz = 0, fmax_hz = 0;
};

inline std::vector<double> apply(const std::vector<double>& in, int fs, const Config& c, Stats& st) {
    st.n_in = in.size();
    std::vector<double> x = resample_clock(in, fs, c.ppm, c.ppm_per_s);
    size_t n = x.size();
    st.n_out = n;
    st.span = active_span(x, fs, c.active_thresh_db);

    bool shift = c.foff_hz != 0 || c.fdrift_hz_per_s != 0 || (c.sweep_amp_hz > 0 && c.sweep_rate_hz_per_s > 0);
    std::vector<double> y;
    st.delay_samples.clear();
    if (c.paths.empty() && !shift) {
        y = x;
    } else {
        std::vector<cd> xa = analytic(x);
        std::vector<cd> ya(n, 0.0);
        std::vector<Path> paths = c.paths.empty() ? std::vector<Path>{Path{}} : c.paths;
        double ptot = 0;
        for (auto& p : paths) ptot += std::pow(10.0, p.gain_db / 10.0);
        for (size_t pi = 0; pi < paths.size(); pi++) {
            const Path& p = paths[pi];
            double amp = std::sqrt(std::pow(10.0, p.gain_db / 10.0) / ptot);
            long L = std::lround(p.delay_ms * 1e-3 * fs);  // E.5.2: rounded to the nearest sample
            st.delay_samples.push_back((int)L);
            Rng rng(c.seed, 100 + pi);
            std::vector<cd> g = fading_gains(n, fs, p.doppler_hz, p.shift_hz, rng);
            for (size_t s = (size_t)L; s < n; s++) ya[s] += amp * g[s] * xa[s - L];
        }
        double ph = 0, f = c.foff_hz, sw = c.sweep_start_hz, dir = 1;
        st.fmin_hz = st.fmax_hz = c.foff_hz + (c.sweep_amp_hz > 0 ? sw : 0);
        y.resize(n);
        for (size_t s = 0; s < n; s++) {
            double t = (double)s / fs;
            f = c.foff_hz + c.fdrift_hz_per_s * t;
            if (c.sweep_amp_hz > 0 && c.sweep_rate_hz_per_s > 0) {
                f += sw;
                sw += dir * c.sweep_rate_hz_per_s / fs;
                if (sw > c.sweep_amp_hz) { sw = 2 * c.sweep_amp_hz - sw; dir = -1; }
                if (sw < -c.sweep_amp_hz) { sw = -2 * c.sweep_amp_hz - sw; dir = 1; }
            }
            st.fmin_hz = std::min(st.fmin_hz, f);
            st.fmax_hz = std::max(st.fmax_hz, f);
            y[s] = std::real(ya[s] * std::polar(1.0, ph));
            ph += 2 * kPi * f / fs;
            if (ph > kPi) ph -= 2 * kPi;
            if (ph < -kPi) ph += 2 * kPi;
        }
    }

    // E.5.5: noise power in noise_bw_hz equals P_signal / SNR. White real noise of variance v
    // puts v * B / (fs/2) in a band of B Hz.
    st.noise_var = 0;
    if (std::isfinite(c.snr_db)) {
        double v = st.span.power / std::pow(10.0, c.snr_db / 10.0) * (fs / 2.0) / c.noise_bw_hz;
        st.noise_var = v;
        double sd = std::sqrt(v);
        Rng rng(c.seed, 1);
        for (auto& s : y) s += sd * rng.normal();
    }
    run_biquads(audio_filter(c, fs), y);
    return y;
}

}  // namespace appe

#endif  // M110_APPE_CHANNEL_H
