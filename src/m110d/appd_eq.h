// MIL-STD-188-110D Appendix D, 3 kHz: probe-aided channel estimation and block MMSE-DFE for
// waveforms 1-13.
//
// What the waveform gives the receiver (D.5.2, D.5.2.2, Tables D-XI, D-XII, D-XXI):
//   every data block of U symbols is followed by a mini-probe of K known symbols, and the
//   preamble ends with one, so each data block sits between two known probes. A probe is its
//   base sequence (length L = 13, 16 or 25 at 3 kHz) cyclically extended to K, so its first
//   K-L symbols repeat the last K-L: a cyclic prefix. While the channel's delay spread is at
//   most K-L symbols, the last L received probe symbols hold no data energy, and a periodic
//   correlation with the base gives the channel impulse response (CIR) over L delays.
//
//   WID   U    K   L   K-L (ms)    Table D-LXV static channel at 3 kHz
//   1-2   48   48  25  23 (9.6)    WID 2: 0 / 3.0 / 9.0 ms
//   3-4   96   32  16  16 (6.7)
//   5-10  256  32  16  16 (6.7)    WID 10: 0 / 2.0 / 4.5 ms
//   11-12 360  24  13  11 (4.6)    WID 12: 0 / 1.5 ms
//   13    256  32  16  16 (6.7)
//
// So the design follows the frame: the CIR is estimated on every probe, interpolated across
// the data block between two probes, and the block is equalised as a whole with the probe
// symbols on both sides known. The equaliser is a block MMSE decision-feedback equaliser
// (Cholesky / LDL^H form of the MMSE-DFE): its feedforward part is the MMSE solution for the
// block, and decisions are fed back from the end of the block, next to the known following
// probe, towards its start. A DFE rather than a linear MMSE equaliser because the Table D-LXV
// channels are equal-power paths, whose spectral nulls a linear equaliser cannot fill without
// noise enhancement. Each equalised symbol comes with its own error variance, used to weight
// the soft decisions passed to the Viterbi decoder.
//
// The CIR support (which delays carry energy) is found once per transmission from the
// probe-averaged power-delay profile. Delays are only known modulo L from a periodic
// correlation, so each way of unwrapping the profile into a span of at most K-L+1 delays is
// tested by a least-squares fit on the probe samples that span leaves clean of data; the
// unwrapping with the smallest residual is kept. Per probe the CIR is then a least-squares
// fit on that support, using every probe sample the support leaves clean (K samples for a
// single path, as the one-tap estimate used before the equaliser).
#ifndef M110D_APPD_EQ_H
#define M110D_APPD_EQ_H

#include "m110d/appd_tables.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace m110d {

// Periodic extension of a mini-probe: symbol i of the infinitely repeated base sequence, in
// the probe's own indexing (index 0 = first transmitted probe symbol).
inline cplx probe_per(const std::vector<cplx>& p, int L, int i) { return p[(size_t)(((i % L) + L) % L)]; }

