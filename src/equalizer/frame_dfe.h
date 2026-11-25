#ifndef M110A_FRAME_DFE_H
#define M110A_FRAME_DFE_H

#include "equalizer/dfe.h"
#include "modem/scrambler_fixed.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace m110a {

/// True for the probe of the last two frames of an interleaver block.
/// MIL-STD-188-110C 5.3.2.3.7.1.2: the two known-symbol patterns before
/// each new interleaver block carry D1 and D2 (Walsh patterns) instead of
/// zeros. A transmitter that follows it (the Harris RT-1694D does) sends
/// them; this modem's TX sends zeros. These probes are left out of probe
/// training and of the quality figure, so either kind of transmitter is
/// read the same way.
inline bool block_boundary_probe(int frame, int block_frames) {
    return block_frames > 2 && (frame % block_frames) >= block_frames - 2;
}

/// Output of frame_dfe_equalize: the equalized stream (probes passed through),
/// and per frame inside the quality span its end (symbols), and the probe error
/// and reference power of the DFE's a-priori outputs on that frame's probes.
struct FrameDfeOutput {
    std::vector<complex_t> symbols;
    std::vector<std::array<double, 3>> probe_frames;
};

/**
 * Probe-aided channel equalization with the DFE, fed the data a piece at a time
 *
 * Frame structure: [unknown_len data][known_len probes], e.g. [32 data][16 probes]
 * Strategy:
 * 1. Pretrain DFE on preamble (known sequence)
 * 2. Enter the data with the delay lines holding the last preamble symbols
 * 3. Equalize each frame's data, then update on its probes
 *
 * advance() takes the data symbols received so far (from data symbol 0) and equalizes each
 * whole frame whose lookahead (AHEAD symbols past it) is in; with eof, every whole frame, the
 * lookahead past the end read as zero, and the symbols after the last whole frame passed
 * through. So it gives the same output fed a piece at a time as fed everything at once.
 */
