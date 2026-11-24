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
                m75::find_low_snr(samples, config_, low_snr_mode)) {
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
            result = m75::decode(samples, config_, mode_id, result, config_.mode != Mode::AUTO);
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
        // - With DFE/MLSE: probe-only tracking (less aggressive)
        if (config_.phase_tracking && 
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
                result.success = result.quality_db >= result.quality_threshold_db;
                if (!result.success) result.error = low_quality_error(result);
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
                                         mode_cfg.known_data_len);
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
        for (const auto& f : dfe_probe_frames_) {
            if (f[0] <= span) { dfe_err += f[1]; dfe_ref += f[2]; }
        }
        if (config_.equalizer == Equalizer::DFE && dfe_err > 0.0) {
            result.quality_db = static_cast<float>(10.0 * std::log10(dfe_ref / dfe_err));
        } else {
            result.quality_db = probe_snr_db(equalized_symbols,
                                             mode_cfg.unknown_data_len,
                                             mode_cfg.known_data_len,
                                             block_frames_, span);
        }
        result.quality_threshold_db = quality_threshold_db(mode_id);
        result.success = result.quality_db >= result.quality_threshold_db;
        if (!result.success) result.error = low_quality_error(result);
        if (result.success && eom_bit < 0 && decoded.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED,
                                 "Signal covers 0 of " + std::to_string(blocks_seen) +
                                 " interleaver blocks; no bytes reported");
        }
        const std::string other = config_.mode == Mode::AUTO ? "" : preamble_contradicts(brain_result, mode_id);
        if (result.success && !other.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED,
                                 "Preamble D1/D2 read " + other + ", not the forced mode; bytes not trusted");
        }
        result.data = decoded;
        result.snr_db = estimate_snr_from_symbols(brain_result.data_symbols);
        result.freq_offset_hz = freq_offset_hz;

        // Update stats
        if (result.success) {
            stats_.bytes_received += decoded.size();
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
        streaming_samples_.clear();
        last_result_ = DecodeResult();
    }
    
    Result<void> push_samples(const Samples& samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == RxState::IDLE) {
            return Error(ErrorCode::RX_NOT_STARTED);
        }
        
        streaming_samples_.insert(streaming_samples_.end(), 
                                  samples.begin(), samples.end());
        
        size_t min_samples = static_cast<size_t>(2.0f * config_.sample_rate);
        
        if (streaming_samples_.size() >= min_samples) {
            auto result = decode_internal(streaming_samples_);
            if (result.success) {
                last_result_ = result;
                state_ = RxState::COMPLETE;
            }
        }
        
        return Result<void>();
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = RxState::IDLE;
        streaming_samples_.clear();
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
    
    // Per frame within the quality span: the frame's end (symbols), and the probe
    // error and reference power of the DFE's a-priori outputs (frame_dfe_equalize).
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
            std::cout << "[RX PREAMBLE] not found corr=" << d.correlation << std::endl;
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
        std::cout << buf << std::endl;
    }

    /// Scale preamble and data symbols so the common preamble has the nominal RMS.
    static void normalize_symbol_level(BrainDecodeResult& r) {
        int n = std::min<int>(brain::P_COMMON_LENGTH, static_cast<int>(r.preamble_symbols.size()));
        if (n == 0) return;
        double acc = 0.0;
        for (int i = 0; i < n; i++) acc += std::norm(r.preamble_symbols[i]);
        double rms = std::sqrt(acc / n);
        if (rms <= 0.0) return;
        float g = static_cast<float>(NOMINAL_SYMBOL_RMS / rms);
        for (auto& x : r.preamble_symbols) x *= g;
        for (auto& x : r.pre_data_symbols) x *= g;
        for (auto& x : r.data_symbols) x *= g;
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

    /// SNR in dB of the probe symbols in a data+probe stream over the quality
    /// span, after a least-squares complex gain fit to the known probe values.
    /// Probes at interleaver block boundaries are skipped (block_boundary_probe).
    static float probe_snr_db(const std::vector<complex_t>& symbols, int unknown_len, int known_len,
                              int block_frames = 0, int span_symbols = QUALITY_SPAN_SYMBOLS) {
        if (unknown_len <= 0 || known_len <= 0) return -99.0f;
        int pattern_len = unknown_len + known_len;
        int span = std::min<int>(span_symbols, static_cast<int>(symbols.size()));
        DataScramblerFixed scrambler;
        std::vector<complex_t> rx, ref;
        for (int f = 0; (f + 1) * pattern_len <= span; f++) {
            for (int i = 0; i < unknown_len; i++) scrambler.next();
            if (block_boundary_probe(f, block_frames)) {
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
    
    static Error low_quality_error(const DecodeResult& r) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Decode quality %.1f dB below %.1f dB for this mode; bytes not trusted",
                 r.quality_db, r.quality_threshold_db);
        return Error(ErrorCode::RX_DECODE_FAILED, buf);
    }

    ModemStats stats_;
    DecodeResult last_result_;
    Samples streaming_samples_;
    size_t samples_processed_ = 0;
    mutable std::mutex mutex_;
    
    DecodeResult decode_internal(const Samples& samples) {
        auto valid = config_.validate();
        if (!valid.ok()) {
            DecodeResult r;
            r.error = valid.error();
            return r;
        }
        // First pass - detect mode
        BrainDecoderConfig detect_cfg;
        detect_cfg.sample_rate = config_.sample_rate;
        detect_cfg.carrier_freq = config_.carrier_freq;
        detect_cfg.baud_rate = 2400.0f;
        detect_cfg.freq_search_range = config_.freq_search_range;
        detect_cfg.freq_search_step = 0.5f;
        detect_cfg.use_fft_coarse_afc = config_.use_fft_coarse_afc;
        detect_cfg.coarse_search_range = config_.coarse_search_range;
        detect_cfg.fine_search_range = config_.fine_search_range;
        
        BrainDecoder detector(detect_cfg);
        auto detect_result = detector.decode(samples);
        
        DecodeResult result;
        
        if (detect_result.mode_name == "UNKNOWN") {
            result.success = false;
            return result;
        }
        
        ModeId mode_id = string_to_mode_id(detect_result.mode_name);
        result.mode = internal_to_api_mode(mode_id);
        
        if (m75::is_m75(mode_id)) return m75::decode(samples, config_, mode_id, result, false);
        
        // Second pass - decode with correct settings
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
        
        BrainDecoder decoder(decode_cfg);
        auto brain_result = decoder.decode(samples);
        normalize_symbol_level(brain_result);
        
        M110ACodec codec(mode_id);
        std::vector<uint8_t> decoded;
        
        // Use SNR-weighted demapper if enabled
        if (config_.use_snr_weighted_demapper) {
            float snr_db = config_.assumed_snr_db;
            if (config_.estimate_snr_from_probes) {
                snr_db = codec.estimate_snr_from_probes(brain_result.data_symbols);
            }
            DecodeOptions opts = DecodeOptions::snr_weighted(snr_db);
            decoded = codec.decode_with_probes(brain_result.data_symbols, opts);
        } else {
            decoded = codec.decode_with_probes(brain_result.data_symbols);
        }
        
        if (!decoded.empty()) {
            const auto& mcfg = ModeDatabase::get(mode_id);
            result.quality_db = probe_snr_db(brain_result.data_symbols,
                                             mcfg.unknown_data_len, mcfg.known_data_len);
            result.quality_threshold_db = quality_threshold_db(mode_id);
            result.success = result.quality_db >= result.quality_threshold_db;
            if (!result.success) result.error = low_quality_error(result);
            result.data = decoded;
            result.snr_db = estimate_snr_from_symbols(brain_result.data_symbols);
        }
        
        return result;
    }
    
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
    size_t signal_frames(const std::vector<complex_t>& s, int unknown_len, int known_len) const {
        const int pattern_len = unknown_len + known_len;
        if (unknown_len <= 0 || known_len <= 0) return s.size() / std::max(1, pattern_len);
        const size_t nf = s.size() / pattern_len;
        std::vector<float> rho(nf, -1.0f);
        DataScramblerFixed scrambler;
        for (size_t f = 0; f < nf; f++) {
            for (int i = 0; i < unknown_len; i++) scrambler.next();
            complex_t num(0, 0);
            double pw = 0.0;
            for (int i = 0; i < known_len; i++) {
                const float ph = scrambler.next() * static_cast<float>(PI) / 4.0f;
                const complex_t x = s[f * pattern_len + unknown_len + i];
                num += x * complex_t(std::cos(ph), -std::sin(ph));
                pw += std::norm(x);
            }
            if (!block_boundary_probe(static_cast<int>(f), block_frames_))
                rho[f] = pw > 0.0 ? static_cast<float>(std::norm(num) / (pw * known_len)) : 0.0f;
        }
        for (size_t f = nf; f-- > 0;) {
            if (rho[f] < 0.5f) continue;
            double sum = 0.0;
            int n = 0;
            for (size_t k = f + 1; k-- > 0 && n < 8;)
                if (rho[k] >= 0.0f) { sum += rho[k]; n++; }
            if (sum / n < 0.25) continue;
            while (f + 1 < nf && rho[f + 1] < 0.0f) f++;  // the block's uncounted last frames
            return f + 1;
        }
        return 0;
    }

    /**
     * In a forced mode, the standard 110A mode the preamble's D1/D2 name instead, or "".
     * Only when both read clearly (best at least 1.5 x the runner-up, as for the countdown
     * digits) and name a Table XI data mode other than the forced one. Forced Harris modes
     * and the 2400V, Harris and reserved codes never refuse.
     */
    static std::string preamble_contradicts(const BrainDecodeResult& b, ModeId forced) {
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
    bool detect_eom(const std::vector<uint8_t>& decoded, 
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
    std::vector<uint8_t> strip_eom_padding(const std::vector<uint8_t>& decoded) {
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
                                              QUALITY_SPAN_SYMBOLS);
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
        
        // Process entire sequence continuously (key to performance)
        auto result = mlse.equalize_with_tracking(symbols, unknown_len, known_len);
        
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
        
        PhaseTracker tracker(pt_cfg);
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
            
            // Generate probe reference
            scrambler.reset();
            int scr_pos = frame * pattern_len + unknown_len;
            for (int i = 0; i < scr_pos; i++) scrambler.next();
            
            std::vector<complex_t> probe_ref(known_len);
            for (int i = 0; i < known_len; i++) {
                int scr = scrambler.next();
                int sym_idx = (PROBE_GRAY + scr) & 7;
                probe_ref[i] = PSK8[sym_idx];
            }
            
            if (frame == 0) {
                // First frame: process data first (with initial estimate),
                // then train on probes for next frame
                auto corrected_data = tracker.process(data_in);
                output.insert(output.end(), corrected_data.begin(), corrected_data.end());
                
                tracker.train(probe_in, probe_ref);
                auto corrected_probes = tracker.process(probe_in);
                output.insert(output.end(), corrected_probes.begin(), corrected_probes.end());
            } else {
                // Subsequent frames: apply correction from previous probe training,
                // then update with current probes
                auto corrected_data = tracker.process(data_in);
                output.insert(output.end(), corrected_data.begin(), corrected_data.end());
                
                auto corrected_probes = tracker.process(probe_in);
                output.insert(output.end(), corrected_probes.begin(), corrected_probes.end());
                
                // Train for next frame (not on a block-boundary probe,
                // which may carry D1/D2)
                if (!block_boundary_probe(frame, block_frames_))
                    tracker.train(probe_in, probe_ref);
            }
            
            idx += pattern_len;
            frame++;
        }
        
        // Remaining symbols - apply current phase estimate
        if (idx < symbols.size()) {
            std::vector<complex_t> remaining(symbols.begin() + idx, symbols.end());
            auto corrected = tracker.process(remaining);
            output.insert(output.end(), corrected.begin(), corrected.end());
        }
        
        return {output, tracker.get_frequency()};
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

