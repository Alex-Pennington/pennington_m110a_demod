// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file modem_rx.cpp
 * @brief ModemRX implementation using BrainDecoder + M110ACodec
 * 
 * Brain Modem compatible receiver with optional DFE equalization.
 */

#include "api/modem_rx.h"
#include "m110a/brain_decoder.h"
#include "modem/m110a_codec.h"
#include "m110a/mode_config.h"
#include "m110a/m75_rx.h"
#include "equalizer/dfe.h"
#include "equalizer/frame_dfe.h"
#include "dsp/mlse_equalizer.h"
#include "dsp/mlse_adaptive.h"
#include "dsp/turbo_equalizer.h"
#include "dsp/turbo_equalizer_v2.h"
#include "dsp/turbo_codec_integrated.h"
#include "dsp/phase_tracker.h"
#include "dsp/agc.h"
#include "modem/scrambler_fixed.h"
#include <mutex>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <array>
#include <limits>
#include <deque>
#include <memory>

namespace m110a {
namespace api {

// ============================================================
// Implementation Class
// ============================================================

class ModemRXImpl {
public:
    explicit ModemRXImpl(const RxConfig& config)
        : config_(config)
        , state_(RxState::IDLE) {
    }
    
    const RxConfig& config() const { return config_; }
    
    Result<void> set_config(const RxConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = config.validate();
        if (!result.ok()) return result;
        config_ = config;
        return Result<void>();
    }
    
    Result<void> set_mode(Mode mode) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.mode = mode;
        return Result<void>();
    }
    
    Result<void> set_equalizer(Equalizer eq) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.equalizer = eq;
        return Result<void>();
    }
    
    DecodeResult decode(const Samples& buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        return decode_locked(buffer);
    }

    DecodeResult decode_locked(const Samples& buffer) {
        state_ = RxState::SEARCHING;
        DecodeResult result;
        auto valid = config_.validate();  // the constructor takes the config unchecked
        if (!valid.ok()) {
            result.error = valid.error();
            state_ = RxState::DECODE_ERROR;
            return result;
        }

        // BrainDecoder looks for the preamble in the first 0.2 s only: start the buffer
        // just before its first preamble, wherever that is
        BrainDecoderConfig scan_cfg;
        scan_cfg.sample_rate = config_.sample_rate;
        scan_cfg.carrier_freq = config_.carrier_freq;
        const Samples samples(buffer.begin() + BrainDecoder(scan_cfg).first_preamble_cut(buffer),
                              buffer.end());

        ModeId mode_id;
        float detected_freq_offset = 0.0f;
        
        // Check if we should use known mode or auto-detect
        if (config_.mode != Mode::AUTO) {
            // Known mode - skip detection, use specified mode directly
            mode_id = api_to_internal_mode(config_.mode);
            result.mode = config_.mode;
            state_ = RxState::RECEIVING;
        } else {
            // Auto mode - perform mode detection
            // Step 1: First pass - detect mode with default settings
            BrainDecoderConfig detect_cfg;
            detect_cfg.sample_rate = config_.sample_rate;
            detect_cfg.carrier_freq = config_.carrier_freq;
            detect_cfg.baud_rate = 2400.0f;
            detect_cfg.freq_search_range = config_.freq_search_range;
            detect_cfg.freq_search_step = 0.5f;
            detect_cfg.use_fft_coarse_afc = config_.use_fft_coarse_afc;
            detect_cfg.coarse_search_range = config_.coarse_search_range;
            detect_cfg.fine_search_range = config_.fine_search_range;  // 2 Hz steps
            
            BrainDecoder detector(detect_cfg);
            auto detect_result = detector.decode(samples);
            record_preamble(detect_result, result);

            // A 75 bps preamble below this pass's reach, found by integrating whole frames
            // (src/m110a/m75_acq.h): decode it as the mode its D1/D2 name
            ModeId low_snr_mode;
            if ((detect_result.mode_name == "UNKNOWN" || detect_result.correlation < 0.5f) &&
                m75::find_low_snr(m75::lead_in(buffer, samples, config_.sample_rate), config_, low_snr_mode)) {
                detect_result.mode_name = ModeDatabase::get(low_snr_mode).name;
                detect_result.correlation = 1.0f;
            }

            // Check mode detection
            if (detect_result.mode_name == "UNKNOWN" || detect_result.correlation < 0.5f) {
                result.success = false;
                result.error = Error(ErrorCode::RX_MODE_DETECT_FAILED, 
                                    "Mode detection failed (corr=" + 
                                    std::to_string(detect_result.correlation) + ")");
                state_ = RxState::DECODE_ERROR;
                return result;
            }
            
            state_ = RxState::RECEIVING;
            
            // Convert mode name to ModeId
            mode_id = string_to_mode_id(detect_result.mode_name);
            result.mode = internal_to_api_mode(mode_id);
            detected_freq_offset = detect_result.freq_offset_hz;
        }
        
        // 75 bps: Walsh sets, no probes, its own demodulator (src/m110a/m75_rx.h)
        if (m75::is_m75(mode_id)) {
            result = m75::decode(m75::lead_in(buffer, samples, config_.sample_rate), config_, mode_id, result,
                                 config_.mode != Mode::AUTO);
            if (result.success) { stats_.bytes_received += result.data.size(); stats_.frames_received++; }
            state_ = result.success ? RxState::COMPLETE : RxState::DECODE_ERROR;
            last_result_ = result;
            return result;
        }
        
        // Step 2: Get mode-specific settings and re-decode
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        BrainDecoderConfig decode_cfg;
        decode_cfg.sample_rate = config_.sample_rate;
        decode_cfg.carrier_freq = config_.carrier_freq;
        decode_cfg.baud_rate = 2400.0f;
        decode_cfg.unknown_data_len = mode_cfg.unknown_data_len;
        decode_cfg.known_data_len = mode_cfg.known_data_len;
        decode_cfg.preamble_symbols = mode_cfg.preamble_symbols();  // Critical for LONG modes!
        decode_cfg.freq_search_range = config_.freq_search_range;
        decode_cfg.freq_search_step = 0.5f;
        decode_cfg.use_fft_coarse_afc = config_.use_fft_coarse_afc;
        decode_cfg.coarse_search_range = config_.coarse_search_range;
        decode_cfg.fine_search_range = config_.fine_search_range;
        decode_cfg.forced_mode = config_.mode != Mode::AUTO;

        BrainDecoder decoder(decode_cfg);
        auto brain_result = decoder.decode(samples);
        
        // DEBUG: Trace mode detection and symbol extraction
        std::cerr << "[RX DEBUG] Mode: " << mode_cfg.name 
                  << " preamble_frames: " << mode_cfg.preamble_frames
                  << " preamble_symbols: " << mode_cfg.preamble_symbols()
                  << " detected_mode: " << brain_result.mode_name
                  << " preamble_corr: " << brain_result.correlation
                  << " data_symbols: " << brain_result.data_symbols.size()
                  << " interleaver_rows: " << mode_cfg.interleaver.rows
                  << " interleaver_cols: " << mode_cfg.interleaver.cols
                  << " block_size: " << (mode_cfg.interleaver.rows * mode_cfg.interleaver.cols)
                  << std::endl;
        
        // Check we got data symbols
        if (brain_result.data_symbols.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_NO_SIGNAL, "No data symbols extracted");
            state_ = RxState::DECODE_ERROR;
            return result;
        }
        
        block_frames_ = interleaver_block_frames(mode_cfg);
        dfe_data_step_ = tracking_dfe_step(mode_cfg);

        // Bring symbols to the nominal level. The LMS DFE uses fixed step sizes,
        // so its convergence rate scales with input power: below about 2500 RMS
        // (int16) it never trained and the codec decoded garbage.
        normalize_symbol_level(brain_result);
        dfe_probe_frames_.clear();
        
        // Step 3: Apply phase tracking if enabled
        // Phase tracking corrects frequency offsets - only useful when no equalizer
        // (DFE/MLSE can handle small freq offsets via their adaptation)
        std::vector<complex_t> phase_corrected = brain_result.data_symbols;
        float freq_offset_hz = brain_result.freq_offset_hz;  // From frequency search
        
        // Apply phase tracking to correct frequency offset
        // - With NONE: full decision-directed tracking
        // - With MLSE and the others: probe-only tracking (less aggressive)
        // - With the tracking DFE (150-1200 bps): none. The DFE tracks the carrier itself
        //   (tracking_dfe_step()); the
        //   probe-only tracker in front of it cost the net's 300L burst of 2026-11-25 (01:04:15Z)
        //   its second block, 74 % of the data symbols right with the tracker and 86 % without.
        if (config_.phase_tracking && !(config_.equalizer == Equalizer::DFE && tracking_dfe_step(mode_cfg) > 0) &&
            mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            bool probe_only = (config_.equalizer != Equalizer::NONE);
            auto [corrected, freq_off] = apply_phase_tracking(
                brain_result.data_symbols,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                probe_only  // Probe-only when using equalizer
            );
            phase_corrected = std::move(corrected);
            freq_offset_hz = freq_off;
        }
        
        // Step 4: Apply equalizer if enabled
        std::vector<complex_t> equalized_symbols = phase_corrected;
        
        if (config_.equalizer == Equalizer::DFE && 
            mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            // Apply frame-by-frame DFE with preamble pretraining
            equalized_symbols = apply_dfe_equalization(
                phase_corrected,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                brain_result.preamble_symbols,  // Use preamble for pretraining
                config_.use_nlms,                // Use NLMS if enabled
                brain_result.pre_data_symbols    // Delay-line context at the data start
            );
        } else if ((config_.equalizer == Equalizer::MLSE_L2 || 
                    config_.equalizer == Equalizer::MLSE_L3) &&
                   mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            // Apply MLSE equalization with preamble pretraining
            int channel_memory = (config_.equalizer == Equalizer::MLSE_L2) ? 2 : 3;
            equalized_symbols = apply_mlse_equalization(
                phase_corrected,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                channel_memory,
                brain_result.preamble_symbols
            );
        } else if (config_.equalizer == Equalizer::MLSE_ADAPTIVE &&
                   mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            // Apply Adaptive MLSE with continuous tracking (100x better on fast fading)
            equalized_symbols = apply_adaptive_mlse_equalization(
                phase_corrected,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                brain_result.preamble_symbols
            );
        } else if (config_.equalizer == Equalizer::TURBO &&
                   mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            // Full turbo equalization with mode-aware SISO decoder
            // Turbo iterations improve MLSE via decoder feedback
            // Note: turbo_result.decoded_bits bypasses normal codec path
            auto turbo_result = apply_turbo_equalization_full(
                phase_corrected,
                mode_id,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                brain_result.preamble_symbols
            );
            equalized_symbols = std::move(turbo_result.symbols);
            
            // SISO decoder produces info bits directly - skip codec!
            if (!turbo_result.decoded_bits.empty()) {
                // Convert bits to bytes
                std::vector<uint8_t> turbo_decoded;
                turbo_decoded.reserve(turbo_result.decoded_bits.size() / 8);
                for (size_t i = 0; i + 7 < turbo_result.decoded_bits.size(); i += 8) {
                    uint8_t byte = 0;
                    for (int b = 0; b < 8; b++) {
                        if (turbo_result.decoded_bits[i + b]) {
                            byte |= (1 << b);  // LSB first
                        }
                    }
                    turbo_decoded.push_back(byte);
                }
                
                // Detect and strip EOM
                const long eom_bit = cut_at_eom(turbo_decoded);
                result.eom_sequence = eom_bit >= 0;
                result.eom_detected = result.eom_sequence;
                if (!result.eom_detected) {
                    result.eom_detected = detect_eom(turbo_decoded, mode_cfg.unknown_data_len,
                                                     mode_cfg.known_data_len);
                    if (result.eom_detected) turbo_decoded = strip_eom_padding(turbo_decoded);
                }
                
                result.quality_db = probe_snr_db(phase_corrected,
                                                 mode_cfg.unknown_data_len,
                                                 mode_cfg.known_data_len,
                                                 block_frames_,
                                                 quality_span(mode_cfg, eom_bit));
                result.quality_threshold_db = quality_threshold_db(mode_id);
                {
                    const size_t pat = static_cast<size_t>(mode_cfg.unknown_data_len + mode_cfg.known_data_len);
                    fill_block_quality(result, phase_corrected, mode_cfg, mode_id,
                                       eom_bit >= 0 ? message_symbols(mode_cfg, eom_bit) / pat
                                                    : phase_corrected.size() / pat,
                                       false, block_frames_, dfe_probe_frames_);
                }
                result.success = result.quality_db >= result.quality_threshold_db;
                if (!result.success) result.error = low_quality_error(result);
                if (result.success && result.block_quality_min_db < result.block_quality_threshold_db) {
                    result.success = false;
                    result.error = low_block_quality_error(result);
                }
                result.data = turbo_decoded;
                result.snr_db = estimate_snr_from_symbols(brain_result.data_symbols);
                result.freq_offset_hz = freq_offset_hz;
                
                // Update stats
                stats_.bytes_received += turbo_decoded.size();
                stats_.frames_received++;
                stats_.snr_db = result.snr_db;
                
                state_ = RxState::COMPLETE;
                last_result_ = result;
                
                return result;  // Early return - bypass normal codec
            }
        }
        
        // Step 5: Decode using M110ACodec
        M110ACodec codec(mode_id);
        auto run_codec = [&](const std::vector<complex_t>& syms) {
            // Use SNR-weighted demapper if enabled
            if (config_.use_snr_weighted_demapper) {
                float snr_db = config_.assumed_snr_db;
                if (config_.estimate_snr_from_probes) {
                    snr_db = codec.estimate_snr_from_probes(syms);
                }
                return codec.decode_with_probes(syms, DecodeOptions::snr_weighted(snr_db));
            }
            return codec.decode_with_probes(syms);  // Legacy demapper
        };
        std::vector<uint8_t> decoded = run_codec(equalized_symbols);

