#ifndef M110A_BRAIN_DECODER_H
#define M110A_BRAIN_DECODER_H

/**
 * Brain Modem Compatible Decoder
 * 
 * Implements verified decode algorithm for MIL-STD-188-110A signals:
 * - RRC matched filtering with fine-grained timing
 * - Preamble correlation-based synchronization
 * - D1/D2 mode detection via Walsh correlation
 * - Compatible with Brain Modem reference implementation
 * 
 * Based on verified analysis of reference WAV files:
 * - 2400 baud rate for all modes
 * - 1800 Hz carrier
 * - 0.35 RRC alpha
 * - Sample-level timing optimization
 */

#include "common/types.h"
#include <cstdio>
#include <cstdlib>
#include "common/constants.h"
#include "m110a/brain_preamble.h"
#include "modem/scrambler.h"
#include "dsp/fir_filter.h"
#include "dsp/nco.h"
#include "sync/preamble_acq.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>

namespace m110a {

/**
 * Brain Modem Decoder Configuration
 */
struct BrainDecoderConfig {
    float sample_rate = 48000.0f;
    float carrier_freq = 1800.0f;
    float baud_rate = 2400.0f;
    float rrc_alpha = 0.35f;
    int rrc_span = 6;  // symbols
    int max_search_symbols = 500;  // preamble search range
    float freq_search_range = 10.0f;  // Hz, search Â± this range for carrier
    float freq_search_step = 1.0f;    // Hz, step size for frequency search
    bool use_fft_coarse_afc = true;   // Enable two-stage AFC (FFT coarse + preamble fine)
    float coarse_search_range = 80.0f; // Hz, largest coarse (preamble) estimate accepted
    float fine_search_range = 2.5f;    // Hz, preamble fine search range around coarse estimate
    bool verbose = false;
    
    // Mode-specific frame structure (default M2400S)
    int unknown_data_len = 32;  // Data symbols per mini-frame
    int known_data_len = 16;    // Probe symbols per mini-frame
    int preamble_symbols = 1440; // Preamble length in symbols (3 frames Ã— 480)

    // Multipath acquisition (find_preamble_multipath): when the single-path correlation
    // misses its 0.70 gate, look for the common segment through a channel of up to
    // MP_LAGS symbols either side of the start and take the preamble when that channel
    // explains at least multipath_threshold of the received energy.
    bool multipath_acq = true;
    float multipath_threshold = 0.40f;
    // The caller forces the mode: take the best multipath start even under the threshold
    // (result.gate_bypassed); the caller verifies it by D1/D2 and the data probes.
    bool forced_mode = false;
};

/**
 * Brain Modem Decode Result
 */
struct BrainDecodeResult {
    bool preamble_found = false;
    float correlation = 0.0f;
    float accuracy = 0.0f;  // Hard decision accuracy on preamble
    int start_sample = 0;
    float phase_offset = 0.0f;
    // Residual carrier phase rate after the AFC, radians per symbol, from
    // refine_carrier(); symbol n after start_sample is derotated by
    // exp(j (phase_offset - phase_rate * n))
    float phase_rate = 0.0f;
    float freq_offset_hz = 0.0f;  // Detected frequency offset from nominal carrier
    
    // Preamble countdown of the frame found (frames still to come after it),
    // -1 when it could not be read; see decode_countdown()
    int countdown = -1;
    
    // Mode detection
    int d1 = -1;
    int d2 = -1;
    float d1_corr = 0.0f;
    float d2_corr = 0.0f;
    // Runner-up D value and its correlation, for D1 and D2
    int d1_second = -1;
    int d2_second = -1;
    float d1_corr2 = 0.0f;
    float d2_corr2 = 0.0f;
    // Count segment: the D value (4-7) read for each of the three digits, its
    // |correlation| and the runner-up's, normalised to 1.0 for a clean symbol;
    // filled whether or not the countdown was accepted
    int count_d[3] = {-1, -1, -1};
    float count_corr[3] = {0.0f, 0.0f, 0.0f};
    float count_corr2[3] = {0.0f, 0.0f, 0.0f};
    std::string mode_name = "UNKNOWN";

    // Found by find_preamble_multipath: the share of the received energy over the common
    // segment that the channel estimate explains (about 0.1 on noise, 1 on a clean
    // preamble through any channel within +/-MP_LAGS symbols), and that estimate, lag
    // -MP_LAGS first, from the found frame's last two common blocks. D1/D2, the countdown
    // and the carrier line are then read through it (rake combining).
    bool multipath = false;
    float mp_metric = 0.0f;
    std::vector<complex_t> mp_taps;
    // A forced mode took the best multipath start under the threshold
    bool gate_bypassed = false;

    // Extracted symbols
    std::vector<complex_t> preamble_symbols;
    std::vector<complex_t> data_symbols;
    // The zero segment of the last preamble frame: the P_ZERO_LENGTH symbols just
    // before data_symbols[0], on the same grid. Each is PSK8[pscramble[i]], whatever
    // the mode, so an equalizer can enter the data with its delay lines full.
    std::vector<complex_t> pre_data_symbols;
    
    // Decoded data
    std::vector<uint8_t> data;
};

/**
 * Downconversion and RRC matched filter as BrainDecoder applies them, fed a piece at a time.
 *
 * Input sample i is mixed with exp(-j phase_i), the phase accumulated in float and wrapped
 * past 2 pi exactly as a whole-buffer pass does, and output i is the sum over the taps of the
 * mixed samples i - half .. i + half. An output is produced once its last tap's input has
 * arrived; with eof the remaining outputs are produced with the taps past the end left out,
 * as a whole-buffer pass leaves them out. So the outputs are the same, value for value,
 * however the input is divided.
 */
class BasebandFilter {
public:
    BasebandFilter(const std::vector<float>& taps, float phase_inc)
        : taps_(taps), half_(static_cast<long>(taps.size() / 2)), inc_(phase_inc) {}

    void push(const float* x, size_t n) {
        for (size_t i = 0; i < n; i++) {
            bb_.push_back(complex_t(x[i] * std::cos(phase_), -x[i] * std::sin(phase_)));
            phase_ += inc_;
            if (phase_ > 2*PI) phase_ -= 2*PI;
        }
        in_ += static_cast<long>(n);
    }

    /// Produce every output whose taps are all in (or, with eof, every output). Returns the
    /// number of outputs held.
    size_t run(bool eof) {
        const long nt = static_cast<long>(taps_.size());
        while (static_cast<long>(out_.size()) < in_) {
            const long i = static_cast<long>(out_.size());
            if (!eof && i + nt - half_ > in_) break;
            complex_t sum(0, 0);
            if (i - half_ >= 0 && i - half_ + nt <= in_) {
                // every tap inside the input: the same sum, in the same order, unchecked
                const complex_t* x = bb_.data() + (i - half_ - bb_base_);
                const float* h = taps_.data();
                for (long j = 0; j < nt; j++) sum += x[j] * h[j];
            } else {
                for (long j = 0; j < nt; j++) {
                    const long idx = i - half_ + j;
                    if (idx >= 0 && idx < in_) sum += bb_[static_cast<size_t>(idx - bb_base_)] * taps_[j];
                }
            }
            out_.push_back(sum);
            // the mixed input is needed from the next output's first tap on
            const long keep = static_cast<long>(out_.size()) - half_;
            if (keep - bb_base_ > 65536 && static_cast<size_t>(keep - bb_base_) > bb_.size() / 2) {
                bb_.erase(bb_.begin(), bb_.begin() + (keep - bb_base_));
                bb_base_ = keep;
            }
        }
        return out_.size();
    }

