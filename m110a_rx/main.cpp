/**
 * m110a_rx - receive MIL-STD-188-110A from SDR I/Q (file or stdin) to bytes.
 *
 * Chain: I/Q -> shift dial to 0 Hz (offset + clock-error correction)
 *        -> IQSource decimation to 48 kHz -> USB 300-3300 Hz -> audio
 *        -> preamble scan over the whole stream (any start time, any carrier
 *           offset within +/-87 Hz) -> per preamble chain: find its end,
 *           normalise its level, decode with ModemRX -> cut at EOM -> the chains
 *           of one preamble merged (Receiver::process_messages) -> print one.
 *
 * 75 bps (AUTO, 75S, 75L): the preamble correlator above reads one frame at a time
 * and finds no preamble below about -7 dB in 3 kHz, where 75 bps still decodes. So
 * the 75 bps acquisition (src/m110a/m75_acq.h), which integrates the whole preamble,
 * runs on the audio as well (Receiver::m75_detect): a preamble whose D1/D2 read 7,5
 * or 5,5, with the 3 or 24 frames they name, is one 75 bps message, decoded from its
 * preamble. In AUTO the correlator's chains that fall inside a 75 bps preamble are
 * that message's own and are not reported (Receiver::m75_claims). Forced to 75S or
 * 75L, only the 75 bps acquisition looks for preambles. Every other mode is found and
 * decoded as before. A 75 bps message line gives preamble_hits = the frames its
 * acquisition integrated (3 or 24) and acq_metric = that acquisition's significance
 * (z, found at 9 or more), where other modes give the correlator's hits and metric.
 *
 * A message's end, for the decode window: the power steps down and stays down
 * (see Receiver::find_step). Measured only from the message's own audio, so it is
 * the same whatever lead-in precedes the preamble, and a fade, however deep or
 * slow, is not a step. Without a step the window runs to the next message's
 * preamble, --max-secs or the end of the input. Inside the window ModemRX ends
 * the bytes at the EOM, or at the last interleaver block the probes cover.
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
 *   --ppm P          receiver clock error, ppm, + = fast. The input's true sample
 *                    rate is then rate * (1 + P * 1e-6), which the front end undoes
 *                    on the audio so the symbol timing comes out right, and the dial is looked for at offset - dial * P * 1e-6
 *                    (with --dial 0 the rate alone is corrected). It is the receiver's
 *                    clock only: the sender's symbol clock is followed by the data
 *                    grid's drift tracking (docs/m110a-timing-steps.md), not by --ppm.
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
#include "m110a/m75_rx.h"
#include "sync/preamble_acq.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
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
// End step: the power before the step at least STEP_RATIO times every 0.5 s mean of the
// STEP_HOLD blocks from it.
constexpr int STEP_HOLD = 40;                  // 4 s
constexpr double STEP_RATIO = 2.0;             // 3 dB
constexpr long END_PAD = static_cast<long>(0.5 * FS);  // audio kept after the step
constexpr float TARGET_RMS = 7000.0f / 32768.0f;  // MS-DMT reference level
constexpr long MARGIN = static_cast<long>(0.04 * FS);  // audio before frame 1
// A window with no power step at its end, longer than twice this, is decoded over its first
// FIRST_WINDOW before the whole of it (Receiver::decode); a message that runs past it costs
// at most half a decode more.
constexpr long FIRST_WINDOW = static_cast<long>(30 * FS);
// A decode of a split preamble that clears its block threshold by less than this needs other
// chains to agree on its bytes (Receiver::process_messages).
constexpr float CORROBORATE_DB = 0.75f;
// Chains on one preamble's frame grid: within this of a whole number of frames apart.
// Two Watterson paths 2 ms apart move the correlation peak by 4.8 symbols.
constexpr long GRID_TOL = 10L * SPS_SCAN;
// 75 bps acquisition (Receiver::m75_detect): m75::acquire over the M75_WINDOW of audio before
// each point of a grid M75_STEP apart, run only when the sliding coarse search
// (m75::SlidingAcq) holds an end of preamble there at M75_PRESCREEN of acquire's threshold.
// A window that ends inside a long preamble reads a frame boundary inside it as its end, so
// a preamble found is confirmed by acquire over its whole countdown again, once the audio
// reaches M75_STEP past where that countdown would end if the end found were its first frame's.
constexpr long M75_WINDOW = static_cast<long>(6.0 * FS);
constexpr long M75_STEP = static_cast<long>(0.5 * FS);
constexpr double M75_PRESCREEN = 0.75;
constexpr long M75_FRAME = 480L * SPS_AUDIO;
// Two 75 bps acquisitions of one preamble: ends of preamble within a frame
constexpr long M75_SAME = M75_FRAME;

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

// One decode of a message window, before it is reported.
struct Decoded {
    DecodeResult r;
    std::vector<uint8_t> d;  // the bytes, cut at the EOM
    bool eom = false;
    long a1 = 0;             // window end, absolute audio index
    bool ran = false;        // false: the window was under 0.5 s, nothing decoded
    long end75 = -1;         // 75 bps: where the transmission decoded ends (absolute audio index)
};

struct Message {
    int id;
    long first_frame;     // scan index of the first preamble frame
    long first_hit;       // scan index of the chain's first frame seen
    long preamble_end;    // scan index just after the last frame
    int hits;
    float metric, freq;   // mean over the hits
    long end_audio = -1;  // decided end, absolute audio index
    bool end_at_step = false;  // end_audio is a power step
    std::vector<double> pw;  // 0.1 s block power from first_frame * DECIM on
    bool done = false;    // decoded, result in out
    Decoded out;
    // A 75 bps message found by the 75 bps acquisition (m75_detect, m75_claims)
    bool m75 = false;
    Mode m75_mode = Mode::AUTO;  // the mode D1/D2 read (or the forced one)
    int m75_frames = 0;          // frames the acquisition integrated
    long m75_ds = 0;             // end of its preamble, absolute audio index
    bool m75_checked = false;    // a correlator chain already found not inside a 75 bps preamble
};

class Receiver {
public:
    Receiver(const Options& o, Mode mode, Equalizer eq)
        : opt_(o), mode_(mode), eq_(eq), corr_(SPS_SCAN) {
        m75_forced_ = mode_ == Mode::M75_SHORT || mode_ == Mode::M75_LONG;
        if (m75_forced_ || mode_ == Mode::AUTO) {
            const RxConfig rc;
            sacq_ = std::make_unique<m75::SlidingAcq>(static_cast<float>(FS), rc.carrier_freq, rc.coarse_search_range,
                                                      M75_WINDOW / FS + 1.0, m75::AcqTuning().coarse_step_hz);
        }
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
        if (sacq_) {
            sacq_->push(a.data(), a.size());
            m75_detect(eof);
        }
        if (!m75_forced_) scan(eof);
        process_messages(eof);
        trim();
    }

    int found() const { return next_id_ - 1 - claimed_; }
    int decoded_ok() const { return decoded_ok_; }
    bool expect_met() const { return expect_met_; }

private:
    // ---------------- buffers (absolute indices) ----------------
    float audio_at(long n) const { return audio_[static_cast<size_t>(n - audio_base_)]; }

    void trim() {
        long keep = std::min(m75_forced_ ? audio_end_ : scan_t_ * DECIM, audio_end_) - static_cast<long>(40 * FS);
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
        m.first_hit = chain_.front().t;
        m.preamble_end = last + FRAME_SCAN;
        m.hits = static_cast<int>(chain_.size());
        double ms = 0, fs = 0;
        for (auto& h : chain_) { ms += h.metric; fs += h.freq; }
        m.metric = static_cast<float>(ms / chain_.size());
        m.freq = static_cast<float>(fs / chain_.size());
        pending_.push_back(m);
        chain_.clear();
    }

    // ---------------- 75 bps preambles ----------------
    // The 75 bps acquisition at each grid point up to the audio received (and once at its
    // end): the grid, not the size of the pieces the input arrives in, decides where it runs.
    void m75_detect(bool eof) {
        if (m75_next_ == 0) m75_next_ = M75_STEP;
        for (; m75_next_ <= audio_end_; m75_next_ += M75_STEP) m75_run(m75_next_, false);
        if (eof) m75_run(audio_end_, true);
    }

    // m75::acquire over the M75_WINDOW before audio index t (from the last 75 bps preamble's
    // end on), run when the sliding search holds an end of preamble there. Found: a preamble
    // wholly inside the window whose countdown is the length its D1/D2 name (75S 3 frames,
    // 75L 24: the first frames of a 75L preamble score well as a 3-frame countdown), read
    // best as 7,5 or 5,5 in AUTO (m75::takes_75), the forced mode's length when forced. A
    // window ending inside a 75L preamble finds a frame boundary inside it, with frames before
    // the preamble counted, so what is found is a candidate: acquire runs again over the
    // countdown before it and as long again after it, plus M75_STEP, once that audio is in
    // (or at the end of input), and the preamble that run finds is taken.
    void m75_run(long t, bool eof) {
        Mode mode;
        int frames = 0;
        long ds = 0;
        float foff = 0.0f;
        double z = 0.0;
        if (m75_cand_) {
            if (t < m75_confirm_at_ && !eof) return;
            m75_cand_ = false;
            const long lo = std::max(m75_floor_, m75_cand_ds_ - m75_cand_frames_ * M75_FRAME - M75_STEP);
            if (m75_acquire(lo, t, mode, frames, ds, foff, z)) m75_add(mode, frames, ds, foff, z);
            return;
        }
        const long w0 = std::max({t - M75_WINDOW, m75_floor_, audio_base_});
        if (t - w0 < static_cast<long>(0.8 * FS)) return;
        const m75::AcqTuning tune;
        const m75::SlidingAcq::Best b = sacq_->best(w0, t);
        if (b.z < M75_PRESCREEN * tune.min_z) return;
        // The sliding search's best end was looked at by an earlier run and nothing was taken:
        // acquire would find the same (it stays in the window for 6 s; another mode's data can
        // hold one, measured in 600L and 300L)
        for (long e : m75_seen_)
            if (std::labs(e - b.end) <= M75_FRAME / 4) return;
        // Another mode's preamble (or part of it), read clearly: search after it from now on.
        // A 75 bps preamble read clearly reads 7,5 or 5,5, so this does not skip one.
        long other = -1;
        if (!m75_acquire(w0, t, mode, frames, ds, foff, z, &other)) {
            if (other > 0) m75_floor_ = std::max(m75_floor_, other);
            m75_seen_.push_back(b.end);
            if (m75_seen_.size() > 16) m75_seen_.erase(m75_seen_.begin());
            return;
        }
        if (eof) { m75_add(mode, frames, ds, foff, z); return; }
        m75_cand_ = true;
        m75_cand_ds_ = ds;
        m75_cand_frames_ = frames;
        m75_confirm_at_ = ds + frames * M75_FRAME + M75_STEP;
    }

    // m75::acquire over audio [a0, a1): a 75 bps preamble as m75_run takes it, its end ds
    // (absolute audio index), or false
    // other: set to the end found of a preamble whose D1/D2 read clearly as a code other than
    // 75 bps (another mode's preamble, or part of one), else left alone.
    bool m75_acquire(long a0, long a1, Mode& mode, int& frames, long& ds, float& foff, double& z,
                     long* other = nullptr) const {
        a0 = std::max(a0, audio_base_);
        a1 = std::min(a1, audio_end_);
        if (a1 - a0 < static_cast<long>(0.8 * FS)) return false;
        std::vector<float> rf(audio_.begin() + (a0 - audio_base_), audio_.begin() + (a1 - audio_base_));
        const RxConfig rc;
        const m75::Acquisition a = m75::acquire(rf, static_cast<float>(FS), rc.carrier_freq, rc.coarse_search_range);
        if (!a.found) return false;
        if (other && !a.is_75 && m75::clear_d(a)) *other = a0 + a.data_start;
        if (m75_forced_) {
            // D1/D2 read best as the forced mode's: a strong 75 bps transmission's own data
            // can score as a countdown at some carrier offset, with no 75 bps code read there
            mode = mode_;
            if (!a.is_75 || (a.mode == ModeId::M75NS) != (mode_ == Mode::M75_SHORT)) return false;
        } else {
            if (!m75::takes_75(a)) return false;
            mode = a.mode == ModeId::M75NS ? Mode::M75_SHORT : Mode::M75_LONG;
        }
        if (a.frames != preamble_frames(mode)) return false;
        if (a.data_start < a.frames * M75_FRAME) return false;  // frames before the window counted
        frames = a.frames;
        ds = a0 + a.data_start;
        foff = a.freq_offset_hz;
        z = a.z;
        return true;
    }

    // A 75 bps message ending its preamble at ds, unless one already ends within a frame of it
    // or one that starts a frame or more inside a message of another mode already reported.
    // True when the message is there (added now or before), false when it was refused.
    bool m75_add(Mode mode, int frames, long ds, float foff, double z) {
        for (const auto& p : m75_pre_)
            if (std::labs(p.second - ds) <= M75_SAME) return true;
        m75_floor_ = std::max(m75_floor_, ds);
        const long pre0 = ds - static_cast<long>(frames) * M75_FRAME;
        for (const auto& w : corr_win_)
            if (pre0 > w.first && pre0 + M75_FRAME < w.second) {
                if (opt_.verbose)
                    fprintf(stderr, "m110a_rx: 75 bps preamble at %.3f s is inside the message before it\n", pre0 / FS);
                return false;
            }
        m75_pre_.push_back({pre0, ds});
        if (m75_pre_.size() > 64) m75_pre_.erase(m75_pre_.begin());
        Message m;
        m.id = next_id_++;
        m.first_frame = pre0 / DECIM;
        m.first_hit = m.first_frame;
        m.preamble_end = ds / DECIM;
        m.hits = frames;
        m.metric = static_cast<float>(z);
        m.freq = foff;
        m.m75 = true;
        m.m75_mode = mode;
        m.m75_frames = frames;
        m.m75_ds = ds;
        if (opt_.verbose)
            fprintf(stderr, "m110a_rx: 75 bps preamble %.3f-%.3f s, %s, z=%.1f, %+.1f Hz\n", pre0 / FS, ds / FS,
                    mode_name(mode).c_str(), z, foff);
        auto it = pending_.begin();
        while (it != pending_.end() && it->first_frame <= m.first_frame) ++it;
        pending_.insert(it, m);
        return true;
    }

    // AUTO: a correlator chain that starts inside a 75 bps preamble belongs to that 75 bps
    // message. Known preambles first; otherwise m75::acquire over the audio around the chain
    // (the 75 bps acquisition takes a preamble once its end has passed, later than the chain
    // is due), and a 75 bps preamble found there becomes the message.
    bool m75_claims(const Message& m) {
        const long a = m.first_hit * DECIM, tol = GRID_TOL * DECIM;
        auto inside = [&](long pre0, long ds) { return a >= pre0 - tol && a <= ds + tol; };
        for (const auto& p : m75_pre_)
            if (inside(p.first, p.second)) return true;
        Mode mode;
        int frames = 0;
        long ds = 0;
        float foff = 0.0f;
        double z = 0.0;
        if (!m75_acquire(a - 25L * M75_FRAME, audio_end_, mode, frames, ds, foff, z)) return false;
        if (!inside(ds - static_cast<long>(frames) * M75_FRAME, ds)) return false;
        return m75_add(mode, frames, ds, foff, z);
    }

    // ---------------- message end and decode ----------------
    // Block powers on a grid anchored at the message's first preamble frame, so they
    // do not move with the audio before it.
    void update_power(Message& m) {
        const long a_first = m.first_frame * DECIM;
        for (;;) {
            const long b0 = a_first + static_cast<long>(m.pw.size()) * BLOCK;
            if (b0 + BLOCK > audio_end_) break;
            double p = 0;
            for (long n = b0; n < b0 + BLOCK; n++) {
                double v = n >= audio_base_ ? audio_at(n) : 0.0;
                p += v * v;
            }
            m.pw.push_back(p / BLOCK);
        }
    }

    // First block k (from k0, k + STEP_HOLD <= kmax) where the power steps down and stays
    // down: the 0.3 s before k (k-4..k-2; k-1 may hold the step) and the 1 s before it are
    // both at least STEP_RATIO times the highest 0.5 s mean of the STEP_HOLD blocks from k.
    // A transmitter unkeying leaves that, even while the receiver's AGC brings the noise
    // back up after it. A fade does not: a slow one has no step, and a fast one comes back
    // up inside the hold. -1 when there is none.
    static long find_step(const std::vector<double>& pw, long k0, long kmax) {
        k0 = std::max<long>(k0, 10);
        for (long k = k0; k + STEP_HOLD <= kmax; k++) {
            const double before_short = (pw[k - 4] + pw[k - 3] + pw[k - 2]) / 3.0;
            double before_long = 0;
            for (long j = k - 10; j < k; j++) before_long += pw[j];
            const double before = std::min(before_short, before_long / 10.0);
            bool down = true;
            for (long j = k; j + 5 <= k + STEP_HOLD && down; j += 5) {
                const double mj = (pw[j] + pw[j + 1] + pw[j + 2] + pw[j + 3] + pw[j + 4]) / 5.0;
                down = STEP_RATIO * mj <= before;
            }
            if (down) return k;
        }
        return -1;
    }

    // next: the next message whose preamble is already known, else nullptr.
    bool decide_end(Message& m, const Message* next, bool eof) {
        const long a_first = m.first_frame * DECIM;
        long limit = a_first + static_cast<long>(opt_.max_secs * FS);
        if (next) limit = std::min(limit, next->first_frame * DECIM - MARGIN);
        update_power(m);
        const long k0 = (m.preamble_end * DECIM - a_first + BLOCK - 1) / BLOCK + 1;
        const long kmax = std::min<long>(static_cast<long>(m.pw.size()),
                                         (limit - a_first) / BLOCK);
        const long k = find_step(m.pw, k0, kmax);
        m.end_at_step = k >= 0;
        if (k >= 0) m.end_audio = a_first + k * BLOCK + END_PAD;
        else if (next || audio_end_ >= limit) m.end_audio = limit;
        else if (eof) m.end_audio = audio_end_;
        else return false;
        m.end_audio = std::max(std::min(m.end_audio, limit), m.preamble_end * DECIM);
        if (opt_.verbose)
            fprintf(stderr, "m110a_rx: msg %d end %.3f s (%s)\n", m.id, m.end_audio / FS,
                    k >= 0 ? "power step" : next ? "next preamble" : audio_end_ >= limit ? "max-secs" : "end of input");
        return audio_end_ >= m.end_audio || eof;
    }

    static bool on_grid(long a, long b) {
        long r = (b - a) % FRAME_SCAN;
        if (r < 0) r += FRAME_SCAN;
        return std::min(r, FRAME_SCAN - r) <= GRID_TOL;
    }

    // Preamble frames of a decoded mode; 24, the longest, when the mode was not read.
    static int preamble_frames_of(const Decoded& o) {
        const int n = preamble_frames(o.r.mode);
        return n > 0 ? n : 24;
    }

    // a is a better decode of one message than b: success, then EOM, then the worst
    // interleaver block's quality, then quality_db.
    static bool better(const Decoded& a, const Decoded& b) {
        if (a.ran != b.ran) return a.ran;
        if (a.r.success != b.r.success) return a.r.success;
        if (a.eom != b.eom) return a.eom;
        if (a.r.block_quality_min_db != b.r.block_quality_min_db)
            return a.r.block_quality_min_db > b.r.block_quality_min_db;
        return a.r.quality_db > b.r.quality_db;
    }

    // One transmission, one message. A fade, or multipath moving the correlation peak by
    // more than TOL_SCAN from frame to frame, splits one preamble into several chains (on
    // Watterson moderate and poor, 14-16 for one 24-frame preamble), and each chain's window
    // holds the whole message. The chains that start inside the front message's preamble, on
    // its frame grid, are decoded each as before and reported as one message: the best
    // decode (better()), with preamble_chains saying how many there were. Inside its
    // preamble means at most P - 1 frames after the front chain's first frame, P the
    // preamble length of the mode both decodes read (24 when either is unread), so a short
    // message that follows within 24 frames stays a message of its own.
    void process_messages(bool eof) {
        while (!pending_.empty()) {
            if (pending_.front().m75) {  // one message, no chains to merge
                // Its window is not ended by a 75 bps preamble found after it: a strong 75 bps
                // transmission's data can score as one. Those that start before the decoded
                // transmission ends are dropped once it is decoded.
                Message& c = pending_.front();
                const Message* next = nullptr;
                for (size_t j = 1; j < pending_.size() && !next; j++)
                    if (!pending_[j].m75 && pending_[j].first_frame >= c.first_frame + 24 * FRAME_SCAN)
                        next = &pending_[j];
                if (!decide_end(c, next, eof)) return;
                c.end_audio = std::min(c.end_audio, audio_end_);
                c.out = decode(c);
                report(c, c.out, c.hits, 1, 1);
                const long end = c.out.end75;
                pending_.pop_front();
                for (auto it = pending_.begin(); it != pending_.end();) {
                    if (it->m75 && it->first_frame * DECIM < end) {
                        if (opt_.verbose)
                            fprintf(stderr, "m110a_rx: 75 bps preamble at %.3f s is inside the message before it\n",
                                    it->first_frame * DECIM / FS);
                        claimed_++;
                        it = pending_.erase(it);
                    } else {
                        ++it;
                    }
                }
                continue;
            }
            const Message& m = pending_.front();
            const long span_end = m.first_hit + 23 * FRAME_SCAN + GRID_TOL;
            // every chain that can start inside the longest preamble is closed
            if (!eof && (scan_t_ <= span_end + HOLD_SCAN + 1 || (have_chain_ && chain_.front().t <= span_end)))
                break;
            if (sacq_ && !m.m75_checked) {
                const long hit = m.first_hit;  // m75_claims may add a message in front of m
                const bool claimed = m75_claims(m);
                if (!claimed) pending_.front().m75_checked = true;  // decoding it may wait for audio
                if (claimed) {  // part of a 75 bps preamble: that message reports it
                    if (opt_.verbose)
                        fprintf(stderr, "m110a_rx: chain at %.3f s is inside a 75 bps preamble\n", hit * DECIM / FS);
                    claimed_++;
                    for (auto it = pending_.begin(); it != pending_.end(); ++it)
                        if (!it->m75 && it->first_hit == hit) { pending_.erase(it); break; }
                    continue;
                }
            }
            std::vector<size_t> cand;
            for (size_t i = 0; i < pending_.size() && pending_[i].first_hit <= span_end; i++)
                if (i == 0 || (!pending_[i].m75 && on_grid(m.first_hit, pending_[i].first_hit))) cand.push_back(i);
            for (size_t i : cand) {
                Message& c = pending_[i];
                if (c.done) continue;
                // A preamble that starts within 24 frames (the longest preamble) of c's first
                // frame may be c's own: only a later one is a different message, and bounds c.
                // A 75 bps preamble the 75 bps acquisition found does not: inside another mode's
                // data it can be a false one (measured on a net 300L burst, z 13.8 and 17.4); a
                // real one also has its correlator chain here at the SNR c was found at.
                const Message* next = nullptr;
                for (size_t j = i + 1; j < pending_.size() && !next; j++)
                    if (!pending_[j].m75 && pending_[j].first_frame >= c.first_frame + 24 * FRAME_SCAN)
                        next = &pending_[j];
                if (!decide_end(c, next, eof)) return;
                c.end_audio = std::min(c.end_audio, audio_end_);
                c.out = decode(c);
                c.done = true;
            }
            const int pm = preamble_frames_of(m.out);
            std::vector<size_t> group;
            for (size_t i : cand) {
                const Message& c = pending_[i];
                const int p = std::min(pm, preamble_frames_of(c.out));
                if (i == 0 || c.first_hit <= m.first_hit + (p - 1) * FRAME_SCAN + GRID_TOL) group.push_back(i);
            }
            // Which bytes: the ones most chains with an EOM decoded. A chain decodes the whole
            // transmission from its own entry point; right bytes come out the same from
            // several, wrong ones differ from chain to chain. Ties go to bytes a chain with
            // success carries, then to the better decode. Without an EOM on any chain, the
            // better decode. Reported from the best decode carrying those bytes, so success
            // means a chain with them cleared the gate.
            std::vector<int> votes(group.size(), 0);
            for (size_t a = 0; a < group.size(); a++) {
                const Decoded& oa = pending_[group[a]].out;
                if (!oa.ran || !oa.eom) continue;
                for (size_t b = 0; b < group.size(); b++) {
                    const Decoded& ob = pending_[group[b]].out;
                    if (ob.ran && ob.eom && ob.d == oa.d) votes[a]++;
                }
            }
            auto has_success = [&](size_t a) {
                for (size_t b = 0; b < group.size(); b++)
                    if (votes[b] > 0 && pending_[group[b]].out.d == pending_[group[a]].out.d &&
                        pending_[group[b]].out.r.success) return true;
                return false;
            };
            size_t bi = 0;
            for (size_t a = 1; a < group.size(); a++) {
                const bool sa = has_success(a), sb = has_success(bi);
                if (votes[a] != votes[bi] ? votes[a] > votes[bi]
                    : sa != sb ? sa : better(pending_[group[a]].out, pending_[group[bi]].out))
                    bi = a;
            }
            size_t best = group[bi];
            // Near the gate, one chain's word is not enough: when the preamble split into
            // chains and the reported decode clears its block threshold by less than
            // CORROBORATE_DB, a third of the chains with an EOM (at least 2) must have decoded
            // the same bytes. A message with a single chain is judged by the gate alone.
            Decoded out = pending_[best].out;
            int eom_chains = 0;
            for (size_t a = 0; a < group.size(); a++)
                if (pending_[group[a]].out.ran && pending_[group[a]].out.eom) eom_chains++;
            const int need = std::max(2, (eom_chains + 2) / 3);
            if (out.r.success && group.size() > 1 &&
                out.r.block_quality_min_db < out.r.block_quality_threshold_db + CORROBORATE_DB &&
                votes[bi] < need) {
                out.r.success = false;
                char e[160];
                snprintf(e, sizeof e, "Worst block %.1f dB within %.2f dB of the gate, and %d of %d chains with EOM "
                         "decoded these bytes (%d needed); bytes not trusted",
                         out.r.block_quality_min_db, CORROBORATE_DB, votes[bi], eom_chains, need);
                out.r.error = Error(ErrorCode::RX_DECODE_FAILED, e);
            }
            if (opt_.verbose && group.size() > 1)
                fprintf(stderr, "m110a_rx: preamble at %.3f s: %zu chains, reporting the one at %.3f s\n",
                        m.first_frame * DECIM / FS, group.size(), pending_[best].first_frame * DECIM / FS);
            int hits = 0;
            for (size_t i : group) hits += pending_[i].hits;
            report(m, out, hits, static_cast<int>(group.size()), std::max(1, votes[bi]));
            // a message of another mode: its decode read a mode, not 75 bps
            const bool other = out.ran && out.r.mode != Mode::AUTO && out.r.mode != Mode::M75_SHORT &&
                               out.r.mode != Mode::M75_LONG;
            long end = 0;  // the latest decode window of the message
            if (other)
                for (size_t i : group) end = std::max(end, pending_[i].out.a1);
            const long first = m.first_frame;
            if (other) {
                corr_win_.push_back({first * DECIM, end});
                if (corr_win_.size() > 64) corr_win_.erase(corr_win_.begin());
            }
            for (auto it = group.rbegin(); it != group.rend(); ++it)
                pending_.erase(pending_.begin() + static_cast<long>(*it));
            // 75 bps preambles found starting a frame or more before that window ends are inside
            // the message just reported (a 75 bps transmission's own chain ends it first)
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (it->m75 && it->first_frame > first && it->first_frame * DECIM + M75_FRAME < end) {
                    if (opt_.verbose)
                        fprintf(stderr, "m110a_rx: 75 bps preamble at %.3f s is inside the message before it\n",
                                it->first_frame * DECIM / FS);
                    claimed_++;
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // A window not ended by a power step (at about 0 dB SNR the end is a 3 dB step, which the
    // step test can miss) runs to the next preamble, --max-secs or the end of the input. When
    // it is longer than twice FIRST_WINDOW, decode its first FIRST_WINDOW first, and keep that
    // decode when it found the EOM, read no mode, or its signal (the whole blocks the probes
    // cover) ends at least two blocks before that window does: the rest is noise. Otherwise
    // decode the whole window.
    Decoded decode(const Message& m) {
        const long a_first = m.first_frame * DECIM;
        if (!m.end_at_step && m.end_audio - a_first > 2 * FIRST_WINDOW) {
            const long a1 = a_first + FIRST_WINDOW;
            Decoded o = decode_window(m, a1);
            if (!o.ran) return o;
            const int pf = preamble_frames(o.r.mode);
            const double block_s = pf == 24 ? 4.8 : 0.6;
            const double covered = pf * 0.2 + static_cast<double>(o.r.block_quality_db.size()) * block_s;
            const bool ended = covered + 2 * block_s <= static_cast<double>(a1 - a_first) / FS;
            if (opt_.verbose)
                fprintf(stderr, "m110a_rx: msg at %.3f s: first %.0f s decoded, eom=%d, signal %.1f s: %s\n",
                        a_first / FS, FIRST_WINDOW / FS, o.eom ? 1 : 0, covered,
                        o.eom || pf == 0 || ended ? "kept" : "decoding the whole window");
            if (o.eom || pf == 0 || ended) return o;
        }
        return decode_window(m, m.end_audio);
    }

    Decoded decode_window(const Message& m, long a1) {
        Decoded o;
        const long a_first = m.first_frame * DECIM;
        const long a0 = std::max(a_first - MARGIN, audio_base_);
        o.a1 = a1;
        if (a1 - a0 < static_cast<long>(0.5 * FS)) return o;
        o.ran = true;
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
        cfg.mode = m.m75 ? m.m75_mode : mode_;
        cfg.equalizer = eq_;

        std::stringstream sink;
        std::streambuf* old_out = std::cout.rdbuf();
        std::streambuf* old_err = std::cerr.rdbuf();
        std::cout.rdbuf(opt_.verbose ? old_err : sink.rdbuf());
        if (!opt_.verbose) std::cerr.rdbuf(sink.rdbuf());
        o.r = m110a::api::decode(s, cfg);
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);

        o.d = o.r.data;
        // ModemRX cuts the data at the EOM itself and says so in eom_sequence
        long eom_bit = find_eom(o.d);
        if (eom_bit >= 0) o.d.resize(static_cast<size_t>(eom_bit / 8));
        o.eom = eom_bit >= 0 || o.r.eom_sequence;
        if (m.m75) {  // to the end of the block holding the EOM and flush, or of the last block decoded
            const m75::RxOut& x = m75::last_rx();
            const ModeId id = m.m75_mode == Mode::M75_SHORT ? ModeId::M75NS : ModeId::M75NL;
            const long S = m75::sets_per_block(id);
            long sets = x.blocks * S;
            if (x.eom_bit >= 0) sets = (x.eom_bit + 32 + m75::FLUSH_BITS + S - 1) / S * S;
            o.end75 = x.preamble ? m.m75_ds + sets * m75::SET_SYMBOLS * SPS_AUDIO : -1;
        }
        return o;
    }

    // first: the message's first preamble chain (its start); o: the decode reported.
    // hits: preamble frames seen over all its chains.
    // agreeing: chains of it that decoded these same bytes, with an EOM (1 when none had one).
    void report(const Message& first, const Decoded& o, int hits, int chains, int agreeing) {
        const DecodeResult& r = o.r;
        const std::vector<uint8_t>& d = o.d;
        if (!o.ran) return;  // no chain of it had a window of 0.5 s
        const long a_first = first.first_frame * DECIM;
        const long a1 = o.a1;
        const bool eom = o.eom;
        std::string text, hex;
        char hx[4];
        for (uint8_t c : d) {
            text += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
            snprintf(hx, sizeof hx, "%02x", c);
            hex += hx;
        }
        std::string bq;
        for (float q : r.block_quality_db) {
            snprintf(hx, sizeof hx, "%s", bq.empty() ? "" : ",");
            bq += hx;
            char qb[16];
            snprintf(qb, sizeof qb, "%.1f", q);
            bq += qb;
        }
        printf("msg=%d start_s=%.3f end_s=%.3f mode=%s preamble_hits=%d acq_metric=%.2f "
               "acq_foff_hz=%+.2f success=%d quality_db=%.1f quality_threshold_db=%.1f eom=%d "
               "bytes=%zu snr_db=%.1f d1=%d d2=%d countdown=%d blocks=%zu block_quality_db=%s "
               "block_quality_min_db=%.1f block_quality_threshold_db=%.2f preamble_chains=%d chains_agreeing=%d "
               "err=\"%s\"\n",
               ++reported_, a_first / FS, a1 / FS, mode_name(r.mode).c_str(), hits, first.metric,
               first.freq, r.success ? 1 : 0, r.quality_db, r.quality_threshold_db,
               eom ? 1 : 0, d.size(), r.snr_db, r.d1, r.d2, r.countdown,
               r.block_quality_db.size(), bq.empty() ? "-" : bq.c_str(),
               r.block_quality_min_db, r.block_quality_threshold_db, chains, agreeing,
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
    // 75 bps acquisition (m75_detect): only in AUTO and forced 75S/75L
    bool m75_forced_ = false;
    std::unique_ptr<m75::SlidingAcq> sacq_;
    long m75_next_ = 0;   // next grid point, audio index
    long m75_floor_ = 0;  // it searches from here on: the end of the last 75 bps preamble
    bool m75_cand_ = false;     // a preamble found, to be confirmed at m75_confirm_at_
    long m75_cand_ds_ = 0;
    int m75_cand_frames_ = 0;
    long m75_confirm_at_ = 0;
    std::vector<long> m75_seen_;  // sliding-search ends acquire looked at and did not take
    std::vector<std::pair<long, long>> m75_pre_;  // 75 bps preambles taken: first frame, end (audio)
    std::vector<std::pair<long, long>> corr_win_;  // other modes' messages reported: first frame, window end (audio)
    int claimed_ = 0;     // correlator chains inside a 75 bps preamble, not reported
    int reported_ = 0;
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

    // A receiver clock P ppm fast samples P ppm fast as well as tuning off (SdrFrontEnd step 5)
    SdrFrontEnd fe(o.rate, shift, o.ppm);
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
