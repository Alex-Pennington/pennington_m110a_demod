// ModemTX output properties (ctest test_modem_tx; docs/TX_CHAIN.md)
//
// level: for every standard mode at 48 kHz, RRC and rectangular pulses give the
//        same RMS, amplitude / sqrt(2) within 0.5 dB, and RRC peaks stay below
//        full scale at the default amplitude.
#include "api/modem.h"

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

}  // namespace

int main() {
    level();
    printf(failures ? "test_modem_tx: %d FAILED\n" : "test_modem_tx: all passed\n", failures);
    return failures ? 1 : 0;
}
