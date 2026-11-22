// MIL-STD-188-110D Appendix D (WBHF), 3 kHz single serial tone: constants and tables.
//
// Every value below is taken from MIL-STD-188-110D (Distribution Statement A, approved for
// public release), Appendix D. Section and table numbers are cited at each item. Tables that
// the published PDF carries only as images (D-XVIII, D-XIX) were transcribed by hand from the
// page image and are checked against received signal by m110d_rx --check-tables.
#ifndef M110D_APPD_TABLES_H
#define M110D_APPD_TABLES_H

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace m110d {

using cplx = std::complex<double>;

// Table D-I: 3 kHz bandwidth -> 2400 symbols/s on a 1800 Hz sub-carrier (300 + BW/2).
constexpr double kSymbolRate = 2400.0;
constexpr double kCarrierHz = 1800.0;
// D.5.1.5: square-root raised cosine, 35% roll-off, recommended for TX and RX.
constexpr double kRolloff = 0.35;

// Table D-XIII: preamble Walsh channel symbol length at 3 kHz.
constexpr int kWalshLen = 32;

// Table D-XIV: di-bit -> 4-element Walsh sequence of 8PSK symbols (0 or 4), repeated to
// kWalshLen chips.
constexpr int kWalsh4[4][4] = {{0, 0, 0, 0}, {0, 4, 0, 4}, {0, 0, 4, 4}, {0, 4, 4, 0}};

// D.5.2.1.3: nine-symbol fixed subsection di-bits, last one transmitted last; the
// single-symbol fixed subsection is di-bit 3 with M = 1.
constexpr int kFixedNine[9] = {0, 0, 2, 1, 2, 1, 0, 2, 3};
constexpr int kFixedSingle[1] = {3};
constexpr int kCountSyms = 4;  // c3 c2 c1 c0
constexpr int kWidSyms = 5;    // w4 w3 w2 w1 w0

// Table D-XVIII: TLC / Fixed synchronization preamble scrambling sequence.
constexpr int kFixedPN[256] = {
    2,4,0,0,6,2,1,4,6,1,0,5,7,3,4,1,2,6,1,7,0,7,3,2,2,2,3,2,4,6,3,6,
    6,3,7,5,4,7,5,6,7,4,0,2,6,1,5,3,0,4,2,4,6,4,5,2,5,4,5,3,1,5,4,5,
    6,5,1,0,7,1,0,1,0,5,3,5,2,2,4,5,4,0,6,4,1,4,0,3,3,0,0,3,3,7,3,4,
    2,7,4,4,4,0,3,4,7,6,4,2,6,2,0,3,5,3,2,2,4,5,2,0,0,3,5,0,3,2,6,6,
    1,4,2,3,6,1,3,0,3,3,2,4,2,2,6,5,5,3,6,7,6,5,6,6,5,2,5,4,2,3,3,3,
    5,7,5,5,3,7,0,4,7,0,4,1,6,2,3,5,5,6,2,6,4,6,3,4,0,7,0,0,5,2,1,5,
    4,3,4,5,7,0,5,3,7,6,6,6,4,5,6,0,2,0,4,2,3,4,4,0,7,6,6,2,0,0,3,3,
    0,5,2,4,2,2,4,5,4,6,6,6,3,2,1,0,3,2,6,0,6,2,4,0,6,4,1,3,3,5,3,6};

// Table D-XIX: Count synchronization preamble scrambling sequence.
constexpr int kCountPN[256] = {
    5,5,2,2,0,2,5,6,7,1,3,5,1,5,6,5,3,7,0,4,0,3,3,2,1,3,0,3,1,6,2,6,
    0,6,4,1,2,5,6,3,5,3,7,4,2,6,7,3,0,2,0,1,7,5,0,6,1,5,0,3,2,2,5,2,
    5,2,3,4,2,7,6,1,1,5,2,1,5,4,0,3,5,5,0,3,1,4,0,5,0,3,0,6,0,0,3,1,
    6,1,4,4,7,7,0,5,7,0,1,5,1,0,1,3,1,5,0,7,1,2,2,2,7,1,2,5,0,3,3,2,
    2,0,4,5,1,3,1,3,5,3,1,7,5,2,7,1,3,1,5,6,2,4,6,0,6,1,0,0,3,6,2,7,
    3,2,4,7,6,4,1,3,6,6,0,3,0,0,7,5,4,5,1,2,1,5,0,3,1,0,4,6,6,1,0,5,
    2,6,3,2,7,4,2,4,0,1,7,0,7,0,5,1,4,5,7,2,0,4,4,3,5,2,7,7,4,5,1,4,
    4,6,3,3,0,5,1,5,5,4,3,2,0,3,0,4,7,4,5,1,5,5,7,7,6,2,4,3,5,2,2,4};

// Table D-XX: Waveform Number synchronization preamble scrambling sequence.
constexpr int kWidPN[256] = {
    2,3,0,3,7,3,3,0,1,4,4,6,5,5,4,5,6,2,0,5,6,6,5,3,5,5,2,2,1,2,3,6,
    1,1,4,3,1,0,5,1,0,3,3,0,3,0,4,4,6,2,5,6,1,7,2,6,2,0,0,4,7,2,3,5,
    2,7,1,6,5,0,4,1,6,2,1,5,4,3,5,0,3,4,1,3,2,1,6,1,5,7,0,4,7,6,6,0,
    4,7,6,6,6,6,2,3,5,0,7,0,3,1,5,1,2,0,5,3,2,4,5,6,6,7,7,3,5,1,6,0,
    1,4,4,5,6,0,6,7,2,4,4,0,3,7,2,0,0,1,4,0,7,1,7,4,5,4,5,5,5,3,3,2,
    0,5,1,3,1,5,3,4,1,5,4,1,4,4,2,2,4,3,0,7,4,1,5,7,1,4,7,2,5,5,6,6,
    1,6,5,6,3,0,2,5,7,7,4,4,3,4,4,6,0,7,2,2,0,0,2,1,0,0,3,6,6,4,0,2,
    4,3,4,5,2,6,3,7,7,5,7,3,0,7,0,0,7,2,6,2,2,6,1,4,3,7,6,5,0,6,5,4};

// Table D-VI: 8PSK symbol number k -> exp(j*k*pi/4).
inline cplx psk8(int k) {
    static const std::array<cplx, 8> tab = [] {
        std::array<cplx, 8> t{};
        for (int i = 0; i < 8; i++) t[i] = std::polar(1.0, i * M_PI / 4.0);
        return t;
    }();
    return tab[((k % 8) + 8) % 8];
}

// Table D-XXI with Tables D-XXIII (16) and D-XXV (25): mini-probe base sequences. The
// tabulated values are Frank sequences s[m*N+n] = exp(-j*2*pi*m*n/N), N*N = base length;
// frank_base() generates them and tables_selftest() checks the generated values against the
// entries printed in Tables D-XXIII and D-XXV.
inline std::vector<cplx> frank_base(int n) {
    std::vector<cplx> s(n * n);
    for (int m = 0; m < n; m++)
        for (int k = 0; k < n; k++) s[m * n + k] = std::polar(1.0, -2.0 * M_PI * m * k / n);
    return s;
}

// D.5.2.2: a mini-probe of length K is its base sequence cyclically extended to K. The
// interleaver-boundary probe is the extended sequence starting at the cyclic shift.
inline std::vector<cplx> mini_probe(int K, bool boundary) {
    int n = 0, shift = 0;
    switch (K) {  // Table D-XXI rows used at 3 kHz
        case 24: {  // base 13, Table D-XXII(a); shift 6
            static const int b13[13] = {1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1};
            std::vector<cplx> p(K);
            for (int i = 0; i < K; i++) p[i] = b13[((boundary ? 6 : 0) + i) % 13];
            return p;
        }
        case 32: n = 4; shift = 8; break;   // base 16
        case 48: n = 5; shift = 12; break;  // base 25
        default: return {};
    }
    std::vector<cplx> base = frank_base(n);
    int L = (int)base.size();
    std::vector<cplx> p(K);
    for (int i = 0; i < K; i++) p[i] = base[((boundary ? shift : 0) + i) % L];
    return p;
}

// Per-waveform parameters at 3 kHz.
struct WaveformParams {
    int wid;
    int bits_per_sym;  // 1 BPSK, 2 QPSK, 3 8PSK, ... (0 = Walsh)
    int U;             // Table D-XI: data symbols per frame
    int K;             // Table D-XII: mini-probe symbols per frame
    // Table D-XLIX / D-L code rate at 3 kHz, K=7: repeats, then puncture pattern (rows T1, T2)
    int repeats;
    const char* punct_t1;
    const char* punct_t2;
    const char* rate;
};

// Tables D-XI, D-XII (3 kHz row), D-XLIX (3 kHz row) and D-L.
inline const WaveformParams* waveform_params(int wid) {
    static const WaveformParams tab[] = {
        {0, 0, 0, 0, 1, "1", "1", "1/2"},  // Walsh: no data frames; decoded by appd_walsh.h
        {1, 1, 48, 48, 4, "1", "1", "1/8"},
        {2, 1, 48, 48, 2, "1", "1", "1/4"},
        {3, 1, 96, 32, 2, "11", "10", "1/3"},  // 2/3 repeated 2x
        {4, 1, 96, 32, 1, "11", "10", "2/3"},
        {5, 1, 256, 32, 1, "110", "101", "3/4"},
        {6, 2, 256, 32, 1, "110", "101", "3/4"},
        {7, 3, 256, 32, 1, "110", "101", "3/4"},
        {8, 4, 256, 32, 1, "110", "101", "3/4"},
        {9, 5, 256, 32, 1, "110", "101", "3/4"},
        {10, 6, 256, 32, 1, "110", "101", "3/4"},
        {11, 6, 360, 24, 1, "11110100", "10001011", "8/9"},
        {12, 8, 360, 24, 1, "11110100", "10001011", "8/9"},
        {13, 2, 256, 32, 1, "111101111", "111111011", "9/16"},
    };
    if (wid < 0 || wid > 13) return nullptr;
    return &tab[wid];
}

// Table D-XXXVII (frames per interleaver block, interleaver size in bits, input bits) and
// Table D-LI (interleaver increment), 3 kHz. Index: [wid][interleaver 0..3 = US, S, M, L].
struct InterleaverParams {
    int frames;
    int bits;
    int input_bits;
    int increment;
};

inline const InterleaverParams* interleaver_params(int wid, int il) {
    static const InterleaverParams tab[14][4] = {
        {{0, 0, 0, 0}, {40, 80, 40, 11}, {144, 288, 144, 37}, {576, 1152, 576, 145}},
        {{4, 192, 24, 25}, {16, 768, 96, 97}, {64, 3072, 384, 385}, {256, 12288, 1536, 1543}},
        {{4, 192, 48, 25}, {16, 768, 192, 97}, {64, 3072, 768, 385}, {256, 12288, 3072, 1543}},
        {{2, 192, 64, 25}, {8, 768, 256, 97}, {32, 3072, 1024, 385}, {128, 12288, 4096, 1549}},
        {{2, 192, 128, 25}, {8, 768, 512, 97}, {32, 3072, 2048, 385}, {128, 12288, 8192, 1549}},
        {{1, 256, 192, 33}, {4, 1024, 768, 129}, {16, 4096, 3072, 513}, {64, 16384, 12288, 2081}},
        {{1, 512, 384, 65}, {4, 2048, 1536, 257}, {16, 8192, 6144, 1025}, {64, 32768, 24576, 4161}},
        {{1, 768, 576, 97}, {4, 3072, 2304, 385}, {16, 12288, 9216, 1537}, {64, 49152, 36864, 6241}},
        {{1, 1024, 768, 129}, {4, 4096, 3072, 513}, {16, 16384, 12288, 2049}, {64, 65536, 49152, 8321}},
        {{1, 1280, 960, 161}, {4, 5120, 3840, 641}, {16, 20480, 15360, 2561}, {64, 81920, 61440, 10403}},
        {{1, 1536, 1152, 193}, {4, 6144, 4608, 769}, {16, 24576, 18432, 3073}, {64, 98304, 73728, 12481}},
        {{1, 2160, 1920, 271}, {4, 8640, 7680, 1081}, {16, 34560, 30720, 4321}, {64, 138240, 122880, 17551}},
        {{1, 2880, 2560, 361}, {4, 11520, 10240, 1441}, {16, 46080, 40960, 5761}, {64, 184320, 163840, 23401}},
        {{1, 512, 288, 65}, {4, 2048, 1152, 257}, {16, 8192, 4608, 1025}, {64, 32768, 18432, 4161}},
    };
    if (wid < 0 || wid > 13 || il < 0 || il > 3) return nullptr;
    if (tab[wid][il].bits == 0) return nullptr;
    return &tab[wid][il];
}

// Table D-XVI names.
inline const char* interleaver_name(int il) {
    static const char* n[] = {"ultra-short", "short", "medium", "long"};
    return (il >= 0 && il < 4) ? n[il] : "?";
}

// D.5.1.3 / Figure D-6: data scrambler, x^9 + x^4 + 1, initialised to 00000001 at the start of
// each data frame. Bit 0 of `reg` is the rightmost cell of Figure D-6; the scrambling value
// is the rightmost three cells (bit 2 the MSB). Each shift feeds rightmost XOR the x^4 cell
// output into the leftmost cell.
struct DataScrambler {
    unsigned reg = 1;
    void reset() { reg = 1; }
    int value3() const { return (int)(reg & 7u); }
    void shift(int n) {
        for (int i = 0; i < n; i++) {
            unsigned nb = (reg ^ (reg >> 4)) & 1u;
            reg = (reg >> 1) | (nb << 8);
        }
    }
};

// D.5.4.3: 32-bit EOM, left-most bit sent first.
constexpr uint32_t kEOM = 0x4B65A5B2u;

// Self-check of the generated mini-probe base sequences against values printed in the
// standard (Tables D-XXIII and D-XXV). Returns the number of mismatches.
inline int tables_selftest() {
    int bad = 0;
    // Table D-XXIII, 16-symbol base, (I, Q) for symbols 0..15
    const double t16[16][2] = {{1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {0, -1}, {-1, 0}, {0, 1},
                               {1, 0}, {-1, 0}, {1, 0}, {-1, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    auto b16 = frank_base(4);
    for (int i = 0; i < 16; i++)
        if (std::abs(b16[i] - cplx(t16[i][0], t16[i][1])) > 1e-5) bad++;
    // Table D-XXV, 25-symbol base
    const double t25[25][2] = {
        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
        {1, 0}, {0.309017, -0.951057}, {-0.809017, -0.587785}, {-0.809017, 0.587785}, {0.309017, 0.951056},
        {1, 0}, {-0.809017, -0.587785}, {0.309017, 0.951056}, {0.309017, -0.951057}, {-0.809017, 0.587785},
        {1, 0}, {-0.809017, 0.587785}, {0.309017, -0.951057}, {0.309017, 0.951057}, {-0.809017, -0.587785},
        {1, 0}, {0.309017, 0.951056}, {-0.809017, 0.587785}, {-0.809017, -0.587785}, {0.309016, -0.951057}};
    auto b25 = frank_base(5);
    for (int i = 0; i < 25; i++)
        if (std::abs(b25[i] - cplx(t25[i][0], t25[i][1])) > 1e-5) bad++;
    return bad;
}

}  // namespace m110d

#endif
