/**
 * m110a_rx - receive MIL-STD-188-110A from SDR I/Q (file or stdin) to bytes.
 *
 * Chain: I/Q -> shift dial to 0 Hz (offset + clock-error correction)
 *        -> IQSource decimation to 48 kHz -> USB 300-3300 Hz -> audio
 *        -> preamble scan over the whole stream (any start time, any carrier
 *           offset within +/-87 Hz) -> per message: find its end, normalise
 *           its level, decode with ModemRX -> cut at EOM -> print.
 *
 * Usage: m110a_rx [options] INPUT
 *   INPUT            .cs16 (int16 I/Q), .cf32 (float32 I/Q), .iqr, or - for stdin.
 *                    PREFIX.json beside PREFIX.cs16 (rsp2_stream.py --record)
 *                    supplies --rate, --offset and --dial.
 *   --mode M         AUTO (default: the receiver reads the mode from D1/D2),
 *                    or 75S..2400L to force a mode
 *   --rate HZ        input sample rate (default: .json/.iqr, else 2000000)
 *   --offset HZ      dial - tuner frequency (default: .json, else 100000)
 *   --dial HZ        dial frequency, used with --ppm (default: .json wwv_hz)
 *   --ppm P          receiver clock error, ppm, + = fast. The dial is then
 *                    looked for at offset - dial * P * 1e-6.
 *   --format F       cs16 | cf32 (default: by extension, else cs16)
 *   --eq E           NONE DFE DFE_RLS MLSE_L2 MLSE_L3 MLSE_ADAPTIVE TURBO (DFE)
 *   --threshold T    preamble detection metric, 0..1 (default 0.45)
 *   --max-secs S     longest message to decode, seconds (default 120)
 *   --expect TEXT    exit 0 only if a message decodes to exactly TEXT with EOM
 *   --dump-audio F   also write the 48 kHz audio as float32 (unnormalised)
 *   -v               pass the decoder's debug output through to stderr
 */

#include "api/modem.h"
#include "api/iq_file_source.h"
#include "api/sdr_frontend.h"
#include "sync/preamble_acq.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace m110a;
using namespace m110a::api;

namespace {

constexpr double FS = 48000.0;
constexpr int SPS_AUDIO = 20;          // 48000 / 2400
constexpr int DECIM = 5;               // scan baseband at 9600 Hz
constexpr int SPS_SCAN = SPS_AUDIO / DECIM;
constexpr long FRAME_SCAN = 480L * SPS_SCAN;   // preamble frame, scan samples
constexpr long HOLD_SCAN = 120L * SPS_SCAN;    // peak hold window
constexpr long TOL_SCAN = 2L * SPS_SCAN;       // frame spacing tolerance
constexpr long BLOCK = 4800;                   // 0.1 s power blocks
constexpr float TARGET_RMS = 7000.0f / 32768.0f;  // MS-DMT reference level
constexpr long MARGIN = static_cast<long>(0.04 * FS);  // audio before frame 1

struct Options {
    std::string input, mode_s = "AUTO", eq_s = "DFE", format, expect, dump_audio;
    double rate = 0, offset = NAN, dial = NAN, ppm = 0;
    double threshold = 0.45, max_secs = 120;
    bool have_expect = false, verbose = false;
};

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "m110a_rx: %s\n", msg);
    fprintf(stderr,
        "usage: m110a_rx [--mode AUTO|M] [--rate HZ] [--offset HZ] [--dial HZ] [--ppm P]\n"
        "                [--format cs16|cf32] [--eq E] [--threshold T] [--max-secs S]\n"
        "                [--expect TEXT] [--dump-audio FILE] [-v] INPUT|-\n");
    exit(2);
}

bool json_number(const std::string& text, const std::string& key, double& out) {
    size_t p = text.find("\"" + key + "\"");
    if (p == std::string::npos) return false;
    p = text.find(':', p);
    if (p == std::string::npos) return false;
    const char* s = text.c_str() + p + 1;
    char* end = nullptr;
    double v = strtod(s, &end);
    if (end == s) return false;
    out = v;
    return true;
}