        if (decoded.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED, "Viterbi decode failed");
            state_ = RxState::DECODE_ERROR;
            return result;
        }
        
        // Step 6: Detect EOM (End of Message). The 110A EOM word 4B65A5B2 ends the
        // data where it is found. Without it, fall back to the older ModemTX ending
        // of 4 frames of zero data, which decode as trailing zero bytes.
        long eom_bit = cut_at_eom(decoded);
        // Without the EOM the message ends where the signal does, in whole interleaver
        // blocks: a block counts when the signal frames reach to within 4 frames of its
        // end (a partial block cannot be trusted: its erased part is spread over all of it).
        // Decode only those blocks and keep the bytes they carry.
        const size_t pattern = static_cast<size_t>(mode_cfg.unknown_data_len + mode_cfg.known_data_len);
        const size_t sig = signal_frames(phase_corrected, mode_cfg.unknown_data_len,
                                         mode_cfg.known_data_len, block_frames_, brain_result.multipath);
        const size_t bf = static_cast<size_t>(block_frames_);
        const size_t whole = bf > 0 ? (sig + 4) / bf * bf : sig;
        const size_t blocks_seen = bf > 0 ? (phase_corrected.size() / pattern + bf - 1) / bf : 0;
        if (eom_bit < 0) {
            if (whole * pattern < equalized_symbols.size()) {
                equalized_symbols.resize(whole * pattern);
                decoded = whole ? run_codec(equalized_symbols) : std::vector<uint8_t>();
            }
            decoded.resize(std::min(decoded.size(), info_bytes(mode_cfg, whole)));
            eom_bit = cut_at_eom(decoded);
        }
        const std::string other = config_.mode == Mode::AUTO ? "" : preamble_contradicts(brain_result, mode_id);
        // The DFE and the adaptive MLSE report their own a-priori probe error per frame
        const bool eq_probes = config_.equalizer == Equalizer::DFE || config_.equalizer == Equalizer::MLSE_ADAPTIVE;
        judge(result, decoded, eom_bit, mode_cfg, mode_id, block_frames_, sig, whole, blocks_seen,
              equalized_symbols, dfe_probe_frames_, eq_probes, other);
        result.snr_db = estimate_snr_from_symbols(brain_result.data_symbols);
        result.freq_offset_hz = freq_offset_hz;

        // Update stats
        if (result.success) {
            stats_.bytes_received += result.data.size();
            stats_.frames_received++;
        }
        stats_.snr_db = result.snr_db;
        
        state_ = result.success ? RxState::COMPLETE : RxState::DECODE_ERROR;
        last_result_ = result;
        
        return result;
    }
    
    DecodeResult decode_file(const std::string& filename) {
        Samples samples;
        float file_sample_rate = config_.sample_rate;
        
        std::string ext = filename.substr(filename.find_last_of('.') + 1);
        
        if (ext == "wav" || ext == "WAV") {
            std::ifstream file(filename, std::ios::binary);
            if (!file) {
                DecodeResult result;
                result.success = false;
                result.error = Error(ErrorCode::FILE_NOT_FOUND, "Cannot open: " + filename);
                return result;
            }
            
            char header[44];
            file.read(header, 44);
            file_sample_rate = *reinterpret_cast<uint32_t*>(&header[24]);
            
            std::vector<int16_t> raw;
            int16_t sample;
            while (file.read(reinterpret_cast<char*>(&sample), 2)) {
                raw.push_back(sample);
            }
            
            samples.resize(raw.size());
            for (size_t i = 0; i < raw.size(); i++) {
                samples[i] = raw[i] / 32768.0f;
            }
        } else {
            std::ifstream file(filename, std::ios::binary);
            if (!file) {
                DecodeResult result;
                result.success = false;
                result.error = Error(ErrorCode::FILE_NOT_FOUND, "Cannot open: " + filename);
                return result;
            }
            
            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0);
            
            std::vector<int16_t> raw(size / 2);
            file.read(reinterpret_cast<char*>(raw.data()), size);
            
            samples.resize(raw.size());
            for (size_t i = 0; i < raw.size(); i++) {
                samples[i] = raw[i] / 32768.0f;
            }
        }
        
        float saved_sr = config_.sample_rate;
        config_.sample_rate = file_sample_rate;
        auto result = decode(samples);
        config_.sample_rate = saved_sr;
        
        return result;
    }
    
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = RxState::SEARCHING;
        last_result_ = DecodeResult();
        st_ = std::make_unique<StreamState>();
        st_->cfg = config_;
        const RxConfig& c = config_;
        // What the stream does not follow is decoded whole at finish(): the equalizers
        // other than the DFE (and none), the AFC other than the two-stage one, and 75 bps
        // (its own demodulator, m75::decode)
        const bool forced75 = c.mode == Mode::M75_SHORT || c.mode == Mode::M75_LONG;
        if ((c.equalizer != Equalizer::DFE && c.equalizer != Equalizer::NONE) ||
            !c.use_fft_coarse_afc || c.coarse_search_range <= 0.0f || forced75)
            st_->phase = StreamState::Phase::FALLBACK;
    }
    
    Result<void> push_samples(const Samples& samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!st_ || state_ == RxState::IDLE) {
            return Error(ErrorCode::RX_NOT_STARTED);
        }
        auto valid = st_->cfg.validate();
        if (!valid.ok()) return valid;
        if (st_->phase == StreamState::Phase::DONE || st_->eof) return Result<void>();
        st_->raw.insert(st_->raw.end(), samples.begin(), samples.end());
        samples_processed_ += samples.size();
        stream_run();
        return Result<void>();
    }

    bool take_sync(StreamSync& sync) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!st_ || !st_->sync_ready || st_->sync_taken) return false;
        st_->sync_taken = true;
        sync = st_->sync;
        return true;
    }

    std::vector<BlockRecord> take_blocks() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BlockRecord> r;
        if (st_) r.swap(st_->ready);
        return r;
    }

    DecodeResult finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!st_) {
            DecodeResult r;
            r.error = Error(ErrorCode::RX_NOT_STARTED);
            return r;
        }
        StreamState& S = *st_;
        if (S.phase != StreamState::Phase::DONE) {
            auto valid = S.cfg.validate();
            if (!valid.ok()) {
                DecodeResult r;
                r.error = valid.error();
                state_ = RxState::DECODE_ERROR;
                S.phase = StreamState::Phase::DONE;
                S.result = r;
                return r;
            }
            S.eof = true;
            stream_run();
            if (S.phase == StreamState::Phase::DATA) stream_finish_data();
            if (S.phase != StreamState::Phase::DONE) stream_fallback();
        }
        return S.result;
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = RxState::IDLE;
        st_.reset();
    }
    
    RxState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    
    bool is_complete() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == RxState::COMPLETE;
    }
    
    bool has_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == RxState::DECODE_ERROR;
    }
    
    DecodeResult get_result() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_result_;
    }
    
    Mode detected_mode() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_result_.mode;
    }
    
    float snr() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_result_.snr_db;
    }
    
    float freq_offset() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_result_.freq_offset_hz;
    }
    
    ModemStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    
    void reset_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = ModemStats();
    }
    
    size_t samples_processed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_processed_;
    }
    
    static bool signal_present(const Samples& samples, float threshold_db) {
        if (samples.empty()) return false;
        
        float sum_sq = 0.0f;
        for (float s : samples) {
            sum_sq += s * s;
        }
        float rms = std::sqrt(sum_sq / samples.size());
        float db = 20.0f * std::log10(rms + 1e-10f);
        
        return db > threshold_db;
    }
    
    static float estimate_snr(const Samples& samples) {
        if (samples.size() < 1000) return 0.0f;
        
        float mean = std::accumulate(samples.begin(), samples.end(), 0.0f) / samples.size();
        
        float var = 0.0f;
        for (float s : samples) {
            float d = s - mean;
            var += d * d;
        }
        var /= samples.size();
        
        float signal_power = var;
        float noise_power = var * 0.01f;
        
        return 10.0f * std::log10(signal_power / (noise_power + 1e-10f));
    }

