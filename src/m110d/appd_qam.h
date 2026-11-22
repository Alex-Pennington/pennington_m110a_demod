// MIL-STD-188-110D Appendix D, 3 kHz: soft demapping of the QPSK, 8PSK and QAM data
// symbols of waveforms 6-13, one data frame at a time.
//
//   D.5.1.2.1.2 / Table D-IV   QPSK di-bit -> 8PSK symbol number, leftmost bit older
//   D.5.1.2.1.3 / Table D-V    8PSK tri-bit -> 8PSK symbol number, leftmost bit oldest
//   D.5.1.2.2, Tables D-VII to D-X   QAM: bits are the symbol number, leftmost bit oldest
//   D.5.1.3                    PSK: transmitted = (symbol + rightmost 3 register bits) mod 8;
//                              QAM: transmitted = symbol XOR rightmost N register bits;
//                              generator reset to 1 at each data frame and iterated after
//                              each symbol (3 for 8PSK, N for 2^N QAM)
//
// The caller passes channel-corrected symbols (unit gain at the mini-probe amplitude). Soft
// output convention matches viterbi_tb(): positive = bit 0. Bits are appended oldest first.
#ifndef M110D_APPD_QAM_H
#define M110D_APPD_QAM_H

#include "m110d/appd_constellations.h"

#include <cmath>
#include <vector>

namespace m110d {

struct QamStats {
    int symbols = 0;
    double err2 = 0;   // sum |z - nearest point|^2 after scaling
    double sig2 = 0;   // sum |nearest point|^2
    double scale_sum = 0;
    int frames = 0;
    // per transmitted constellation point (8PSK symbol number, or QAM symbol number after
    // scrambling): times decided, and the largest |z - point| seen
    std::vector<int> hits;
    std::vector<double> max_err;
    double evm_db() const { return sig2 > 0 && err2 > 0 ? 10 * std::log10(err2 / sig2) : -99.0; }
    double mean_scale() const { return frames ? scale_sum / frames : 0; }
};

// Transmitted constellation point for data symbol value v (the bits, MSB = oldest) under
// scrambler value s.
inline cplx data_point(int bits, int v, int s) {
    switch (bits) {
        case 2: return psk8(kQpskTranscode[v] + s);
        case 3: return psk8(k8pskTranscode[v] + s);
        default: return qam_point(bits, v ^ s);
    }
}

// Demap one data frame. psk_shifts: scrambler iterations per QPSK/8PSK symbol (D.5.1.3
// gives 3 for 8PSK and does not state the QPSK count). qam_scale: factor applied to the
// Table D-VII..D-X points relative to the unit-amplitude mini-probe; 0 = scale each frame so
// the RMS of the received symbols matches the RMS of the constellation. The receiver default
// is 1: on the interop test set the table amplitudes at probe gain fit to -54 dB EVM, the
// per-frame RMS estimate to about -35 dB (docs/110d-appd.md, measured).
inline void demap_frame(int bits, const std::vector<cplx>& z, int psk_shifts, double qam_scale,
                        std::vector<double>& out, QamStats& st) {
    const int M = 1 << bits;
    const bool psk = bits <= 3;
    const int shifts = psk ? psk_shifts : bits;
    const unsigned mask = psk ? 7u : (unsigned)(M - 1);
    double a = 1.0;
    if (!psk) {
        if (qam_scale > 0) a = qam_scale;
        else {
            double pc = 0, pz = 0;
            for (int s = 0; s < M; s++) pc += std::norm(qam_point(bits, s));
            pc /= M;
            for (const cplx& v : z) pz += std::norm(v);
            pz /= z.empty() ? 1 : (double)z.size();
            a = pc > 0 ? std::sqrt(pz / pc) : 1.0;
        }
    }
    const int npts = psk ? 8 : M;
    if ((int)st.hits.size() != npts) { st.hits.assign(npts, 0); st.max_err.assign(npts, 0.0); }
    st.scale_sum += a;
    st.frames++;
    DataScrambler scr;  // D.5.1.3: initialised at the start of each data frame
    std::vector<double> d(M);
    for (const cplx& v : z) {
        int s = (int)(scr.reg & mask);
        scr.shift(shifts);
        int best = 0;
        for (int k = 0; k < M; k++) {
            d[k] = std::norm(v - a * data_point(bits, k, s));
            if (d[k] < d[best]) best = k;
        }
        st.symbols++;
        st.err2 += d[best];
        st.sig2 += std::norm(a * data_point(bits, best, s));
        int tp = bits == 2 ? (kQpskTranscode[best] + s) % 8
               : bits == 3 ? (k8pskTranscode[best] + s) % 8 : (best ^ s);
        st.hits[tp]++;
        st.max_err[tp] = std::max(st.max_err[tp], std::sqrt(d[best]) / a);
        // max-log soft bit, oldest (leftmost) bit first; positive = bit 0
        for (int b = bits - 1; b >= 0; b--) {
            double m0 = 1e300, m1 = 1e300;
            for (int k = 0; k < M; k++) {
                if ((k >> b) & 1) m1 = std::min(m1, d[k]);
                else m0 = std::min(m0, d[k]);
            }
            out.push_back((m1 - m0) / (a * a));
        }
    }
}

}  // namespace m110d

#endif