class FrameDfe {
public:
    /**
     * @param block_frames Frames per interleaver block, 0 when unknown
     * @param preamble_symbols First preamble frame (480 symbols), for pretraining
     * @param pre_data_symbols The 32 symbols just before data symbol 0 (zero segment)
     * @param quality_span_symbols Frames ending within this many symbols are scored
     * @param data_step The mode's data points, as an 8PSK index step (4: 150-600 bps, 2: 1200,
     *        1: 2400/4800), for tracking (see TRACKING); 0, the earlier DFE: free 8PSK
     *        decisions, the slower step, and the probes passed through as received
     */
    FrameDfe(int unknown_len, int known_len, int block_frames,
             const std::vector<complex_t>& preamble_symbols = {},
             bool use_nlms = false,
             const std::vector<complex_t>& pre_data_symbols = {},
             int quality_span_symbols = 1440,
             int data_step = 0)
        : unknown_len_(unknown_len), known_len_(known_len), block_frames_(block_frames),
          have_preamble_(!preamble_symbols.empty()), pre_data_(pre_data_symbols),
          quality_span_symbols_(quality_span_symbols), data_step_(data_step),
          dfe_(config(use_nlms, data_step > 0)) {
        AHEAD = static_cast<size_t>(config(use_nlms, data_step > 0).lookahead);
        
        // ========================================
        // Preamble Pretraining
        // ========================================
        if (!preamble_symbols.empty()) {
            // Generate expected common preamble (first 288 symbols)
            // This is the most reliable part for channel estimation
            int pretrain_len = std::min(288, static_cast<int>(preamble_symbols.size()));
            
            std::vector<complex_t> preamble_ref;
            preamble_ref.reserve(pretrain_len);
            
            int scram_idx = 0;
            for (int i = 0; i < 9 && static_cast<int>(preamble_ref.size()) < pretrain_len; i++) {
                uint8_t d_val = p_c_seq()[i];
                for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                    uint8_t base = psymbol()[d_val][j % 8];
                    uint8_t scrambled = (base + pscramble()[scram_idx % 32]) % 8;
                    preamble_ref.push_back(PSK8()[scrambled]);
                    scram_idx++;
                }
            }
            
            // LMS: the least-squares taps over the common preamble, which the LMS would
            // need a 20x step to reach and would then carry that step's noise into the
            // data (on a flat channel at low SNR, 0.3-0.6 dB of decode quality).
            // NLMS: train on the preamble in chunks, two passes.
            const bool fitted = !use_nlms &&
                dfe_.fit_least_squares(preamble_symbols, preamble_ref, static_cast<int>(AHEAD), pretrain_len);
            int chunk_size = 32;
            for (int pass = 0; pass < (fitted ? 0 : 2); pass++) {
                const int first = static_cast<int>(prime_known(preamble_symbols, preamble_ref));
                for (int i = first; i < pretrain_len; i += chunk_size) {
                    const int n = std::min(chunk_size, pretrain_len - i);
                    std::vector<complex_t> rx_chunk = ahead(preamble_symbols, i, n);
                    std::vector<complex_t> ref_chunk(
                        preamble_ref.begin() + i,
                        preamble_ref.begin() + i + n);
                    
                    dfe_.train(rx_chunk, ref_chunk);
                }
            }
        }
    }

    /**
     * Equalize what `symbols` (the data from symbol 0, as received so far) allows, appending
     * to out.symbols (equalized data, probes passed through) and out.probe_frames. With eof,
     * symbols is all of it.
     */
    void advance(const std::vector<complex_t>& symbols, bool eof, FrameDfeOutput& out) {
        if (finished_) return;
        if (!entered_) {
            if (!eof && symbols.size() < AHEAD) return;
            enter(symbols);
            entered_ = true;
        }
        const int pattern_len = unknown_len_ + known_len_;
        std::vector<complex_t>& output = out.symbols;
        while (idx_ + pattern_len <= symbols.size()) {
            if (!eof && idx_ + pattern_len + AHEAD > symbols.size()) return;
            // Get current frame data and probes
            std::vector<complex_t> data_in = ahead(symbols, idx_, unknown_len_);
            
            std::vector<complex_t> probe_in(
                symbols.begin() + idx_ + unknown_len_,
                symbols.begin() + idx_ + pattern_len);
            const std::vector<complex_t> probe_eq = ahead(symbols, idx_ + unknown_len_, known_len_);
            
            // Generate probe reference using synchronized scrambler
            // (the sequence repeats every 160 symbols)
            scrambler_.reset();
            int scr_pos = frame_ * pattern_len + unknown_len_;
            for (int i = 0; i < scr_pos % 160; i++) scrambler_.next();
            
            std::vector<complex_t> probe_out;
            std::vector<complex_t> probe_ref(known_len_);
            for (int i = 0; i < known_len_; i++) {
                int scr = scrambler_.next();
                int sym_idx = (PROBE_GRAY + scr) & 7;
                probe_ref[i] = PSK8()[sym_idx];
            }
            
            // With preamble pretraining, DFE is already initialized
            // So we can equalize immediately on first frame
            if (frame_ == 0 && !have_preamble_) {
                // No preamble: train on the first probe (its feed-forward past is the
                // data before it, its feedback past unknown), then go back to the data
                // start with empty lines and run the frame through in order.
                dfe_.clear_lines();
                for (size_t k = 0; k < 2 * AHEAD; k++)
                    dfe_.push(idx_ + unknown_len_ + k >= AHEAD && idx_ + unknown_len_ + k - AHEAD < symbols.size()
                              ? symbols[idx_ + unknown_len_ + k - AHEAD] : complex_t(0.0f, 0.0f));
                dfe_.train(probe_eq, probe_ref);
                dfe_.clear_lines();
                for (size_t k = 0; k < AHEAD && k < symbols.size(); k++) dfe_.push(symbols[k]);
                equalize_data(data_in, output);
                dfe_.train(probe_eq, probe_ref);
            } else {
                // With preamble or subsequent frames: equalize first, then update.
                // A block-boundary probe may carry D1/D2 instead of the zero probe:
                // no adaptation, and each symbol decided between the two candidates,
                // so the feedback line stays a line of decisions.
                equalize_data(data_in, output);
                if (!block_boundary_probe(frame_, block_frames_))
                    {
                    // Probe error of the DFE's output before it adapts to each probe,
                    // per frame while the frame lies inside the quality span
                    const float mse = dfe_.train(probe_eq, probe_ref, &probe_out);
                    const size_t frame_end = idx_ + pattern_len;
                    if (frame_end <= static_cast<size_t>(quality_span_symbols_) && !probe_ref.empty()) {
                        double ref_pow = 0.0;
                        for (const auto& r : probe_ref) ref_pow += std::norm(r);
                        out.probe_frames.push_back({static_cast<double>(frame_end),
                                                    static_cast<double>(mse) * probe_ref.size(), ref_pow});
                    }
                }
                else
                    for (int i = 0; i < known_len_; i++) probe_out.push_back(dfe_.process_antipodal(probe_eq[i], probe_ref[i]));
            }
            
            // The probes: tracking, the DFE's own a-priori outputs on them, so they read in the
            // reference phase as the data does (the codec's 180-degree check reads them); the
            // earlier DFE passes the received probes through
            if (data_step_ > 0 && probe_out.size() == probe_in.size())
                output.insert(output.end(), probe_out.begin(), probe_out.end());
            else
                output.insert(output.end(), probe_in.begin(), probe_in.end());
            
            idx_ += pattern_len;
            frame_++;
        }
        if (!eof) return;
        // Remaining symbols - pass through
        if (idx_ < symbols.size()) {
            output.insert(output.end(), symbols.begin() + idx_, symbols.end());
        }
        finished_ = true;
    }

    /// Whole frames equalized so far
    int frames() const { return frame_; }