    const std::vector<complex_t>& out() const { return out_; }
    std::vector<complex_t>& out() { return out_; }
    long input_samples() const { return in_; }

private:
    std::vector<float> taps_;
    long half_;
    float inc_;
    float phase_ = 0.0f;
    std::vector<complex_t> bb_;
    long bb_base_ = 0;
    long in_ = 0;
    std::vector<complex_t> out_;
};

/**
 * Brain Modem Compatible Decoder
 */
class BrainDecoder {
public:
    explicit BrainDecoder(const BrainDecoderConfig& cfg = BrainDecoderConfig())
        : config_(cfg) {
        
        // Pre-compute samples per symbol
        sps_ = static_cast<int>(cfg.sample_rate / cfg.baud_rate);
        
        // Generate RRC filter
        rrc_taps_ = generate_srrc_taps(cfg.rrc_alpha, cfg.rrc_span, static_cast<float>(sps_));
        
        // Generate expected common preamble pattern (288 symbols)
        generate_common_pattern();
        data_scrambler_ = RefScrambler().generate_sequence();
    }
    
    /**
     * Decode RF samples
     */
    BrainDecodeResult decode(const std::vector<float>& rf_samples) {
        BrainDecodeResult result;
        
        // Step 1: Two-stage AFC (if enabled)
        float best_freq_offset = 0.0f;
        float best_preamble_corr = 0.0f;
        std::vector<complex_t> best_filtered;
        
        if (config_.use_fft_coarse_afc && config_.coarse_search_range > 0.0f) {
            // STAGE 1: coarse frequency from the common preamble (coarse_afc)
            const float coarse_freq_offset = coarse_afc(downconvert_and_filter(rf_samples));
            
            // STAGE 2: Fine preamble-based search around coarse estimate
            // Search Â±fine_search_range around the coarse estimate
            for (float freq_off : fine_offsets(coarse_freq_offset)) {
                auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                if (filtered.size() < 288 * sps_) continue;
                
                float corr = quick_preamble_correlation(filtered, freq_off);
                
                if (corr > best_preamble_corr) {
                    best_preamble_corr = corr;
                    best_freq_offset = freq_off;
                    best_filtered = std::move(filtered);
                }
            }
            
            if (best_filtered.empty()) {
                // Fine search failed, try full range fallback
                if (config_.verbose) {
                    printf("Fine AFC failed, trying full range search\n");
                }
                for (float freq_off = -config_.freq_search_range; 
                     freq_off <= config_.freq_search_range; 
                     freq_off += config_.freq_search_step) {
                    
                    auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                    if (filtered.size() < 288 * sps_) continue;
                    
                    float corr = quick_preamble_correlation(filtered, freq_off);
                    
                    if (corr > best_preamble_corr) {
                        best_preamble_corr = corr;
                        best_freq_offset = freq_off;
                        best_filtered = std::move(filtered);
                    }
                }
            }
            
            result.freq_offset_hz = best_freq_offset;
            
        } else if (config_.freq_search_range > 0.0f) {
            // Legacy single-stage preamble-only AFC
            for (float freq_off = -config_.freq_search_range; 
                 freq_off <= config_.freq_search_range; 
                 freq_off += config_.freq_search_step) {
                
                auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                if (filtered.size() < 288 * sps_) continue;
                
                float corr = quick_preamble_correlation(filtered, freq_off);
                
                if (corr > best_preamble_corr) {
                    best_preamble_corr = corr;
                    best_freq_offset = freq_off;
                    best_filtered = std::move(filtered);
                }
            }
            
            if (best_filtered.empty()) {
                return result;  // No valid frequency found
            }
            
            result.freq_offset_hz = best_freq_offset;
        } else {
            // No frequency search
            best_filtered = downconvert_and_filter(rf_samples);
            if (best_filtered.empty() || best_filtered.size() < 288 * sps_) {
                return result;  // Signal too short
            }
        }

        if (best_filtered.empty()) {
            return result;  // AFC failed
        }

        auto& filtered = best_filtered;
        // Steps 2-4: preamble timing, mode, countdown, carrier line
        if (!read_header(filtered, result)) {
            return result;
        }

        // Step 5: Extract data symbols
        extract_data_symbols(filtered, result);
        
        return result;
    }

    /**
     * STAGE 1 of the two-stage AFC: the coarse carrier offset from the common preamble in the
     * first max_search_symbols of initial_filtered (the input at the nominal carrier), or 0.
     * The 288-symbol common segment is correlated in nine 32-symbol blocks; block magnitudes
     * find it at any carrier offset, and the block-to-block phase step measures the offset
     * (sync/preamble_acq.h, three offset hypotheses, +/-87.5 Hz). This replaces a
     * delay-multiply estimate on the data, whose product of two 8-PSK symbols carries a
     * random multiple of 45 degrees: it averaged to noise, so decoding failed beyond about
     * +/-5 Hz. Reads initial_filtered[0, coarse_afc_need()) only.
     */
    float coarse_afc(const std::vector<complex_t>& initial_filtered) const {
        float coarse_freq_offset = 0.0f;
        PreambleCorrelator coarse(sps_, config_.baud_rate);
        if (initial_filtered.size() >= coarse.span()) {
            const size_t window = static_cast<size_t>(config_.max_search_symbols) * sps_;
            PreambleHit hit = coarse.search(initial_filtered, 0, window, 1);
            if (hit.metric >= 0.3f &&
                std::abs(hit.freq_hz) <= config_.coarse_search_range) {
                coarse_freq_offset = hit.freq_hz;
            }
            if (config_.verbose) {
                printf("Preamble coarse AFC: %.2f Hz (metric %.2f at sample %zu)\n",
                       hit.freq_hz, hit.metric, hit.index);
            }
        }
        return coarse_freq_offset;
    }

    /// Filtered samples coarse_afc() reads when the input is at least this long
    size_t coarse_afc_need() const {
        return static_cast<size_t>(config_.max_search_symbols) * sps_ +
               PreambleCorrelator(sps_, config_.baud_rate).span() - 1;
    }

    /// STAGE 2 candidates: the coarse offset +/- fine_search_range, freq_search_step apart
    std::vector<float> fine_offsets(float coarse_freq_offset) const {
        std::vector<float> v;
        float search_start = coarse_freq_offset - config_.fine_search_range;
        float search_end = coarse_freq_offset + config_.fine_search_range;
        for (float freq_off = search_start; 
             freq_off <= search_end; 
             freq_off += config_.freq_search_step) {
            v.push_back(freq_off);
        }
        return v;
    }

    /// Filtered samples quick_preamble_correlation() reads when the input is at least this long
    size_t fine_afc_need() const { return static_cast<size_t>(200 + 288) * sps_; }

    /// Filtered samples read_header() reads, at least: its preamble search window. It also
    /// reads up to data_start(): call it with that much when the result says more.
    size_t header_need() const { return static_cast<size_t>(config_.max_search_symbols + 288) * sps_ + 1; }

    /// A BasebandFilter for this decoder's carrier, offset by freq_offset_hz
    BasebandFilter baseband_filter() const {
        return BasebandFilter(rrc_taps_, 2.0f * PI * config_.carrier_freq / config_.sample_rate);
    }
    BasebandFilter baseband_filter(float freq_offset_hz) const {
        return BasebandFilter(rrc_taps_, 2.0f * PI * (config_.carrier_freq + freq_offset_hz) / config_.sample_rate);
    }

    /**
     * Steps 2-4 of decode() on the filtered input chosen by the AFC: preamble timing
     * (find_preamble), the first preamble frame's symbols, the mode from D1/D2, the
     * countdown, the carrier line over the preamble frames from the one found to the last,
     * and the preamble symbols again on that line. False when no preamble is found.
     * Reads filtered up to max(header_need(), data_start(result)).
     */
    bool read_header(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        // Step 2: Find preamble with fine-grained timing
        find_preamble(filtered, result);
        if (!result.preamble_found) {
            return false;
        }
        
        // Step 3: Extract preamble symbols
        extract_preamble_symbols(filtered, result);
        
        // Step 4: Detect mode from D1/D2
        detect_mode(filtered, result);
        
        // Step 4b: Which preamble frame was found (count segment)
        decode_countdown(filtered, result);

        // Step 4c: Carrier phase and residual frequency from the common segments
        // of every preamble frame from the one found to the last; re-extract the
        // preamble symbols with that phase line
        refine_carrier(filtered, result);
        result.preamble_symbols.clear();
        extract_preamble_symbols(filtered, result);
        return true;
    }