bool parse_mode(const std::string& s, Mode& m) {
    for (int i = 0; i <= static_cast<int>(Mode::M1200_HARRIS); i++)
        if (mode_name(static_cast<Mode>(i)) == s) { m = static_cast<Mode>(i); return true; }
    return false;
}

bool parse_eq(const std::string& s, Equalizer& e) {
    const char* n[] = {"NONE", "DFE", "DFE_RLS", "MLSE_L2", "MLSE_L3", "MLSE_ADAPTIVE", "TURBO"};
    for (int i = 0; i < 7; i++)
        if (s == n[i]) { e = static_cast<Equalizer>(i); return true; }
    return false;
}

int preamble_frames(Mode m) {
    switch (m) {
        case Mode::M75_LONG: case Mode::M150_LONG: case Mode::M300_LONG:
        case Mode::M600_LONG: case Mode::M1200_LONG: case Mode::M2400_LONG:
        case Mode::M4800_LONG:
            return 24;
        case Mode::AUTO:
            return 0;
        default:
            return 3;
    }
}

// MIL-STD-188-110A EOM 0x4B65A5B2, as the decoder emits it (LSB-first bytes).
const uint8_t EOM_BYTES[4] = {0xD2, 0xA6, 0xA5, 0x4D};

// Bit position of the first EOM in the LSB-first bit stream, or -1.
long find_eom(const std::vector<uint8_t>& d) {
    const long nbits = static_cast<long>(d.size()) * 8;
    auto bit = [&](const uint8_t* p, long i) { return (p[i >> 3] >> (i & 7)) & 1; };
    for (long pos = 0; pos + 32 <= nbits; pos++) {
        bool ok = true;
        for (long i = 0; i < 32 && ok; i++) ok = bit(d.data(), pos + i) == bit(EOM_BYTES, i);
        if (ok) return pos;
    }
    return -1;
}

struct Hit { long t; float metric; float freq; };

struct Message {
    int id;
    long first_frame;     // scan index of the first preamble frame
    long preamble_end;    // scan index just after the last frame
    int hits;
    float metric, freq;   // mean over the hits
    long end_audio = -1;  // decided end, absolute audio index
};

class Receiver {
public:
    Receiver(const Options& o, Mode mode, Equalizer eq)
        : opt_(o), mode_(mode), eq_(eq), corr_(SPS_SCAN) {
        // RRC matched filter at 48 kHz, alpha 0.35, 6 symbols
        const int half = 3 * SPS_AUDIO;
        const double a = 0.35;
        double sum = 0;
        for (int k = -half; k <= half; k++) {
            double t = static_cast<double>(k) / SPS_AUDIO, h;
            if (k == 0) h = 1.0 - a + 4.0 * a / M_PI;
            else if (std::fabs(std::fabs(4.0 * a * t) - 1.0) < 1e-9)
                h = a / std::sqrt(2.0) * ((1 + 2 / M_PI) * std::sin(M_PI / (4 * a)) +
                                          (1 - 2 / M_PI) * std::cos(M_PI / (4 * a)));
            else
                h = (std::sin(M_PI * t * (1 - a)) + 4 * a * t * std::cos(M_PI * t * (1 + a))) /
                    (M_PI * t * (1 - 16 * a * a * t * t));
            rrc_.push_back(static_cast<float>(h));
            sum += h;
        }
        for (auto& h : rrc_) h /= static_cast<float>(sum);
        for (int i = 0; i < 80; i++)  // 1800 Hz at 48 kHz repeats every 80 samples
            lo_[i] = std::polar(1.0f, static_cast<float>(-2.0 * M_PI * 1800.0 * i / FS));
    }

    void push_audio(const std::vector<float>& a, bool eof) {
        audio_.insert(audio_.end(), a.begin(), a.end());
        audio_end_ += static_cast<long>(a.size());
        update_blocks();
        scan(eof);
        process_messages(eof);
        trim();
    }

    int found() const { return next_id_ - 1; }
    int decoded_ok() const { return decoded_ok_; }
    bool expect_met() const { return expect_met_; }

private:
    // ---------------- buffers (absolute indices) ----------------
    float audio_at(long n) const { return audio_[static_cast<size_t>(n - audio_base_)]; }

