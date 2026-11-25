// m75_codec.h - MIL-STD-188-110A/B/C/D serial tone 75 bps (fixed frequency, "75N"): the
// coding and symbol formation on both sides of the channel.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// Written from MIL-STD-188-110D section 5.3.2 (identical in 110C and 110C Change 1 for
// everything used here); section numbers below are 110D's. MS-DMT's m188110a source was
// read to confirm its behaviour matches (docs/m110a-75bps.md cites it by file:line); no
// code was taken from it.
//
// Transmit chain at 75 bps (5.3.2.3.3 to 5.3.2.3.8):
//   information bits -> [EOM 4B65A5B2 msb first, 144 zero flush bits] (5.3.2.3.1, .2)
//   -> rate 1/2 K=7 convolutional code, no repetition (Table V)
//   -> zeros to the end of the interleaver block (5.3.2.3.2)
//   -> block interleaver, 10 x 9 (short) or 20 x 36 (long), load row step 7, fetch
//      column decrement 7 (5.3.2.3.4, 5.3.2.3.5, Table VI)
//   -> two bits per channel symbol, first bit fetched is the first bit (Table VII)
//   -> modified Gray decoding, Table IX
//   -> 32 tribits per channel symbol, Table Xa, or Table Xb for every 45th (short) or
//      360th (long) set after the end of the sync preamble (5.3.2.3.7.1.1)
//   -> modulo-8 addition of the data scrambler, continuous from the first data symbol,
//      period 160 (5.3.2.3.8.1); no known symbols (5.3.2.2.2 b).
// 45 sets of 32 symbols are 1440 symbols, 9 scrambler periods, and 360 sets are 72: every
// interleaver block starts at scrambler offset 0 and its exceptional set at offset 128.

#ifndef M110A_M75_CODEC_H
#define M110A_M75_CODEC_H