    /**
     * Where the data starts (filtered samples): after the last preamble frame. find_preamble()
     * may lock onto any frame (it takes the first correlation above 0.80 in its window, so a
     * weak frame 0 hands it frame 1), so count from the frame actually found: its countdown
     * says how many frames follow. Without a readable countdown, assume the found frame is
     * frame 0. Uses the configured preamble length (set by the caller from the known mode),
     * which is critical for interoperability: D1/D2 may not match the configured mode.
     */
    long data_start(const BrainDecodeResult& result) const {
        int preamble_symbols = config_.preamble_symbols;
        int frames_after = preamble_symbols / brain::P_FRAME_LENGTH;
        if (result.countdown >= 0) frames_after = result.countdown + 1;
        return result.start_sample + frames_after * brain::P_FRAME_LENGTH * sps_;
    }
    
    /**
     * Where to cut rf_samples so its first preamble lies inside the windows decode() searches
     * (the fine AFC tries starts in the first 200 symbols, find_preamble in the first
     * max_search_symbols). The whole buffer is scanned a quarter symbol apart with
     * PreambleCorrelator (+/-87.5 Hz; about 0.17 on noise, 1 on a clean preamble); the first
     * start whose metric reaches threshold, less margin_symbols, is returned. 0 when no start
     * reaches threshold, so decode() then sees the buffer as before.
     */
    size_t first_preamble_cut(const std::vector<float>& rf_samples, float threshold = 0.45f,
                              int margin_symbols = 96) {
        const auto bb = downconvert_and_filter(rf_samples);
        size_t t = 0;
        const long cut = scan_first_preamble(bb, t, threshold, margin_symbols);
        return cut < 0 ? 0 : static_cast<size_t>(cut);
    }

    /**
     * first_preamble_cut()'s scan over the filtered input bb (downconvert_and_filter), from
     * start t on: the cut at the first start reaching threshold, or -1 when none does in bb
     * (t is then where the scan resumes once bb holds more).
     */
    long scan_first_preamble(const std::vector<complex_t>& bb, size_t& t, float threshold = 0.45f,
                             int margin_symbols = 96) const {
        PreambleCorrelator corr(sps_, config_.baud_rate);
        for (; t + corr.span() <= bb.size(); t += std::max(1, sps_ / 4))
            if (corr.evaluate(&bb[t]) >= threshold)
                return t > static_cast<size_t>(margin_symbols * sps_) ? static_cast<long>(t - margin_symbols * sps_) : 0;
        return -1;
    }

    /// The fine AFC's score of a candidate (quick_preamble_correlation)
    float fine_afc_metric(const std::vector<complex_t>& filtered, float freq_offset_hz) {
        return quick_preamble_correlation(filtered, freq_offset_hz);
    }

    /// Samples per symbol
    int sps() const { return sps_; }

    /**
     * Get expected preamble pattern (for external use)
     */
    const std::vector<uint8_t>& common_pattern() const {
        return common_pattern_;
    }

private:
    BrainDecoderConfig config_;
    int sps_;  // Samples per symbol
    std::vector<float> rrc_taps_;
    std::vector<uint8_t> common_pattern_;  // 288-symbol expected pattern
    std::vector<uint8_t> data_scrambler_;  // 160-symbol data scrambler sequence (probe values)
    
    /**
     * Generate expected common preamble pattern
     */
    void generate_common_pattern() {
        common_pattern_.clear();
        int scram_idx = 0;
        
        for (int i = 0; i < 9; i++) {
            uint8_t d_val = brain::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = brain::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                common_pattern_.push_back(scrambled);
                scram_idx++;
            }
        }
    }
    
    /**
     * Downconvert to baseband and apply RRC matched filter
     */
    std::vector<complex_t> downconvert_and_filter(const std::vector<float>& rf_samples) {
        BasebandFilter f = baseband_filter();
        f.push(rf_samples.data(), rf_samples.size());
        f.run(true);
        return std::move(f.out());
    }

    /**
     * Downconvert with frequency offset and apply RRC matched filter
     */
    std::vector<complex_t> downconvert_and_filter_with_offset(
            const std::vector<float>& rf_samples, float freq_offset_hz) {
        BasebandFilter f = baseband_filter(freq_offset_hz);
        f.push(rf_samples.data(), rf_samples.size());
        f.run(true);
        return std::move(f.out());
    }
    
    /**
     * Quick preamble correlation for frequency search
     * Returns correlation metric (higher = better frequency match)
     * Uses phase consistency across multiple segments of preamble
     * to discriminate between frequency candidates
     * 
     * @param filtered Baseband samples after downconversion with trial freq offset
     * @param trial_freq_offset_hz The trial frequency offset that was applied (Hz)
     * @return Metric value (higher = better match, strongly prefers correct frequency)
     */
    float quick_preamble_correlation(const std::vector<complex_t>& filtered, 
                                     float trial_freq_offset_hz = 0.0f) {
        // 8-PSK constellation
        static const std::array<complex_t, 8> PSK8 = {{
            complex_t( 1.000f,  0.000f),
            complex_t( 0.707f,  0.707f),
            complex_t( 0.000f,  1.000f),
            complex_t(-0.707f,  0.707f),
            complex_t(-1.000f,  0.000f),
            complex_t(-0.707f, -0.707f),
            complex_t( 0.000f, -1.000f),
            complex_t( 0.707f, -0.707f)
        }};
        
        // Generate expected preamble symbols
        const int SEGMENT_LEN = 72;  // Use 4 segments of 72 symbols each
        const int NUM_SEGMENTS = 4;
        std::vector<complex_t> expected;
        for (int i = 0; i < SEGMENT_LEN * NUM_SEGMENTS && i < (int)common_pattern_.size(); i++) {
            expected.push_back(PSK8[common_pattern_[i]]);
        }
        
        // Search for best correlation with multi-segment phase consistency
        float best_metric = 0.0f;
        int max_search = std::min((int)filtered.size() - SEGMENT_LEN * NUM_SEGMENTS * sps_, 200 * sps_);
        
        for (int start = 0; start < max_search; start += sps_ * 8) {  // Every 8 symbols
            // Compute correlation for each segment
            std::vector<complex_t> segment_corr(NUM_SEGMENTS);
            std::vector<float> segment_power(NUM_SEGMENTS, 0.0f);
            
            for (int seg = 0; seg < NUM_SEGMENTS; seg++) {
                complex_t corr(0, 0);
                float power = 0.0f;
                
                for (int i = 0; i < SEGMENT_LEN; i++) {
                    int idx = start + (seg * SEGMENT_LEN + i) * sps_;
                    if (idx < (int)filtered.size()) {
                        int pattern_idx = seg * SEGMENT_LEN + i;
                        corr += filtered[idx] * std::conj(expected[pattern_idx]);
                        power += std::norm(filtered[idx]);
                    }
                }
                
                segment_corr[seg] = corr;
                segment_power[seg] = power;
            }
            
            // Average correlation magnitude across segments
            float total_correlation = 0.0f;
            for (int seg = 0; seg < NUM_SEGMENTS; seg++) {
                total_correlation += std::abs(segment_corr[seg]) / std::sqrt(segment_power[seg] + 1e-10f);
            }
            float avg_correlation = total_correlation / NUM_SEGMENTS;
            
            // Simple metric - just use correlation
            // With two-stage AFC, we're already searching a narrow range
            // so frequency discrimination is less critical
            float metric = avg_correlation;            if (metric > best_metric) {
                best_metric = metric;
            }
        }
        
        return best_metric;
    }
    