    void update_blocks() {
        while ((static_cast<long>(blocks_.size()) + 1) * BLOCK <= audio_end_) {
            long b0 = static_cast<long>(blocks_.size()) * BLOCK;
            double p = 0;
            for (long n = b0; n < b0 + BLOCK; n++) { double v = audio_at(n); p += v * v; }
            blocks_.push_back(p / BLOCK);
        }
    }

    void trim() {
        long keep = std::min(scan_t_ * DECIM, audio_end_) - static_cast<long>(40 * FS);
        for (auto& m : pending_) keep = std::min(keep, m.first_frame * DECIM - static_cast<long>(4 * FS));
        if (keep - audio_base_ > static_cast<long>(30 * FS)) {
            audio_.erase(audio_.begin(), audio_.begin() + (keep - audio_base_));
            audio_base_ = keep;
        }
        long keep_bb = scan_t_ - 8 * FRAME_SCAN;
        if (keep_bb - bb_base_ > 20 * FRAME_SCAN) {
            bb_.erase(bb_.begin(), bb_.begin() + (keep_bb - bb_base_));
            bb_base_ = keep_bb;
        }
    }

    // ---------------- preamble scan ----------------
    void scan(bool eof) {
        const long half = static_cast<long>(rrc_.size() / 2);
        // extend scan baseband: bb[j] = sum_m h[m] * a[5j+m] * lo[5j+m]
        for (;;) {
            long j = bb_base_ + static_cast<long>(bb_.size());
            long c = j * DECIM;
            if (c + half >= audio_end_) break;
            std::complex<float> acc(0, 0);
            for (long m = -half; m <= half; m++) {
                long n = c + m;
                if (n < audio_base_) continue;
                acc += rrc_[m + half] * audio_at(n) * lo_[n % 80];
            }
            bb_.push_back(acc);
        }
        const long span = static_cast<long>(corr_.span());
        const long bb_end = bb_base_ + static_cast<long>(bb_.size());
        for (; scan_t_ + span <= bb_end; scan_t_++) {
            if (have_cand_ && scan_t_ > cand_.t + HOLD_SCAN) finish_cand();
            float m = corr_.evaluate(&bb_[static_cast<size_t>(scan_t_ - bb_base_)]);
            if (m >= opt_.threshold && (!have_cand_ || m > cand_.metric)) {
                cand_ = {scan_t_, m, 0.0f};
                have_cand_ = true;
            }
            if (have_chain_ && scan_t_ > chain_last_ + 2 * FRAME_SCAN + HOLD_SCAN + TOL_SCAN)
                close_chain();
        }
        if (eof) {
            if (have_cand_) finish_cand();
            if (have_chain_) close_chain();
        }
    }

    void finish_cand() {
        corr_.evaluate(&bb_[static_cast<size_t>(cand_.t - bb_base_)], &cand_.freq);
        have_cand_ = false;
        const Hit h = cand_;
        if (have_chain_) {
            for (long k = 1; k <= 2; k++) {
                if (std::labs(h.t - chain_last_ - k * FRAME_SCAN) <= TOL_SCAN) {
                    chain_last_ = h.t;
                    chain_.push_back(h);
                    return;
                }
            }
            close_chain();
        }
        have_chain_ = true;
        chain_.assign(1, h);
        chain_last_ = h.t;
    }

    void close_chain() {
        have_chain_ = false;
        Message m;
        m.id = next_id_++;
        const int nf = preamble_frames(mode_);
        const long last = chain_.back().t;
        // For a known mode, count back from the last frame (data follows it),
        // so a missed first frame costs nothing. For AUTO, start at the first
        // frame seen: the receiver reads the countdown of whichever frame it
        // locks onto, so any frame is a valid entry point.
        m.first_frame = nf > 0 ? last - (nf - 1) * FRAME_SCAN : chain_.front().t;
        m.preamble_end = last + FRAME_SCAN;
        m.hits = static_cast<int>(chain_.size());
        double ms = 0, fs = 0;
        for (auto& h : chain_) { ms += h.metric; fs += h.freq; }
        m.metric = static_cast<float>(ms / chain_.size());
        m.freq = static_cast<float>(fs / chain_.size());
        pending_.push_back(m);
        chain_.clear();
    }

