// MIL-STD-188-110D Appendix D, 3 kHz: Waveform ID 0 (75 bps, Walsh orthogonal modulation).
//
// Sections implemented:
//   D.5.1.2.3.1  di-bit -> 4-element Walsh sequence repeated 8 times; the last di-bit of every
//                interleaver block uses the alternate 8-element set repeated 4 times
//   D.5.1.4      trinomial (159, 31) 8PSK scrambling sequence, 2048 values, 16 iterations per
//                value, 32 values per channel symbol, wrapped at 2048, reset at each
//                interleaver boundary
//   Tables D-XI, D-XII: WID 0 has no data frames and no mini-probes (N/A at every bandwidth)
//   D.5.2.2      mini-probes are not sent for WID 0
//   Table D-XXXVII / D-LI (via interleaver_params), D.5.3.2 rate 1/2 K=7 tail-biting (via
//                viterbi_tb), D.5.4.3 EOM
//
// Included from appd_rx.h after RxResult and viterbi_tb are defined; not a standalone header.
#ifndef M110D_APPD_WALSH_H
#define M110D_APPD_WALSH_H

#include <array>
#include <functional>

namespace m110d {

// D.5.1.4: initial state of the 159-cell shift register, bitshift[0..158].
constexpr int kTriInit[159] = {
    0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1,
    1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0,
    1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1,
    0, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0,
    1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0,
    1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1};

constexpr int kWalshScrLen = 2048;

// D.5.1.4 tri(): 16 iterations, then the value of cells 2,1,0 (cell 2 the MSB).
inline const std::array<int, kWalshScrLen>& walsh_scrambler() {
    static const std::array<int, kWalshScrLen> tab = [] {
        std::array<int, kWalshScrLen> t{};
        int bs[159];
        for (int i = 0; i < 159; i++) bs[i] = kTriInit[i];
        for (int k = 0; k < kWalshScrLen; k++) {
            for (int j = 0; j < 16; j++) {
                int bitout = bs[158], bittap = bs[31];
                for (int i = 158; i >= 1; i--) bs[i] = bs[i - 1];
                bs[0] = bitout ^ bittap;
            }
            t[k] = (bs[2] << 2) + (bs[1] << 1) + bs[0];
        }
        return t;
    }();
    return tab;
}

// D.5.1.2.3.1: normal set (4 elements, repeated 8x) and alternate set (8 elements, repeated
// 4x, last di-bit of each interleaver block). Index = di-bit, first bit the MSB.
constexpr int kWalshData4[4][4] = {{0, 0, 0, 0}, {0, 4, 0, 4}, {0, 0, 4, 4}, {0, 4, 4, 0}};
constexpr int kWalshAlt8[4][8] = {{0, 0, 0, 0, 4, 4, 4, 4},
                                  {0, 4, 0, 4, 4, 0, 4, 0},
                                  {0, 0, 4, 4, 4, 4, 0, 0},
                                  {0, 4, 4, 0, 4, 0, 0, 4}};

inline int walsh_chip(int dibit, bool alt, int j) {
    return alt ? kWalshAlt8[dibit][j % 8] : kWalshData4[dibit][j % 4];
}

// Checks the generator against the D.5.1.4 example: the first 32 scramble values, and the
// 32 channel chips for di-bit 01. Returns the number of mismatches.
inline int walsh_selftest() {
    const int first32[32] = {5, 6, 2, 1, 7, 3, 1, 1, 6, 0, 5, 4, 0, 7, 7, 0,
                             5, 3, 1, 3, 3, 2, 2, 5, 5, 4, 7, 3, 5, 4, 3, 0};
    const int chips01[32] = {5, 2, 2, 5, 7, 7, 1, 5, 6, 4, 5, 0, 0, 3, 7, 4,
                             5, 7, 1, 7, 3, 6, 2, 1, 5, 0, 7, 7, 5, 0, 3, 4};
    const auto& s = walsh_scrambler();
    int bad = 0;
    for (int j = 0; j < 32; j++) {
        bad += s[j] != first32[j];
        bad += (s[j] + walsh_chip(1, false, j)) % 8 != chips01[j];
    }
    return bad;
}

struct WalshInput {
    std::function<cplx(long)> sample;  // symbol-spaced, frequency-corrected, sym index from t0
    long start_sym;                    // first data channel symbol
    cplx h0;                           // channel gain from the preamble fixed subsection
    long max_sym;                      // symbols available after t0
    bool verbose;
};

// D.5.4.5.1: scan the decoded bits for the 32-bit EOM.
inline bool walsh_find_eom(const std::vector<int>& bits, RxResult& r) {
    uint32_t sh = 0;
    for (size_t i = 0; i < bits.size(); i++) {
        sh = (sh << 1) | (uint32_t)bits[i];
        if (i >= 31 && sh == kEOM) {
            r.eom = true;
            r.eom_bit = (long)(i - 31);
            return true;
        }
    }
    return false;
}

// Coherent correlation of one 32-chip channel symbol against the 4 di-bit hypotheses.
inline void walsh_corr(const WalshInput& in, long pos, int scr_off, bool alt, cplx acc[4], double* energy) {
    const auto& scr = walsh_scrambler();
    double e = 0;
    cplx v[kWalshLen];
    for (int j = 0; j < kWalshLen; j++) { v[j] = in.sample(pos + j); e += std::norm(v[j]); }
    for (int d = 0; d < 4; d++) {
        cplx a = 0;
        for (int j = 0; j < kWalshLen; j++)
            a += v[j] * std::conj(psk8(scr[(scr_off + j) % kWalshScrLen] + walsh_chip(d, alt, j)));
        acc[d] = a;
    }
    if (energy) *energy = e;
}

inline void decode_walsh(const WalshInput& in, RxResult& r) {
    const WidField& w = r.wid;
    if (!w.checksum_ok) { r.payload_skip_reason = "WID checksum failed"; return; }
    if (w.constraint != 7) { r.payload_skip_reason = "K=9 not decoded"; return; }
    const WaveformParams* wp = waveform_params(0);
    const InterleaverParams* ip = interleaver_params(0, w.interleaver);
    if (!wp || !ip) { r.payload_skip_reason = "waveform/interleaver not defined at 3 kHz"; return; }
    r.payload_attempted = true;

    const int nbits = ip->bits;
    const int nsym = nbits / 2;  // one di-bit per channel symbol
    const ConvCode code = ConvCode::make(false);
    std::vector<int> outbits;
    long pos = in.start_sym;
    cplx h = in.h0;
    double agree_sum = 0, qsum = 0;
    long qn = 0;
    r.walsh_alt_blocks = 0;
    for (;;) {
        if (pos + (long)nsym * kWalshLen > in.max_sym) break;
        std::vector<double> blk(nbits);
        double bq = 0;
        int alt_wins = 0;
        for (int i = 0; i < nsym; i++) {
            bool alt = i == nsym - 1;
            int off = (int)(((long)i * kWalshLen) % kWalshScrLen);
            cplx acc[4];
            double e;
            walsh_corr(in, pos + (long)i * kWalshLen, off, alt, acc, &e);
            double m[4];
            for (int d = 0; d < 4; d++) m[d] = std::real(acc[d] * std::conj(h)) / (std::abs(h) + 1e-12);
            int dh = (int)(std::max_element(m, m + 4) - m);
            bq += e > 0 ? std::abs(acc[dh]) / std::sqrt(e * kWalshLen) : 0;
            // decision-directed gain tracking
            h = 0.8 * h + 0.2 * (acc[dh] / (double)kWalshLen);
            // soft bits, positive = bit 0; first bit (MSB) is fetched first (D.5.1.2.3.1)
            double b1_0 = std::max(m[0], m[1]), b1_1 = std::max(m[2], m[3]);
            double b0_0 = std::max(m[0], m[2]), b0_1 = std::max(m[1], m[3]);
            blk[2 * i] = (b1_0 - b1_1) / kWalshLen;
            blk[2 * i + 1] = (b0_0 - b0_1) / kWalshLen;
            if (alt) {
                // block-boundary check: does the alternate set beat the normal set here?
                cplx accn[4];
                walsh_corr(in, pos + (long)i * kWalshLen, off, false, accn, nullptr);
                double an = 0, aa = 0;
                for (int d = 0; d < 4; d++) { an = std::max(an, std::abs(accn[d])); aa = std::max(aa, std::abs(acc[d])); }
                alt_wins = aa > an;
            }
        }
        bq /= nsym;
        if (in.verbose)
            std::fprintf(stderr, "walsh block %d at sym %ld: corr %.3f alt_last %d\n", r.blocks_decoded, pos, bq,
                         alt_wins);
        if (bq < 0.5) break;  // no Walsh signal where a block is due
        qsum += bq;
        qn++;
        r.walsh_alt_blocks += alt_wins;
        pos += (long)nsym * kWalshLen;
        // D.5.3.3.2 load / D.5.3.3.3 fetch: coded bit n sits at (n*inc) mod size
        std::vector<double> coded(nbits);
        for (int n = 0; n < nbits; n++) coded[n] = blk[(size_t)(((long)n * ip->increment) % nbits)];
        std::vector<double> s1, s2;
        if (!depuncture(coded, ip->input_bits, *wp, s1, s2)) { r.payload_skip_reason = "puncture length mismatch"; return; }
        double ag = 0;
        std::vector<int> v = viterbi_tb(s1, s2, code, &ag);
        agree_sum += ag;
        int Nin = ip->input_bits;
        for (int i = 0; i < Nin; i++) outbits.push_back(v[((i - 6) % Nin + Nin) % Nin]);
        r.blocks_decoded++;
        if (walsh_find_eom(outbits, r)) break;
    }
    r.code_reversed = 0;
    r.reencode_agree = r.blocks_decoded ? agree_sum / r.blocks_decoded : 0;
    r.probe_quality = qn ? qsum / qn : 0;  // WID 0 has no probes: mean Walsh correlation
    long nb = r.eom ? r.eom_bit : (long)outbits.size();
    for (long i = 0; i + 8 <= nb; i += 8) {
        int b = 0;
        for (int k = 0; k < 8; k++) b = (b << 1) | outbits[i + k];  // MSB first
        r.bytes.push_back((uint8_t)b);
    }
    if (r.blocks_decoded == 0) r.payload_skip_reason = "no complete interleaver block";
}

}  // namespace m110d

#endif