    /**
     * Find preamble with sample-level timing optimization
     */
    void find_preamble(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        float best_corr = 0.0f;
        int best_start = 0;
        float best_phase = 0.0f;
        
        int max_search = std::min(
            static_cast<int>(filtered.size()) - 288 * sps_,
            config_.max_search_symbols * sps_
        );
        if (max_search < 0) return;
        
        // Use "first strong peak" detection to avoid false peaks from noise
        // Once we find correlation > threshold, refine locally and stop
        // Lower threshold (0.80) ensures we stop at first frame, not a slightly
        // higher peak at frame 1 (which would cause 1-frame timing error)
        const float early_stop_threshold = 0.80f;
        bool found_strong = false;
        
        // Search over sample positions
        for (int start = 0; start < max_search; start++) {
            // Compute correlation without rotation
            complex_t corr(0, 0);
            float power = 0;
            
            for (int i = 0; i < 288; i++) {
                int idx = start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                
                complex_t ref(brain::psk8_i[common_pattern_[i]], 
                              brain::psk8_q[common_pattern_[i]]);
                corr += filtered[idx] * std::conj(ref);
                power += std::norm(filtered[idx]);
            }
            
            float c = std::abs(corr) / std::sqrt(power * 288 + 0.0001f);
            if (c > best_corr) {
                best_corr = c;
                best_start = start;
                // Extract phase directly from correlation using atan2
                // This gives continuous phase estimate robust to quantization
                best_phase = -std::atan2(corr.imag(), corr.real());
                
                // Early termination: first strong peak wins
                // This prevents later spurious noise peaks from winning
                if (c > early_stop_threshold) {
                    found_strong = true;
                    // Search a small window around this peak to find true maximum
                    int local_end = std::min(start + sps_ * 2, max_search);
                    for (int s2 = start + 1; s2 < local_end; s2++) {
                        complex_t c2(0, 0);
                        float p2 = 0;
                        for (int i = 0; i < 288; i++) {
                            int idx = s2 + i * sps_;
                            if (idx >= static_cast<int>(filtered.size())) break;
                            complex_t ref(brain::psk8_i[common_pattern_[i]], 
                                          brain::psk8_q[common_pattern_[i]]);
                            c2 += filtered[idx] * std::conj(ref);
                            p2 += std::norm(filtered[idx]);
                        }
                        float corr2 = std::abs(c2) / std::sqrt(p2 * 288 + 0.0001f);
                        if (corr2 > best_corr) {
                            best_corr = corr2;
                            best_start = s2;
                            best_phase = -std::atan2(c2.imag(), c2.real());
                        }
                    }
                    break;  // Stop searching
                }
            }
        }
        
        result.correlation = best_corr;
        result.start_sample = best_start;
        result.phase_offset = best_phase;
        result.preamble_found = (best_corr > 0.7f);
        
        // Compute hard decision accuracy
        if (result.preamble_found) {
            complex_t rot(std::cos(best_phase), std::sin(best_phase));
            int matches = 0;
            
            for (int i = 0; i < 288; i++) {
                int idx = best_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                
                complex_t sym = filtered[idx] * rot;
                float ph = std::atan2(sym.imag(), sym.real());
                int rcv = ((static_cast<int>(std::round(ph * 4.0f / PI)) + 8) % 8);
                if (rcv == common_pattern_[i]) matches++;
            }
            
            result.accuracy = 100.0f * matches / 288.0f;
        }
        if (!result.preamble_found && config_.multipath_acq) find_preamble_multipath(filtered, result);
    }

public:
    /// Lags either side of the start the multipath search and the rake read (+-1.25 ms)
    static constexpr int MP_LAGS = 3;
private:
    /// Common-segment pieces the multipath search adds up without their phase, so a
    /// channel that changes over the 120 ms segment still counts in full
    static constexpr int MP_SEG = 72;

    /**
     * The preamble through a frequency-selective channel. Under multipath the common segment
     * arrives as several delayed copies, and the single-path correlation of find_preamble
     * sees only one of them: on air, a 600L burst with 1.5 ms of spread scored 0.64 and a 300L
     * one 0.43, under the 0.70 gate, while 99 % and 66 % of their energy matched the preamble
     * through a short channel. Here each start t is scored by
     *
     *   m(t) = sum over lags k in [-MP_LAGS, MP_LAGS] and 72-symbol pieces b of
     *          |sum_i x[t + (72 b + i + k) sps] conj(c[72 b + i])|^2  /  (72 sum_i |x[t + i sps]|^2)
     *
     * the share of the received energy a channel of 2 MP_LAGS + 1 symbol-spaced taps,
     * constant over each 72-symbol piece, explains. A clean preamble scores about 1 through
     * any such channel, noise (2 MP_LAGS + 1) 4 / 288 = 0.1 on average (0.19 at most over
     * 20 s of band-limited noise, every sample a start). The start is the t with the highest
     * m: the window of lags centred on the channel's energy, so the DFE's feed-forward taps
     * (+-5) cover it. Taken when m reaches multipath_threshold, or always for a forced mode
     * (gate_bypassed; the caller checks D1/D2 and the probes). correlation is then sqrt(m).
     */
    void find_preamble_multipath(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        const long sps = sps_;
        const long n = static_cast<long>(filtered.size());
        const long max_search = std::min<long>(n - 288 * sps, static_cast<long>(config_.max_search_symbols) * sps);
        if (max_search <= 0) return;
        // E[u]: the summed piece energies with the pieces starting at u - MP_LAGS sps
        const long u0 = -MP_LAGS * sps;
        const long nu = max_search + 2 * MP_LAGS * sps;
        std::vector<double> E(static_cast<size_t>(nu), 0.0);
        for (long j = 0; j < nu; j++) {
            const long u = u0 + j;
            double s = 0.0;
            for (int b = 0; b < 288 / MP_SEG; b++) {
                complex_t c(0, 0);
                for (int i = 0; i < MP_SEG; i++) {
                    const long idx = u + static_cast<long>(b * MP_SEG + i) * sps;
                    if (idx < 0 || idx >= n) continue;
                    const uint8_t p = common_pattern_[b * MP_SEG + i];
                    c += filtered[idx] * complex_t(brain::psk8_i[p], -brain::psk8_q[p]);
                }
                s += std::norm(c);
            }
            E[static_cast<size_t>(j)] = s;
        }
        // energy over the 288 symbols from each start, by a running sum per sample phase
        double best = -1.0;
        long best_t = 0;
        for (long t = 0; t < max_search; t++) {
            double e = 0.0;
            for (int i = 0; i < 288; i++) e += std::norm(filtered[t + i * sps]);
            double m = 0.0;
            for (int k = -MP_LAGS; k <= MP_LAGS; k++) m += E[static_cast<size_t>(t + k * sps - u0)];
            m = e > 0.0 ? m / (MP_SEG * e) : 0.0;
            if (m > best) { best = m; best_t = t; }
        }
        if (best < 0.0) return;
        const bool pass = best >= config_.multipath_threshold;
        if (!pass && !config_.forced_mode) {
            result.mp_metric = static_cast<float>(best);
            return;
        }
        result.multipath = true;
        result.gate_bypassed = !pass;
        result.mp_metric = static_cast<float>(best);
        result.correlation = static_cast<float>(std::sqrt(best));
        result.start_sample = static_cast<int>(best_t);
        result.preamble_found = true;
        // channel estimate from the last two common blocks, nearest D1/D2
        result.mp_taps.assign(2 * MP_LAGS + 1, complex_t(0, 0));
        for (int k = -MP_LAGS; k <= MP_LAGS; k++) {
            complex_t c(0, 0);
            for (int i = 224; i < 288; i++) {
                const long idx = best_t + static_cast<long>(i + k) * sps;
                if (idx < 0 || idx >= n) continue;
                const uint8_t p = common_pattern_[i];
                c += filtered[idx] * complex_t(brain::psk8_i[p], -brain::psk8_q[p]);
            }
            result.mp_taps[static_cast<size_t>(k + MP_LAGS)] = c / 64.0f;
        }
        // carrier phase at the start: the rake-combined common segment
        const complex_t c = rake_corr(filtered, result, best_t, common_pattern_.data(), 288);
        result.phase_offset = -std::arg(c);
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        int matches = 0;
        for (int i = 0; i < 288; i++) {
            const long idx = best_t + static_cast<long>(i) * sps;
            complex_t sym(0, 0);
            for (int k = -MP_LAGS; k <= MP_LAGS; k++) {
                const long j = idx + static_cast<long>(k) * sps;
                if (j >= 0 && j < n) sym += std::conj(result.mp_taps[static_cast<size_t>(k + MP_LAGS)]) * filtered[j];
            }
            sym *= rot;
            const int rcv = (static_cast<int>(std::round(std::atan2(sym.imag(), sym.real()) * 4.0f / PI)) + 8) % 8;
            if (rcv == common_pattern_[i]) matches++;
        }
        result.accuracy = 100.0f * matches / 288.0f;
    }