    // ---------------- message end and decode ----------------
    bool decide_end(Message& m, bool eof) {
        const long a_first = m.first_frame * DECIM;
        const long cap = a_first + static_cast<long>(opt_.max_secs * FS);
        // power of the preamble, and of the noise before it
        auto mean_blocks = [&](long a0, long a1, bool median) {
            std::vector<double> v;
            for (long b = (a0 + BLOCK - 1) / BLOCK; (b + 1) * BLOCK <= a1; b++)
                if (b >= 0 && b < static_cast<long>(blocks_.size())) v.push_back(blocks_[b]);
            if (v.empty()) return std::make_pair(0.0, 0);
            if (median) {
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                return std::make_pair(v[v.size() / 2], static_cast<int>(v.size()));
            }
            double s = 0; for (double x : v) s += x;
            return std::make_pair(s / v.size(), static_cast<int>(v.size()));
        };
        auto sig = mean_blocks(a_first, m.preamble_end * DECIM, false);
        auto noise = mean_blocks(a_first - static_cast<long>(3 * FS), a_first - BLOCK, true);
        double thr = -1;
        if (sig.second > 0) {
            if (noise.second >= 5 && sig.first > 1.4 * noise.first)
                thr = noise.first + 0.35 * (sig.first - noise.first);
            else if (noise.second < 5)
                thr = 0.45 * sig.first;  // no noise reference: half the preamble power
        }
        if (thr > 0) {
            const long quiet = 15;  // 1.5 s below threshold ends the message
            long run = 0;
            for (long b = (m.preamble_end * DECIM) / BLOCK + 1;
                 b < static_cast<long>(blocks_.size()) && b * BLOCK < cap; b++) {
                run = blocks_[b] < thr ? run + 1 : 0;
                if (run == quiet) {
                    m.end_audio = (b - quiet + 1) * BLOCK + static_cast<long>(0.5 * FS);
                    break;
                }
            }
        }
        if (m.end_audio < 0 && audio_end_ >= cap) m.end_audio = cap;
        if (m.end_audio < 0 && eof) m.end_audio = audio_end_;
        if (m.end_audio < 0) return false;
        m.end_audio = std::min(m.end_audio, cap);
        return audio_end_ >= m.end_audio || eof;
    }

    void process_messages(bool eof) {
        while (!pending_.empty()) {
            Message& m = pending_.front();
            if (!decide_end(m, eof)) break;
            m.end_audio = std::min(m.end_audio, audio_end_);
            decode(m);
            pending_.pop_front();
        }
    }

    void decode(const Message& m) {
        const long a_first = m.first_frame * DECIM;
        const long a0 = std::max(a_first - MARGIN, audio_base_);
        const long a1 = m.end_audio;
        if (a1 - a0 < static_cast<long>(0.5 * FS)) return;
        Samples s(static_cast<size_t>(a1 - a0));
        double p = 0;
        for (long n = a0; n < a1; n++) {
            float v = audio_at(n);
            s[static_cast<size_t>(n - a0)] = v;
            if (n >= a_first) p += static_cast<double>(v) * v;
        }
        const double rms = std::sqrt(p / std::max<long>(1, a1 - std::max(a_first, a0)));
        const float gain = rms > 0 ? static_cast<float>(TARGET_RMS / rms) : 1.0f;
        for (auto& v : s) v = std::max(-1.0f, std::min(1.0f, v * gain));

        RxConfig cfg;
        cfg.sample_rate = static_cast<float>(FS);
        cfg.mode = mode_;
        cfg.equalizer = eq_;

        std::stringstream sink;
        std::streambuf* old_out = std::cout.rdbuf();
        std::streambuf* old_err = std::cerr.rdbuf();
        std::cout.rdbuf(opt_.verbose ? old_err : sink.rdbuf());
        if (!opt_.verbose) std::cerr.rdbuf(sink.rdbuf());
        DecodeResult r = m110a::api::decode(s, cfg);
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);

