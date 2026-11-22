// MIL-STD-188-110D Appendix D receiver: streaming front end (`m110d_rx --stream`).
//
// Decodes a continuous feed, a WAV on stdin with any number of transmissions in it, and hands
// each transmission to a callback as soon as it is complete. Memory is bounded by the longest
// transmission allowed (max_message_s), not by the length of the feed.
//
//   input      the WAV header is read once; the data chunk is read to EOF when its length is
//              0 or 0xFFFFFFFF (a streaming writer), else to its length. Samples are read in
//              0.1 s pieces, so a real-time feed is decoded as it arrives.
//   resampler  StreamResampler gives sample for sample what Resampler::run gives on the whole
//              feed (appd_input.h), so a rate that is not a multiple of 2400 Hz is handled as
//              in file mode.
//   search     the preamble detector (AppDRx::detect) runs on each new second of audio plus
//              0.5 s of overlap. Between transmissions only those 1.5 s are kept, plus 1.5 s
//              of history before a detection.
//   decode     from 1.5 s before the detection to the newest sample, the file decoder
//              (AppDRx::run) is re-run as audio arrives: every 0.5 s until the preamble is
//              complete, then every max(1 s, 0.2 x segment). The preamble search is confined to
//              the detection (then to the preamble the previous run read), so a later
//              transmission in the segment cannot take the lock.
//   complete   the transmission is emitted when the EOM is decoded; when the signal was lost
//              (no EOM) and 1 s of input has followed the last probe or block used; when the
//              waveform has no payload decoder (K=9) and 1 s has followed the preamble; when
//              the segment reaches max_message_s (emitted as decoded so far, note "cut"); or at
//              the end of the input. Search resumes where the emitted transmission ended.
//   unreadable a detection whose preamble gives no valid WID within 9.5 s is dropped.
//
// Peak memory: about 2 kB per second of segment per kHz of the internal rate (input, the copy
// to baseband and its mixer product), so 120 s at 48 kHz is about 230 MB.
#ifndef M110D_APPD_STREAM_H
#define M110D_APPD_STREAM_H

#include "m110d/appd_input.h"
#include "m110d/appd_rx.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace m110d {

// ---------------------------------------------------------------------------------------------
// WAV header and sample reader for a stream.
// ---------------------------------------------------------------------------------------------
struct WavStream {
    FILE* f = nullptr;
    int fs = 0, ch = 0, bits = 0, fmt = 0;
    bool bounded = false;      // data length field is a real length
    uint64_t remaining = 0;    // bytes left in the data chunk when bounded

    bool open(FILE* in, std::string& err) {
        f = in;
        uint8_t h[12];
        if (std::fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) {
            err = "not a RIFF/WAVE stream"; return false;
        }
        bool have_fmt = false;
        for (;;) {
            uint8_t c[8];
            if (std::fread(c, 1, 8, f) != 8) { err = "no data chunk"; return false; }
            uint32_t sz = le32(c + 4);
            if (!std::memcmp(c, "data", 4)) {
                if (!have_fmt) { err = "data chunk before fmt chunk"; return false; }
                bool pcm = fmt == 1 && (bits == 16 || bits == 24 || bits == 32);
                bool flt = fmt == 3 && bits == 32;
                if (!(pcm || flt) || ch < 1) { err = "only PCM 16/24/32-bit or float 32-bit supported"; return false; }
                bounded = !(sz == 0 || sz == 0xFFFFFFFFu);
                remaining = sz;
                return true;
            }
            std::vector<uint8_t> b(sz + (sz & 1));
            if (!b.empty() && std::fread(b.data(), 1, b.size(), f) != b.size()) { err = "short chunk"; return false; }
            if (!std::memcmp(c, "fmt ", 4)) {
                if (sz < 16) { err = "short fmt chunk"; return false; }
                fmt = le16(&b[0]); ch = le16(&b[2]); fs = (int)le32(&b[4]); bits = le16(&b[14]);
                if (fmt == 0xFFFE && sz >= 40) fmt = le16(&b[24]);
                have_fmt = true;
            }
        }
    }