    /**
     * Correlation of the n symbols from filtered sample first with the 8PSK pattern pat:
     * sum_i x[first + i sps] conj(ref(pat[i])), or, when the preamble was found through a
     * channel (r.multipath), the same summed over the lags with the channel estimate's
     * conjugate weights and divided by its norm (maximal-ratio combining of the paths).
     * Either way |result| / sqrt(32 sum |x|^2)-style normalisations read about 1 when clean.
     * Samples outside filtered count as 0.
     */
    complex_t rake_corr(const std::vector<complex_t>& filtered, const BrainDecodeResult& r, long first,
                        const uint8_t* pat, int n) const {
        const long size = static_cast<long>(filtered.size());
        auto at = [&](long lag) {
            complex_t c(0, 0);
            for (int i = 0; i < n; i++) {
                const long idx = first + (static_cast<long>(i) + lag) * sps_;
                if (idx < 0 || idx >= size) continue;
                c += filtered[idx] * complex_t(brain::psk8_i[pat[i]], -brain::psk8_q[pat[i]]);
            }
            return c;
        };
        if (!r.multipath || r.mp_taps.empty()) return at(0);
        complex_t c(0, 0);
        double hn = 0.0;
        for (int k = -MP_LAGS; k <= MP_LAGS; k++) {
            const complex_t h = r.mp_taps[static_cast<size_t>(k + MP_LAGS)];
            c += std::conj(h) * at(k);
            hn += std::norm(h);
        }
        return hn > 0.0 ? c / static_cast<float>(std::sqrt(hn)) : c;
    }

    /// Derotation for symbol n after the start of the frame found:
    /// exp(j (phase_offset - phase_rate * n)). phase_rate is 0 until
    /// refine_carrier() has measured it.
    complex_t derotation(const BrainDecodeResult& r, long n) const {
        const double a = r.phase_offset - r.phase_rate * static_cast<double>(n);
        return complex_t(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    }

    /**
     * Fit carrier phase against time over the preamble.
     *
     * The fine AFC search (quick_preamble_correlation) scores correlation
     * magnitude over 72-symbol segments, which hardly changes within +/-1 Hz,
     * so it can settle 0.5 Hz or more off: on a clean own-TX 2400S signal it
     * chose -0.5 Hz for 0 Hz, and the data phase then drifted about 100
     * degrees over one 0.6 s interleaver block. Here each 32-symbol block of
     * the common segment (known) of each frame from the one found to the last
     * gives a phase; a least-squares line through the unwrapped phases gives
     * the residual frequency (phase_rate, radians per symbol) and the phase at
     * the start of the frame found (phase_offset). Blocks are 32 symbols
     * apart within a frame and 224 between frames, so the unwrap holds for a
     * residual under about 5 Hz. Blocks whose correlation is under half the
     * strongest are left out. With fewer than 4 blocks nothing changes.
     */
    void refine_carrier(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        if (result.multipath) { refine_carrier_multipath(filtered, result); return; }
        const int frames = result.countdown >= 0 ? result.countdown + 1 : 1;
        struct Blk { double t; complex_t c; };
        std::vector<Blk> blk;
        float cmax = 0.0f;
        for (int f = 0; f < frames; f++) {
            for (int b = 0; b < 9; b++) {
                complex_t c(0, 0);
                bool ok = true;
                for (int i = 0; i < 32; i++) {
                    const int k = b * 32 + i;
                    const long idx = result.start_sample +
                        (static_cast<long>(f) * brain::P_FRAME_LENGTH + k) * sps_;
                    if (idx < 0 || idx >= static_cast<long>(filtered.size())) { ok = false; break; }
                    const uint8_t p = common_pattern_[k];
                    c += filtered[idx] * std::conj(complex_t(brain::psk8_i[p], brain::psk8_q[p]));
                }
                if (!ok) break;
                blk.push_back({static_cast<double>(f) * brain::P_FRAME_LENGTH + b * 32 + 15.5, c});
                cmax = std::max(cmax, std::abs(c));
            }
        }
        std::vector<Blk> use;
        for (const auto& b : blk) if (std::abs(b.c) >= 0.5f * cmax) use.push_back(b);
        if (use.size() < 4) return;
        std::vector<double> ph(use.size());
        ph[0] = std::arg(use[0].c);
        for (size_t i = 1; i < use.size(); i++) {
            double d = std::arg(use[i].c) - std::arg(use[i - 1].c);
            while (d > PI) d -= 2 * PI;
            while (d < -PI) d += 2 * PI;
            ph[i] = ph[i - 1] + d;
        }
        double st = 0, sp = 0, stt = 0, stp = 0;
        const double n = static_cast<double>(use.size());
        for (size_t i = 0; i < use.size(); i++) {
            st += use[i].t; sp += ph[i]; stt += use[i].t * use[i].t; stp += use[i].t * ph[i];
        }
        const double den = n * stt - st * st;
        if (den <= 0) return;
        const double rate = (n * stp - st * sp) / den;      // rad / symbol
        const double phi0 = (sp - rate * st) / n;            // phase at symbol 0
        result.phase_rate = static_cast<float>(rate);
        result.phase_offset = static_cast<float>(-phi0);
        result.freq_offset_hz += static_cast<float>(rate * config_.baud_rate / (2 * PI));
    }

    /**
     * refine_carrier for a preamble found through a channel (find_preamble_multipath). The
     * channel changes over a long preamble (24 frames, 4.8 s), so one estimate cannot combine
     * every frame's paths: each frame from the one found to the last gets its own estimate
     * from its common segment, its nine 32-symbol blocks are rake-combined with it, and a
     * least-squares line through their unwrapped phases gives the frame's phase slope. The
     * residual frequency (phase_rate) is the slopes' mean weighted by each frame's combined
     * energy, so a faded frame counts little. phase_offset puts the found frame's blocks,
     * combined with the found estimate (mp_taps), on that slope at symbol 0. Frames with an
     * estimate under a tenth of the strongest are left out; with none, nothing changes.
     */
    void refine_carrier_multipath(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        const int frames = result.countdown >= 0 ? result.countdown + 1 : 1;
        const long size = static_cast<long>(filtered.size());
        const int L = 2 * MP_LAGS + 1;
        struct Fr { double slope, w; };
        std::vector<Fr> fr;
        double wmax = 0.0;
        // blocks of frame f combined with taps h: (time, combined correlation)
        auto blocks = [&](int f, const std::vector<complex_t>& h, std::vector<std::pair<double, complex_t>>& out) {
            BrainDecodeResult r;
            r.multipath = true;
            r.mp_taps = h;
            out.clear();
            for (int b = 0; b < 9; b++) {
                const long first = result.start_sample + (static_cast<long>(f) * brain::P_FRAME_LENGTH + b * 32) * sps_;
                if (first + (31 + MP_LAGS) * sps_ >= size) return false;
                out.emplace_back(static_cast<double>(f) * brain::P_FRAME_LENGTH + b * 32 + 15.5,
                                 rake_corr(filtered, r, first, common_pattern_.data() + b * 32, 32));
            }
            return true;
        };
        auto line = [](const std::vector<std::pair<double, complex_t>>& v, double& slope, double& icpt) {
            std::vector<double> ph(v.size());
            ph[0] = std::arg(v[0].second);
            for (size_t i = 1; i < v.size(); i++) {
                double d = std::arg(v[i].second) - std::arg(v[i - 1].second);
                while (d > PI) d -= 2 * PI;
                while (d < -PI) d += 2 * PI;
                ph[i] = ph[i - 1] + d;
            }
            double st = 0, sp = 0, stt = 0, stp = 0;
            const double n = static_cast<double>(v.size());
            for (size_t i = 0; i < v.size(); i++) {
                st += v[i].first; sp += ph[i]; stt += v[i].first * v[i].first; stp += v[i].first * ph[i];
            }
            const double den = n * stt - st * st;
            if (den <= 0) return false;
            slope = (n * stp - st * sp) / den;
            icpt = (sp - slope * st) / n;
            return true;
        };
        std::vector<std::pair<double, complex_t>> v;
        for (int f = 0; f < frames; f++) {
            std::vector<complex_t> h(static_cast<size_t>(L));
            const long f0 = result.start_sample + static_cast<long>(f) * brain::P_FRAME_LENGTH * sps_;
            BrainDecodeResult one;  // single-lag correlations at each lag
            for (int k = -MP_LAGS; k <= MP_LAGS; k++)
                h[static_cast<size_t>(k + MP_LAGS)] =
                    rake_corr(filtered, one, f0 + static_cast<long>(k) * sps_, common_pattern_.data(), 288) / 288.0f;
            if (!blocks(f, h, v)) break;
            double w = 0.0;
            for (const auto& b : v) w += std::norm(b.second);
            double slope, icpt;
            if (!line(v, slope, icpt)) continue;
            fr.push_back({slope, w});
            wmax = std::max(wmax, w);
        }
        double sw = 0.0, ss = 0.0;
        for (const auto& f : fr) if (f.w >= 0.1 * wmax) { sw += f.w; ss += f.w * f.slope; }
        if (sw <= 0.0) return;
        const double rate = ss / sw;
        if (!blocks(0, result.mp_taps, v)) return;
        // phase of the found frame's blocks on the common slope
        complex_t acc(0, 0);
        for (const auto& b : v) {
            const double a = std::arg(b.second) - rate * b.first;
            acc += std::abs(b.second) * complex_t(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
        }
        result.phase_rate = static_cast<float>(rate);
        result.phase_offset = static_cast<float>(-std::arg(acc));
        result.freq_offset_hz += static_cast<float>(rate * config_.baud_rate / (2 * PI));
    }

    /**
     * Extract preamble symbols for further processing
     */
    void extract_preamble_symbols(const std::vector<complex_t>& filtered,
                                   BrainDecodeResult& result) {
        // Extract all 480 symbols of first preamble frame
        for (int i = 0; i < 480; i++) {
            int idx = result.start_sample + i * sps_;
            if (idx >= static_cast<int>(filtered.size())) break;
            result.preamble_symbols.push_back(filtered[idx] * derotation(result, i));
        }
    }
    
    /**
     * Detect mode from D1/D2 patterns
     */
    void detect_mode(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // Preamble frame: common (9 x 32 = 288), D1 (32), D2 (32), count (3 x 32),
        // zero (32). D1 starts at symbol 288 and D2 at 320, matching
        // BrainPreambleEncoder::encode_frame and MIL-STD-188-110A.
        int d1_start = result.start_sample + brain::P_COMMON_LENGTH * sps_;
        int d2_start = d1_start + 32 * sps_;
        
        float best_d1_corr = 0.0f;
        float best_d2_corr = 0.0f;
        float second_d1_corr = 0.0f;
        float second_d2_corr = 0.0f;
        int d1 = 0, d2 = 0;
        int d1_second = -1, d2_second = -1;

        for (int d = 0; d < 8; d++) {
            complex_t corr2_mp(0, 0);  // D2 through the channel (multipath)
            // D1 correlation
            complex_t corr1(0, 0);
            float pow1 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (brain::psymbol[d][i % 8] + brain::pscramble[i % 32]) % 8;
                int idx = d1_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(brain::psk8_i[pattern], brain::psk8_q[pattern]);
                corr1 += sym * std::conj(ref);
                pow1 += std::norm(filtered[idx]);
            }
            if (result.multipath) {
                // through the channel estimate; |.| leaves the carrier phase out
                uint8_t pat[32];
                for (int i = 0; i < 32; i++) pat[i] = (brain::psymbol[d][i % 8] + brain::pscramble[i % 32]) % 8;
                corr1 = rake_corr(filtered, result, d1_start, pat, 32);
                corr2_mp = rake_corr(filtered, result, d2_start, pat, 32);
            }
            float c1 = std::abs(corr1) / std::sqrt(pow1 * 32 + 0.0001f);
            if (c1 > best_d1_corr) {
                second_d1_corr = best_d1_corr; d1_second = d1;
                best_d1_corr = c1; d1 = d;
            } else if (c1 > second_d1_corr) {
                second_d1_corr = c1; d1_second = d;
            }
            
            // D2 correlation
            complex_t corr2(0, 0);
            float pow2 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (brain::psymbol[d][i % 8] + brain::pscramble[i % 32]) % 8;
                int idx = d2_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(brain::psk8_i[pattern], brain::psk8_q[pattern]);
                corr2 += sym * std::conj(ref);
                pow2 += std::norm(filtered[idx]);
            }
            if (result.multipath) corr2 = corr2_mp;
            float c2 = std::abs(corr2) / std::sqrt(pow2 * 32 + 0.0001f);
            if (c2 > best_d2_corr) {
                second_d2_corr = best_d2_corr; d2_second = d2;
                best_d2_corr = c2; d2 = d;
            } else if (c2 > second_d2_corr) {
                second_d2_corr = c2; d2_second = d;
            }
        }

        result.d1 = d1;
        result.d2 = d2;
        result.d1_corr = best_d1_corr;
        result.d2_corr = best_d2_corr;
        result.d1_second = d1_second;
        result.d2_second = d2_second;
        result.d1_corr2 = second_d1_corr;
        result.d2_corr2 = second_d2_corr;
        
        // Look up mode name
        result.mode_name = lookup_mode_name(d1, d2);
    }
    