private:
    RxConfig config_;
    RxState state_;
    
    // Per frame of the data: the frame's end (symbols), and the probe error and
    // reference power of the DFE's a-priori outputs (frame_dfe_equalize).
    std::vector<std::array<double, 3>> dfe_probe_frames_;
    
    /// Data symbols scored for decode quality: 0.6 s at 2400 baud, one short
    /// interleaver block, the least any 110A transmission sends.
    static constexpr int QUALITY_SPAN_SYMBOLS = 1440;
    
    /// Common-preamble symbol RMS of the MS-DMT reference recordings at their
    /// native level (about 7000 RMS int16): 0.5517 on all ten. The DFE step
    /// sizes were tuned at this level, so normalising to it leaves normal-level
    /// decodes as they were and lifts quiet ones to the same operating point.
    static constexpr double NOMINAL_SYMBOL_RMS = 0.55;
    
    /// Copy what the AUTO pass read from the preamble into the result, and
    /// print it on one line to stdout (m110a_rx passes stdout through under -v):
    /// D1/D2 with best and runner-up correlation, the three count digits with
    /// theirs, and the mode they map to.
    static void record_preamble(const BrainDecodeResult& d, DecodeResult& r) {
        if (!d.preamble_found) {
            std::cout << "[RX PREAMBLE] not found corr=" << d.correlation << " multipath=" << d.mp_metric << std::endl;
            return;
        }
        r.d1 = d.d1;
        r.d2 = d.d2;
        r.countdown = d.countdown;
        int raw = 0;
        for (int i = 0; i < 3; i++) {
            if (d.count_d[i] < 4) { raw = -1; break; }
            raw = (raw << 2) | (d.count_d[i] - 4);
        }
        r.countdown_raw = raw;
        char buf[512];
        snprintf(buf, sizeof buf,
                 "[RX PREAMBLE] common_corr=%.3f acc=%.1f%% foff=%+.2f Hz "
                 "D1=%d (%.3f, 2nd D%d %.3f) D2=%d (%.3f, 2nd D%d %.3f) "
                 "count=%d,%d,%d (%.3f/%.3f %.3f/%.3f %.3f/%.3f) countdown_raw=%d countdown=%d mode=%s",
                 d.correlation, d.accuracy, d.freq_offset_hz,
                 d.d1, d.d1_corr, d.d1_second, d.d1_corr2,
                 d.d2, d.d2_corr, d.d2_second, d.d2_corr2,
                 d.count_d[0], d.count_d[1], d.count_d[2],
                 d.count_corr[0], d.count_corr2[0], d.count_corr[1], d.count_corr2[1],
                 d.count_corr[2], d.count_corr2[2], raw, d.countdown, d.mode_name.c_str());
        std::cout << buf;
        if (d.multipath)
            std::cout << " multipath=" << d.mp_metric << (d.gate_bypassed ? " (under the gate, forced mode)" : "");
        std::cout << std::endl;
    }

    /// Scale preamble and data symbols so the common preamble has the nominal RMS.
    static void normalize_symbol_level(BrainDecodeResult& r) {
        float g;
        if (!symbol_gain(r, g)) return;
        for (auto& x : r.preamble_symbols) x *= g;
        for (auto& x : r.pre_data_symbols) x *= g;
        for (auto& x : r.data_symbols) x *= g;
    }

    /// The gain normalize_symbol_level() applies; false when it applies none
    static bool symbol_gain(const BrainDecodeResult& r, float& g) {
        int n = std::min<int>(brain::P_COMMON_LENGTH, static_cast<int>(r.preamble_symbols.size()));
        if (n == 0) return false;
        double acc = 0.0;
        for (int i = 0; i < n; i++) acc += std::norm(r.preamble_symbols[i]);
        double rms = std::sqrt(acc / n);
        if (rms <= 0.0) return false;
        g = static_cast<float>(NOMINAL_SYMBOL_RMS / rms);
        return true;
    }
    
    /// Frames (data + probe patterns) per interleaver block, or 0 when unknown.
    /// Each block is rows x cols coded bits, carried unknown_len data symbols
    /// per frame at bits_per_symbol each: 30 frames at 2400 and 4800 short,
    /// 36 at 150-1200 short, 240 / 288 long.
    static int interleaver_block_frames(const ModeConfig& m) {
        const int per_frame = m.bits_per_symbol * m.unknown_data_len;
        if (per_frame <= 0 || m.known_data_len <= 0) return 0;
        const int bits = m.interleaver.rows * m.interleaver.cols;
        return bits % per_frame == 0 ? bits / per_frame : 0;
    }

    /// True for the probe of the last two frames of an interleaver block
    /// (m110a::block_boundary_probe, src/equalizer/frame_dfe.h).
    static bool block_boundary_probe(int frame, int block_frames) {
        return m110a::block_boundary_probe(frame, block_frames);
    }

    /// Frames per interleaver block of the mode being decoded (see above).
    int block_frames_ = 0;

    /// The DFE's data decisions for mode m (FrameDfe's data_step): the mode's own points around
    /// each symbol's scrambler value, as an 8PSK index step, 4 for the BPSK of 150-600 bps and 2
    /// for the QPSK of 1200 bps. With it the DFE tracks (FrameDfe::TRACKING) and returns its own
    /// outputs on the probes, and no phase tracker runs in front of it. 0 for the 8PSK modes
    /// (2400, 4800 bps): the earlier DFE. Tracking there turned 2400S/2400L CCIR transmissions
    /// with 1-byte errors into successes at worst blocks of 9-11 dB, among exact ones at 9-11.5 dB,
    /// so no threshold parts them (docs/m110a-acq-fading.md).
    static int tracking_dfe_step(const ModeConfig& m) {
        return m.bits_per_symbol >= 3 ? 0 : m.bits_per_symbol == 2 ? 2 : 4;
    }
    int dfe_data_step_ = 1;

    /// SNR in dB of the probe symbols in a data+probe stream over the quality
    /// span, after a least-squares complex gain fit to the known probe values.
    /// Probes at interleaver block boundaries are skipped (block_boundary_probe).
    static float probe_snr_db(const std::vector<complex_t>& symbols, int unknown_len, int known_len,
                              int block_frames = 0, int span_symbols = QUALITY_SPAN_SYMBOLS) {
        if (unknown_len <= 0 || known_len <= 0) return -99.0f;
        const int span = std::min<int>(span_symbols, static_cast<int>(symbols.size()));
        return probe_snr_frames_db(symbols, unknown_len, known_len, block_frames, 0,
                                   span / (unknown_len + known_len));
    }

    /// probe_snr_db over frames [f0, f1) only.
    static float probe_snr_frames_db(const std::vector<complex_t>& symbols, int unknown_len, int known_len,
                                     int block_frames, int f0, int f1) {
        if (unknown_len <= 0 || known_len <= 0) return -99.0f;
        int pattern_len = unknown_len + known_len;
        f1 = std::min<int>(f1, static_cast<int>(symbols.size()) / pattern_len);
        DataScramblerFixed scrambler;
        std::vector<complex_t> rx, ref;
        for (int f = 0; f < f1; f++) {
            for (int i = 0; i < unknown_len; i++) scrambler.next();
            if (f < f0 || block_boundary_probe(f, block_frames)) {
                for (int i = 0; i < known_len; i++) scrambler.next();
                continue;
            }
            for (int i = 0; i < known_len; i++) {
                int scr = scrambler.next();
                float ph = scr * static_cast<float>(PI) / 4.0f;
                rx.push_back(symbols[f * pattern_len + unknown_len + i]);
                ref.push_back(complex_t(std::cos(ph), std::sin(ph)));
            }
        }
        if (rx.empty()) return -99.0f;
        complex_t num(0, 0);
        double den = 0.0;
        for (size_t i = 0; i < rx.size(); i++) { num += rx[i] * std::conj(ref[i]); den += std::norm(ref[i]); }
        complex_t g = num / static_cast<float>(den);
        double err = 0.0;
        for (size_t i = 0; i < rx.size(); i++) err += std::norm(rx[i] - g * ref[i]);
        err /= rx.size();
        return static_cast<float>(10.0 * std::log10(std::norm(g) / (err + 1e-12)));
    }
    
    /// Minimum decode quality per mode, in dB. Set from 952 DFE decodes of the
    /// MS-DMT reference recordings with white noise (20 to -4 dB in 3 kHz):
    /// each sits above the highest quality_db of any decode with wrong bytes.
    ///   600: wrong max 2.27, exact min 3.08    1200S: wrong max 4.81
    ///   1200L: wrong max 4.14                  2400L: wrong max 7.56
    /// 150 and 300 never produced wrong bytes (the preamble is lost first);
    /// their exact decodes scored 2.2 dB and up, and an untrained DFE about 0.8.
    /// 2400S takes the 2400L value.
    /// 4800S is uncoded, so no threshold makes success mean bit-exact: at 17 dB in
    /// 3 kHz some decodes still carry 1-2 bit errors at 13-15 dB. 12.5 dB is set from
    /// 440 ModemTX RRC decodes (1-400 bytes, 0-18 dB) and checked on 416 more
    /// (docs/TX_CHAIN.md): every decode it admits had BER below 1e-2 (worst 9.4e-3),
    /// and clean messages of 1-600 bytes score 13.8 dB and up.
    static float quality_threshold_db(ModeId mode) {
        switch (mode) {
            case ModeId::M4800S:                      return 12.5f;
            case ModeId::M150S:  case ModeId::M150L:  return 1.5f;
            case ModeId::M300S:  case ModeId::M300L:  return 1.5f;
            case ModeId::M600S:  case ModeId::M600L:
            case ModeId::M600H:                       return 2.75f;
            case ModeId::M1200S: case ModeId::M1200H: return 5.0f;
            case ModeId::M1200L:                      return 4.5f;
            default:                                  return 7.75f;
        }
    }
    
    /// Minimum block_quality_min_db per mode, in dB (see DecodeResult and
    /// docs/m110a-quality-gate.md). Each is at most the worst block of any exact AWGN
    /// decode that clears quality_db (156 cases, the AWGN set), so no flat-channel success
    /// is lost; under that, the value with the fewest wrong-byte successes, then the fewest
    /// rejected exact decodes, on CCIR moderate and poor (5 and 10 dB, seeds 1-5). 300L is
    /// set where the net 300L bursts' blocks part as reencode_check parts them: the two that
    /// do not fit score 2.0 and 2.3 dB, the lowest that fit 2.5 dB. 4800S is uncoded and
    /// keeps its quality_db threshold.
    static float block_quality_threshold_db(ModeId mode) {
        switch (mode) {
            case ModeId::M4800S:                      return 12.5f;
            case ModeId::M150S:  case ModeId::M150L:  return 0.5f;
            case ModeId::M300S:                       return 0.5f;
            case ModeId::M300L:                       return 2.25f;
            case ModeId::M600S:  case ModeId::M600H:  return 2.75f;
            case ModeId::M600L:                       return 3.0f;
            case ModeId::M1200S: case ModeId::M1200H: return 4.75f;
            case ModeId::M1200L:                      return 4.5f;
            case ModeId::M2400L:                      return 8.25f;
            default:                                  return 8.0f;
        }
    }

    /// fill_block_quality: the worst fraction of a block's frames set aside as erasures, and
    /// the margin the rest must clear by to stand for the block (docs/m110a-quality-gate.md).
    static constexpr double BLOCK_TRIM_FRACTION = 0.25;
    static constexpr float BLOCK_TRIM_MARGIN_DB = 3.25f;

    /// Expected coded-bit error rate of a symbol at probe SNR snr (linear) in mode m, as the
    /// Viterbi decoder's input: 8PSK (Gray) 2/3 Q(sqrt(2 snr) sin(pi/8)), QPSK Q(sqrt(snr)),
    /// BPSK with r-fold repetition Q(sqrt(2 r snr)).
    static double coded_bit_error(const ModeConfig& m, double snr) {
        auto q = [](double x) { return 0.5 * std::erfc(x / std::sqrt(2.0)); };
        if (m.bits_per_symbol >= 3) return 2.0 / 3.0 * q(std::sqrt(2.0 * snr) * std::sin(PI / 8.0));
        if (m.bits_per_symbol == 2) return q(std::sqrt(snr));
        return q(std::sqrt(2.0 * snr * std::max(1, m.symbol_repetition)));
    }

    /// The SNR in dB at which coded_bit_error is pe: the flat channel with the same
    /// expected error rate. Clamped to [-20, 40] dB.
    static float flat_equivalent_db(const ModeConfig& m, double pe) {
        double lo = -20.0, hi = 40.0;
        if (pe >= coded_bit_error(m, std::pow(10.0, lo / 10.0))) return static_cast<float>(lo);
        if (pe <= coded_bit_error(m, std::pow(10.0, hi / 10.0))) return static_cast<float>(hi);
        for (int i = 0; i < 50; i++) {
            const double mid = 0.5 * (lo + hi);
            (coded_bit_error(m, std::pow(10.0, mid / 10.0)) > pe ? lo : hi) = mid;
        }
        return static_cast<float>(0.5 * (lo + hi));
    }

    /**
     * Fill block_quality_db / block_quality_min_db / block_quality_threshold_db for the
     * message's first msg_frames frames, one entry per interleaver block (one in all when
     * the block size is unknown).
     *
     * A block's quality is the flat-channel SNR with the same expected coded-bit error rate:
     * each frame's probe SNR (with the DFE its own a-priori probe error, dfe_probe_frames_;
     * otherwise probe_snr over that frame of syms) gives the error rate of that frame's
     * symbols (coded_bit_error); the block's mean of it is what the deinterleaver hands the
     * Viterbi decoder, and flat_equivalent_db puts it back in dB. On a flat channel this is
     * about quality_db. A fade inside the block counts by the errors it causes, not by its
     * share of the power.
     *
     * Or, when higher, the same over the block's best (1 - BLOCK_TRIM_FRACTION) of frames,
     * less BLOCK_TRIM_MARGIN_DB. A dropout (the signal gone for a second, the noise still
     * there) reads about 0 dB on each of its frames, but its symbols reach the decoder near
     * zero, as erasures, which cost it far less than errors: a 300L block 20 % of which was
     * a dropout decoded exact at 1.8 dB by the mean alone. A block degraded throughout has no
     * clean three quarters to fall back on (docs/m110a-quality-gate.md).
     */
    static void fill_block_quality(DecodeResult& r, const std::vector<complex_t>& syms, const ModeConfig& m,
                                   ModeId mode_id, size_t msg_frames, bool from_dfe, int block_frames,
                                   const std::vector<std::array<double, 3>>& probe_frames) {
        r.block_quality_db.clear();
        const int pattern = m.unknown_data_len + m.known_data_len;
        if (m.unknown_data_len > 0 && m.known_data_len > 0) {
            msg_frames = std::min(msg_frames, syms.size() / static_cast<size_t>(pattern));
            const size_t bf = block_frames > 0 ? static_cast<size_t>(block_frames) : std::max<size_t>(msg_frames, 1);
            const size_t nb = (msg_frames + bf - 1) / bf;
            const std::vector<double> snr = frame_snr(syms, m, msg_frames, from_dfe, block_frames, probe_frames);
            for (size_t b = 0; b < nb; b++) {
                float q;
                if (block_quality(m, snr, b * bf, std::min(msg_frames, (b + 1) * bf), q))
                    r.block_quality_db.push_back(q);
            }
        }
        r.block_quality_min_db = r.block_quality_db.empty()
            ? r.quality_db : *std::min_element(r.block_quality_db.begin(), r.block_quality_db.end());
        r.block_quality_threshold_db = block_quality_threshold_db(mode_id);
    }

    /// Probe SNR (linear) of each of the first msg_frames frames, -1 where no probe is scored:
    /// with the DFE its own a-priori probe error (probe_frames); otherwise probe_snr over that
    /// frame of syms. Block-boundary probes are not scored.
    static std::vector<double> frame_snr(const std::vector<complex_t>& syms, const ModeConfig& m,
                                         size_t msg_frames, bool from_dfe, int block_frames,
                                         const std::vector<std::array<double, 3>>& probe_frames,
                                         size_t from_frame = 0) {
        const int pattern = m.unknown_data_len + m.known_data_len;
        std::vector<double> snr(msg_frames, -1.0);  // linear, -1: no probe scored
        if (from_dfe) {
            for (const auto& f : probe_frames) {
                const size_t fi = static_cast<size_t>(f[0]) / pattern - 1;
                if (fi < msg_frames && fi >= from_frame) snr[fi] = f[1] > 0.0 ? f[2] / f[1] : 1e4;
            }
        } else {
            for (size_t fi = from_frame; fi < msg_frames; fi++) {
                if (block_boundary_probe(static_cast<int>(fi), block_frames)) continue;
                snr[fi] = std::pow(10.0, probe_snr_frames_db(syms, m.unknown_data_len, m.known_data_len,
                                                             block_frames, static_cast<int>(fi),
                                                             static_cast<int>(fi) + 1) / 10.0);
            }
        }
        return snr;
    }

    /// One block's quality (frames [f0, f1) of snr, from frame_snr), as fill_block_quality
    /// describes it; false when none of its frames is scored
    static bool block_quality(const ModeConfig& m, const std::vector<double>& snr, size_t f0, size_t f1,
                              float& q) {
        std::vector<double> pe;
        for (size_t fi = f0; fi < f1; fi++)
            if (snr[fi] >= 0.0) pe.push_back(coded_bit_error(m, snr[fi]));
        if (pe.empty()) return false;
        std::sort(pe.begin(), pe.end());
        const size_t keep = pe.size() - static_cast<size_t>(BLOCK_TRIM_FRACTION * pe.size());
        const double all = std::accumulate(pe.begin(), pe.end(), 0.0) / pe.size();
        const double best = std::accumulate(pe.begin(), pe.begin() + keep, 0.0) / keep;
        q = std::max(flat_equivalent_db(m, all), flat_equivalent_db(m, best) - BLOCK_TRIM_MARGIN_DB);
        return true;
    }

    static Error low_block_quality_error(const DecodeResult& r) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Worst interleaver block quality %.1f dB below %.2f dB for this mode; bytes not trusted",
                 r.block_quality_min_db, r.block_quality_threshold_db);
        return Error(ErrorCode::RX_DECODE_FAILED, buf);
    }

    static Error low_quality_error(const DecodeResult& r) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Decode quality %.1f dB below %.1f dB for this mode; bytes not trusted",
                 r.quality_db, r.quality_threshold_db);
        return Error(ErrorCode::RX_DECODE_FAILED, buf);
    }

    /**
     * Steps 6b-7 of decode(), shared with the streaming decode: the EOM flags (and the older
     * ModemTX zero ending, stripped from the bytes), the decode quality, every interleaver
     * block's quality, and the verdict; result.data gets the bytes.
     *
     * decoded: the bytes, cut at the EOM (eom_bit, -1 for none) or to the whole blocks the
     * signal covers (whole frames; sig the signal frames, blocks_seen the blocks begun).
     * equalized_symbols and probe_frames: what the codec read and the equalizer's per-frame
     * a-priori probe error (dfe: the DFE or the adaptive MLSE ran and reported it). other: the
     * mode a forced decode's preamble names instead, or "".
     */
    static void judge(DecodeResult& result, std::vector<uint8_t> decoded, long eom_bit,
                      const ModeConfig& mode_cfg, ModeId mode_id, int block_frames,
                      size_t sig, size_t whole, size_t blocks_seen,
                      const std::vector<complex_t>& equalized_symbols,
                      const std::vector<std::array<double, 3>>& probe_frames, bool dfe,
                      const std::string& other) {
        const size_t pattern = static_cast<size_t>(mode_cfg.unknown_data_len + mode_cfg.known_data_len);
        result.eom_sequence = eom_bit >= 0;
        result.eom_detected = result.eom_sequence;
        if (!result.eom_detected) {
            result.eom_detected = detect_eom(decoded, mode_cfg.unknown_data_len,
                                             mode_cfg.known_data_len);
            if (result.eom_detected) decoded = strip_eom_padding(decoded);
        }

        // Step 7: Decode quality. With the DFE, score its own output on the
        // known probes; otherwise score the probes in the stream the codec read.
        // Either way over the quality span, or only the message's own symbols when
        // the EOM says it is shorter (a 4800S message can be 2 frames long), or the
        // signal is (no signal after the preamble: nothing is scored, no success).
        const int span = std::min<int>(quality_span(mode_cfg, eom_bit),
                                       static_cast<int>(eom_bit < 0 ? sig * pattern : equalized_symbols.size()));
        double dfe_err = 0.0, dfe_ref = 0.0;
        for (const auto& f : probe_frames) {
            if (f[0] <= span) { dfe_err += f[1]; dfe_ref += f[2]; }
        }
        if (dfe && dfe_err > 0.0) {
            result.quality_db = static_cast<float>(10.0 * std::log10(dfe_ref / dfe_err));
        } else {
            result.quality_db = probe_snr_db(equalized_symbols,
                                             mode_cfg.unknown_data_len,
                                             mode_cfg.known_data_len,
                                             block_frames, span);
        }
        result.quality_threshold_db = quality_threshold_db(mode_id);
        // Every interleaver block the message occupies: up to the EOM's, or the whole
        // blocks the signal covers.
        const size_t msg_frames = eom_bit >= 0 ? message_symbols(mode_cfg, eom_bit) / pattern : whole;
        fill_block_quality(result, equalized_symbols, mode_cfg, mode_id, msg_frames,
                           dfe && dfe_err > 0.0, block_frames, probe_frames);
        result.success = result.quality_db >= result.quality_threshold_db;
        if (!result.success) result.error = low_quality_error(result);
        if (result.success && result.block_quality_min_db < result.block_quality_threshold_db) {
            result.success = false;
            result.error = low_block_quality_error(result);
        }
        if (result.success && eom_bit < 0 && decoded.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED,
                                 "Signal covers 0 of " + std::to_string(blocks_seen) +
                                 " interleaver blocks; no bytes reported");
        }
        if (result.success && !other.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED,
                                 "Preamble D1/D2 read " + other + ", not the forced mode; bytes not trusted");
        }
        result.data = std::move(decoded);
    }

    ModemStats stats_;
    DecodeResult last_result_;
    size_t samples_processed_ = 0;
    mutable std::mutex mutex_;
    
    static Mode internal_to_api_mode(ModeId mode) {
        switch (mode) {
            case ModeId::M75NS: return Mode::M75_SHORT;
            case ModeId::M75NL: return Mode::M75_LONG;
            case ModeId::M150S: return Mode::M150_SHORT;
            case ModeId::M150L: return Mode::M150_LONG;
            case ModeId::M300S: return Mode::M300_SHORT;
            case ModeId::M300L: return Mode::M300_LONG;
            case ModeId::M600S: return Mode::M600_SHORT;
            case ModeId::M600L: return Mode::M600_LONG;
            case ModeId::M1200S: return Mode::M1200_SHORT;
            case ModeId::M1200L: return Mode::M1200_LONG;
            case ModeId::M2400S: return Mode::M2400_SHORT;
            case ModeId::M2400L: return Mode::M2400_LONG;
            case ModeId::M4800S: return Mode::M4800_SHORT;
            case ModeId::M2400V: return Mode::M2400_VOICE;
            case ModeId::M600H: return Mode::M600_HARRIS;
            case ModeId::M1200H: return Mode::M1200_HARRIS;
            default: return Mode::AUTO;
        }
    }
    
    static ModeId api_to_internal_mode(Mode mode) {
        switch (mode) {
            case Mode::M75_SHORT: return ModeId::M75NS;
            case Mode::M75_LONG: return ModeId::M75NL;
            case Mode::M150_SHORT: return ModeId::M150S;
            case Mode::M150_LONG: return ModeId::M150L;
            case Mode::M300_SHORT: return ModeId::M300S;
            case Mode::M300_LONG: return ModeId::M300L;
            case Mode::M600_SHORT: return ModeId::M600S;
            case Mode::M600_LONG: return ModeId::M600L;
            case Mode::M1200_SHORT: return ModeId::M1200S;
            case Mode::M1200_LONG: return ModeId::M1200L;
            case Mode::M2400_SHORT: return ModeId::M2400S;
            case Mode::M2400_LONG: return ModeId::M2400L;
            case Mode::M4800_SHORT: return ModeId::M4800S;
            case Mode::M2400_VOICE: return ModeId::M2400V;
            case Mode::M600_HARRIS: return ModeId::M600H;
            case Mode::M1200_HARRIS: return ModeId::M1200H;
            default: return ModeId::M2400S;  // Fallback to M2400S
        }
    }
    
    static ModeId string_to_mode_id(const std::string& name) {
        if (name == "M75NS" || name == "M75S") return ModeId::M75NS;
        if (name == "M75NL" || name == "M75L") return ModeId::M75NL;
        if (name == "M150S") return ModeId::M150S;
        if (name == "M150L") return ModeId::M150L;
        if (name == "M300S") return ModeId::M300S;
        if (name == "M300L") return ModeId::M300L;
        if (name == "M600S") return ModeId::M600S;
        if (name == "M600L") return ModeId::M600L;
        if (name == "M1200S") return ModeId::M1200S;
        if (name == "M1200L") return ModeId::M1200L;
        if (name == "M2400S") return ModeId::M2400S;
        if (name == "M2400L") return ModeId::M2400L;
        if (name == "M4800S") return ModeId::M4800S;
        if (name == "M2400V") return ModeId::M2400V;
        if (name == "M600H") return ModeId::M600H;
        if (name == "M1200H") return ModeId::M1200H;
        return ModeId::M2400S;
    }
    
    float estimate_snr_from_symbols(const std::vector<complex_t>& symbols) {
        if (symbols.empty()) return 0.0f;
        
        float sum_mag = 0.0f;
        for (const auto& s : symbols) {
            sum_mag += std::abs(s);
        }
        float mean_mag = sum_mag / symbols.size();
        
        float sum_var = 0.0f;
        for (const auto& s : symbols) {
            float d = std::abs(s) - mean_mag;
            sum_var += d * d;
        }
        float var = sum_var / symbols.size();
        
        float snr_linear = (mean_mag * mean_mag) / (var + 1e-10f);
        return 10.0f * std::log10(snr_linear);
    }
    
    /**
     * Cut decoded data at the 110A EOM 4B65A5B2 (MIL-STD-188-110 5.3.2.3.1)
     *
     * The EOM goes to the FEC encoder most significant digit first, so in the
     * LSB-first bytes the decoder emits it reads D2 A6 A5 4D when byte aligned.
     * Searched at every bit offset, as m110a_rx does.
     *
     * @param decoded Decoded data bytes; on a hit, cut to the whole bytes before it
     * @return bit position of the EOM in the decoded stream, or -1
     */
    static long cut_at_eom(std::vector<uint8_t>& decoded) {
        static const uint8_t EOM_BYTES[4] = {0xD2, 0xA6, 0xA5, 0x4D};
        auto bit = [](const uint8_t* d, size_t i) { return (d[i / 8] >> (i % 8)) & 1; };
        const size_t nbits = decoded.size() * 8;
        for (size_t pos = 0; pos + 32 <= nbits; pos++) {
            bool ok = true;
            for (size_t i = 0; i < 32 && ok; i++) ok = bit(decoded.data(), pos + i) == bit(EOM_BYTES, i);
            if (ok) {
                decoded.resize(pos / 8);
                return static_cast<long>(pos);
            }
        }
        return -1;
    }

    /**
     * Channel symbols (data + probes) the transmitter sent for a message whose EOM
     * starts at decoded bit eom_bit: the bits up to it, the EOM, the 144 flush bits,
     * coded and repeated as the mode does, padded to the interleaver block (or, with
     * the interleaver bypassed, to the frame). Probes after that carry no signal.
     */
    static size_t message_symbols(const ModeConfig& m, long eom_bit) {
        const int per_frame = m.bits_per_symbol * m.unknown_data_len;
        if (eom_bit < 0 || per_frame <= 0) return static_cast<size_t>(QUALITY_SPAN_SYMBOLS);
        size_t bits = static_cast<size_t>(eom_bit) + 32 + 144;
        if (m.bps != 4800) bits *= 2;
        if (m.symbol_repetition > 1 && m.modulation == Modulation::BPSK) bits *= m.symbol_repetition;
        const bool bypass = m.interleaver.row_inc == 0 && m.interleaver.col_inc == 0;
        const size_t unit = bypass ? static_cast<size_t>(per_frame)
                                   : static_cast<size_t>(m.interleaver.rows * m.interleaver.cols);
        bits = (bits + unit - 1) / unit * unit;
        const size_t frames = (bits / m.bits_per_symbol + m.unknown_data_len - 1) / m.unknown_data_len;
        return frames * static_cast<size_t>(m.unknown_data_len + m.known_data_len);
    }

    /// The quality span: QUALITY_SPAN_SYMBOLS, or the message when it is shorter
    static int quality_span(const ModeConfig& m, long eom_bit) {
        return static_cast<int>(std::min<size_t>(QUALITY_SPAN_SYMBOLS, message_symbols(m, eom_bit)));
    }

    /// Data bytes that `frames` frames of mode m carry (rate 1/2 code but 4800S, BPSK repetition)
    static size_t info_bytes(const ModeConfig& m, size_t frames) {
        size_t bits = frames * static_cast<size_t>(m.unknown_data_len * m.bits_per_symbol);
        if (m.bps != 4800) bits /= 2;
        if (m.symbol_repetition > 1 && m.modulation == Modulation::BPSK) bits /= m.symbol_repetition;
        return bits / 8;
    }

    /**
     * Data frames up to the last one that carries signal. A frame's probe coherence is
     * |sum rx conj(ref)|^2 / (sum |rx|^2 * K) over its K probes: s / (1 + s) at probe SNR s,
     * about 1/K (0.05-0.06) on noise, phase and gain free. The signal ends after the last
     * frame whose coherence reaches 0.5 while the mean over it and the 7 counted frames
     * before it reaches 0.25. Searched from the end, so a fade inside the message does not
     * end it. Block-boundary probes (D1/D2 on some transmitters) are not counted; the
     * uncounted frames right after the last signal frame (a block's last two) are kept.
     */
    static size_t signal_frames(const std::vector<complex_t>& s, int unknown_len, int known_len,
                                int block_frames, bool multipath = false) {
        const int pattern_len = unknown_len + known_len;
        if (unknown_len <= 0 || known_len <= 0) return s.size() / std::max(1, pattern_len);
        const size_t nf = s.size() / pattern_len;
        std::vector<float> rho;
        frame_coherence(s, unknown_len, known_len, block_frames, rho, multipath, true);
        for (size_t f = nf; f-- > 0;) {
            if (!signal_frame(rho, f)) continue;
            while (f + 1 < nf && rho[f + 1] < 0.0f) f++;  // the block's uncounted last frames
            return f + 1;
        }
        return 0;
    }

    /// Append to rho the probe coherence of each whole frame of s past those it holds
    /// (-1 for a block-boundary probe's frame), as signal_frames() reads it. With multipath
    /// (the preamble was found through a channel), frame_coherence_multipath's figure.
    /// final: s will not grow (a multipath frame waits for the symbols after it until then).
    static void frame_coherence(const std::vector<complex_t>& s, int unknown_len, int known_len,
                                int block_frames, std::vector<float>& rho, bool multipath = false,
                                bool final = true) {
        if (multipath) {
            frame_coherence_multipath(s, unknown_len, known_len, block_frames, rho, final);
            return;
        }
        const int pattern_len = unknown_len + known_len;
        const size_t nf = s.size() / pattern_len;
        DataScramblerFixed scrambler;
        for (size_t i = 0; i < (rho.size() * pattern_len) % 160; i++) scrambler.next();
        for (size_t f = rho.size(); f < nf; f++) {
            for (int i = 0; i < unknown_len; i++) scrambler.next();
            complex_t num(0, 0);
            double pw = 0.0;
            for (int i = 0; i < known_len; i++) {
                const float ph = scrambler.next() * static_cast<float>(PI) / 4.0f;
                const complex_t x = s[f * pattern_len + unknown_len + i];
                num += x * complex_t(std::cos(ph), -std::sin(ph));
                pw += std::norm(x);
            }
            rho.push_back(-1.0f);
            if (!block_boundary_probe(static_cast<int>(f), block_frames))
                rho[f] = pw > 0.0 ? static_cast<float>(std::norm(num) / (pw * known_len)) : 0.0f;
        }
    }

    /// Lags either side of a symbol that frame_coherence_multipath combines (BrainDecoder's)
    static constexpr int MP_LAGS = BrainDecoder::MP_LAGS;

    /**
     * frame_coherence through a frequency-selective channel. The single-symbol coherence reads
     * only the share of the signal on the sampled path: on air a 600L burst whose strongest
     * path held a quarter of its energy never reached 0.5 on a frame, so no frame counted as
     * signal and nothing was decoded. Here frame f's probes are first combined over the lags
     * -MP_LAGS..MP_LAGS with the conjugate of a channel estimate (maximal-ratio), and the
     * coherence is taken of that: s / (1 + s) at the combined probe SNR s, as before. The
     * estimate is the lagged probe correlation over the counted frames among the 4 before f,
     * not f itself, so on noise f still reads about 1/K; frame 0 (no frame before it) uses
     * itself. A frame is scored once the MP_LAGS symbols after it are in s, or when final.
     */
    static void frame_coherence_multipath(const std::vector<complex_t>& s, int unknown_len, int known_len,
                                          int block_frames, std::vector<float>& rho, bool final) {
        const long p = unknown_len + known_len;
        const long size = static_cast<long>(s.size());
        const size_t nf = s.size() / static_cast<size_t>(p);
        static const std::array<complex_t, 160> ref = [] {
            std::array<complex_t, 160> r{};
            DataScramblerFixed sc;
            for (int i = 0; i < 160; i++) {
                const float ph = sc.next() * static_cast<float>(PI) / 4.0f;
                r[i] = complex_t(std::cos(ph), -std::sin(ph));  // conjugated
            }
            return r;
        }();
        auto lagged = [&](long g, int lag) {  // sum over frame g's probes of s[n + lag] conj(ref[n])
            complex_t c(0, 0);
            for (long i = 0; i < known_len; i++) {
                const long n = g * p + unknown_len + i, j = n + lag;
                if (j >= 0 && j < size) c += s[static_cast<size_t>(j)] * ref[static_cast<size_t>(n % 160)];
            }
            return c;
        };
        for (size_t f = rho.size(); f < nf; f++) {
            if (!final && static_cast<long>(f + 1) * p + MP_LAGS > size) return;
            rho.push_back(-1.0f);
            if (block_boundary_probe(static_cast<int>(f), block_frames)) continue;
            std::array<complex_t, 2 * MP_LAGS + 1> h{};
            int used = 0;
            for (long g = static_cast<long>(f) - 1; g >= 0 && g >= static_cast<long>(f) - 4; g--) {
                if (block_boundary_probe(static_cast<int>(g), block_frames)) continue;
                for (int l = -MP_LAGS; l <= MP_LAGS; l++) h[static_cast<size_t>(l + MP_LAGS)] += lagged(g, l);
                used++;
            }
            if (used == 0)
                for (int l = -MP_LAGS; l <= MP_LAGS; l++) h[static_cast<size_t>(l + MP_LAGS)] = lagged(static_cast<long>(f), l);
            complex_t num(0, 0);
            double pw = 0.0;
            for (long i = 0; i < known_len; i++) {
                const long n = static_cast<long>(f) * p + unknown_len + i;
                complex_t y(0, 0);
                for (int l = -MP_LAGS; l <= MP_LAGS; l++) {
                    const long j = n + l;
                    if (j >= 0 && j < size) y += std::conj(h[static_cast<size_t>(l + MP_LAGS)]) * s[static_cast<size_t>(j)];
                }
                num += y * ref[static_cast<size_t>(n % 160)];
                pw += std::norm(y);
            }
            rho[f] = pw > 0.0 ? static_cast<float>(std::norm(num) / (pw * known_len)) : 0.0f;
        }
    }

    /// Frame f carries signal, as signal_frames() counts it: coherence 0.5 or more, and the
    /// mean over it and the 7 counted frames before it 0.25 or more. Reads rho[0..f] only.
    static bool signal_frame(const std::vector<float>& rho, size_t f) {
        if (rho[f] < 0.5f) return false;
        double sum = 0.0;
        int n = 0;
        for (size_t k = f + 1; k-- > 0 && n < 8;)
            if (rho[k] >= 0.0f) { sum += rho[k]; n++; }
        return !(sum / n < 0.25);
    }

    /**
     * In a forced mode, the standard 110A mode the preamble's D1/D2 name instead, or "".
     * Only when both read clearly (best at least 1.5 x the runner-up, as for the countdown
     * digits) and name a Table XI data mode other than the forced one. Forced Harris modes
     * and the 2400V, Harris and reserved codes never refuse.
     */
    static std::string preamble_contradicts(const BrainDecodeResult& b, ModeId forced) {
        // A preamble taken under the acquisition gate because the mode was forced
        // (gate_bypassed) stands only when its D1/D2 read the forced mode itself.
        if (b.gate_bypassed && b.mode_name != ModeDatabase::get(forced).name)
            return b.mode_name != "UNKNOWN" ? b.mode_name.substr(1)
                                            : "D1=" + std::to_string(b.d1) + " D2=" + std::to_string(b.d2);
        if (forced == ModeId::M600H || forced == ModeId::M1200H || forced == ModeId::M2400V) return "";
        static const char* const STANDARD[] = {"M150S", "M150L", "M300S", "M300L", "M600S", "M600L",
                                               "M1200S", "M1200L", "M2400S", "M2400L", "M4800S"};
        if (std::find(std::begin(STANDARD), std::end(STANDARD), b.mode_name) == std::end(STANDARD)) return "";
        if (b.d1_corr < 1.5f * b.d1_corr2 || b.d2_corr < 1.5f * b.d2_corr2) return "";
        return string_to_mode_id(b.mode_name) == forced ? "" : b.mode_name.substr(1);
    }

    /**
     * Detect the older ModemTX end of message in decoded data
     * 
     * EOM consists of 4 frames of zero data. After FEC decoding,
     * this produces zero bytes at the end of the transmission.
     * 
     * Challenge: Interleaver padding also creates trailing zeros!
     * - Interleaver pads short messages to block boundary
     * - This padding decodes as zeros (can be 30+ bytes)
     * - Need to distinguish EOM zeros from padding zeros
     * 
     * EOM produces: 4 × unknown_len × 3 / 16 bytes ≈ 24 bytes for M2400S
     * 
     * To distinguish from padding, we require:
     * - At least 40 trailing zeros (exceeds typical padding)
     * - This may miss EOM on very short messages, but avoids false positives
     * 
     * @param decoded Decoded data bytes  
     * @param unknown_len Data symbols per frame
     * @param known_len Probe symbols per frame (unused)
     * @return true if EOM likely present
     */
    static bool detect_eom(const std::vector<uint8_t>& decoded, 
                           int unknown_len, int known_len) {
        if (decoded.size() < 50) return false;
        
        // Count trailing zeros
        int trailing_zeros = 0;
        for (auto it = decoded.rbegin(); it != decoded.rend(); ++it) {
            if (*it == 0) {
                trailing_zeros++;
            } else {
                break;
            }
        }
        
        // Calculate expected EOM size
        // EOM = 4 frames × unknown_len × 3 bits / 2 (FEC) / 8 (bits/byte)
        int expected_eom_bytes = (4 * unknown_len * 3) / 16;
        
        // Require trailing zeros >= expected EOM + 50% margin
        // This helps distinguish from pure interleaver padding
        int min_zeros = expected_eom_bytes * 3 / 2;  // 36 for M2400S
        
        // Also require at least 40 zeros absolute minimum
        min_zeros = std::max(40, min_zeros);
        
        return trailing_zeros >= min_zeros;
    }
    
    /**
     * Strip EOM zero padding from decoded data
     * 
     * Removes trailing zeros that were part of EOM marker.
     * Preserves intentional trailing zeros in user data by only
     * removing the expected EOM amount.
     * 
     * @param decoded Decoded data with EOM padding
     * @return Data with EOM padding removed
     */
    static std::vector<uint8_t> strip_eom_padding(const std::vector<uint8_t>& decoded) {
        if (decoded.empty()) return decoded;
        
        // Find last non-zero byte
        size_t last_nonzero = decoded.size();
        for (auto it = decoded.rbegin(); it != decoded.rend(); ++it) {
            if (*it != 0) {
                break;
            }
            last_nonzero--;
        }
        
        // Keep at least 1 byte even if all zeros
        if (last_nonzero == 0) {
            return {0};
        }
        
        return std::vector<uint8_t>(decoded.begin(), decoded.begin() + last_nonzero);
    }
    
    /// Probe-aided DFE over the data (frame_dfe_equalize, src/equalizer/frame_dfe.h);
    /// keeps the per-frame probe error for the decode quality (dfe_probe_frames_).
    std::vector<complex_t> apply_dfe_equalization(
            const std::vector<complex_t>& symbols,
            int unknown_len, int known_len,
            const std::vector<complex_t>& preamble_symbols = {},
            bool use_nlms = false,
            const std::vector<complex_t>& pre_data_symbols = {}) {
        FrameDfeOutput r = frame_dfe_equalize(symbols, unknown_len, known_len, block_frames_,
                                              preamble_symbols, use_nlms, pre_data_symbols,
                                              std::numeric_limits<int>::max(), dfe_data_step_);
        dfe_probe_frames_.insert(dfe_probe_frames_.end(), r.probe_frames.begin(), r.probe_frames.end());
        return std::move(r.symbols);
    }
    
    /**
     * Apply MLSE equalization using Viterbi algorithm
     * 
     * MLSE provides optimal detection for multipath channels by searching
     * over all possible transmitted sequences.
     * 
     * @param symbols Input symbols (data + probes interleaved)
     * @param unknown_len Data symbols per frame
     * @param known_len Probe symbols per frame  
     * @param channel_memory L parameter (2 = 8 states, 3 = 64 states)
     * @param preamble_symbols Optional preamble for initial channel estimation
     */
    std::vector<complex_t> apply_mlse_equalization(
            const std::vector<complex_t>& symbols,
            int unknown_len, int known_len,
            int channel_memory,
            const std::vector<complex_t>& preamble_symbols = {}) {
        
        // 8-PSK constellation (same as DFE)
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
        
        static const int PROBE_GRAY = 0;  // MGD3[0] = 0
        
        // Preamble scrambling sequence
        static const std::array<uint8_t, 32> pscramble = {
            7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
            5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
        };
        
        // Common preamble pattern (D values)
        static const std::array<uint8_t, 9> p_c_seq = {0, 1, 3, 0, 1, 3, 1, 2, 0};
        
        // PSK symbol patterns
        static const std::array<std::array<uint8_t, 8>, 8> psymbol = {{
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 4, 0, 4, 0, 4, 0, 4},
            {0, 0, 4, 4, 0, 0, 4, 4},
            {0, 4, 4, 0, 0, 4, 4, 0},
            {0, 0, 0, 0, 4, 4, 4, 4},
            {0, 4, 0, 4, 4, 0, 4, 0},
            {0, 0, 4, 4, 4, 4, 0, 0},
            {0, 4, 4, 0, 4, 0, 0, 4}
        }};
        
        // Configure MLSE
        MLSEConfig mlse_cfg;
        mlse_cfg.channel_memory = channel_memory;
        mlse_cfg.traceback_depth = 20;
        
        MLSEEqualizer mlse(mlse_cfg);
        
        // ========================================
        // Preamble Pretraining (Channel Estimation)
        // ========================================
        if (!preamble_symbols.empty()) {
            // Use first 288 preamble symbols for channel estimation
            int pretrain_len = std::min(288, static_cast<int>(preamble_symbols.size()));
            
            std::vector<complex_t> preamble_ref;
            preamble_ref.reserve(pretrain_len);
            
            int scram_idx = 0;
            for (int i = 0; i < 9 && static_cast<int>(preamble_ref.size()) < pretrain_len; i++) {
                uint8_t d_val = p_c_seq[i];
                for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                    uint8_t base = psymbol[d_val][j % 8];
                    uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
                    preamble_ref.push_back(PSK8[scrambled]);
                    scram_idx++;
                }
            }
            
            // Initial channel estimate from preamble
            std::vector<complex_t> preamble_rx(
                preamble_symbols.begin(),
                preamble_symbols.begin() + pretrain_len);
            
            mlse.estimate_channel(preamble_ref, preamble_rx);
        }
        
        // ========================================
        // Data Frame Processing
        // ========================================
        DataScramblerFixed scrambler;
        
        int pattern_len = unknown_len + known_len;
        std::vector<complex_t> output;
        output.reserve(symbols.size());
        
        size_t idx = 0;
        int frame = 0;
        
        while (idx + pattern_len <= symbols.size()) {
            // Get current frame data and probes
            std::vector<complex_t> data_in(
                symbols.begin() + idx,
                symbols.begin() + idx + unknown_len);
            
            std::vector<complex_t> probe_in(
                symbols.begin() + idx + unknown_len,
                symbols.begin() + idx + pattern_len);
            
            // Generate probe reference using synchronized scrambler
            scrambler.reset();
            int scr_pos = frame * pattern_len + unknown_len;
            for (int i = 0; i < scr_pos; i++) scrambler.next();
            
            std::vector<complex_t> probe_ref(known_len);
            for (int i = 0; i < known_len; i++) {
                int scr = scrambler.next();
                int sym_idx = (PROBE_GRAY + scr) & 7;
                probe_ref[i] = PSK8[sym_idx];
            }
            
            // Estimate channel from probes (Least Squares)
            mlse.estimate_channel(probe_ref, probe_in);
            
            // Equalize data symbols using MLSE
            std::vector<int> decoded_indices = mlse.equalize(data_in);
            
            // Convert decoded indices back to complex symbols
            for (int sym_idx : decoded_indices) {
                if (sym_idx >= 0 && sym_idx < 8) {
                    output.push_back(PSK8[sym_idx]);
                }
            }
            
            // Pad if MLSE returned fewer symbols (shouldn't happen normally)
            while (output.size() < idx + unknown_len) {
                output.push_back(complex_t(1, 0));  // Default
            }
            
            // Pass through original probes
            output.insert(output.end(), probe_in.begin(), probe_in.end());
            
            idx += pattern_len;
            frame++;
        }
        
        // Remaining symbols - pass through
        if (idx < symbols.size()) {
            output.insert(output.end(), symbols.begin() + idx, symbols.end());
        }
        
        return output;
    }
    
    /**
     * Apply Adaptive MLSE equalization with continuous tracking
     * 
     * This provides 100x better performance on fast fading channels compared to RLS.
     * Uses continuous processing without frame-by-frame reset.
     * 
     * Test results:
     * - Fade 0.001: MLSE 3.6% vs RLS 6.1%
     * - Fade 0.01:  MLSE 0.9% vs RLS 79.8%
     * - Fade 0.02:  MLSE 0.6% vs RLS 86.4%
     */
    std::vector<complex_t> apply_adaptive_mlse_equalization(
            const std::vector<complex_t>& symbols,
            int unknown_len, int known_len,
            const std::vector<complex_t>& preamble_symbols = {}) {
        
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
        
        // Preamble scrambling sequence
        static const std::array<uint8_t, 32> pscramble = {
            7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
            5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
        };
        
        // Common preamble pattern (D values)
        static const std::array<uint8_t, 9> p_c_seq = {0, 1, 3, 0, 1, 3, 1, 2, 0};
        
        // PSK symbol patterns
        static const std::array<std::array<uint8_t, 8>, 8> psymbol = {{
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 4, 0, 4, 0, 4, 0, 4},
            {0, 0, 4, 4, 0, 0, 4, 4},
            {0, 4, 4, 0, 0, 4, 4, 0},
            {0, 0, 0, 0, 4, 4, 4, 4},
            {0, 4, 0, 4, 4, 0, 4, 0},
            {0, 0, 4, 4, 4, 4, 0, 0},
            {0, 4, 4, 0, 4, 0, 0, 4}
        }};
        
        // Configure Adaptive MLSE (L=3 for best performance)
        AdaptiveMLSEConfig mlse_cfg;
        mlse_cfg.channel_memory = 3;     // 64 states
        mlse_cfg.traceback_depth = 25;
        mlse_cfg.track_during_data = false;  // Use probe-based tracking only
        mlse_cfg.adaptation_rate = 0.01f;
        
        AdaptiveMLSE mlse(mlse_cfg);
        
        // Estimate channel from preamble
        if (!preamble_symbols.empty()) {
            int pretrain_len = std::min(288, static_cast<int>(preamble_symbols.size()));
            
            std::vector<complex_t> preamble_ref;
            preamble_ref.reserve(pretrain_len);
            
            int scram_idx = 0;
            for (int i = 0; i < 9 && static_cast<int>(preamble_ref.size()) < pretrain_len; i++) {
                uint8_t d_val = p_c_seq[i];
                for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                    uint8_t base = psymbol[d_val][j % 8];
                    uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
                    preamble_ref.push_back(PSK8[scrambled]);
                    scram_idx++;
                }
            }
            
            std::vector<complex_t> preamble_rx(
                preamble_symbols.begin(),
                preamble_symbols.begin() + pretrain_len);
            
            mlse.estimate_channel(preamble_rx, preamble_ref);
        }
        
        // Process entire sequence continuously (key to performance). Its a-priori probe
        // error per frame stands for the decode quality, as the DFE's does: the output's
        // probes are the known symbols themselves, so scoring them read about 80 dB on any
        // input, noise included, and every block at the 40 dB clamp.
        std::vector<std::array<double, 3>> pf;
        auto result = mlse.equalize_with_tracking(symbols, unknown_len, known_len, &pf);
        const int pattern = unknown_len + known_len;
        for (const auto& f : pf)
            if (!block_boundary_probe(static_cast<int>(f[0]) / pattern - 1, block_frames_))
                dfe_probe_frames_.push_back(f);
        
        // Convert symbol indices to complex symbols
        std::vector<complex_t> output;
        output.reserve(result.size());
        for (int sym_idx : result) {
            if (sym_idx >= 0 && sym_idx < 8) {
                output.push_back(PSK8[sym_idx]);
            }
        }
        
        return output;
    }
    
    /**
     * Result from turbo equalization
     */
    struct TurboResult {
        std::vector<complex_t> symbols;     // Equalized symbols
        std::vector<uint8_t> decoded_bits;  // SISO decoded bits (already FEC decoded)
    };
    
    /**
     * Apply turbo equalization with TurboCodecIntegrated
     * 
     * Uses TurboCodecIntegrated which properly handles:
     *   - Scrambler with correct frame indexing (accounting for probe gaps)
     *   - Gray code conversion (MGD3/INV_MGD3)
     *   - Mode-specific helical interleaver
     *   - SISO decoder (BCJR, K=7, rate 1/2)
     *   - Iterative MLSE ↔ SISO exchange
     * 
     * Returns improved symbols for passing to normal codec path.
     * 
     * @param symbols Input symbols (data + probes interleaved)
     * @param mode_id Mode identifier for codec chain
     * @param unknown_len Data symbols per frame
     * @param known_len Probe symbols per frame
     * @param preamble_symbols Preamble for channel estimation
     * @return TurboResult with improved symbols
     */
    TurboResult apply_turbo_equalization_full(
            const std::vector<complex_t>& symbols,
            ModeId mode_id,
            int unknown_len, int known_len,
            const std::vector<complex_t>& preamble_symbols = {}) {
        
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
        
        // Preamble scrambling sequence
        static const std::array<uint8_t, 32> pscramble = {
            7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
            5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
        };
        
        // Common preamble pattern (D values)
        static const std::array<uint8_t, 9> p_c_seq = {0, 1, 3, 0, 1, 3, 1, 2, 0};
        
        // PSK symbol patterns
        static const std::array<std::array<uint8_t, 8>, 8> psymbol = {{
            {0, 0, 0, 0, 0, 0, 0, 0},
            {0, 4, 0, 4, 0, 4, 0, 4},
            {0, 0, 4, 4, 0, 0, 4, 4},
            {0, 4, 4, 0, 0, 4, 4, 0},
            {0, 0, 0, 0, 4, 4, 4, 4},
            {0, 4, 0, 4, 4, 0, 4, 0},
            {0, 0, 4, 4, 4, 4, 0, 0},
            {0, 4, 4, 0, 4, 0, 0, 4}
        }};
        
        TurboResult result;
        
        // Extract data-only symbols (remove probes)
        int pattern_len = unknown_len + known_len;
        std::vector<complex_t> data_only;
        data_only.reserve((symbols.size() / pattern_len) * unknown_len);
        
        size_t idx = 0;
        while (idx + pattern_len <= symbols.size()) {
            for (int i = 0; i < unknown_len; i++) {
                data_only.push_back(symbols[idx + i]);
            }
            idx += pattern_len;
        }
        
        // Handle remaining partial frame
        size_t remaining = symbols.size() - idx;
        for (size_t i = 0; i < remaining && i < static_cast<size_t>(unknown_len); i++) {
            data_only.push_back(symbols[idx + i]);
        }
        
        // Generate preamble reference
        std::vector<complex_t> preamble_ref;
        std::vector<complex_t> preamble_rx;
        
        if (!preamble_symbols.empty()) {
            int pretrain_len = std::min(288, static_cast<int>(preamble_symbols.size()));
            preamble_ref.reserve(pretrain_len);
            
            int scram_idx = 0;
            for (int i = 0; i < 9 && static_cast<int>(preamble_ref.size()) < pretrain_len; i++) {
                uint8_t d_val = p_c_seq[i];
                for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                    uint8_t base = psymbol[d_val][j % 8];
                    uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
                    preamble_ref.push_back(PSK8[scrambled]);
                    scram_idx++;
                }
            }
            
            preamble_rx.assign(preamble_symbols.begin(), 
                              preamble_symbols.begin() + pretrain_len);
        }
        
        // Configure turbo codec
        TurboIntegratedConfig cfg;
        cfg.mode_id = mode_id;
        cfg.max_iterations = 5;
        cfg.extrinsic_scale = 0.7f;
        cfg.early_termination = true;
        cfg.convergence_threshold = 0.05f;
        cfg.channel_memory = 3;
        cfg.noise_variance = 0.1f;
        cfg.verbose = false;
        
        // Create turbo codec
        TurboCodecIntegrated turbo(cfg);
        
        // Get improved symbols after turbo iterations
        auto improved_data = turbo.equalize_symbols(data_only, preamble_rx, preamble_ref, 0);
        
        // Reconstruct full symbol stream with probes
        // (probes are passed through unchanged since they're known)
        result.symbols.reserve(symbols.size());
        
        idx = 0;
        size_t data_idx = 0;
        while (idx + pattern_len <= symbols.size()) {
            // Add improved data symbols
            for (int i = 0; i < unknown_len && data_idx < improved_data.size(); i++) {
                result.symbols.push_back(improved_data[data_idx++]);
            }
            // Add original probes unchanged
            for (int i = 0; i < known_len; i++) {
                result.symbols.push_back(symbols[idx + unknown_len + i]);
            }
            idx += pattern_len;
        }
        
        // Add remaining symbols
        while (result.symbols.size() < symbols.size() && data_idx < improved_data.size()) {
            result.symbols.push_back(improved_data[data_idx++]);
        }
        while (result.symbols.size() < symbols.size()) {
            result.symbols.push_back(symbols[result.symbols.size()]);
        }
        
        // decoded_bits is empty - use normal codec path for final decode
        // (turbo improves symbol estimates, Viterbi does final decode)
        
        return result;
    }
    
    /**
     * Apply adaptive phase tracking using probe-aided + decision-directed PLL
     * 
     * @param symbols Input symbols (data + probes interleaved)
     * @param unknown_len Data symbols per frame
     * @param known_len Probe symbols per frame
     * @param conservative If true, use probe-only tracking (for use with equalizers)
     * @return {phase-corrected symbols, estimated frequency offset in Hz}
     */
    std::pair<std::vector<complex_t>, float> apply_phase_tracking(
            const std::vector<complex_t>& symbols,
            int unknown_len, int known_len,
            bool conservative = false) {
        PhaseTracking pt(unknown_len, known_len, block_frames_, conservative);
        std::vector<complex_t> output;
        output.reserve(symbols.size());
        pt.advance(symbols, true, output);
        return {output, pt.frequency()};
    }

    /**
     * apply_phase_tracking a frame at a time: advance() takes the data symbols received so
     * far (from symbol 0) and corrects each whole frame not yet done; with eof, the symbols
     * after the last whole frame too, at the current estimate.
     */
    class PhaseTracking {
    public:
        PhaseTracking(int unknown_len, int known_len, int block_frames, bool conservative)
            : unknown_len_(unknown_len), known_len_(known_len), block_frames_(block_frames),
              tracker_(config(conservative)) {}

        void advance(const std::vector<complex_t>& symbols, bool eof, std::vector<complex_t>& output) {
            if (finished_) return;
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
            
            static const int PROBE_GRAY = 0;
            
            const int pattern_len = unknown_len_ + known_len_;
            while (idx_ + pattern_len <= symbols.size()) {
                // Get current frame data and probes
                std::vector<complex_t> data_in(
                    symbols.begin() + idx_,
                    symbols.begin() + idx_ + unknown_len_);
                
                std::vector<complex_t> probe_in(
                    symbols.begin() + idx_ + unknown_len_,
                    symbols.begin() + idx_ + pattern_len);
                
                // Generate probe reference (the sequence repeats every 160 symbols)
                scrambler_.reset();
                int scr_pos = frame_ * pattern_len + unknown_len_;
                for (int i = 0; i < scr_pos % 160; i++) scrambler_.next();
                
                std::vector<complex_t> probe_ref(known_len_);
                for (int i = 0; i < known_len_; i++) {
                    int scr = scrambler_.next();
                    int sym_idx = (PROBE_GRAY + scr) & 7;
                    probe_ref[i] = PSK8[sym_idx];
                }
                
                if (frame_ == 0) {
                    // First frame: process data first (with initial estimate),
                    // then train on probes for next frame
                    auto corrected_data = tracker_.process(data_in);
                    output.insert(output.end(), corrected_data.begin(), corrected_data.end());
                    
                    tracker_.train(probe_in, probe_ref);
                    auto corrected_probes = tracker_.process(probe_in);
                    output.insert(output.end(), corrected_probes.begin(), corrected_probes.end());
                } else {
                    // Subsequent frames: apply correction from previous probe training,
                    // then update with current probes
                    auto corrected_data = tracker_.process(data_in);
                    output.insert(output.end(), corrected_data.begin(), corrected_data.end());
                    
                    auto corrected_probes = tracker_.process(probe_in);
                    output.insert(output.end(), corrected_probes.begin(), corrected_probes.end());
                    
                    // Train for next frame (not on a block-boundary probe,
                    // which may carry D1/D2)
                    if (!block_boundary_probe(frame_, block_frames_))
                        tracker_.train(probe_in, probe_ref);
                }
                
                idx_ += pattern_len;
                frame_++;
            }
            if (!eof) return;
            
            // Remaining symbols - apply current phase estimate
            if (idx_ < symbols.size()) {
                std::vector<complex_t> remaining(symbols.begin() + idx_, symbols.end());
                auto corrected = tracker_.process(remaining);
                output.insert(output.end(), corrected.begin(), corrected.end());
            }
            finished_ = true;
        }

        float frequency() const { return tracker_.get_frequency(); }

    private:
        static PhaseTrackerConfig config(bool conservative) {
            // Configure phase tracker
            PhaseTrackerConfig pt_cfg;
            pt_cfg.symbol_rate = 2400.0f;
            pt_cfg.max_freq_hz = 15.0f;  // Support up to ±15 Hz offset
            
            if (conservative) {
                // Conservative mode for use with equalizers
                // Corrects frequency offset without interfering with DFE
                pt_cfg.alpha = 0.02f;    // Moderate phase updates
                pt_cfg.beta = 0.001f;    // Moderate frequency updates
                pt_cfg.decision_directed = false;
            } else {
                // Full tracking: more aggressive for NONE equalizer
                pt_cfg.alpha = 0.05f;
                pt_cfg.beta = 0.002f;
                pt_cfg.decision_directed = false;
                pt_cfg.dd_threshold = 0.3f;
            }
            return pt_cfg;
        }

        int unknown_len_, known_len_, block_frames_;
        PhaseTracker tracker_;
        DataScramblerFixed scrambler_;
        size_t idx_ = 0;
        int frame_ = 0;
        bool finished_ = false;
    };

    // ============================================================
    // Streaming decode (start / push_samples / take_sync / take_blocks / finish)
    // ============================================================
    //
    // The burst's samples, as they arrive, go through the same steps as decode() over the
    // whole buffer: the cut before the first preamble, the two-stage AFC, the preamble
    // (AUTO: the detection pass's reading, then the mode's), the data symbols on the grid
    // the timing steps move (BrainDecoder::DataGrid), the level, phase tracking
    // (PhaseTracking), the DFE (FrameDfe), and per interleaver block the soft demapper,
    // deinterleaver and Viterbi decoder. Each step runs on what its lookahead allows and
    // gives what it gives over the whole buffer. The exception is the soft demapper's SNR:
    // decode() measures it on every probe of the buffer, the stream on the probes up to the
    // block being decoded. Anything the stream does not follow (StreamState::Phase::FALLBACK)
    // is decoded whole at finish().

    struct StreamState {
        enum class Phase { CUT, AFC, HEADER, DATA, DONE, FALLBACK };
        Phase phase = Phase::CUT;
        RxConfig cfg;
        Samples raw;
        bool eof = false;
        // the cut before the first preamble (BrainDecoder::first_preamble_cut)
        std::unique_ptr<BrainDecoder> scan;
        std::unique_ptr<BasebandFilter> scan_bb;
        size_t scan_t = 0;
        size_t cut = 0;
        // the AFC
        std::unique_ptr<BrainDecoder> afc;
        std::unique_ptr<BasebandFilter> coarse_bb;
        bool have_coarse = false;
        std::vector<float> fine_off;
        std::vector<std::unique_ptr<BasebandFilter>> fine_bb;
        std::unique_ptr<BasebandFilter> bb;  // the input at the carrier the AFC chose
        float afc_offset = 0.0f;
        // the preamble
        std::unique_ptr<BrainDecoder> det, dec;
        bool detected = false;
        size_t hdr_wait = 0;
        ModeId mode_id = ModeId::M2400S;
        BrainDecodeResult br;
        bool sync_ready = false, sync_taken = false;
        StreamSync sync;
        // the data
        ModeConfig mcfg;
        int u = 0, k = 0, p = 0, bf = 0;
        size_t ib = 0;  // info bits per interleaver block
        bool have_gain = false;
        float gain = 1.0f;
        std::unique_ptr<BrainDecoder::DataGrid> grid;
        std::vector<complex_t> pre_data;
        bool pre_done = false;
        std::vector<complex_t> data;  // at the nominal level
        std::unique_ptr<PhaseTracking> pt;
        std::vector<complex_t> pc;    // phase corrected (with tracking)
        std::unique_ptr<FrameDfe> dfe;
        FrameDfeOutput eq;            // equalized (with the DFE)
        std::vector<float> rho;
        long last_sig = -1;           // the last frame signal_frame() counts
        std::unique_ptr<M110ACodec> codec;
        int phase_offset = -1;
        size_t blocks_fed = 0;
        ViterbiDecoder vit;
        std::vector<uint8_t> bits;    // decided info bits
        struct Snap { size_t block; ViterbiDecoder vit; size_t bits; };
        std::deque<Snap> snaps;       // the decoder before each block not yet reported + 1
        size_t eom_scan = 0;
        long eom_bit = -1;
        size_t emitted = 0;
        std::vector<BlockRecord> ready;
        DecodeResult result;

        const std::vector<complex_t>& pcv() const { return pt ? pc : data; }
        const std::vector<complex_t>& eqv() const {
            return cfg.equalizer == Equalizer::DFE ? eq.symbols : pcv();
        }
        // push the input not yet given to f (fed from the cut on)
        void feed(BasebandFilter& f, size_t from) {
            const size_t have = from + static_cast<size_t>(f.input_samples());
            if (raw.size() > have) f.push(raw.data() + have, raw.size() - have);
            f.run(eof);
        }
    };
    std::unique_ptr<StreamState> st_;

    static BrainDecoderConfig afc_config(const RxConfig& c) {
        BrainDecoderConfig d;
        d.sample_rate = c.sample_rate;
        d.carrier_freq = c.carrier_freq;
        d.baud_rate = 2400.0f;
        d.freq_search_range = c.freq_search_range;
        d.freq_search_step = 0.5f;
        d.use_fft_coarse_afc = c.use_fft_coarse_afc;
        d.coarse_search_range = c.coarse_search_range;
        d.fine_search_range = c.fine_search_range;
        return d;
    }

    void stream_run() {
        StreamState& S = *st_;
        for (;;) {
            const auto ph = S.phase;
            switch (ph) {
                case StreamState::Phase::CUT: stream_cut(); break;
                case StreamState::Phase::AFC: stream_afc(); break;
                case StreamState::Phase::HEADER: stream_header(); break;
                case StreamState::Phase::DATA: stream_data(); break;
                default: return;
            }
            if (S.phase == ph) return;
        }
    }

    // decode(): BrainDecoder looks for the preamble in the first 0.2 s only: start the
    // buffer just before its first preamble, wherever that is
    void stream_cut() {
        StreamState& S = *st_;
        if (!S.scan) {
            BrainDecoderConfig scan_cfg;
            scan_cfg.sample_rate = S.cfg.sample_rate;
            scan_cfg.carrier_freq = S.cfg.carrier_freq;
            S.scan = std::make_unique<BrainDecoder>(scan_cfg);
            S.scan_bb = std::make_unique<BasebandFilter>(S.scan->baseband_filter());
        }
        S.feed(*S.scan_bb, 0);
        const long c = S.scan->scan_first_preamble(S.scan_bb->out(), S.scan_t);
        if (c < 0 && !S.eof) return;
        S.cut = c < 0 ? 0 : static_cast<size_t>(c);
        S.scan_bb.reset();
        S.afc = std::make_unique<BrainDecoder>(afc_config(S.cfg));
        S.phase = StreamState::Phase::AFC;
    }

    // decode()'s two-stage AFC (BrainDecoder::decode): the coarse offset, then the fine
    // candidates around it, each read over its first fine_afc_need() samples
    void stream_afc() {
        StreamState& S = *st_;
        if (!S.have_coarse) {
            if (!S.coarse_bb) S.coarse_bb = std::make_unique<BasebandFilter>(S.afc->baseband_filter());
            S.feed(*S.coarse_bb, S.cut);
            if (S.coarse_bb->out().size() < S.afc->coarse_afc_need() && !S.eof) return;
            const float coarse = S.afc->coarse_afc(S.coarse_bb->out());
            S.coarse_bb.reset();
            S.fine_off = S.afc->fine_offsets(coarse);
            for (float f : S.fine_off) S.fine_bb.push_back(std::make_unique<BasebandFilter>(S.afc->baseband_filter(f)));
            S.have_coarse = true;
        }
        bool all = true;
        for (auto& f : S.fine_bb) {
            S.feed(*f, S.cut);
            if (f->out().size() < S.afc->fine_afc_need()) all = false;
        }
        if (!all && !S.eof) return;
        float best_corr = 0.0f;
        int bi = -1;
        for (size_t i = 0; i < S.fine_bb.size(); i++) {
            const auto& F = S.fine_bb[i]->out();
            if (F.size() < static_cast<size_t>(288 * S.afc->sps())) continue;
            const float corr = S.afc->fine_afc_metric(F, S.fine_off[i]);
            if (corr > best_corr) { best_corr = corr; bi = static_cast<int>(i); }
        }
        if (bi < 0) { S.phase = StreamState::Phase::FALLBACK; return; }  // decode()'s full-range search
        S.bb = std::move(S.fine_bb[static_cast<size_t>(bi)]);
        S.afc_offset = S.fine_off[static_cast<size_t>(bi)];
        S.fine_bb.clear();
        S.phase = StreamState::Phase::HEADER;
    }

    // The preamble, read as decode()'s passes read it once the input reaches the end of
    // what they read: AUTO, the detection pass (BrainDecoder with the default 2400S
    // configuration) for the mode; then the mode's own pass for the data start
    void stream_header() {
        StreamState& S = *st_;
        S.feed(*S.bb, S.cut);
        const auto& F = S.bb->out();
        const size_t sps = static_cast<size_t>(S.afc->sps());
        if (!S.eof && (F.size() < S.afc->header_need() || F.size() < S.hdr_wait)) return;
        const bool automode = S.cfg.mode == Mode::AUTO;
        if (automode && !S.detected) {
            if (!S.det) S.det = std::make_unique<BrainDecoder>(afc_config(S.cfg));
            BrainDecodeResult dr;
            dr.freq_offset_hz = S.afc_offset;
            const bool found = S.det->read_header(F, dr);
            if (found) {
                const size_t need = std::max<size_t>(static_cast<size_t>(std::max(0L, S.det->data_start(dr))),
                                                     static_cast<size_t>(dr.start_sample) + 480 * sps);
                if (F.size() < need && !S.eof) { S.hdr_wait = need; return; }
            }
            record_preamble(dr, S.result);
            if (!found || dr.mode_name == "UNKNOWN" || dr.correlation < 0.5f) {
                S.phase = StreamState::Phase::FALLBACK;  // decode() fails, or finds 75 bps below its reach
                return;
            }
            S.mode_id = string_to_mode_id(dr.mode_name);
            S.result.mode = internal_to_api_mode(S.mode_id);
            if (m75::is_m75(S.mode_id)) { S.phase = StreamState::Phase::FALLBACK; return; }
            S.detected = true;
            S.hdr_wait = 0;
            report_sync(dr);
        } else if (!automode) {
            S.mode_id = api_to_internal_mode(S.cfg.mode);
            S.result.mode = S.cfg.mode;
        }
        const auto& mode_cfg = ModeDatabase::get(S.mode_id);
        if (!S.dec) {
            BrainDecoderConfig dc = afc_config(S.cfg);
            dc.unknown_data_len = mode_cfg.unknown_data_len;
            dc.known_data_len = mode_cfg.known_data_len;
            dc.preamble_symbols = mode_cfg.preamble_symbols();  // Critical for LONG modes!
            dc.forced_mode = !automode;
            S.dec = std::make_unique<BrainDecoder>(dc);
        }
        BrainDecodeResult r;
        r.freq_offset_hz = S.afc_offset;
        if (!S.dec->read_header(F, r)) { S.phase = StreamState::Phase::FALLBACK; return; }
        const size_t need = std::max<size_t>(static_cast<size_t>(std::max(0L, S.dec->data_start(r))),
                                             static_cast<size_t>(r.start_sample) + 480 * sps);
        if (F.size() < need && !S.eof) { S.hdr_wait = need; return; }
        if (!automode) report_sync(r);
        S.br = std::move(r);
        // the data
        S.mcfg = mode_cfg;
        S.u = mode_cfg.unknown_data_len;
        S.k = mode_cfg.known_data_len;
        S.p = S.u + S.k;
        S.bf = interleaver_block_frames(mode_cfg);
        if (S.u <= 0 || S.k <= 0 || S.bf <= 0) { S.phase = StreamState::Phase::FALLBACK; return; }
        S.codec = std::make_unique<M110ACodec>(S.mode_id);
        const int rep = mode_cfg.symbol_repetition;
        const size_t vin = static_cast<size_t>(rep > 1 && mode_cfg.modulation == Modulation::BPSK
                                               ? S.codec->block_size() / rep : S.codec->block_size());
        S.ib = S.codec->uncoded() ? vin : vin / 2;
        S.have_gain = symbol_gain(S.br, S.gain);
        if (S.have_gain) for (auto& x : S.br.preamble_symbols) x *= S.gain;
        S.grid = std::make_unique<BrainDecoder::DataGrid>(*S.dec, S.br, S.dec->data_start(S.br));
        if (S.cfg.phase_tracking && !(S.cfg.equalizer == Equalizer::DFE && tracking_dfe_step(mode_cfg) > 0))  // as decode()
            S.pt = std::make_unique<PhaseTracking>(S.u, S.k, S.bf, S.cfg.equalizer != Equalizer::NONE);
        state_ = RxState::RECEIVING;
        S.phase = StreamState::Phase::DATA;
    }

    void report_sync(const BrainDecodeResult& r) {
        StreamState& S = *st_;
        S.sync.mode = S.result.mode;
        S.sync.d1 = r.d1;
        S.sync.d2 = r.d2;
        S.sync.freq_offset_hz = r.freq_offset_hz;
        S.sync.preamble_sample = S.cut + static_cast<size_t>(std::max(0, r.start_sample));
        S.sync_ready = true;
    }

    void stream_data() {
        StreamState& S = *st_;
        S.feed(*S.bb, S.cut);
        const auto& F = S.bb->out();
        S.grid->advance(F, S.eof);
        const size_t n0 = S.data.size();
        S.grid->extract(F, S.eof, S.data, &S.pre_data);
        if (!S.pre_done && S.grid->pre_data_done()) {
            S.pre_done = true;
            if (S.have_gain) for (auto& x : S.pre_data) x *= S.gain;
        }
        if (S.have_gain) for (size_t i = n0; i < S.data.size(); i++) S.data[i] *= S.gain;
        if (S.pt) S.pt->advance(S.data, S.eof, S.pc);
        if (S.cfg.equalizer == Equalizer::DFE) {
            if (!S.dfe && S.pre_done)
                S.dfe = std::make_unique<FrameDfe>(S.u, S.k, S.bf, S.br.preamble_symbols, S.cfg.use_nlms,
                                                   S.pre_data, std::numeric_limits<int>::max(),
                                                   tracking_dfe_step(S.mcfg));
            if (S.dfe) S.dfe->advance(S.pcv(), S.eof, S.eq);
        }
        const size_t f0 = S.rho.size();
        frame_coherence(S.pcv(), S.u, S.k, S.bf, S.rho, S.br.multipath, S.eof);
        for (size_t f = f0; f < S.rho.size(); f++) if (signal_frame(S.rho, f)) S.last_sig = static_cast<long>(f);
        const auto& E = S.eqv();
        const size_t eframes = E.size() / static_cast<size_t>(S.p);
        while (S.eom_bit < 0 && (S.blocks_fed + 1) * static_cast<size_t>(S.bf) <= eframes)
            stream_feed_block(S.blocks_fed * S.bf, (S.blocks_fed + 1) * S.bf);
        stream_emit();
    }

    /// Demap frames [f0, f1) of the equalized data (a whole block, or at the end the last
    /// part of one, padded with zeros as decode_soft_bits() pads it) and feed the decoder
    void stream_feed_block(size_t f0, size_t f1) {
        StreamState& S = *st_;
        const auto& E = S.eqv();
        if (S.phase_offset < 0) S.phase_offset = S.codec->probe_phase_offset(E);
        DecodeOptions opts = DecodeOptions::legacy();
        if (S.cfg.use_snr_weighted_demapper) {
            float snr_db = S.cfg.assumed_snr_db;
            if (S.cfg.estimate_snr_from_probes)
                snr_db = S.codec->estimate_snr_from_probes(
                    std::vector<complex_t>(E.begin(), E.begin() + static_cast<long>(f1 * S.p)));
            opts = DecodeOptions::snr_weighted(snr_db);
        }
        std::vector<soft_bit_t> soft;
        S.codec->demap_frames(E, f0, f1, S.phase_offset, opts, soft);
        soft.resize(static_cast<size_t>(S.codec->block_size()), 0);
        S.snaps.push_back({S.blocks_fed, S.vit, S.bits.size()});
        const auto v = S.codec->block_viterbi_input(soft);
        if (S.codec->uncoded()) {
            M110ACodec::hard_bits(v, S.bits);
        } else {
            for (size_t i = 0; i + 1 < v.size(); i += 2) {
                const int bit = S.vit.decode_soft(v[i], v[i + 1]);
                if (bit >= 0) S.bits.push_back(static_cast<uint8_t>(bit));
            }
        }
        S.blocks_fed++;
        // The EOM 4B65A5B2 at any bit offset, in the whole bytes decided (cut_at_eom)
        static const uint8_t EOM_BYTES[4] = {0xD2, 0xA6, 0xA5, 0x4D};
        const size_t nbits = S.bits.size() / 8 * 8;
        for (; S.eom_bit < 0 && S.eom_scan + 32 <= nbits; S.eom_scan++) {
            bool ok = true;
            for (size_t i = 0; i < 32 && ok; i++)
                ok = S.bits[S.eom_scan + i] == ((EOM_BYTES[i / 8] >> (i % 8)) & 1);
            if (ok) S.eom_bit = static_cast<long>(S.eom_scan);
        }
    }

    /// Quality over the first span data symbols, as judge() reads it; from_dfe as it passes
    /// on to the block quality
    static float span_quality(const std::vector<complex_t>& syms, const ModeConfig& m, int block_frames,
                              const std::vector<std::array<double, 3>>& probe_frames, bool dfe, int span,
                              bool& from_dfe) {
        double dfe_err = 0.0, dfe_ref = 0.0;
        for (const auto& f : probe_frames) {
            if (f[0] <= span) { dfe_err += f[1]; dfe_ref += f[2]; }
        }
        from_dfe = dfe && dfe_err > 0.0;
        if (from_dfe) return static_cast<float>(10.0 * std::log10(dfe_ref / dfe_err));
        return probe_snr_db(syms, m.unknown_data_len, m.known_data_len, block_frames, span);
    }

    /// Block record b: its bytes of `bytes` (the last record takes the rest), its quality over
    /// its frames before msg_frames (the message's end, when known: with the interleaver
    /// bypassed, 4800S, a message ends at a frame, not at a block)
    BlockRecord stream_record(size_t b, const std::vector<uint8_t>& bytes, bool last, float q_span,
                              bool from_dfe, size_t msg_frames = std::numeric_limits<size_t>::max()) const {
        const StreamState& S = *st_;
        BlockRecord r;
        r.index = b;
        const size_t from = std::min(bytes.size(), S.ib * b / 8);
        const size_t to = last ? bytes.size() : std::min(bytes.size(), S.ib * (b + 1) / 8);
        r.data.assign(bytes.begin() + static_cast<long>(from), bytes.begin() + static_cast<long>(std::max(from, to)));
        r.quality_db = q_span;
        r.quality_threshold_db = quality_threshold_db(S.mode_id);
        r.block_quality_threshold_db = block_quality_threshold_db(S.mode_id);
        const auto& E = S.eqv();
        const size_t fb0 = b * S.bf,
                     fb1 = std::min({(b + 1) * static_cast<size_t>(S.bf), E.size() / S.p, msg_frames});
        float q = 0.0f;
        bool scored = fb1 > fb0 &&
            block_quality(S.mcfg, frame_snr(E, S.mcfg, fb1, from_dfe, S.bf, S.eq.probe_frames, fb0), fb0, fb1, q);
        r.block_quality_db = scored ? q : q_span;
        r.passes = q_span >= r.quality_threshold_db && (!scored || q >= r.block_quality_threshold_db);
        r.below_quality = !r.passes;
        r.last = last;
        return r;
    }

    /// Blocks reported for a message of n bytes: those up to the one holding its last byte
    /// (one, holding nothing, for an empty message)
    size_t record_blocks(size_t n) const {
        const StreamState& S = *st_;
        size_t b = 0;
        while (S.ib * (b + 1) / 8 < n) b++;
        return b + 1;
    }

    /// Report each block whose bytes are decided and known not to be the message's last,
    /// or, once the EOM is decoded and its blocks are in, the rest of the message
    void stream_emit() {
        StreamState& S = *st_;
        const auto& E = S.eqv();
        const size_t eframes = E.size() / static_cast<size_t>(S.p);
        if (S.eom_bit >= 0) {
            // The bytes end at the EOM: report the blocks up to the one holding the last
            // byte now (its quality is in: the EOM was decided after it), and judge the
            // message once all its blocks (the EOM and flush bits may fill one more) are in
            const size_t msg_bytes = static_cast<size_t>(S.eom_bit) / 8;
            const size_t nrec = record_blocks(msg_bytes);
            bool from_dfe = false;
            const int span = quality_span(S.mcfg, S.eom_bit);
            const float q_span = span_quality(E, S.mcfg, S.bf, S.eq.probe_frames,
                                              S.cfg.equalizer == Equalizer::DFE, span, from_dfe);
            if (S.emitted < nrec) {
                const std::vector<uint8_t> bytes = M110ACodec::bits_to_bytes(
                    std::vector<uint8_t>(S.bits.begin(), S.bits.begin() + static_cast<long>(8 * msg_bytes)));
                const size_t msg_frames = message_symbols(S.mcfg, S.eom_bit) / static_cast<size_t>(S.p);
                for (size_t e = S.emitted; e < nrec; e++) {
                    BlockRecord r = stream_record(e, bytes, e + 1 == nrec, q_span, from_dfe, msg_frames);
                    r.eom = r.last;
                    S.ready.push_back(std::move(r));
                }
                S.emitted = nrec;
            }
            const size_t msg_frames = message_symbols(S.mcfg, S.eom_bit) / static_cast<size_t>(S.p);
            if (msg_frames > eframes && !S.eof) return;
            std::vector<uint8_t> decoded = M110ACodec::bits_to_bytes(S.bits);
            const long eom_bit = cut_at_eom(decoded);
            stream_complete(std::move(decoded), eom_bit, 0, 0, 0);
            return;
        }
        const int span = QUALITY_SPAN_SYMBOLS;
        if (eframes * static_cast<size_t>(S.p) < static_cast<size_t>(span)) return;
        bool from_dfe = false;
        const float q_span = span_quality(E, S.mcfg, S.bf, S.eq.probe_frames,
                                          S.cfg.equalizer == Equalizer::DFE, span, from_dfe);
        for (;;) {
            const size_t e = S.emitted;
            const size_t through = S.ib * (e + 1) / 8;  // bytes through block e
            // no EOM begins in block e's bytes or the byte after them (one more byte follows)
            if (S.bits.size() / 8 * 8 < 8 * through + 40) break;
            // the next block is inside the signal (signal_frames() reaches past its end)
            if (S.last_sig < static_cast<long>((e + 2) * S.bf) - 5) break;
            const std::vector<uint8_t> bytes = M110ACodec::bits_to_bytes(
                std::vector<uint8_t>(S.bits.begin(), S.bits.begin() + static_cast<long>(8 * through)));
            S.ready.push_back(stream_record(e, bytes, false, q_span, from_dfe));
            S.emitted++;
            while (!S.snaps.empty() && S.snaps.front().block < S.emitted + 1) S.snaps.pop_front();
        }
    }

    /// The end of a burst without the EOM decoded: as decode(), the whole interleaver blocks
    /// the signal covers, the decoder flushed at the end of the last of them
    void stream_finish_data() {
        StreamState& S = *st_;
        const auto& PC = S.pcv();
        const auto& E = S.eqv();
        const size_t sig = signal_frames(PC, S.u, S.k, S.bf, S.br.multipath);
        const size_t bf = static_cast<size_t>(S.bf);
        const size_t whole = (sig + 4) / bf * bf;
        const size_t blocks_seen = (PC.size() / S.p + bf - 1) / bf;
        std::vector<uint8_t> fbits;
        if (whole * S.p < E.size()) {
            const size_t K = whole / bf;
            if (K > 0) {
                ViterbiDecoder v = S.vit;
                size_t nb = S.bits.size();
                if (K < S.blocks_fed) {
                    for (const auto& sn : S.snaps) if (sn.block == K) { v = sn.vit; nb = sn.bits; }
                }
                fbits.assign(S.bits.begin(), S.bits.begin() + static_cast<long>(nb));
                if (S.codec->uncoded()) fbits.resize(std::min(fbits.size(), K * S.ib));
                else for (uint8_t b : v.flush_decoder()) fbits.push_back(b);
            }
        } else {
            const size_t nframes = E.size() / S.p;
            if (nframes > S.blocks_fed * bf) stream_feed_block(S.blocks_fed * bf, nframes);
            fbits = S.bits;
            if (!S.codec->uncoded()) for (uint8_t b : S.vit.flush_decoder()) fbits.push_back(b);
        }
        std::vector<uint8_t> decoded = M110ACodec::bits_to_bytes(fbits);
        decoded.resize(std::min(decoded.size(), info_bytes(S.mcfg, whole)));
        const long eom_bit = cut_at_eom(decoded);
        stream_complete(std::move(decoded), eom_bit, sig, whole, blocks_seen);
    }

    /// Judge the message as decode() judges it and report its remaining blocks
    void stream_complete(std::vector<uint8_t> decoded, long eom_bit, size_t sig, size_t whole,
                         size_t blocks_seen) {
        StreamState& S = *st_;
        DecodeResult& result = S.result;
        const auto& E = S.eqv();
        const std::string other = S.cfg.mode == Mode::AUTO ? "" : preamble_contradicts(S.br, S.mode_id);
        judge(result, std::move(decoded), eom_bit, S.mcfg, S.mode_id, S.bf, sig, whole, blocks_seen,
              E, S.eq.probe_frames, S.cfg.equalizer == Equalizer::DFE, other);
        result.snr_db = estimate_snr_from_symbols(S.data);
        result.freq_offset_hz = S.pt ? S.pt->frequency() : S.br.freq_offset_hz;
        // the blocks of the message: up to the EOM's, or the whole blocks the signal covers
        const size_t pattern = static_cast<size_t>(S.p);
        size_t msg_frames = eom_bit >= 0 ? message_symbols(S.mcfg, eom_bit) / pattern : whole;
        msg_frames = std::min(msg_frames, E.size() / pattern);
        const size_t nb = std::min((msg_frames + S.bf - 1) / S.bf, record_blocks(result.data.size()));
        const bool quality_fail = result.quality_db < result.quality_threshold_db ||
                                  result.block_quality_min_db < result.block_quality_threshold_db;
        bool from_dfe = false;
        span_quality(E, S.mcfg, S.bf, S.eq.probe_frames, S.cfg.equalizer == Equalizer::DFE,
                     std::min<int>(quality_span(S.mcfg, eom_bit),
                                   static_cast<int>(eom_bit < 0 ? sig * pattern : E.size())), from_dfe);
        for (size_t b = S.emitted; b < nb; b++) {
            BlockRecord r = stream_record(b, result.data, b + 1 == nb, result.quality_db, from_dfe, msg_frames);
            if (!result.success && !quality_fail) { r.passes = false; r.below_quality = false; }
            r.eom = r.last && result.eom_detected;
            S.ready.push_back(std::move(r));
        }
        S.emitted = std::max(S.emitted, nb);
        if (result.success) { stats_.bytes_received += result.data.size(); stats_.frames_received++; }
        stats_.snr_db = result.snr_db;
        state_ = result.success ? RxState::COMPLETE : RxState::DECODE_ERROR;
        last_result_ = result;
        S.phase = StreamState::Phase::DONE;
    }

    /// What the stream does not follow: decode() over all the samples, its bytes reported
    /// as blocks of the mode's info bits, every block judged by its verdict
    void stream_fallback() {
        StreamState& S = *st_;
        DecodeResult result = decode_locked(S.raw);
        const bool quality_fail = result.quality_db < result.quality_threshold_db ||
                                  result.block_quality_min_db < result.block_quality_threshold_db;
        if (!S.sync_ready && result.mode != Mode::AUTO) {
            S.sync.mode = result.mode;
            S.sync.d1 = result.d1;
            S.sync.d2 = result.d2;
            S.sync.freq_offset_hz = result.freq_offset_hz;
            S.sync_ready = true;
        }
        if (!result.data.empty()) {
            const auto& m = ModeDatabase::get(api_to_internal_mode(result.mode));
            const size_t ib = std::max<size_t>(8, static_cast<size_t>(std::lround(m.bps * m.interleave_depth_sec)));
            const size_t nb = (result.data.size() * 8 + ib - 1) / ib;
            for (size_t b = 0; b < nb; b++) {
                BlockRecord r;
                r.index = b;
                const size_t from = ib * b / 8, to = b + 1 == nb ? result.data.size() : ib * (b + 1) / 8;
                r.data.assign(result.data.begin() + static_cast<long>(from), result.data.begin() + static_cast<long>(to));
                r.quality_db = result.quality_db;
                r.quality_threshold_db = result.quality_threshold_db;
                r.block_quality_db = b < result.block_quality_db.size() ? result.block_quality_db[b] : result.quality_db;
                r.block_quality_threshold_db = result.block_quality_threshold_db;
                r.passes = result.success;
                r.below_quality = !result.success && quality_fail;
                r.last = b + 1 == nb;
                r.eom = r.last && result.eom_detected;
                S.ready.push_back(std::move(r));
            }
        }
        S.result = result;
        S.phase = StreamState::Phase::DONE;
    }
    
};