    // Up to max_frames samples (channel 0) into out; 0 at the end of the stream.
    size_t read(std::vector<double>& out, size_t max_frames) {
        size_t bps = (size_t)bits / 8, frame = bps * (size_t)ch;
        size_t want = max_frames * frame;
        if (bounded) want = (size_t)std::min<uint64_t>(want, remaining / frame * frame);
        raw_.resize(want);
        size_t got = want ? std::fread(raw_.data(), 1, want, f) : 0;
        if (bounded) remaining -= got;
        size_t n = got / frame;
        out.resize(n);
        for (size_t i = 0; i < n; i++) {
            const uint8_t* p = &raw_[i * frame];
            double v;
            if (fmt == 3) { float x; std::memcpy(&x, p, 4); v = x; }
            else if (bits == 16) v = (int16_t)le16(p) / 32768.0;
            else if (bits == 24) v = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | (uint32_t)p[2] << 24)) / 2147483648.0;
            else v = (int32_t)le32(p) / 2147483648.0;
            out[i] = v;
        }
        // a partial frame at the end of a pipe read is dropped only at EOF (fread fills whole
        // requests until then)
        return n;
    }

private:
    std::vector<uint8_t> raw_;
};

// ---------------------------------------------------------------------------------------------
// Streaming form of Resampler::run: the same sums in the same order, over a moving window.
// ---------------------------------------------------------------------------------------------
struct StreamResampler {
    Resampler rs;
    std::vector<double> x;  // input from absolute index xb
    long long xb = 0, nin = 0, m = 0;