    /**
     * Read the countdown from the count segment of the frame found.
     *
     * MIL-STD-188-110A preamble frame: common (288), D1 (32), D2 (32),
     * count (3 x 32, symbols 352-447), zero (32). The count segment carries
     * the number of frames still to come as three base-4 digits, each sent
     * as Walsh pattern D4..D7 (BrainPreambleEncoder::encode_frame).
     *
     * Each digit is taken non-coherently (largest |correlation| of the four
     * patterns), so carrier phase and a residual offset do not matter. The
     * countdown is kept only when every digit wins clearly and it is below
     * the configured number of preamble frames; otherwise it stays -1.
     */
    void decode_countdown(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        const int frames = config_.preamble_symbols / brain::P_FRAME_LENGTH;
        const int count_start = result.start_sample +
            (brain::P_COMMON_LENGTH + brain::P_MODE_LENGTH) * sps_;
        int countdown = 0;
        bool clear = true;
        for (int seg = 0; seg < 3; seg++) {
            float best = 0.0f, second = 0.0f;
            int best_d = -1;
            float power = 0.0f;
            for (int i = 0; i < 32; i++) {
                int idx = count_start + (seg * 32 + i) * sps_;
                if (idx < 0 || idx >= static_cast<int>(filtered.size())) return;
                power += std::norm(filtered[idx]);
            }
            for (int d = 4; d < 8; d++) {
                complex_t corr(0, 0);
                for (int i = 0; i < 32; i++) {
                    int idx = count_start + (seg * 32 + i) * sps_;
                    uint8_t pattern = (brain::psymbol[d][i % 8] + brain::pscramble[i]) % 8;
                    corr += filtered[idx] *
                            std::conj(complex_t(brain::psk8_i[pattern], brain::psk8_q[pattern]));
                }
                if (result.multipath) {
                    uint8_t pat[32];
                    for (int i = 0; i < 32; i++) pat[i] = (brain::psymbol[d][i % 8] + brain::pscramble[i]) % 8;
                    corr = rake_corr(filtered, result, count_start + seg * 32 * sps_, pat, 32);
                }
                float mag = std::abs(corr);
                if (mag > best) { second = best; best = mag; best_d = d; }
                else if (mag > second) { second = mag; }
            }
            const float norm = std::sqrt(power * 32.0f + 0.0001f);
            result.count_d[seg] = best_d;
            result.count_corr[seg] = best / norm;
            result.count_corr2[seg] = second / norm;
            if (best_d < 0 || best < 1.5f * second) clear = false;  // ambiguous digit
            else countdown = (countdown << 2) | (best_d - 4);
        }
        if (clear && countdown < frames) result.countdown = countdown;
    }
    