// ============================================================
// ModemRX Public Interface
// ============================================================

ModemRX::ModemRX(const RxConfig& config)
    : impl_(std::make_unique<ModemRXImpl>(config)) {
}

ModemRX::~ModemRX() = default;

ModemRX::ModemRX(ModemRX&&) noexcept = default;
ModemRX& ModemRX::operator=(ModemRX&&) noexcept = default;

const RxConfig& ModemRX::config() const {
    return impl_->config();
}

Result<void> ModemRX::set_config(const RxConfig& config) {
    return impl_->set_config(config);
}

Result<void> ModemRX::set_mode(Mode mode) {
    return impl_->set_mode(mode);
}

Result<void> ModemRX::set_equalizer(Equalizer eq) {
    return impl_->set_equalizer(eq);
}

DecodeResult ModemRX::decode(const Samples& samples) {
    return impl_->decode(samples);
}

DecodeResult ModemRX::decode_file(const std::string& filename) {
    return impl_->decode_file(filename);
}

void ModemRX::start() {
    impl_->start();
}

Result<void> ModemRX::push_samples(const Samples& samples) {
    return impl_->push_samples(samples);
}

void ModemRX::stop() {
    impl_->stop();
}

RxState ModemRX::state() const {
    return impl_->state();
}

bool ModemRX::is_complete() const {
    return impl_->is_complete();
}

bool ModemRX::has_error() const {
    return impl_->has_error();
}

DecodeResult ModemRX::get_result() const {
    return impl_->get_result();
}

bool ModemRX::take_sync(StreamSync& sync) {
    return impl_->take_sync(sync);
}

std::vector<BlockRecord> ModemRX::take_blocks() {
    return impl_->take_blocks();
}

DecodeResult ModemRX::finish() {
    return impl_->finish();
}

Mode ModemRX::detected_mode() const {
    return impl_->detected_mode();
}

float ModemRX::snr() const {
    return impl_->snr();
}

float ModemRX::freq_offset() const {
    return impl_->freq_offset();
}

ModemStats ModemRX::stats() const {
    return impl_->stats();
}

void ModemRX::reset_stats() {
    impl_->reset_stats();
}

size_t ModemRX::samples_processed() const {
    return impl_->samples_processed();
}

bool ModemRX::signal_present(const Samples& samples, float threshold_db) {
    return ModemRXImpl::signal_present(samples, threshold_db);
}

float ModemRX::estimate_snr(const Samples& samples) {
    return ModemRXImpl::estimate_snr(samples);
}

} // namespace api
} // namespace m110a