#include "common/types.h"
#include "m110a/mode_config.h"
#include "modem/multimode_interleaver.h"
#include "modem/scrambler_fixed.h"
#include "modem/viterbi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace m110a {
namespace m75 {

constexpr int SET_SYMBOLS = 32;     // tribits per 4-ary channel symbol (Table XV)
constexpr int SCRAMBLER_PERIOD = 160;
constexpr uint32_t EOM_WORD = 0x4B65A5B2u;  // 5.3.2.3.1
constexpr int FLUSH_BITS = 144;            // 5.3.2.3.2

inline bool is_m75(ModeId m) { return m == ModeId::M75NS || m == ModeId::M75NL; }

/// Channel symbols (sets) per interleaver block: 45 short, 360 long (5.3.2.3.7.1.1)
inline int sets_per_block(ModeId m) {
    const auto& c = ModeDatabase::get(m).interleaver;
    return c.rows * c.cols / 2;
}

/// Table IX, modified Gray decoding at 75 bps: (first bit, last bit) -> channel symbol
inline int mgd(int first, int last) {
    static const int T[4] = {0, 1, 3, 2};  // 00->00, 01->01, 10->11, 11->10
    return T[(first << 1) | last];
}

/// Table IX inverted: channel symbol -> (first bit, last bit)
inline void mgd_inverse(int v, int& first, int& last) {
    static const int F[4] = {0, 0, 1, 1}, L[4] = {0, 1, 1, 0};
    first = F[v];
    last = L[v];
}

/// Table X: the 32 tribit numbers for channel symbol v (0-3), normal (a) or exceptional (b)
inline int tribit(int v, bool exceptional, int i) {
    static const int NORMAL[4][4] = {{0, 0, 0, 0}, {0, 4, 0, 4}, {0, 0, 4, 4}, {0, 4, 4, 0}};
    static const int EXCEPT[4][8] = {{0, 0, 0, 0, 4, 4, 4, 4},
                                     {0, 4, 0, 4, 4, 0, 4, 0},
                                     {0, 0, 4, 4, 4, 4, 0, 0},
                                     {0, 4, 4, 0, 4, 0, 0, 4}};
    return exceptional ? EXCEPT[v][i % 8] : NORMAL[v][i % 4];
}

/// True for the exceptional set: the 45th / 360th set after the preamble, and every one
/// that many sets after it (set_index counts from 0 at the first data symbol)
inline bool exceptional_set(long set_index, int per_block) {
    return set_index % per_block == per_block - 1;
}

inline std::complex<float> psk8(int t) {
    static const float R = 0.70710678f;
    static const std::complex<float> P[8] = {{1, 0}, {R, R}, {0, 1}, {-R, R},
                                             {-1, 0}, {-R, -R}, {0, -1}, {R, -R}};
    return P[t & 7];
}

/// K=7 rate 1/2 convolutional code, 5.3.2.3.3 and figure 4, in the shift convention of
/// M110ACodec::fec_encode (the newest bit enters at 0x40): T1 = 0x5B, T2 = 0x79, T1 first.
inline std::vector<int> conv_encode(const std::vector<int>& bits) {
    std::vector<int> out;
    out.reserve(bits.size() * 2);
    int state = 0;
    for (int b : bits) {
        state = (state >> 1) | (b ? 0x40 : 0);
        out.push_back(__builtin_parity(state & 0x5B));
        out.push_back(__builtin_parity(state & 0x79));
    }
    return out;
}

/// The information bits followed, with message_end, by the EOM most significant digit
/// first and 144 zero flush bits; without it, by 6 zeros that close the code.
inline std::vector<int> message_bits(std::vector<int> bits, bool message_end) {
    if (message_end) {
        for (int i = 31; i >= 0; i--) bits.push_back((EOM_WORD >> i) & 1);
        bits.insert(bits.end(), FLUSH_BITS, 0);
    } else {
        bits.insert(bits.end(), 6, 0);
    }
    return bits;
}

/**
 * Data symbols of a 75 bps message, from the first symbol after the preamble.
 *
 * @param bits         information bits, 0 or 1, in transmit order
 * @param mode         ModeId::M75NS or ModeId::M75NL
 * @param message_end  append EOM and flush (5.3.2.2.3, 5.3.2.2.4)
 * @param positions    if given, receives each symbol's 8-PSK position (0-7)
 */
inline std::vector<std::complex<float>> encode_symbols(const std::vector<int>& bits, ModeId mode,
                                                       bool message_end,
                                                       std::vector<int>* positions = nullptr) {
    const auto& cfg = ModeDatabase::get(mode);
    MultiModeInterleaver il(cfg.interleaver);
    const int block = il.block_size();
    const int per_block = sets_per_block(mode);

    std::vector<int> coded = conv_encode(message_bits(bits, message_end));
    while (coded.size() % block != 0) coded.push_back(0);

    DataScramblerFixed scr;
    std::vector<std::complex<float>> out;
    out.reserve(coded.size() / 2 * SET_SYMBOLS);
    if (positions) positions->clear();
    long set = 0;
    for (size_t b = 0; b < coded.size(); b += block) {
        std::vector<soft_bit_t> in(coded.begin() + b, coded.begin() + b + block);
        const auto fetched = il.interleave(in);
        for (int k = 0; k + 1 < block; k += 2, set++) {
            const int v = mgd(fetched[k] ? 1 : 0, fetched[k + 1] ? 1 : 0);
            const bool ex = exceptional_set(set, per_block);
            for (int i = 0; i < SET_SYMBOLS; i++) {
                const int t = (tribit(v, ex, i) + scr.next()) & 7;
                out.push_back(psk8(t));
                if (positions) positions->push_back(t);
            }
        }
    }
    return out;
}

/// Info bits -> coded bits as the transmitter sends them before interleaving, for the
/// receiver's agreement check (no tail: the bits given already end the code or not).
inline std::vector<int> reencode(const std::vector<uint8_t>& info) {
    std::vector<int> b(info.begin(), info.end());
    return conv_encode(b);
}

/**
 * Soft channel bits, two per set in set order (+ = 0, - = 1, the Viterbi convention), to
 * information bits: deinterleave whole blocks, then K=7 Viterbi over them all.
 *
 * @param soft          a whole number of interleaver blocks of soft bits
 * @param deinterleaved if given, receives the deinterleaved soft bits (code order)
 */
inline std::vector<uint8_t> decode_soft(const std::vector<soft_bit_t>& soft, ModeId mode,
                                        std::vector<soft_bit_t>* deinterleaved = nullptr) {
    const auto& cfg = ModeDatabase::get(mode);
    MultiModeInterleaver il(cfg.interleaver);
    const size_t block = static_cast<size_t>(il.block_size());
    std::vector<soft_bit_t> code;
    code.reserve(soft.size());
    for (size_t b = 0; b + block <= soft.size(); b += block) {
        std::vector<soft_bit_t> in(soft.begin() + b, soft.begin() + b + block);
        const auto d = il.deinterleave(in);
        code.insert(code.end(), d.begin(), d.end());
    }
    std::vector<uint8_t> info;
    if (!code.empty()) {
        ViterbiDecoder vit;
        vit.decode_block(code, info, true);
    }
    if (deinterleaved) *deinterleaved = std::move(code);
    return info;
}

/**
 * Max-log a-posteriori reliability of each information bit of the K=7 code (the
 * transmitter's conv_encode) over code-order soft bits (+ = 0): |L_k| = the best path
 * metric with bit k = 0 less the best with bit k = 1, in magnitude. The path the Viterbi
 * decoder takes has the sign of every L_k, so |L_k| is how far bit k's runner-up path lies
 * behind it. The trellis starts in state 0; `terminated`: it also ends there (the flush).
 */
inline std::vector<double> bit_reliability(const std::vector<soft_bit_t>& code, bool terminated) {
    constexpr int NS = 64;
    const size_t N = code.size() / 2;
    // state M = the last 6 inputs; input b: register M | b << 6, next state (M >> 1) | b << 5
    int out0[NS][2], out1[NS][2];
    for (int M = 0; M < NS; M++)
        for (int b = 0; b < 2; b++) {
            const int full = M | (b << 6);
            out0[M][b] = 1 - 2 * __builtin_parity(full & 0x5B);
            out1[M][b] = 1 - 2 * __builtin_parity(full & 0x79);
        }
    const double NEG = -1e300;
    std::vector<std::array<double, NS>> alpha(N + 1);
    alpha[0].fill(NEG);
    alpha[0][0] = 0.0;
    for (size_t k = 0; k < N; k++) {
        const double s0 = code[2 * k], s1 = code[2 * k + 1];
        for (int ns = 0; ns < NS; ns++) {
            const int b = ns >> 5, p0 = (ns & 31) << 1, p1 = p0 | 1;
            const double a = alpha[k][p0] + s0 * out0[p0][b] + s1 * out1[p0][b];
            const double c = alpha[k][p1] + s0 * out0[p1][b] + s1 * out1[p1][b];
            alpha[k + 1][ns] = std::max(a, c);
        }
    }
    std::array<double, NS> beta, nb;
    beta.fill(terminated ? NEG : 0.0);
    beta[0] = 0.0;
    std::vector<double> rel(N);
    for (size_t k = N; k-- > 0;) {
        const double s0 = code[2 * k], s1 = code[2 * k + 1];
        double best[2] = {NEG, NEG};
        for (int M = 0; M < NS; M++) {
            double bm = NEG;
            for (int b = 0; b < 2; b++) {
                const int ns = (M >> 1) | (b << 5);
                const double g = s0 * out0[M][b] + s1 * out1[M][b] + beta[ns];
                best[b] = std::max(best[b], alpha[k][M] + g);
                bm = std::max(bm, g);
            }
            nb[M] = bm;
        }
        rel[k] = std::fabs(best[0] - best[1]);
        beta = nb;
    }
    return rel;
}

/// First bit position of the EOM word (msb first) in the decoded bits, or -1
inline long find_eom(const std::vector<uint8_t>& info) {
    for (size_t p = 0; p + 32 <= info.size(); p++) {
        bool ok = true;
        for (int i = 0; i < 32 && ok; i++) ok = info[p + i] == ((EOM_WORD >> (31 - i)) & 1);
        if (ok) return static_cast<long>(p);
    }
    return -1;
}

/// The EOM taken with bit errors (a fade over it), recognised by the 144 flush bits after
/// it: up to 3 errors in the EOM with at most 8 ones in the flush, or up to 6 with at most
/// 4. The first such bit position, or -1. A payload is very unlikely to imitate EOM plus
/// flush by chance: a run of 176 zero bits is 16 bits from the EOM.
inline long find_eom_tolerant(const std::vector<uint8_t>& info) {
    for (size_t p = 0; p + 32 + FLUSH_BITS <= info.size(); p++) {
        int err = 0;
        for (int i = 0; i < 32 && err <= 6; i++) err += info[p + i] != ((EOM_WORD >> (31 - i)) & 1);
        if (err > 6) continue;
        int ones = 0;
        for (int i = 0; i < FLUSH_BITS && ones <= 8; i++) ones += info[p + 32 + i] & 1;
        if ((err <= 3 && ones <= 8) || ones <= 4) return static_cast<long>(p);
    }
    return -1;
}

/// Whole bytes, least significant bit first, from the first n bits
inline std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits, size_t n) {
    std::vector<uint8_t> out;
    n = std::min(n, bits.size());
    for (size_t i = 0; i + 8 <= n; i += 8) {
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) v |= static_cast<uint8_t>((bits[i + k] & 1) << k);
        out.push_back(v);
    }
    return out;
}

}  // namespace m75
}  // namespace m110a

#endif  // M110A_M75_CODEC_H
