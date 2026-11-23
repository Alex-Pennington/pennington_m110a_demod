/**
 * @file preamble_acq.h
 * @brief Preamble acquisition for MIL-STD-188-110A: timing, presence and
 *        carrier frequency offset from the 288-symbol common preamble.
 *
 * Every preamble frame (480 symbols, 200 ms at 2400 baud) starts with the
 * same 288-symbol common segment: nine 32-symbol blocks of the Walsh patterns
 * D0 D1 D3 D0 D1 D3 D1 D2 D0, each added (mod 8) to the 32-symbol preamble
 * scrambler. The correlator here splits that segment into its nine blocks:
 *
 *   c_k = sum_{i<32} x[t + (32k + i) * sps] * conj(p[32k + i])
 *
 *   metric  = sum_k |c_k| / sqrt(288 * sum_k E_k)       (E_k = block energy)
 *   freq_hz = arg(sum_k c_{k+1} * conj(c_k)) * baud / (2 pi * 32)
 *
 * Adding block magnitudes rather than the complex sum makes the metric
 * insensitive to a small carrier offset (a 15 Hz offset turns a block by
 * 1.26 rad and costs 6% of the metric). The block-to-block phase step is
 * unambiguous only over +/-37.5 Hz, and a block turns a whole 2 pi at 75 Hz,
 * so the correlation is taken for three offset hypotheses, -50, 0 and +50 Hz
 * (the reference turned by the hypothesis). The hypothesis with the largest
 * metric wins and its phase step gives the residual, within +/-25 Hz of it
 * across +/-75 Hz, where the metric costs at most 17%. Together they cover
 * +/-87.5 Hz (MIL-STD-188-110D C.6.3 and D.6.4 test at +/-75 Hz). The metric
 * is 1 for a clean preamble, about sqrt(g/(1+g)) at symbol SNR g, and about
 * 0.17 on noise.
 *
 * x is complex baseband with the nominal carrier at 0 Hz, filtered to the
 * signal bandwidth, at an integer number of samples per symbol.
 */

#ifndef M110A_SYNC_PREAMBLE_ACQ_H
#define M110A_SYNC_PREAMBLE_ACQ_H

#include "common/types.h"
#include "m110a/brain_preamble.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace m110a {

struct PreambleHit {
    size_t index = 0;      ///< sample index of the first common-preamble symbol
    float metric = 0.0f;   ///< 0..1, see file comment
    float freq_hz = 0.0f;  ///< carrier offset estimate (+ = signal above nominal)
};

class PreambleCorrelator {
public:
    static constexpr int BLOCKS = 9;
    static constexpr int BLOCK_LEN = 32;
    static constexpr int LENGTH = BLOCKS * BLOCK_LEN;  // 288

    static constexpr int HYPS = 3;
    static constexpr float HYP_STEP_HZ = 50.0f;  // hypotheses -50, 0, +50 Hz

    explicit PreambleCorrelator(int sps, float baud = 2400.0f)
        : sps_(sps), baud_(baud) {
        for (int i = 0; i < LENGTH; i++) {
            uint8_t d = brain::p_c_seq[i / BLOCK_LEN];
            uint8_t s = (brain::psymbol[d][i % 8] + brain::pscramble[i % BLOCK_LEN]) % 8;
            const complex_t r = std::conj(complex_t(brain::psk8_i[s], brain::psk8_q[s]));
            for (int h = 0; h < HYPS; h++) {
                const double w = -2.0 * M_PI * hyp_hz(h) * i / baud_;
                ref_conj_[h][i] = r * complex_t(static_cast<float>(std::cos(w)),
                                                static_cast<float>(std::sin(w)));
            }
        }
    }

    static float hyp_hz(int h) { return (h - HYPS / 2) * HYP_STEP_HZ; }

    int sps() const { return sps_; }

    /// Samples spanned from the start index to the last symbol, inclusive.
    size_t span() const { return static_cast<size_t>(LENGTH - 1) * sps_ + 1; }

    /**
     * Evaluate at x[0] = first symbol. x must hold span() samples.
     */
    float evaluate(const complex_t* x, float* freq_hz = nullptr) const {
        std::array<complex_t, LENGTH> v;
        float energy = 0.0f;
        for (int i = 0; i < LENGTH; i++) {
            v[i] = x[static_cast<size_t>(i) * sps_];
            energy += std::norm(v[i]);
        }
        float best = -1.0f;
        int best_h = 0;
        std::array<complex_t, BLOCKS> c, best_c{};
        for (int h = 0; h < HYPS; h++) {
            float mag = 0.0f;
            for (int k = 0; k < BLOCKS; k++) {
                complex_t acc(0.0f, 0.0f);
                const int base = k * BLOCK_LEN;
                for (int i = 0; i < BLOCK_LEN; i++) acc += v[base + i] * ref_conj_[h][base + i];
                c[k] = acc;
                mag += std::abs(acc);
            }
            if (mag > best) { best = mag; best_h = h; best_c = c; }
        }
        if (freq_hz) {
            std::complex<double> step(0.0, 0.0);
            for (int k = 0; k + 1 < BLOCKS; k++)
                step += std::complex<double>(best_c[k + 1]) * std::conj(std::complex<double>(best_c[k]));
            *freq_hz = hyp_hz(best_h) +
                       static_cast<float>(std::arg(step) * baud_ / (2.0 * M_PI * BLOCK_LEN));
        }
        return best / std::sqrt(static_cast<float>(LENGTH) * energy + 1e-30f);
    }

    /**
     * Best metric for start indices in [t0, t1), stepping by stride.
     * The frequency estimate is taken at the best index.
     */
    PreambleHit search(const std::vector<complex_t>& x, size_t t0, size_t t1,
                       size_t stride = 1) const {
        PreambleHit best;
        if (x.size() < span()) return best;
        const size_t last = x.size() - span();
        if (t1 > last + 1) t1 = last + 1;
        for (size_t t = t0; t < t1; t += stride) {
            float m = evaluate(&x[t]);
            if (m > best.metric) { best.metric = m; best.index = t; }
        }
        if (best.metric > 0.0f) evaluate(&x[best.index], &best.freq_hz);
        return best;
    }

private:
    int sps_;
    float baud_;
    std::array<std::array<complex_t, LENGTH>, HYPS> ref_conj_;
};

} // namespace m110a

#endif // M110A_SYNC_PREAMBLE_ACQ_H