    /**
     * Look up mode name from D1/D2 values
     */
    std::string lookup_mode_name(int d1, int d2) {
        // MIL-STD-188-110C/D Table XI, then the Harris application-specific
        // codes (Table XI reserves 5,6 and 5,7 and does not define them)
        struct ModeEntry { int d1; int d2; const char* name; };
        static const ModeEntry modes[] = {
            {7, 5, "M75NS"},
            {5, 5, "M75NL"},
            {7, 4, "M150S"},
            {5, 4, "M150L"},
            {6, 7, "M300S"},
            {4, 7, "M300L"},
            {6, 6, "M600S"},
            {4, 6, "M600L"},
            {6, 5, "M1200S"},
            {4, 5, "M1200L"},
            {6, 4, "M2400S"},
            {4, 4, "M2400L"},
            {7, 6, "M4800S"},
            {7, 7, "M2400V"},  // 2400 digital voice
            {5, 7, "M600H"},   // Harris application-specific: 600 bps, short
            {5, 6, "M1200H"},  // Harris application-specific: 1200 bps, short
        };
        
        for (const auto& m : modes) {
            if (m.d1 == d1 && m.d2 == d2) {
                return m.name;
            }
        }
        return "UNKNOWN";
    }
    
public:
    /**
     * The data symbols, on the grid the timing steps move (see DataGrid::advance), found as
     * the filtered input arrives. advance() searches for steps as far as the input allows,
     * and extract() appends each data symbol once no later step can move it. Given the
     * input a piece at a time it finds the same steps and symbols as given all of it at
     * once (eof): a search window that reaches past the input so far waits for more, and
     * a step at frame f is taken only once the 2W frames from f are in, as the whole-buffer
     * search takes it only when they are in the buffer.
     */
    class DataGrid {
    public:
        DataGrid(const BrainDecoder& dec, const BrainDecodeResult& r, long data_start)
            : d_(dec), ds_(data_start) {
            r_.start_sample = r.start_sample;
            r_.phase_offset = r.phase_offset;
            r_.phase_rate = r.phase_rate;
            p_ = dec.config_.unknown_data_len + dec.config_.known_data_len;
            no_steps_ = dec.config_.unknown_data_len <= 0 || dec.config_.known_data_len <= 0;
        }

        long data_start() const { return ds_; }
        const std::vector<std::pair<long, long>>& steps() const { return steps_; }

        /**
         * Steps in the data's timing: (first frame, grid shift in samples), in frame order.
         * A sender or recorder whose audio skips or repeats a few milliseconds (a sound-card
         * underrun) moves every later symbol at once; on air a 300L burst moved 36.8 symbols
         * (15.3 ms) 1.2 s into its data, and the fixed grid lost its probes for the rest.
         * At frame f the grid steps when, on the current grid, the W frames before f reach
         * coherence 0.25 and the W after fall under 0.1, and a grid MIN_STEP..MAX_STEP symbols
         * away reaches 0.25 on both the 2W frames from f and its second W alone. Noise gives
         * about 1/(W K) = 0.006-0.008 per window (K = 20 or 16 probes), the largest of the 400 grids
         * searched about 0.04. Steps under MIN_STEP are left to the DFE (+-5 symbols), so an
         * echo inside the equalizer's span never moves the grid. A fade or the end of the signal
         * leaves no other grid at 0.25 and changes nothing. The step takes effect at the first
         * frame from f whose probes the new grid reads better than the old.
         *
         * filtered holds final filtered samples only; eof says no more will come.
         */
        void advance(const std::vector<complex_t>& filtered, bool eof) {
            if (done_) return;
            const long size = static_cast<long>(filtered.size());
            if (no_steps_) { done_ = true; return; }
            for (;;) {
                if (eof) {
                    const long nf = (size - ds_) / (p_ * d_.sps_);
                    if (ds_ >= size || f_ + 2 * W > nf) { done_ = true; return; }
                } else if (ds_ + f_ * p_ * d_.sps_ + wait_ > size) {
                    return;  // the last attempt at this frame needed more input
                }
                Iteration it = iterate(filtered, eof);
                if (it.need_more) { wait_ = it.need - (ds_ + f_ * p_ * d_.sps_); return; }
                // a step is taken only where the whole-buffer search runs: the 2W frames from f in
                if (!eof && !it.steps.empty() && ds_ + (f_ + 2 * W) * p_ * d_.sps_ > size) {
                    wait_ = (2 * W) * p_ * d_.sps_;
                    return;
                }
                for (const auto& st : it.steps) steps_.push_back(st);
                shift_ = it.shift;
                f_ = it.next_f;
                wait_ = 0;
            }
        }

        /// True once extract() has filled pre_data
        bool pre_data_done() const { return pre_done_; }

        /// Data frames whose grid no later step can move: all with eof (after advance(eof))
        long settled_frames() const { return done_ ? std::numeric_limits<long>::max() : f_; }

        /**
         * Append to out every data symbol from the next one on whose grid is settled and
         * whose sample is in filtered; with eof, up to the end of filtered. The first call
         * also fills pre_data: the last preamble frame's zero segment, which ends where the
         * data begins, on the grid of data symbol 0 (empty when it is not all in filtered).
         */
        void extract(const std::vector<complex_t>& filtered, bool eof, std::vector<complex_t>& out,
                     std::vector<complex_t>* pre_data = nullptr) {
            if (ended_) return;
            const long size = static_cast<long>(filtered.size());
            if (n_ == 0 && !pre_done_) {
                if (!eof && settled_frames() < 1) return;
                if (!eof && ds_ > size) return;
                long shift0 = 0;
                for (const auto& st : steps_) if (st.first * p_ <= 0) shift0 = st.second;
                std::vector<complex_t> pre;
                for (int i = brain::P_ZERO_LENGTH; i > 0; i--) {
                    const long idx = ds_ - static_cast<long>(i) * d_.sps_ + shift0;
                    if (idx < 0 || idx >= size) continue;
                    pre.push_back(filtered[idx] * d_.derotation(r_, (idx - r_.start_sample) / d_.sps_));
                }
                if (pre.size() != static_cast<size_t>(brain::P_ZERO_LENGTH)) pre.clear();
                if (pre_data) *pre_data = std::move(pre);
                pre_done_ = true;
            }
            const long settled = settled_frames();
            for (;; n_++) {
                if (!eof && n_ / p_ >= settled) return;
                while (next_step_ < steps_.size() && steps_[next_step_].first * p_ <= n_)
                    shift_ex_ = steps_[next_step_++].second;
                const long idx = ds_ + n_ * d_.sps_ + shift_ex_;
                if (idx >= size) {
                    if (eof) ended_ = true;
                    return;
                }
                out.push_back(filtered[idx] * d_.derotation(r_, (idx - r_.start_sample) / d_.sps_));
            }
        }

    private:
        static constexpr long W = 8, MIN_STEP = 8, MAX_STEP = 50, TW = 32;
        static constexpr double ON = 0.25, OFF = 0.1;

        struct Iteration {
            bool need_more = false;
            long need = 0;  // the filtered sample count that would have let it finish
            std::vector<std::pair<long, long>> steps;
            long shift = 0;
            long next_f = 0;
        };

