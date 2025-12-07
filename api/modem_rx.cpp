/**
 * @file modem_rx.cpp
 * @brief ModemRX implementation using MSDMTDecoder + M110ACodec
 * 
 * MS-DMT compatible receiver with optional DFE equalization.
 */

#include "api/modem_rx.h"
#include "m110a/msdmt_decoder.h"
#include "modem/m110a_codec.h"
#include "m110a/mode_config.h"
#include "equalizer/dfe.h"
#include "dsp/mlse_equalizer.h"
#include "dsp/phase_tracker.h"
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
    
    DecodeResult decode(const Samples& samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        state_ = RxState::SEARCHING;
        DecodeResult result;
        
        // Step 1: First pass - detect mode with default settings
        MSDMTDecoderConfig detect_cfg;
        detect_cfg.sample_rate = config_.sample_rate;
        detect_cfg.carrier_freq = config_.carrier_freq;
        detect_cfg.baud_rate = 2400.0f;
        
        MSDMTDecoder detector(detect_cfg);
        auto detect_result = detector.decode(samples);
        
        // Check mode detection
        if (detect_result.mode_name == "UNKNOWN" || detect_result.correlation < 0.5f) {
            result.success = false;
            result.error = Error(ErrorCode::RX_MODE_DETECT_FAILED, 
                                "Mode detection failed (corr=" + 
                                std::to_string(detect_result.correlation) + ")");
            state_ = RxState::ERROR;
            return result;
        }
        
        state_ = RxState::RECEIVING;
        
        // Convert mode name to ModeId
        ModeId mode_id = string_to_mode_id(detect_result.mode_name);
        result.mode = internal_to_api_mode(mode_id);
        
        // M75 modes not yet supported
        if (mode_id == ModeId::M75NS || mode_id == ModeId::M75NL) {
            result.success = false;
            result.error = Error(ErrorCode::NOT_IMPLEMENTED, "M75 modes not yet supported");
            state_ = RxState::ERROR;
            return result;
        }
        
        // Step 2: Get mode-specific settings and re-decode
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        MSDMTDecoderConfig decode_cfg;
        decode_cfg.sample_rate = config_.sample_rate;
        decode_cfg.carrier_freq = config_.carrier_freq;
        decode_cfg.baud_rate = 2400.0f;
        decode_cfg.unknown_data_len = mode_cfg.unknown_data_len;
        decode_cfg.known_data_len = mode_cfg.known_data_len;
        
        MSDMTDecoder decoder(decode_cfg);
        auto msdmt_result = decoder.decode(samples);
        
        // Check we got data symbols
        if (msdmt_result.data_symbols.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_NO_SIGNAL, "No data symbols extracted");
            state_ = RxState::ERROR;
            return result;
        }
        
        // Step 3: Apply phase tracking if enabled
        // Phase tracking corrects frequency offsets - only useful when no equalizer
        // (DFE/MLSE can handle small freq offsets via their adaptation)
        std::vector<complex_t> phase_corrected = msdmt_result.data_symbols;
        float freq_offset_hz = 0.0f;
        
        if (config_.phase_tracking && 
            config_.equalizer == Equalizer::NONE &&
            mode_cfg.unknown_data_len > 0 && mode_cfg.known_data_len > 0) {
            
            auto [corrected, freq_off] = apply_phase_tracking(
                msdmt_result.data_symbols,
                mode_cfg.unknown_data_len,
                mode_cfg.known_data_len,
                false  // Full tracking for NONE
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
                msdmt_result.preamble_symbols  // Use preamble for pretraining
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
                msdmt_result.preamble_symbols
            );
        }
        
        // Step 5: Decode using M110ACodec
        M110ACodec codec(mode_id);
        auto decoded = codec.decode_with_probes(equalized_symbols);
        
        if (decoded.empty()) {
            result.success = false;
            result.error = Error(ErrorCode::RX_DECODE_FAILED, "Viterbi decode failed");
            state_ = RxState::ERROR;
            return result;
        }
        
        result.success = true;
        result.data = decoded;
        result.snr_db = estimate_snr_from_symbols(msdmt_result.data_symbols);
        result.freq_offset_hz = freq_offset_hz;
        
        // Update stats
        stats_.bytes_received += decoded.size();
        stats_.frames_received++;
        stats_.snr_db = result.snr_db;
        
        state_ = RxState::COMPLETE;
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
        return state_ == RxState::ERROR;
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
    ModemStats stats_;
    DecodeResult last_result_;
    Samples streaming_samples_;
    size_t samples_processed_ = 0;
    mutable std::mutex mutex_;
    
    DecodeResult decode_internal(const Samples& samples) {
        // First pass - detect mode
        MSDMTDecoderConfig detect_cfg;
        detect_cfg.sample_rate = config_.sample_rate;
        detect_cfg.carrier_freq = config_.carrier_freq;
        detect_cfg.baud_rate = 2400.0f;
        
        MSDMTDecoder detector(detect_cfg);
        auto detect_result = detector.decode(samples);
        
        DecodeResult result;
        
        if (detect_result.mode_name == "UNKNOWN") {
            result.success = false;
            return result;
        }
        
        ModeId mode_id = string_to_mode_id(detect_result.mode_name);
        result.mode = internal_to_api_mode(mode_id);
        
        if (mode_id == ModeId::M75NS || mode_id == ModeId::M75NL) {
            result.success = false;
            return result;
        }
        
        // Second pass - decode with correct settings
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        MSDMTDecoderConfig decode_cfg;
        decode_cfg.sample_rate = config_.sample_rate;
        decode_cfg.carrier_freq = config_.carrier_freq;
        decode_cfg.baud_rate = 2400.0f;
        decode_cfg.unknown_data_len = mode_cfg.unknown_data_len;
        decode_cfg.known_data_len = mode_cfg.known_data_len;
        
        MSDMTDecoder decoder(decode_cfg);
        auto msdmt_result = decoder.decode(samples);
        
        M110ACodec codec(mode_id);
        auto decoded = codec.decode_with_probes(msdmt_result.data_symbols);
        
        if (!decoded.empty()) {
            result.success = true;
            result.data = decoded;
            result.snr_db = estimate_snr_from_symbols(msdmt_result.data_symbols);
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
            default: return Mode::AUTO;
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
     * Apply probe-aided channel equalization using existing DFE
     * 
     * Frame structure: [32 data][16 probes]
     * Strategy: 
     * 1. Pretrain DFE on preamble (known sequence)
     * 2. Use previous frame's probes to equalize current frame's data
     * 3. Update estimate with current frame's probes
     */
    std::vector<complex_t> apply_dfe_equalization(
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
        
        // Probes are tribit 0 -> MGD3[0] = 0
        static const int PROBE_GRAY = 0;
        
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
        
        // Configure DFE with more conservative settings
        DFE::Config dfe_cfg;
        dfe_cfg.ff_taps = 11;
        dfe_cfg.fb_taps = 5;
        dfe_cfg.mu_ff = 0.005f;   // Very conservative for fading channel
        dfe_cfg.mu_fb = 0.002f;
        dfe_cfg.leak = 0.0001f;
        
        DFE dfe(dfe_cfg);
        
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
                uint8_t d_val = p_c_seq[i];
                for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                    uint8_t base = psymbol[d_val][j % 8];
                    uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
                    preamble_ref.push_back(PSK8[scrambled]);
                    scram_idx++;
                }
            }
            
            // Train DFE on preamble in chunks
            // Use multiple passes for better convergence
            int chunk_size = 32;
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i + chunk_size <= pretrain_len; i += chunk_size) {
                    std::vector<complex_t> rx_chunk(
                        preamble_symbols.begin() + i,
                        preamble_symbols.begin() + i + chunk_size);
                    std::vector<complex_t> ref_chunk(
                        preamble_ref.begin() + i,
                        preamble_ref.begin() + i + chunk_size);
                    
                    dfe.train(rx_chunk, ref_chunk);
                }
            }
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
            
            // With preamble pretraining, DFE is already initialized
            // So we can equalize immediately on first frame
            if (frame == 0 && preamble_symbols.empty()) {
                // No preamble: train first, then equalize (old behavior)
                dfe.train(probe_in, probe_ref);
                dfe.equalize(data_in, output);
            } else {
                // With preamble or subsequent frames: equalize first, then update
                dfe.equalize(data_in, output);
                dfe.train(probe_in, probe_ref);
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
        pt_cfg.max_freq_hz = 5.0f;
        
        if (conservative) {
            // Very conservative: minimal tracking for use with equalizers
            // Only corrects gross frequency offset, doesn't track rapid changes
            pt_cfg.alpha = 0.01f;   // Very slow phase updates
            pt_cfg.beta = 0.0002f;  // Very slow frequency updates
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
                
                // Train for next frame
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