// Solve the n x n complex system A x = b in place (Gaussian elimination, partial pivoting).
inline bool solve_dense(std::vector<cplx>& A, std::vector<cplx>& b, int n) {
    for (int c = 0; c < n; c++) {
        int pr = c;
        for (int r = c + 1; r < n; r++)
            if (std::abs(A[(size_t)r * n + c]) > std::abs(A[(size_t)pr * n + c])) pr = r;
        if (std::abs(A[(size_t)pr * n + c]) < 1e-12) return false;
        if (pr != c) {
            for (int k = 0; k < n; k++) std::swap(A[(size_t)c * n + k], A[(size_t)pr * n + k]);
            std::swap(b[c], b[pr]);
        }
        for (int r = c + 1; r < n; r++) {
            cplx f = A[(size_t)r * n + c] / A[(size_t)c * n + c];
            if (f == cplx(0)) continue;
            for (int k = c; k < n; k++) A[(size_t)r * n + k] -= f * A[(size_t)c * n + k];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; r--) {
        cplx s = b[r];
        for (int k = r + 1; k < n; k++) s -= A[(size_t)r * n + k] * b[k];
        b[r] = s / A[(size_t)r * n + r];
    }
    return true;
}

// Least-squares CIR fit on one probe for a fixed support. Observations are probe-relative
// indices n in [n0, n1] = [max delay, K-1 + min delay], where every contributing symbol is a
// probe symbol. pinv (taps x obs) maps the observation vector to the tap estimates.
struct ProbeFit {
    std::vector<int> taps;  // delays in symbols, ascending
    int n0 = 0, n1 = -1;
    std::vector<cplx> pinv;  // taps.size() x nobs, row-major
    int nobs() const { return n1 - n0 + 1; }
    bool ok() const { return !taps.empty() && nobs() > (int)taps.size(); }
};

inline ProbeFit make_probe_fit(const std::vector<cplx>& p, int L, const std::vector<int>& taps) {
    ProbeFit f;
    f.taps = taps;
    if (taps.empty()) return f;
    const int K = (int)p.size();
    f.n0 = taps.back();
    f.n1 = K - 1 + taps.front();
    const int T = (int)taps.size(), N = f.nobs();
    if (N <= T) { f.n1 = f.n0 - 1; return f; }
    // A[n][t] = x[n - d_t]; normal matrix A^H A, then pinv = (A^H A)^-1 A^H row by row.
    std::vector<cplx> A((size_t)N * T);
    for (int n = 0; n < N; n++)
        for (int t = 0; t < T; t++) {
            int i = f.n0 + n - taps[t];
            A[(size_t)n * T + t] = (i >= 0 && i < K) ? p[(size_t)i] : cplx(0);
        }
    std::vector<cplx> G((size_t)T * T);
    for (int a = 0; a < T; a++)
        for (int b = 0; b < T; b++) {
            cplx s = 0;
            for (int n = 0; n < N; n++) s += std::conj(A[(size_t)n * T + a]) * A[(size_t)n * T + b];
            G[(size_t)a * T + b] = s;
        }
    f.pinv.assign((size_t)T * N, 0.0);
    for (int n = 0; n < N; n++) {
        std::vector<cplx> Gc = G, rhs(T);
        for (int t = 0; t < T; t++) rhs[t] = std::conj(A[(size_t)n * T + t]);
        if (!solve_dense(Gc, rhs, T)) { f.n1 = f.n0 - 1; f.pinv.clear(); return f; }
        for (int t = 0; t < T; t++) f.pinv[(size_t)t * N + n] = rhs[t];
    }
    return f;
}

// Apply a fit to observations obs[0..nobs-1] (probe indices n0..n1); returns the residual
// energy if res is given.
inline std::vector<cplx> apply_probe_fit(const ProbeFit& f, const std::vector<cplx>& p, const std::vector<cplx>& obs,
                                         double* res = nullptr) {
    const int T = (int)f.taps.size(), N = f.nobs(), K = (int)p.size();
    std::vector<cplx> h(T, 0.0);
    for (int t = 0; t < T; t++) {
        cplx s = 0;
        for (int n = 0; n < N; n++) s += f.pinv[(size_t)t * N + n] * obs[(size_t)n];
        h[t] = s;
    }
    if (res) {
        double e = 0;
        for (int n = 0; n < N; n++) {
            cplx m = 0;
            for (int t = 0; t < T; t++) {
                int i = f.n0 + n - f.taps[t];
                if (i >= 0 && i < K) m += h[t] * p[(size_t)i];
            }
            e += std::norm(obs[(size_t)n] - m);
        }
        *res = e;
    }
    return h;
}

// Block MMSE decision-feedback equaliser for one data block.
//
// Model: r[n] = sum_{d in taps} h_n[d] x[n-d] + w[n], n in [taps.front(), U-1 + taps.back()]
// (data-relative symbol indices), with the known symbols (x[i] for i < 0 or i >= U) already
// subtracted by the caller. hrow(n, t) gives h_n[taps[t]]. lambda = noise variance / symbol
// energy. G = H^H H + lambda I = L D L^H (banded, bandwidth B = span of taps); the
// feedforward output y = D^-1 L^-1 H^H r is L^H x plus the MMSE error, so decisions run from
// i = U-1 down to 0 cancelling the already-decided x[j > i]. decide(i, z) returns the decided
// point for unbiased input z. Outputs: z[i] unbiased equalised symbols, var[i] their error
// variance in units of the noise variance (1 / (D_i - lambda)).
template <class HRow, class Decide>
inline bool mmse_dfe_block(int U, const std::vector<int>& taps, const std::vector<cplx>& r, HRow hrow, double lambda,
                           Decide decide, std::vector<cplx>& z, std::vector<double>& var) {
    const int T = (int)taps.size();
    const int dlo = taps.front(), dhi = taps.back(), B = dhi - dlo;
    const int N = U + B;  // observations n = dlo .. U-1+dhi
    if ((int)r.size() != N) return false;
    // banded lower storage: Gb[i*(B+1) + k] = G[i][i-k]
    std::vector<cplx> Gb((size_t)U * (B + 1), 0.0), b(U, 0.0);
    std::vector<int> cols(T);
    std::vector<cplx> vals(T);
    for (int m = 0; m < N; m++) {
        const int n = m + dlo;
        int c = 0;
        for (int t = 0; t < T; t++) {
            int i = n - taps[t];
            if (i < 0 || i >= U) continue;
            cols[c] = i;
            vals[c] = hrow(n, t);
            c++;
        }
        for (int a = 0; a < c; a++) {
            b[cols[a]] += std::conj(vals[a]) * r[(size_t)m];
            for (int e = 0; e < c; e++) {
                if (cols[e] > cols[a]) continue;  // G[i][j] with i = cols[a] >= j = cols[e]
                Gb[(size_t)cols[a] * (B + 1) + (cols[a] - cols[e])] += std::conj(vals[a]) * vals[e];
            }
        }
    }
    // Gb[i][0] = sum conj(h) h: real; G[i][j] above is conj(H[n][i]) H[n][j], i >= j, which is
    // the (i, j) element of H^H H.
    for (int i = 0; i < U; i++) Gb[(size_t)i * (B + 1)] += lambda;
    // LDL^H: Lb[i*(B+1)+k] = L[i][i-k], k >= 1; D[i]
    std::vector<cplx> Lb((size_t)U * (B + 1), 0.0);
    std::vector<double> D(U);
    for (int j = 0; j < U; j++) {
        double dj = std::real(Gb[(size_t)j * (B + 1)]);
        for (int k = std::max(0, j - B); k < j; k++) dj -= std::norm(Lb[(size_t)j * (B + 1) + (j - k)]) * D[k];
        if (!(dj > 0)) return false;
        D[j] = dj;
        for (int i = j + 1; i <= std::min(U - 1, j + B); i++) {
            cplx s = Gb[(size_t)i * (B + 1) + (i - j)];
            for (int k = std::max(0, i - B); k < j; k++)
                s -= Lb[(size_t)i * (B + 1) + (i - k)] * std::conj(Lb[(size_t)j * (B + 1) + (j - k)]) * D[k];
            Lb[(size_t)i * (B + 1) + (i - j)] = s / dj;
        }
    }
    // forward: L v = b, y = D^-1 v
    std::vector<cplx> y(U);
    for (int i = 0; i < U; i++) {
        cplx s = b[i];
        for (int k = std::max(0, i - B); k < i; k++) s -= Lb[(size_t)i * (B + 1) + (i - k)] * y[k];
        y[i] = s;
    }
    for (int i = 0; i < U; i++) y[i] /= D[i];
    // backward decisions
    z.assign(U, 0.0);
    var.assign(U, 0.0);
    std::vector<cplx> xd(U, 0.0);
    for (int i = U - 1; i >= 0; i--) {
        cplx s = y[i];
        for (int j = i + 1; j <= std::min(U - 1, i + B); j++) s -= std::conj(Lb[(size_t)j * (B + 1) + (j - i)]) * xd[j];
        double g = 1.0 - lambda / D[i];
        if (g < 1e-6) g = 1e-6;
        z[i] = s / g;
        var[i] = 1.0 / std::max(D[i] - lambda, 1e-12);
        xd[i] = decide(i, z[i]);
    }
    return true;
}

}  // namespace m110d

#endif
