// test_m75 - MIL-STD-188-110A 75 bps (75S, 75L): the waveform against the standard's
// text, and ModemTX -> ModemRX loopback.
//
// Copyright (C) the pennington_m110a_demod authors.
// Licensed under the GNU General Public License v3.0, the same as this repository's
// LICENSE file. SPDX-License-Identifier: GPL-3.0-only
//
// 1. Table IX and Table X as printed in MIL-STD-188-110D 5.3.2.3.6 and 5.3.2.3.7.1.1,
//    copied here as text, against m75::mgd and m75::tribit.
// 2. The 75N interleaver against a model written from the text of 5.3.2.3.4 and
//    5.3.2.3.5 (load: row + 7 mod rows, next column after the last row; fetch: row + 1,
//    column - 7 mod columns, next start column after the last row), 10 x 9 and 20 x 36.
// 3. ModemTX's data symbols against a symbol model written from 5.3.2.3 in this file:
//    EOM msb first, 144 flush bits, rate 1/2 K=7 (0x5B / 0x79 in M110ACodec's shift
//    convention, which the other modes use), block padding, Table IX, Table X, exceptional set every
//    45th / 360th, scrambler 160 periodic, no probes. 75S and 75L, several lengths.
// 4. Loopback, clean: AUTO reads D1/D2 7,5 / 5,5 and the bytes come back exact with the
//    EOM; forced mode too; lengths around set, block and byte boundaries.
// 5. Noise after a genuine 75 bps preamble: success=0. 30 s of noise forced as 75S and
//    as 75L: success=0. A 75L signal forced as 75S: success=0.

#include "api/modem.h"
#include "m110a/brain_preamble.h"
#include "m110a/m75_codec.h"
#include "modem/scrambler_fixed.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace m110a;
using namespace m110a::api;

static int passed = 0, failed = 0;

static void report(const std::string& what, bool ok) {
    std::cout << what << " ... " << (ok ? "PASS" : "FAIL") << std::endl;
    (ok ? passed : failed)++;
}

// ---------------------------------------------------------------- 1. tables
static void tables() {
    // Table IX: input bits first,last -> decoded value
    const char* ix[4][2] = {{"00", "00"}, {"01", "01"}, {"10", "11"}, {"11", "10"}};
    bool ok = true;
    for (auto& r : ix) {
        const int f = r[0][0] - '0', l = r[0][1] - '0';
        const int v = (r[1][0] - '0') * 2 + (r[1][1] - '0');
        ok &= m75::mgd(f, l) == v;
        int f2, l2;
        m75::mgd_inverse(v, f2, l2);
        ok &= f2 == f && l2 == l;
    }
    report("Table IX modified Gray decoding at 75 bps", ok);

    // Table X a: (0000) (0404) (0044) (0440) repeated 8 times; b: (0000 4444) (0404 4040)
    // (0044 4400) (0440 4004) repeated 4 times
    const char* xa[4] = {"0000", "0404", "0044", "0440"};
    const char* xb[4] = {"00004444", "04044040", "00444400", "04404004"};
    ok = true;
    for (int v = 0; v < 4; v++)
        for (int i = 0; i < 32; i++) {
            ok &= m75::tribit(v, false, i) == xa[v][i % 4] - '0';
            ok &= m75::tribit(v, true, i) == xb[v][i % 8] - '0';
        }
    report("Table X normal and exceptional sets", ok);

    // The four words of each table are orthogonal over a set, and every exceptional word
    // is orthogonal to every normal word
    ok = true;
    for (int a = 0; a < 8; a++)
        for (int b = 0; b < 8; b++) {
            int s = 0;
            for (int i = 0; i < 32; i++)
                s += (m75::tribit(a & 3, a > 3, i) ? -1 : 1) * (m75::tribit(b & 3, b > 3, i) ? -1 : 1);
            ok &= (a == b) ? s == 32 : s == 0;
        }
    report("Table X words orthogonal (normal and exceptional, 8 x 8)", ok);
}

// ---------------------------------------------------------------- 2. interleaver
static std::vector<int> spec_interleave(const std::vector<int>& in, int rows, int cols) {
    std::vector<std::vector<int>> m(rows, std::vector<int>(cols, 0));
    int r = 0, c = 0;
    for (int b : in) {  // 5.3.2.3.4 with the 75 bps change: row advances 7 modulo rows
        m[r][c] = b;
        r = (r + 7) % rows;
        if (r == 0) c++;
    }
    std::vector<int> out;
    for (int start = 0; start < cols; start++) {  // 5.3.2.3.5: row + 1, column - 7
        int col = start;
        for (int row = 0; row < rows; row++) {
            out.push_back(m[row][col]);
            col = ((col - 7) % cols + cols) % cols;
        }
    }
    return out;
}