private:
    // 8-PSK constellation
    static const std::array<complex_t, 8>& PSK8() {
        static const std::array<complex_t, 8> v = {{
            complex_t( 1.000f,  0.000f),
            complex_t( 0.707f,  0.707f),
            complex_t( 0.000f,  1.000f),
            complex_t(-0.707f,  0.707f),
            complex_t(-1.000f,  0.000f),
            complex_t(-0.707f, -0.707f),
            complex_t( 0.000f, -1.000f),
            complex_t( 0.707f, -0.707f)
        }};
        return v;
    }
    
    // Probes are tribit 0 -> MGD3[0] = 0
    static constexpr int PROBE_GRAY = 0;
    
    // Preamble scrambling sequence
    static const std::array<uint8_t, 32>& pscramble() {
        static const std::array<uint8_t, 32> v = {
            7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
            5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
        };
        return v;
    }
    
    // Common preamble pattern (D values)
    static const std::array<uint8_t, 9>& p_c_seq() {
        static const std::array<uint8_t, 9> v = {0, 1, 3, 0, 1, 3, 1, 2, 0};
        return v;
    }
    
    // PSK symbol patterns
    static const std::array<std::array<uint8_t, 8>, 8>& psymbol() {
        static const std::array<std::array<uint8_t, 8>, 8> v = {{
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 4, 0, 4, 0, 4, 0, 4},
            {0, 0, 4, 4, 0, 0, 4, 4},
            {0, 4, 4, 0, 0, 4, 4, 0},
            {0, 0, 0, 0, 4, 4, 4, 4},
            {0, 4, 0, 4, 4, 0, 4, 0},
            {0, 0, 4, 4, 4, 4, 0, 0},
            {0, 4, 4, 0, 4, 0, 0, 4}
        }};
        return v;
    }

    static DFE::Config config(bool use_nlms, bool tracking = false) {
        // Configure DFE
        DFE::Config dfe_cfg;
        dfe_cfg.ff_taps = 11;
        dfe_cfg.fb_taps = 5;
        dfe_cfg.use_nlms = use_nlms;
        
        if (use_nlms) {
            // NLMS: mu gets normalized by input power, so use larger values
            // Typical NLMS mu range: 0.1 to 1.0
            dfe_cfg.mu_ff = 0.3f;    // Aggressive, but normalized
            dfe_cfg.mu_fb = 0.15f;   
            dfe_cfg.nlms_delta = 0.01f;  // Regularization to prevent div-by-zero
        } else {
            // Standard LMS: conservative fixed step sizes
            dfe_cfg.mu_ff = 0.005f;  
            dfe_cfg.mu_fb = 0.002f;
        }
        dfe_cfg.leak = 0.0001f;
        if (tracking && !use_nlms) { dfe_cfg.mu_ff *= TRACKING; dfe_cfg.mu_fb *= TRACKING; }

        // The feed-forward taps span the symbols -5 to +5 around the one decided, so
        // they cancel precursor ISI as well as postcursor: the DFE is fed each input
        // AHEAD symbols early, and its lookahead puts the unity center tap on the
        // symbol decided. An FT-897D SSB filter (top of the 2400-baud band cut from
        // 2300 Hz, 0.8-1.8 ms of edge delay) gave a 0.40 precursor, and 2400S decoded
        // at 6 dB with wrong bytes when the taps saw no later symbol.
        dfe_cfg.lookahead = dfe_cfg.ff_taps / 2;
        return dfe_cfg;
    }

    std::vector<complex_t> ahead(const std::vector<complex_t>& v, size_t i, size_t n) const {
        std::vector<complex_t> w(n, complex_t(0.0f, 0.0f));
        for (size_t k = 0; k < n && i + AHEAD + k < v.size(); k++) w[k] = v[i + AHEAD + k];
        return w;
    }

    // Start a known stretch rx[0..] / ref[0..] with both delay lines full: the
    // inputs before rx[AHEAD] go into the feed-forward line, and the first AHEAD
    // references into the feedback line against rx[AHEAD..2*AHEAD-1]. Training
    // then starts at ref[AHEAD] (returned), with input rx[2*AHEAD].
    size_t prime_known(const std::vector<complex_t>& rx, const std::vector<complex_t>& ref) {
        dfe_.clear_lines();
        for (size_t k = 0; k < AHEAD; k++) dfe_.push(rx[k]);
        for (size_t k = 0; k < AHEAD; k++) dfe_.prime(rx[AHEAD + k], ref[k]);
        return AHEAD;
    }

    // Enter the data with the delay lines holding what precedes it: the last
    // preamble frame's zero segment, each symbol PSK8[pscramble[i]] in every mode,
    // trained on at the tracking step. Its last AHEAD feed-forward inputs are the
    // first data symbols. Without it the data starts from an empty past.
    void enter(const std::vector<complex_t>& symbols) {
        if (have_preamble_ && pre_data_.size() >= 2 * AHEAD) {
            std::vector<complex_t> rx = pre_data_;
            rx.insert(rx.end(), symbols.begin(), symbols.begin() + std::min(AHEAD, symbols.size()));
            rx.resize(pre_data_.size() + AHEAD, complex_t(0.0f, 0.0f));
            std::vector<complex_t> ref;
            for (size_t i = 0; i < pre_data_.size(); i++)
                ref.push_back(PSK8()[pscramble()[i % 32]]);
            const size_t first = prime_known(rx, ref);
            dfe_.train(std::vector<complex_t>(rx.begin() + first + AHEAD, rx.end()),
                       std::vector<complex_t>(ref.begin() + first, ref.end()));
        } else {
            dfe_.clear_lines();
            for (size_t k = 0; k < AHEAD && k < symbols.size(); k++) dfe_.push(symbols[k]);
        }
    }

    /**
     * Step-size factor of the tracking DFE over the earlier one. At the nominal symbol level
     * (0.55 RMS) the earlier LMS steps (0.005, 0.002) make an effective step of about 0.0015 at
     * unit power, too slow for HF fading: on the 2026-11-25 net's 300L burst (01:04:15Z) it
     * decided 81 % and then 66 % of the data symbols right in blocks 1 and 2, where an RLS DFE
     * decides 87 % in both. With the mode's own decisions and 6 times the step it decides 87 %.
     * 4 is the factor taken: on CCIR moderate and poor (seeds 1-5, the modes this DFE runs) it
     * decoded 88 transmissions exact where 6 decoded 89 and 3 decoded 86, none with wrong bytes,
     * and on the AWGN set it gave up 1 exact decode where 6 gave up 6 (its step noise lowers a
     * flat channel's quality figure) (docs/m110a-acq-fading.md).
     */
    static constexpr float TRACKING = 4.0f;

    // Equalize one frame's data: the mode's own points (data_step_) around each symbol's
    // scrambler value, or with the earlier DFE free 8PSK decisions
    void equalize_data(const std::vector<complex_t>& data_in, std::vector<complex_t>& output) {
        if (data_step_ <= 1) { dfe_.equalize(data_in, output); return; }
        DataScramblerFixed sc;
        const int pos = (frame_ * (unknown_len_ + known_len_)) % 160;
        for (int i = 0; i < pos; i++) sc.next();
        for (size_t i = 0; i < data_in.size(); i++)
            output.push_back(dfe_.process_constrained(data_in[i], PSK8()[sc.next() & 7], data_step_));
    }

    int unknown_len_, known_len_, block_frames_;
    bool have_preamble_;
    std::vector<complex_t> pre_data_;
    int quality_span_symbols_;
    int data_step_ = 0;
    DFE dfe_;
    size_t AHEAD = 0;
    DataScramblerFixed scrambler_;
    bool entered_ = false, finished_ = false;
    size_t idx_ = 0;
    int frame_ = 0;
};

/**
 * Apply probe-aided channel equalization using the DFE (FrameDfe over all of symbols)
 *
 * @param symbols Data symbols, data and probes interleaved, from data symbol 0
 * @param block_frames Frames per interleaver block, 0 when unknown
 * @param preamble_symbols First preamble frame (480 symbols), for pretraining
 * @param pre_data_symbols The 32 symbols just before data symbol 0 (zero segment)
 * @param quality_span_symbols Frames ending within this many symbols are scored
 */
inline FrameDfeOutput frame_dfe_equalize(
    const std::vector<complex_t>& symbols,
    int unknown_len, int known_len,
    int block_frames,
    const std::vector<complex_t>& preamble_symbols = {},
    bool use_nlms = false,
    const std::vector<complex_t>& pre_data_symbols = {},
    int quality_span_symbols = 1440,
    int data_step = 0) {
    FrameDfe dfe(unknown_len, known_len, block_frames, preamble_symbols, use_nlms,
                 pre_data_symbols, quality_span_symbols, data_step);
    FrameDfeOutput result;
    result.symbols.reserve(symbols.size());
    dfe.advance(symbols, true, result);
    return result;
}

} // namespace m110a

#endif // M110A_FRAME_DFE_H
