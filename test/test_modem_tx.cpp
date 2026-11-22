// ModemTX output properties (ctest test_modem_tx; docs/TX_CHAIN.md)
//
// level: for every standard mode at 48 kHz, RRC and rectangular pulses give the
//        same RMS, amplitude / sqrt(2) within 0.5 dB, and RRC peaks stay below
//        full scale at the default amplitude.
// d1d2:  in the symbols M110ACodec sends, the probes of the last two frames of every
//        interleaver block carry D1 and D2 (Walsh pattern twice, then zeros), all other
//        probes are zero (MIL-STD-188-110 5.3.2.3.7.1.2).
// rate:  8 kHz (3.33 samples per symbol) is refused by TxConfig/RxConfig validation,
//        by ModemTX::encode and by ModemRX::decode; 48 kHz is taken.
#include "api/modem.h"
#include "modem/m110a_codec.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace m110a::api;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

const Mode MODES[] = {Mode::M150_SHORT, Mode::M150_LONG, Mode::M300_SHORT, Mode::M300_LONG,
                      Mode::M600_SHORT, Mode::M600_LONG, Mode::M1200_SHORT, Mode::M1200_LONG,
                      Mode::M2400_SHORT, Mode::M2400_LONG, Mode::M4800_SHORT};

std::vector<uint8_t> payload(size_t n) {
    std::vector<uint8_t> d(n);
    uint32_t s = 12345;
    for (auto& b : d) { s = s * 1103515245u + 12345u; b = static_cast<uint8_t>(s >> 16); }
    return d;
}

void level() {
    for (Mode m : MODES) {
        double rms[2] = {0, 0}, peak[2] = {0, 0};
        for (int rrc = 0; rrc < 2; rrc++) {
            TxConfig c;
            c.mode = m;
            c.use_pulse_shaping = rrc;
            c.include_leading_symbols = false;
            auto r = ModemTX(c).encode(payload(100));
            check(r.ok(), "encode");
            if (!r.ok()) return;
            double e = 0;
            for (float v : r.value()) { e += double(v) * v; peak[rrc] = std::max(peak[rrc], double(std::fabs(v))); }
            rms[rrc] = std::sqrt(e / r.value().size());
        }
        const double want = 0.32 / std::sqrt(2.0);
        const double d_rect = 20 * std::log10(rms[0] / want), d_rrc = 20 * std::log10(rms[1] / want);
        printf("level %-14s rect rms=%.4f (%+.2f dB) peak=%.3f   rrc rms=%.4f (%+.2f dB) peak=%.3f\n",
               mode_name(m).c_str(), rms[0], d_rect, peak[0], rms[1], d_rrc, peak[1]);
        check(std::fabs(d_rect) < 0.5, "rect RMS = amplitude/sqrt(2) within 0.5 dB");
        check(std::fabs(d_rrc) < 0.5, "RRC RMS = amplitude/sqrt(2) within 0.5 dB");
        check(peak[1] < 1.0, "RRC peak below full scale");
    }
}

int psk8_index(const m110a::complex_t& s) {
    int p = static_cast<int>(std::lround(std::atan2(s.imag(), s.real()) * 4.0 / M_PI));
    return ((p % 8) + 8) % 8;
}

void d1d2() {
    static const int WALSH[8][8] = {
        {0,0,0,0,0,0,0,0}, {0,4,0,4,0,4,0,4}, {0,0,4,4,0,0,4,4}, {0,4,4,0,0,4,4,0},
        {0,0,0,0,4,4,4,4}, {0,4,0,4,4,0,4,0}, {0,0,4,4,4,4,0,0}, {0,4,4,0,4,0,0,4}};
    const m110a::ModeId ids[] = {m110a::ModeId::M150S, m110a::ModeId::M150L, m110a::ModeId::M300S,
        m110a::ModeId::M300L, m110a::ModeId::M600S, m110a::ModeId::M600L, m110a::ModeId::M1200S,
        m110a::ModeId::M1200L, m110a::ModeId::M2400S, m110a::ModeId::M2400L, m110a::ModeId::M4800S};
    for (auto id : ids) {
        const auto& mc = m110a::ModeDatabase::get(id);
        m110a::M110ACodec codec(id);
        std::vector<int> bits(4000, 1);
        auto syms = codec.encode_bits_with_probes(bits, true);
        const int u = mc.unknown_data_len, k = mc.known_data_len, plen = u + k;
        const int fpb = mc.interleaver.rows * mc.interleaver.cols / (mc.bits_per_symbol * u);
        m110a::DataScramblerFixed scr;
        int bad = 0, frames = 0, marked = 0;
        for (size_t f = 0; (f + 1) * plen <= syms.size(); f++, frames++) {
            for (int i = 0; i < u; i++) scr.next();
            const int fb = static_cast<int>(f % fpb);
            const int d = fb == fpb - 2 ? mc.d1_sequence : fb == fpb - 1 ? mc.d2_sequence : -1;
            if (d >= 0) marked++;
            for (int i = 0; i < k; i++) {
                int want = (d >= 0 && i < 16) ? WALSH[d & 7][i % 8] : 0;
                int got = (psk8_index(syms[f * plen + u + i]) - scr.next() + 8) & 7;
                if (got != want) bad++;
            }
        }
        printf("d1d2  %-6s D1,D2=%d,%d frames=%d per_block=%d D1/D2 probes=%d wrong probe symbols=%d\n",
               mc.name.c_str(), mc.d1_sequence, mc.d2_sequence, frames, fpb, marked, bad);
        check(bad == 0, "probe symbols: D1/D2 before each block, zero elsewhere");
        check(marked >= 2, "at least one D1/D2 pair sent");
    }
}

void rate() {
    for (float sr : {8000.0f, 9600.0f, 44100.0f, 48000.0f}) {
        const bool want = sr == 48000.0f;
        TxConfig t;
        t.sample_rate = sr;
        RxConfig r;
        r.sample_rate = sr;
        const bool tx_valid = t.validate().ok(), rx_valid = r.validate().ok();
        auto e = ModemTX(t).encode(payload(10));
        Samples s(48000, 0.0f);
        auto d = ModemRX(r).decode(s);
        const bool rx_refused = d.error && d.error->code == ErrorCode::INVALID_SAMPLE_RATE;
        printf("rate  %6.0f Hz: TxConfig %s, RxConfig %s, encode %s, decode %s\n", sr,
               tx_valid ? "valid" : "refused", rx_valid ? "valid" : "refused",
               e.ok() ? "ok" : "refused", rx_refused ? "refused" : "taken");
        check(tx_valid == want && rx_valid == want, "validate: 48000 only");
        check(e.ok() == want, "encode refuses a rate without whole samples per symbol");
        check(rx_refused == !want, "decode refuses a rate without whole samples per symbol");
    }
}

}  // namespace

int main() {
    level();
    d1d2();
    rate();
    printf(failures ? "test_modem_tx: %d FAILED\n" : "test_modem_tx: all passed\n", failures);
    return failures ? 1 : 0;
}