static void interleaver() {
    for (ModeId id : {ModeId::M75NS, ModeId::M75NL}) {
        const auto& p = ModeDatabase::get(id).interleaver;
        MultiModeInterleaver il(p);
        std::vector<int> in(il.block_size());
        std::vector<soft_bit_t> ins(il.block_size());
        for (int i = 0; i < il.block_size(); i++) { in[i] = i; }
        // the interleaver moves int8 values: check the permutation 128 indices at a time
        bool ok = true;
        const auto want = spec_interleave(in, p.rows, p.cols);
        for (int base = 0; base < il.block_size(); base += 100) {
            for (int i = 0; i < il.block_size(); i++) ins[i] = (i >= base && i < base + 100) ? i - base + 1 : 0;
            const auto got = il.interleave(ins);
            for (int i = 0; i < il.block_size(); i++) {
                const int w = want[i];
                const int g = got[i];
                if (w >= base && w < base + 100) ok &= g == w - base + 1;
            }
            ok &= il.deinterleave(got) == ins;
        }
        char buf[120];
        snprintf(buf, sizeof buf, "interleaver %s %dx%d against the text of 5.3.2.3.4/5", id == ModeId::M75NS ? "75S" : "75L",
                 p.rows, p.cols);
        report(buf, ok && p.rows * p.cols == (id == ModeId::M75NS ? 90 : 720));
    }
}

// ---------------------------------------------------------------- 3. symbol model
static std::vector<int> spec_symbols(const std::vector<int>& info, bool shortil) {
    std::vector<int> b = info;
    const uint32_t eom = 0x4B65A5B2u;
    for (int i = 31; i >= 0; i--) b.push_back((eom >> i) & 1);
    b.insert(b.end(), 144, 0);
    std::vector<int> coded;
    int sr = 0;  // seven most recent bits, newest in bit 6
    for (int x : b) {
        sr = (sr >> 1) | (x << 6);
        coded.push_back(__builtin_parity(sr & 0x5B));
        coded.push_back(__builtin_parity(sr & 0x79));
    }
    const int rows = shortil ? 10 : 20, cols = shortil ? 9 : 36, blk = rows * cols;
    while (coded.size() % blk) coded.push_back(0);
    const char* xa[4] = {"0000", "0404", "0044", "0440"};
    const char* xb[4] = {"00004444", "04044040", "00444400", "04404004"};
    const int every = shortil ? 45 : 360;
    DataScramblerFixed scr;
    std::vector<int> out;
    long set = 0;
    for (size_t o = 0; o < coded.size(); o += blk) {
        const auto f = spec_interleave(std::vector<int>(coded.begin() + o, coded.begin() + o + blk), rows, cols);
        for (int k = 0; k < blk; k += 2) {
            const int first = f[k], last = f[k + 1];
            const int v = first ? (last ? 2 : 3) : (last ? 1 : 0);  // Table IX
            set++;
            const bool ex = set % every == 0;  // every 45th / 360th set after the sync
            for (int i = 0; i < 32; i++) {
                const int t = (ex ? xb[v][i % 8] : xa[v][i % 4]) - '0';
                out.push_back((t + scr.next()) % 8);
            }
        }
    }
    return out;
}

static void symbols() {
    for (Mode m : {Mode::M75_SHORT, Mode::M75_LONG}) {
        for (size_t nbytes : {1u, 5u, 6u, 12u, 100u}) {
            std::mt19937 rng(static_cast<unsigned>(nbytes * 7 + (m == Mode::M75_LONG)));
            std::vector<uint8_t> bits(nbytes * 8);
            for (auto& b : bits) b = rng() & 1;
            TxConfig t;
            t.mode = m;
            t.include_leading_symbols = false;
            ModemTX tx(t);
            std::vector<int> pos;
            auto r = tx.encode_bits(bits, &pos);
            const int pre = (m == Mode::M75_SHORT ? 3 : 24) * 480;
            const auto want = spec_symbols(std::vector<int>(bits.begin(), bits.end()), m == Mode::M75_SHORT);
            bool ok = r.ok() && pos.size() == static_cast<size_t>(pre) + want.size();
            for (size_t i = 0; ok && i < want.size(); i++) ok = pos[pre + i] == want[i];
            // the preamble is the generator's, with D1/D2 7,5 or 5,5 (Table XI)
            auto pre_syms = BrainPreambleEncoder().encode(m == Mode::M75_SHORT ? 0 : 1, m == Mode::M75_LONG);
            ok = ok && pre_syms.size() == static_cast<size_t>(pre);
            char buf[160];
            snprintf(buf, sizeof buf, "%s %zu bytes: ModemTX symbols = 5.3.2 model (%zu data symbols, %zu sets)",
                     mode_name(m).c_str(), nbytes, want.size(), want.size() / 32);
            report(buf, ok);
        }
    }
}

// ---------------------------------------------------------------- 4/5. loopback
static DecodeResult rx(const Samples& s, Mode m) {
    RxConfig cfg;
    cfg.sample_rate = 48000;
    cfg.mode = m;
    std::ofstream sink;
    std::streambuf* out = std::cout.rdbuf(sink.rdbuf());
    std::streambuf* err = std::cerr.rdbuf(sink.rdbuf());
    DecodeResult r = decode(s, cfg);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    return r;
}

