// MIL-STD-188-110D Appendix D, 3 kHz: data constellations for waveforms 6-13.
//
// Every value below is from MIL-STD-188-110D (Distribution Statement A, approved for public
// release), Appendix D. The published PDF carries these tables as page images; they were
// transcribed by hand from the page images (printed pages 146, 149, 150, 152, 154-157) and
// are checked against received signal by decoding the interop test set (docs/110d-appd.md).
#ifndef M110D_APPD_CONSTELLATIONS_H
#define M110D_APPD_CONSTELLATIONS_H

#include "m110d/appd_tables.h"

#include <vector>

namespace m110d {

// Table D-IV: QPSK di-bit (leftmost = older) -> 8PSK symbol number.
constexpr int kQpskTranscode[4] = {0, 2, 6, 4};  // 00->0, 01->2, 10->6, 11->4
// Table D-V: 8PSK tri-bit (leftmost = oldest) -> 8PSK symbol number.
constexpr int k8pskTranscode[8] = {1, 0, 2, 3, 6, 7, 5, 4};

// Table D-VII: 16QAM.
constexpr double kQam16[16][2] = {
    {0.866025, 0.500000},   {0.500000, 0.866025},   {1.000000, 0.000000},   {0.258819, 0.258819},
    {-0.500000, 0.866025},  {0.000000, 1.000000},   {-0.866025, 0.500000},  {-0.258819, 0.258819},
    {0.500000, -0.866025},  {0.000000, -1.000000},  {0.866025, -0.500000},  {0.258819, -0.258819},
    {-0.866025, -0.500000}, {-0.500000, -0.866025}, {-1.000000, 0.000000},  {-0.258819, -0.258819}};

// Table D-VIII: 32QAM, symbols 0-7. The table gives symbols 8-15 as 0-7 with the in-phase
// component negated, 16-23 as 0-7 with the quadrature component negated, and 24-31 with both
// negated; qam_point() builds them that way and tables_selftest_qam() checks a sample of the
// printed entries.
constexpr double kQam32Base[8][2] = {
    {0.866380, 0.499386}, {0.984849, 0.173415}, {0.499386, 0.866380}, {0.173415, 0.984849},
    {0.520246, 0.520246}, {0.520246, 0.173415}, {0.173415, 0.520246}, {0.173415, 0.173415}};

// Table D-IX: 64QAM.
constexpr double kQam64[64][2] = {
    {1.000000, 0.000000},   {0.822878, 0.568218},   {0.821137, 0.152996},   {0.932897, 0.360142},
    {0.000000, -1.000000},  {0.822878, -0.568218},  {0.821137, -0.152996},  {0.932897, -0.360142},
    {0.568218, 0.822878},   {0.588429, 0.588429},   {0.588429, 0.117686},   {0.588429, 0.353057},
    {0.568218, -0.822878},  {0.588429, -0.588429},  {0.588429, -0.117686},  {0.588429, -0.353057},
    {0.152996, 0.821137},   {0.117686, 0.588429},   {0.117686, 0.117686},   {0.117686, 0.353057},
    {0.152996, -0.821137},  {0.117686, -0.588429},  {0.117686, -0.117686},  {0.117686, -0.353057},
    {0.360142, 0.932897},   {0.353057, 0.588429},   {0.353057, 0.117686},   {0.353057, 0.353057},
    {0.360142, -0.932897},  {0.353057, -0.588429},  {0.353057, -0.117686},  {0.353057, -0.353057},
    {0.000000, 1.000000},   {-0.822878, 0.568218},  {-0.821137, 0.152996},  {-0.932897, 0.360142},
    {-1.000000, 0.000000},  {-0.822878, -0.568218}, {-0.821137, -0.152996}, {-0.932897, -0.360142},
    {-0.568218, 0.822878},  {-0.588429, 0.588429},  {-0.588429, 0.117686},  {-0.588429, 0.353057},
    {-0.568218, -0.822878}, {-0.588429, -0.588429}, {-0.588429, -0.117686}, {-0.588429, -0.353057},
    {-0.152996, 0.821137},  {-0.117686, 0.588429},  {-0.117686, 0.117686},  {-0.117686, 0.353057},
    {-0.152996, -0.821137}, {-0.117686, -0.588429}, {-0.117686, -0.117686}, {-0.117686, -0.353057},
    {-0.360142, 0.932897},  {-0.353057, 0.588429},  {-0.353057, 0.117686},  {-0.353057, 0.353057},
    {-0.360142, -0.932897}, {-0.353057, -0.588429}, {-0.353057, -0.117686}, {-0.353057, -0.353057}};

// Table D-X: 256QAM. The printed table has this structure, which qam_point() uses:
//   bit 7 of the symbol number negates the in-phase component (symbols 128-255 are 0-127
//   with I negated), bit 3 negates the quadrature component (rows 8-15 of every group of 16
//   are rows 0-7 with Q negated), and bits 6-4 with bits 2-0 select a first-quadrant point.
// Groups (bits 6-4) 2 to 7 share one quadrature column, indexed by bits 2-0:
constexpr double k256Q[8] = {0.846499, 0.733632, 0.507899, 0.620766, 0.056433, 0.169300, 0.395033, 0.282166};
// In-phase value of groups 2..7 (symbols 32-47, 48-63, 64-79, 80-95, 96-111, 112-127):
constexpr double k256GroupI[8] = {0, 0, 0.507899, 0.620766, 0.056433, 0.169300, 0.395033, 0.282166};
// Groups 0 and 1 (symbols 0-7, 16-23) and symbol 48 are listed point by point:
constexpr double k256G0[8][2] = {
    {0.959366, 0.056433}, {0.959366, 0.169300}, {0.846499, 0.507899}, {0.959366, 0.282166},
    {0.846499, 0.056433}, {0.846499, 0.169300}, {0.846499, 0.395033}, {0.846499, 0.282166}};
constexpr double k256G1[8][2] = {
    {0.169300, 0.959366}, {0.056433, 0.998304}, {0.733632, 0.507899}, {0.733632, 0.620766},
    {0.733632, 0.056433}, {0.733632, 0.169300}, {0.733632, 0.395033}, {0.733632, 0.282166}};
constexpr double k256Sym48[2] = {0.282166, 0.959366};

// Constellation point for symbol number s of a 2^bits QAM (bits 4, 5, 6 or 8).
inline cplx qam_point(int bits, int s) {
    switch (bits) {
        case 4: return {kQam16[s & 15][0], kQam16[s & 15][1]};
        case 5: {
            double i = kQam32Base[s & 7][0], q = kQam32Base[s & 7][1];
            if (s & 8) i = -i;
            if (s & 16) q = -q;
            return {i, q};
        }
        case 6: return {kQam64[s & 63][0], kQam64[s & 63][1]};
        case 8: {
            int g = (s >> 4) & 7, k = s & 7;
            double i, q;
            if (g == 0) { i = k256G0[k][0]; q = k256G0[k][1]; }
            else if (g == 1) { i = k256G1[k][0]; q = k256G1[k][1]; }
            else if (g == 3 && k == 0) { i = k256Sym48[0]; q = k256Sym48[1]; }
            else { i = k256GroupI[g]; q = k256Q[k]; }
            if (s & 8) q = -q;
            if (s & 128) i = -i;
            return {i, q};
        }
    }
    return 0.0;
}

// Spot checks of qam_point() against entries printed in Tables D-VIII and D-X that the
// structural rules generate. Returns the number of mismatches.
inline int tables_selftest_qam() {
    struct E { int bits, s; double i, q; };
    static const E e[] = {
        {5, 8, -0.866380, 0.499386}, {5, 13, -0.520246, 0.173415}, {5, 16, 0.866380, -0.499386},
        {5, 23, 0.173415, -0.173415}, {5, 26, -0.499386, -0.866380}, {5, 31, -0.173415, -0.173415},
        {8, 0, 0.959366, 0.056433}, {8, 11, 0.959366, -0.282166}, {8, 25, 0.056433, -0.998304},
        {8, 33, 0.507899, 0.733632}, {8, 43, 0.507899, -0.620766}, {8, 48, 0.282166, 0.959366},
        {8, 56, 0.282166, -0.959366}, {8, 57, 0.620766, -0.733632}, {8, 67, 0.056433, 0.620766},
        {8, 89, 0.169300, -0.733632}, {8, 102, 0.395033, 0.395033}, {8, 127, 0.282166, -0.282166},
        {8, 130, -0.846499, 0.507899}, {8, 145, -0.056433, 0.998304}, {8, 176, -0.282166, 0.959366},
        {8, 185, -0.620766, -0.733632}, {8, 203, -0.056433, -0.620766}, {8, 230, -0.395033, 0.395033},
        {8, 255, -0.282166, -0.282166}};
    int bad = 0;
    for (const E& x : e)
        if (std::abs(qam_point(x.bits, x.s) - cplx(x.i, x.q)) > 1e-6) bad++;
    // 256QAM must have 256 distinct points
    std::vector<cplx> p;
    for (int s = 0; s < 256; s++) p.push_back(qam_point(8, s));
    for (int a = 0; a < 256; a++)
        for (int b = a + 1; b < 256; b++)
            if (std::abs(p[a] - p[b]) < 1e-4) bad++;
    return bad;
}

}  // namespace m110d

#endif