    void push(const double* in, size_t n, std::vector<double>& out) {
        x.insert(x.end(), in, in + n);
        nin += (long long)n;
        emit(false, out);
    }
    void finish(std::vector<double>& out) { emit(true, out); }

private:
    void emit(bool eof, std::vector<double>& out) {
        const long long L = rs.L, M = rs.M, c = (long long)rs.H * L, N = (long long)rs.h.size();
        const long long nout = eof ? (nin * L + M - 1) / M : -1;
        for (;; m++) {
            long long t = m * M + c, i0 = t / L, ph = t - i0 * L;
            if (eof ? m >= nout : i0 >= nin) break;
            double acc = 0;
            for (long long k = ph, i = i0; k < N; k += L, i--)
                if (i >= 0 && i < nin) acc += rs.h[k] * x[(size_t)(i - xb)];
            out.push_back(acc);
        }
        // keep what the next output needs: back to i0 - (N-1)/L
        long long keep = (m * M + c) / L - N / L - 1;
        if (keep > xb) {
            long long d = std::min<long long>(keep - xb, (long long)x.size());
            x.erase(x.begin(), x.begin() + d);
            xb += d;
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Segmenter and decoder.
// ---------------------------------------------------------------------------------------------
class StreamRx {
public:
    // seg0: absolute sample index of the decoded segment's first sample; note: "" or "cut".
    using Emit = std::function<void(const RxResult&, long long seg0, int fs, const std::string& note)>;

    StreamRx(RxOptions o, int fs, double max_message_s, Emit emit) : opt_(o), fs_(fs), emit_(std::move(emit)) {
        auto s = [&](double sec) { return (long long)(sec * fs_); };
        hop_ = s(1.0); overlap_ = s(0.5); hist_ = s(1.5); max_pre_ = s(8.0); guard_ = s(1.0);
        sf_ = s(0.25); max_len_ = s(max_message_s);
    }

    void push(const std::vector<double>& x) {
        buf_.insert(buf_.end(), x.begin(), x.end());
        process(false);
    }
    void finish() { process(true); }

    size_t buffered() const { return buf_.size(); }

private:
    RxOptions opt_;
    int fs_;
    Emit emit_;
    long long hop_, overlap_, hist_, max_pre_, guard_, sf_, max_len_;

    std::vector<double> buf_;  // samples from absolute index b0_
    long long b0_ = 0;
    bool decoding_ = false;
    long long scan_ = 0;   // detector has looked at everything before this
    long long floor_ = 0;  // end of the last transmission handed out; segments start at or after it
    long long t_det_ = 0, seg0_ = 0, next_try_ = 0;
    bool pre_known_ = false;
    long long pre_lo_ = 0, pre_hi_ = 0;

    long long end() const { return b0_ + (long long)buf_.size(); }

    void trim(long long from) {
        if (from <= b0_) return;
        long long d = std::min<long long>(from - b0_, (long long)buf_.size());
        buf_.erase(buf_.begin(), buf_.begin() + d);
        b0_ += d;
    }

    void process(bool eof) {
        for (;;) {
            if (!decoding_) {
                if (!(end() - scan_ >= hop_ || (eof && end() > scan_))) return;
                long long w0 = std::max({b0_, floor_, scan_ - overlap_});
                std::vector<double> win(buf_.begin() + (w0 - b0_), buf_.end());
                long t = -1;
                double m = 0;
                if ((long long)win.size() > sf_) {
                    AppDRx d(opt_);
                    m = d.detect(win, fs_, t);
                }
                if (opt_.verbose)
                    std::fprintf(stderr, "stream: search %.2f-%.2f s metric %.3f\n", (double)w0 / fs_,
                                 (double)end() / fs_, m);
                if (t >= 0) {
                    t_det_ = w0 + t;
                    seg0_ = std::max({b0_, floor_, t_det_ - hist_});
                    trim(seg0_);
                    decoding_ = true;
                    pre_known_ = false;
                    next_try_ = t_det_ + sf_ + sf_ / 4;
                    continue;
                }
                scan_ = end();
                trim(end() - overlap_ - hist_);
                continue;
            }
            if (!eof && end() < next_try_) return;
            attempt(eof);
        }
    }

    void attempt(bool eof) {
        RxOptions o = opt_;
        o.verbose = false;
        if (pre_known_) { o.acq_lo = pre_lo_ - seg0_; o.acq_hi = pre_hi_ - seg0_; }
        else { o.acq_lo = 0; o.acq_hi = t_det_ - seg0_ + max_pre_; }
        AppDRx rx(o);
        RxResult r = rx.run(buf_, fs_);  // buf_ starts at seg0_ while decoding
        const long long len = end() - seg0_;
        const bool ok = r.acquired && !r.counts.empty() && r.wid.checksum_ok;
        const bool cut = len >= max_len_;
        if (opt_.verbose)
            std::fprintf(stderr, "stream: decode %.2f-%.2f s ok %d wid %d blocks %d eom %d end %.2f s by_input %d\n",
                         (double)seg0_ / fs_, (double)end() / fs_, ok, r.wid.waveform, r.blocks_decoded, r.eom,
                         r.end_sample >= 0 ? (double)seg0_ / fs_ + r.end_sample / fs_ : -1.0, r.end_by_input);
        if (!ok) {
            if (eof || cut || end() - t_det_ > max_pre_ + guard_) {
                if (opt_.verbose) std::fprintf(stderr, "stream: detection at %.2f s dropped\n", (double)t_det_ / fs_);
                floor_ = scan_ = end();
                decoding_ = false;
                trim(end() - overlap_ - hist_);
            } else {
                next_try_ = end() + fs_ / 2;
            }
            return;
        }
        const long long ds = seg0_ + (long long)r.data_start_sample;
        const long long es = seg0_ + (long long)r.end_sample;
        if (!pre_known_ && end() > ds + fs_ / 10) {
            pre_known_ = true;
            pre_lo_ = std::max(seg0_, seg0_ + (long long)r.preamble_start_sample - fs_ / 10);
            pre_hi_ = ds;
        }
        bool done = eof || cut || r.eom;
        if (r.payload_attempted) done = done || (!r.end_by_input && end() - es >= guard_);
        else done = done || end() >= ds + guard_;
        if (!done) {
            long long gap = pre_known_ ? std::max<long long>(fs_, len / 5) : fs_ / 2;
            next_try_ = end() + gap;
            return;
        }
        emit_(r, seg0_, fs_, cut && !r.eom ? "cut" : "");
        long long stop = r.payload_attempted ? es : ds;
        if (cut && !r.eom) stop = end();
        floor_ = scan_ = std::min(std::max(stop, t_det_ + sf_), end());
        decoding_ = false;
        trim(floor_);
    }
};

}  // namespace m110d

#endif