static Samples tx_audio(Mode m, const std::string& text, bool lead = true) {
    TxConfig t;
    t.mode = m;
    t.sample_rate = 48000;
    t.include_leading_symbols = false;
    ModemTX tx(t);
    auto e = tx.encode(std::vector<uint8_t>(text.begin(), text.end()));
    Samples s;
    if (!e.ok()) return s;
    if (lead) s.assign(24000, 0.0f);
    s.insert(s.end(), e.value().begin(), e.value().end());
    s.insert(s.end(), 24000, 0.0f);
    return s;
}

static void loopback(Mode m, Mode rx_mode, const std::string& text) {
    const Samples s = tx_audio(m, text);
    if (s.empty()) { report("encode " + mode_name(m), false); return; }
    const DecodeResult r = rx(s, rx_mode);
    const bool exact = std::string(r.data.begin(), r.data.end()) == text;
    char buf[240];
    snprintf(buf, sizeof buf, "%s -> RX %s, %zu bytes: mode %s D1,D2 %d,%d success=%d eom=%d q=%.1f bytes %s",
             mode_name(m).c_str(), mode_name(rx_mode).c_str(), text.size(), mode_name(r.mode).c_str(), r.d1, r.d2,
             r.success, r.eom_sequence, r.quality_db, exact ? "exact" : "WRONG");
    const int d1 = m == Mode::M75_SHORT ? 7 : 5;
    report(buf, r.success && r.eom_sequence && exact && r.mode == m && r.d1 == d1 && r.d2 == 5);
}

static void noise_after_preamble(Mode m) {
    TxConfig t;
    t.mode = m;
    t.sample_rate = 48000;
    ModemTX tx(t);
    auto pre = tx.generate_preamble();
    Samples s(24000, 0.0f);
    s.insert(s.end(), pre.value().begin(), pre.value().end());
    std::mt19937 rng(5);
    std::normal_distribution<float> n(0.0f, 0.2f);
    for (int i = 0; i < 48000 * (m == Mode::M75_SHORT ? 4 : 12); i++) s.push_back(n(rng));
    const DecodeResult r = rx(s, Mode::AUTO);
    char buf[200];
    snprintf(buf, sizeof buf, "%s preamble then noise: mode %s success=%d q=%.1f bytes %zu", mode_name(m).c_str(),
             mode_name(r.mode).c_str(), r.success, r.quality_db, r.data.size());
    report(buf, !r.success && r.mode == m);
}

// test_signal_end's "30 s noise, every forced mode" leaves out 75S and 75L: here they are
static void noise_forced(Mode m) {
    std::mt19937 rng(3);
    std::normal_distribution<float> n(0.0f, 0.2f);
    Samples s(30 * 48000);
    for (auto& v : s) v = n(rng);
    const DecodeResult r = rx(s, m);
    char buf[200];
    snprintf(buf, sizeof buf, "30 s noise, forced %s: success=%d q=%.1f bytes %zu", mode_name(m).c_str(), r.success,
             r.quality_db, r.data.size());
    report(buf, !r.success);
}

static void forced_wrong() {
    const std::string text = "DE AAM402 FORCED";
    const DecodeResult r = rx(tx_audio(Mode::M75_LONG, text), Mode::M75_SHORT);
    char buf[200];
    snprintf(buf, sizeof buf, "75L sent, forced 75S: success=%d err=%s", r.success,
             r.error ? r.error->message.c_str() : "");
    report(buf, !r.success);
}

int main() {
    tables();
    interleaver();
    symbols();
    const std::string msg = "DE AAM402 75 BPS TEST THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789";
    loopback(Mode::M75_SHORT, Mode::AUTO, msg);
    loopback(Mode::M75_LONG, Mode::AUTO, msg);
    loopback(Mode::M75_SHORT, Mode::M75_SHORT, msg);
    loopback(Mode::M75_LONG, Mode::M75_LONG, msg);
    // lengths: 1 byte; 5 and 6 bytes (40/48 bits: the EOM and flush cross a 45-bit block);
    // 45 bits of payload per 75S block, so 11 bytes = 88 bits ends near two blocks
    for (size_t n : {1u, 5u, 6u, 11u, 23u, 200u}) {
        std::string t = "DE AAM402 ";
        while (t.size() < n) t += static_cast<char>('A' + t.size() % 26);
        t.resize(n);
        loopback(Mode::M75_SHORT, Mode::AUTO, t);
    }
    loopback(Mode::M75_LONG, Mode::AUTO, "D");
    noise_after_preamble(Mode::M75_SHORT);
    noise_after_preamble(Mode::M75_LONG);
    noise_forced(Mode::M75_SHORT);
    noise_forced(Mode::M75_LONG);
    forced_wrong();
    std::cout << "\n" << passed << "/" << (passed + failed) << " 75 bps cases passed" << std::endl;
    return failed ? 1 : 0;
}