        /// One pass of the search at frame f_, nothing changed: what it adds and where it goes
        /// next, or that it reads past the input so far (not at eof)
        Iteration iterate(const std::vector<complex_t>& filtered, bool eof) const {
            Iteration it;
            const long f = f_;
            long shift = shift_;
            const long size = static_cast<long>(filtered.size());
            auto coh = [&](long f0, long f1, long s) -> double {
                if (it.need_more) return 0.0;
                double c = d_.grid_coherence(filtered, r_, ds_, f0, f1, s);
                if (c == -2.0) {
                    if (eof) return -1.0;
                    it.need_more = true;
                    it.need = std::max(it.need, ds_ + f1 * p_ * d_.sps_ + s + 1);
                    return 0.0;
                }
                return c;
            };
            it.next_f = f + 1;
            it.shift = shift;
            if (coh(f - W, f, shift) < ON) return it;
            if (it.need_more) return it;
            // Clock drift: the sender's and recorder's sample clocks differ (36 ppm on air
            // moved a 300L burst's probes 1.5 symbols in 18 s). Every W frames, fit a parabola
            // to the last TW frames' coherence on the grid and a quarter symbol either side;
            // when its peak is a sample or more off, move the grid one sample towards it.
            if (f % W == 0) {
                const long d = d_.sps_ / 4, f0 = std::max(0L, f - TW);
                const double cm = coh(f0, f, shift - d), c0 = coh(f0, f, shift), cp = coh(f0, f, shift + d);
                if (it.need_more) return it;
                const double v = cm >= 0.0 && cp >= 0.0 && cm - 2 * c0 + cp < 0.0 ? 0.5 * d * (cm - cp) / (cm - 2 * c0 + cp) : 0.0;
                if (std::fabs(v) >= 1.0) it.steps.emplace_back(f, shift += v > 0 ? 1 : -1);
            }
            it.shift = shift;
            const double after = coh(f, f + W, shift);
            if (it.need_more) return it;
            if (after < 0.0 || after >= OFF) return it;
            long best = shift;
            double bc = ON;
            for (long s = shift - MAX_STEP * d_.sps_; s <= shift + MAX_STEP * d_.sps_; s += std::max(1, d_.sps_ / 4)) {
                if (std::labs(s - shift) < MIN_STEP * d_.sps_) continue;
                const double c = coh(f, f + 2 * W, s);
                if (it.need_more) return it;
                if (c >= bc) { bc = c; best = s; }
            }
            if (best == shift) return it;
            for (long s = best - d_.sps_ / 4; s <= best + d_.sps_ / 4; s++) {
                const double c = coh(f, f + 2 * W, s);
                if (it.need_more) return it;
                if (c > bc) { bc = c; best = s; }
            }
            const double second = coh(f + W, f + 2 * W, best);
            if (it.need_more) return it;
            if (second < ON) return it;
            long g = f;
            for (;;) {
                if (!(g + 1 < f + W)) break;
                const double cn = coh(g, g + 1, best), co = coh(g, g + 1, shift);
                if (it.need_more) return it;
                if (!(cn <= co)) break;
                g++;
            }
            const double cg = coh(g, g + W, best);
            if (it.need_more) return it;
            std::cout << "[RX] grid step at data frame " << g << " (data +"
                      << static_cast<double>(g * p_) / d_.config_.baud_rate << " s): "
                      << static_cast<double>(best - shift) / d_.sps_ << " symbols ("
                      << 1000.0 * (best - shift) / d_.config_.sample_rate << " ms), coherence "
                      << cg << " on the new grid, " << after << " on the old" << std::endl;
            it.steps.emplace_back(g, best);
            it.shift = best;
            it.next_f = g + 1;
            return it;
        }

        const BrainDecoder& d_;
        BrainDecodeResult r_;  // the carrier line only
        long ds_;
        long p_ = 0;
        bool no_steps_ = false;
        // the search
        long f_ = W;
        long shift_ = 0;
        long wait_ = 0;  // input past frame f_'s start the last attempt needed
        bool done_ = false;
        std::vector<std::pair<long, long>> steps_;
        // the extraction
        long n_ = 0;
        size_t next_step_ = 0;
        long shift_ex_ = 0;
        bool pre_done_ = false;
        bool ended_ = false;
    };

private:
    /**
     * Extract data symbols (after preamble)
     */
    void extract_data_symbols(const std::vector<complex_t>& filtered,
                               BrainDecodeResult& result) {
        const long ds = data_start(result);
        
        // Debug output for comparing modes
        std::cout << "[RX] extract_data: preamble_symbols=" << config_.preamble_symbols
                  << " start_sample=" << result.start_sample
                  << " data_start=" << ds 
                  << " filtered.size()=" << filtered.size()
                  << " sps=" << sps_ << std::endl;
        
        // Extract data symbols, the grid moved at each step DataGrid finds
        DataGrid grid(*this, result, ds);
        grid.advance(filtered, true);
        result.pre_data_symbols.clear();
        grid.extract(filtered, true, result.data_symbols, &result.pre_data_symbols);
        
        std::cout << "[RX] extracted " << result.data_symbols.size() << " data symbols" << std::endl;
    }
    
    /**
     * Probe coherence |sum x conj(ref)|^2 / (sum |x|^2 * K) of data frames [f0, f1) read on the
     * data grid moved by shift samples: s / (1 + s) at probe SNR s, about 1/K on noise (K the
     * probes counted). -1 when a probe falls before the buffer, -2 when one falls past its end.
     */
    double grid_coherence(const std::vector<complex_t>& filtered, const BrainDecodeResult& r,
                          long data_start, long f0, long f1, long shift) const {
        const long u = config_.unknown_data_len, p = u + config_.known_data_len;
        complex_t num(0, 0);
        double pw = 0.0;
        long k = 0;
        for (long n = f0 * p; n < f1 * p; n++) {
            if (n % p < u) continue;
            const long idx = data_start + n * sps_ + shift;
            if (idx < 0) return -1.0;
            if (idx >= static_cast<long>(filtered.size())) return -2.0;
            const complex_t x = filtered[idx] * derotation(r, (idx - r.start_sample) / sps_);
            const uint8_t c = data_scrambler_[n % 160];
            num += x * std::conj(complex_t(brain::psk8_i[c], brain::psk8_q[c]));
            pw += std::norm(x);
            k++;
        }
        return pw > 0.0 ? std::norm(num) / (pw * k) : 0.0;
    }

    /**
     * Descramble and demap data symbols to soft bits
     * 
     * Process:
     * 1. Apply descrambler (complex conjugate multiply)
     * 2. Find nearest constellation point
     * 3. Apply inverse Gray code
     * 4. Generate soft decisions
     */
    std::vector<float> descramble_and_demap(const std::vector<complex_t>& symbols,
                                             int unknown_len, int known_len) {
        std::vector<float> soft_bits;
        
        // Initialize scrambler
        RefScrambler scr;
        
        int pattern_len = unknown_len + known_len;
        int sym_idx = 0;
        
        while (sym_idx + pattern_len <= static_cast<int>(symbols.size())) {
            // Process unknown (data) symbols
            for (int i = 0; i < unknown_len; i++) {
                complex_t sym = symbols[sym_idx + i];
                uint8_t scr_val = scr.next_tribit();
                
                // Descramble: rotate by -scr_val * 45Â°
                float scr_phase = -scr_val * (PI / 4.0f);
                sym *= std::polar(1.0f, scr_phase);
                
                // Soft demap to 3 bits
                auto soft = soft_demap_8psk(sym);
                soft_bits.insert(soft_bits.end(), soft.begin(), soft.end());
            }
            
            // Skip known (probe) symbols - still need to advance scrambler
            for (int i = 0; i < known_len; i++) {
                scr.next_tribit();
            }
            
            sym_idx += pattern_len;
        }
        
        return soft_bits;
    }
    
    /**
     * Soft demap 8-PSK symbol to 3 soft bits
     * Uses inverse Gray code: position â†’ tribit
     */
    std::vector<float> soft_demap_8psk(complex_t sym) {
        // Inverse Gray code mapping (position to tribit)
        static const int inv_gray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        
        // Find angle and nearest position
        float angle = std::atan2(sym.imag(), sym.real());
        int pos = static_cast<int>(std::round(angle * 4.0f / PI));
        pos = ((pos % 8) + 8) % 8;
        
        // Get tribit from inverse Gray code
        int tribit = inv_gray[pos];
        
        // Calculate soft decisions based on distance
        float mag = std::abs(sym);
        float confidence = mag * 10.0f;  // Scale factor
        
        std::vector<float> soft(3);
        soft[0] = (tribit & 4) ? confidence : -confidence;
        soft[1] = (tribit & 2) ? confidence : -confidence;
        soft[2] = (tribit & 1) ? confidence : -confidence;
        
        return soft;
    }
};

} // namespace m110a

#endif // M110A_BRAIN_DECODER_H