        std::vector<uint8_t> d = r.data;
        // ModemRX cuts the data at the EOM itself and says so in eom_sequence
        long eom_bit = find_eom(d);
        if (eom_bit >= 0) d.resize(static_cast<size_t>(eom_bit / 8));
        const bool eom = eom_bit >= 0 || r.eom_sequence;
        std::string text, hex;
        char hx[4];
        for (uint8_t c : d) {
            text += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
            snprintf(hx, sizeof hx, "%02x", c);
            hex += hx;
        }
        printf("msg=%d start_s=%.3f end_s=%.3f mode=%s preamble_hits=%d acq_metric=%.2f "
               "acq_foff_hz=%+.2f success=%d quality_db=%.1f quality_threshold_db=%.1f eom=%d "
               "bytes=%zu snr_db=%.1f d1=%d d2=%d countdown=%d err=\"%s\"\n",
               m.id, a_first / FS, a1 / FS, mode_name(r.mode).c_str(), m.hits, m.metric,
               m.freq, r.success ? 1 : 0, r.quality_db, r.quality_threshold_db,
               eom ? 1 : 0, d.size(), r.snr_db, r.d1, r.d2, r.countdown,
               r.error ? r.error->message.c_str() : "");
        printf("  text=\"%s\"\n  hex=%s\n", text.c_str(), hex.c_str());
        fflush(stdout);
        if (r.success) decoded_ok_++;
        if (opt_.have_expect && r.success && eom &&
            std::string(d.begin(), d.end()) == opt_.expect)
            expect_met_ = true;
    }

    const Options& opt_;
    Mode mode_;
    Equalizer eq_;
    PreambleCorrelator corr_;
    std::vector<float> rrc_;
    std::complex<float> lo_[80];

    std::vector<float> audio_;
    long audio_base_ = 0, audio_end_ = 0;
    std::vector<double> blocks_;
    std::vector<std::complex<float>> bb_;
    long bb_base_ = 0;
    long scan_t_ = 0;

    bool have_cand_ = false;
    Hit cand_{};
    bool have_chain_ = false;
    long chain_last_ = 0;
    std::vector<Hit> chain_;
    std::deque<Message> pending_;
    int next_id_ = 1;
    int decoded_ok_ = 0;
    bool expect_met_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) usage(("missing value for " + a).c_str());
            return argv[++i];
        };
        if (a == "--mode") o.mode_s = val();
        else if (a == "--rate") o.rate = atof(val().c_str());
        else if (a == "--offset") o.offset = atof(val().c_str());
        else if (a == "--dial") o.dial = atof(val().c_str());
        else if (a == "--ppm") o.ppm = atof(val().c_str());
        else if (a == "--format") o.format = val();
        else if (a == "--eq") o.eq_s = val();
        else if (a == "--threshold") o.threshold = atof(val().c_str());
        else if (a == "--max-secs") o.max_secs = atof(val().c_str());
        else if (a == "--expect") { o.expect = val(); o.have_expect = true; }
        else if (a == "--dump-audio") o.dump_audio = val();
        else if (a == "-v") o.verbose = true;
        else if (a == "-h" || a == "--help") usage(nullptr);
        else if (!a.empty() && a[0] == '-' && a != "-") usage(("unknown option " + a).c_str());
        else if (o.input.empty()) o.input = a;
        else usage("more than one input");
    }
    if (o.input.empty()) usage("no input");
    Mode mode;
    if (!parse_mode(o.mode_s, mode)) usage(("unknown mode " + o.mode_s).c_str());
    Equalizer eq;
    if (!parse_eq(o.eq_s, eq)) usage(("unknown equalizer " + o.eq_s).c_str());

    FILE* in = o.input == "-" ? stdin : fopen(o.input.c_str(), "rb");
    if (!in) { fprintf(stderr, "m110a_rx: cannot open %s\n", o.input.c_str()); return 2; }

    auto ends_with = [&](const std::string& s) {
        return o.input.size() >= s.size() && o.input.compare(o.input.size() - s.size(), s.size(), s) == 0;
    };
    // rsp2_stream.py sidecar
    if (o.input != "-" && ends_with(".cs16")) {
        std::ifstream js(o.input.substr(0, o.input.size() - 5) + ".json");
        if (js) {
            std::string t((std::istreambuf_iterator<char>(js)), std::istreambuf_iterator<char>());
            double rate, dial, tuner;
            if (o.rate <= 0 && json_number(t, "rate_readback", rate)) o.rate = rate;
            bool have_dial = json_number(t, "wwv_hz", dial);
            if (std::isnan(o.dial) && have_dial) o.dial = dial;
            if (std::isnan(o.offset) && have_dial && json_number(t, "tuner_hz_readback", tuner))
                o.offset = dial - tuner;
        }
    }
    // .iqr header (64 bytes, magic IQR1)
    if (o.input != "-" && ends_with(".iqr")) {
        IQRHeader h;
        if (fread(&h, sizeof h, 1, in) != 1 || memcmp(h.magic, "IQR1", 4) != 0) {
            fprintf(stderr, "m110a_rx: %s is not an IQR1 file\n", o.input.c_str());
            return 2;
        }
        if (o.rate <= 0 && h.sample_rate > 0) o.rate = h.sample_rate;
        if (o.format.empty()) o.format = "cs16";
    }
    if (o.format.empty()) o.format = ends_with(".cf32") ? "cf32" : "cs16";
    if (o.format != "cs16" && o.format != "cf32") usage("--format must be cs16 or cf32");
    if (o.rate <= 0) o.rate = 2000000.0;
    if (std::isnan(o.offset)) o.offset = 100000.0;
    if (o.ppm != 0 && std::isnan(o.dial)) usage("--ppm needs --dial (or a .json with wwv_hz)");
    const double shift = o.offset - (o.ppm != 0 ? o.dial * o.ppm * 1e-6 : 0.0);

    fprintf(stderr, "m110a_rx: %s %s @ %.0f S/s, dial at %+.2f Hz (offset %.0f, ppm %.3f), mode %s, eq %s\n",
            o.input.c_str(), o.format.c_str(), o.rate, shift, o.offset, o.ppm,
            o.mode_s.c_str(), o.eq_s.c_str());

    SdrFrontEnd fe(o.rate, shift);
    Receiver rx(o, mode, eq);
    FILE* dump = o.dump_audio.empty() ? nullptr : fopen(o.dump_audio.c_str(), "wb");

    const size_t CHUNK = 1 << 16;
    std::vector<int16_t> b16(2 * CHUNK);
    std::vector<float> b32(2 * CHUNK);
    std::vector<float> audio;
    size_t pend = 0;  // bytes of a partial sample pair carried over (stdin)
    const size_t pair_bytes = o.format == "cs16" ? 4 : 8;
    std::vector<uint8_t> raw(CHUNK * pair_bytes);
    for (;;) {
        size_t got = fread(raw.data() + pend, 1, raw.size() - pend, in);
        bool eof = got == 0;
        size_t total = pend + got;
        size_t pairs = total / pair_bytes;
        audio.clear();
        if (pairs > 0) {
            if (o.format == "cs16") {
                memcpy(b16.data(), raw.data(), pairs * 4);
                fe.push_cs16(b16.data(), pairs, audio);
            } else {
                memcpy(b32.data(), raw.data(), pairs * 8);
                fe.push_cf32(b32.data(), pairs, audio);
            }
        }
        pend = total - pairs * pair_bytes;
        if (pend) memmove(raw.data(), raw.data() + pairs * pair_bytes, pend);
        if (dump && !audio.empty()) fwrite(audio.data(), sizeof(float), audio.size(), dump);
        rx.push_audio(audio, eof);
        if (eof) break;
    }
    if (dump) fclose(dump);
    if (in != stdin) fclose(in);

    fprintf(stderr, "m110a_rx: %d preamble(s) found, %d decoded with success\n",
            rx.found(), rx.decoded_ok());
    if (o.have_expect) return rx.expect_met() ? 0 : 1;
    return rx.decoded_ok() > 0 ? 0 : 1;
}
