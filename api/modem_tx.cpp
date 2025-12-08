// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file modem_tx.cpp
 * @brief ModemTX implementation using M110ACodec
 * 
 * MS-DMT compatible transmitter.
 */

#include "api/modem_tx.h"
#include "modem/m110a_codec.h"
#include "m110a/mode_config.h"
#include "m110a/msdmt_preamble.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include <mutex>
#include <cmath>

namespace m110a {
namespace api {

// ============================================================
// Implementation Class
// ============================================================

class ModemTXImpl {
public:
    explicit ModemTXImpl(const TxConfig& config)
        : config_(config)
        , codec_(api_to_internal_mode(config.mode))
        , nco_(config.sample_rate, config.carrier_freq) {
        
        // Initialize RRC pulse shaping filter
        init_pulse_shaping();
    }
    
    const TxConfig& config() const { return config_; }
    
    Result<void> set_config(const TxConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = config.validate();
        if (!result.ok()) return result;
        config_ = config;
        codec_.set_mode(api_to_internal_mode(config.mode));
        nco_ = NCO(config.sample_rate, config.carrier_freq);
        init_pulse_shaping();
        return Result<void>();
    }
    
    Result<void> set_mode(Mode mode) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode == Mode::AUTO) {
            return Error(ErrorCode::INVALID_MODE, "AUTO not valid for TX");
        }
        config_.mode = mode;
        codec_.set_mode(api_to_internal_mode(mode));
        return Result<void>();
    }
    
    Result<Samples> encode(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (data.empty()) {
            return Error(ErrorCode::TX_DATA_EMPTY);
        }
        
        ModeId mode_id = api_to_internal_mode(config_.mode);
        
        // M75 modes not yet supported
        if (mode_id == ModeId::M75NS || mode_id == ModeId::M75NL) {
            return Error(ErrorCode::NOT_IMPLEMENTED, "M75 modes not yet supported");
        }
        
        Samples output;
        
        // Reset NCO phase for consistent output
        nco_ = NCO(config_.sample_rate, config_.carrier_freq);
        
        // Generate preamble if requested
        if (config_.include_preamble) {
            auto preamble_symbols = generate_preamble(mode_id);
            auto preamble_audio = config_.use_pulse_shaping ? 
                                  modulate_with_rrc(preamble_symbols) :
                                  modulate_simple(preamble_symbols);
            output.insert(output.end(), preamble_audio.begin(), preamble_audio.end());
        }
        
        // Encode data to symbols using M110ACodec with probes integrated
        auto symbols_with_probes = codec_.encode_with_probes(data);
        
        // Modulate to audio (with or without RRC pulse shaping)
        auto data_audio = config_.use_pulse_shaping ?
                          modulate_with_rrc(symbols_with_probes) :
                          modulate_simple(symbols_with_probes);
        output.insert(output.end(), data_audio.begin(), data_audio.end());
        
        // Generate EOM (End of Message) if requested
        if (config_.include_eom) {
            auto eom_symbols = generate_eom(mode_id, symbols_with_probes.size());
            auto eom_audio = config_.use_pulse_shaping ?
                             modulate_with_rrc(eom_symbols) :
                             modulate_simple(eom_symbols);
            output.insert(output.end(), eom_audio.begin(), eom_audio.end());
        }
        
        // Update stats
        stats_.bytes_transmitted += data.size();
        stats_.frames_transmitted++;
        
        return output;
    }
    
    Result<Samples> generate_preamble() {
        std::lock_guard<std::mutex> lock(mutex_);
        ModeId mode_id = api_to_internal_mode(config_.mode);
        auto symbols = generate_preamble(mode_id);
        return config_.use_pulse_shaping ? 
               modulate_with_rrc(symbols) :
               modulate_simple(symbols);
    }
    
    Result<Samples> generate_tone(float duration, float freq) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freq == 0.0f) freq = config_.carrier_freq;
        
        size_t num_samples = static_cast<size_t>(duration * config_.sample_rate);
        Samples output(num_samples);
        
        float phase = 0.0f;
        float phase_inc = 2.0f * M_PI * freq / config_.sample_rate;
        
        for (size_t i = 0; i < num_samples; i++) {
            output[i] = std::cos(phase) * config_.amplitude;
            phase += phase_inc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        
        return output;
    }
    
    ModemStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    
    void reset_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = ModemStats();
    }
    
    float calculate_duration(size_t data_bytes) const {
        ModeId mode_id = api_to_internal_mode(config_.mode);
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        // Rough calculation
        size_t data_bits = data_bytes * 8;
        size_t coded_bits = (mode_id == ModeId::M4800S) ? data_bits : data_bits * 2;
        size_t symbols = (coded_bits * mode_cfg.symbol_repetition) / mode_cfg.bits_per_symbol;
        
        float data_duration = static_cast<float>(symbols) / 2400.0f;
        
        if (config_.include_preamble) {
            data_duration += 0.6f;  // ~3 frames
        }
        
        return data_duration;
    }
    
    size_t max_data_size() const {
        return 65536;
    }

private:
    TxConfig config_;
    M110ACodec codec_;
    NCO nco_;
    ModemStats stats_;
    mutable std::mutex mutex_;
    
    // RRC pulse shaping
    std::vector<float> rrc_taps_;
    int sps_;  // Samples per symbol
    static constexpr float RRC_ALPHA = 0.35f;
    static constexpr int RRC_SPAN = 6;  // symbols each side
    
    void init_pulse_shaping() {
        sps_ = static_cast<int>(config_.sample_rate / 2400.0f);
        rrc_taps_ = generate_srrc_taps(RRC_ALPHA, RRC_SPAN, static_cast<float>(sps_));
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
            case Mode::M4800_LONG: return ModeId::M4800S;
            default: return ModeId::M2400S;
        }
    }
    
    std::vector<complex_t> generate_preamble(ModeId mode_id) {
        ::m110a::MSDMTPreambleEncoder encoder;
        
        // Map ModeId to mode_index for MSDMTPreambleEncoder
        int mode_index;
        bool is_long;
        
        switch (mode_id) {
            case ModeId::M75NS: mode_index = 0; is_long = false; break;
            case ModeId::M75NL: mode_index = 1; is_long = true; break;
            case ModeId::M150S: mode_index = 2; is_long = false; break;
            case ModeId::M150L: mode_index = 3; is_long = true; break;
            case ModeId::M300S: mode_index = 4; is_long = false; break;
            case ModeId::M300L: mode_index = 5; is_long = true; break;
            case ModeId::M600S: mode_index = 6; is_long = false; break;
            case ModeId::M600L: mode_index = 7; is_long = true; break;
            case ModeId::M1200S: mode_index = 8; is_long = false; break;
            case ModeId::M1200L: mode_index = 9; is_long = true; break;
            case ModeId::M2400S: mode_index = 10; is_long = false; break;
            case ModeId::M2400L: mode_index = 11; is_long = true; break;
            case ModeId::M4800S: mode_index = 17; is_long = false; break;
            default: mode_index = 10; is_long = false; break;
        }
        
        return encoder.encode(mode_index, is_long);
    }
    
    /**
     * Generate EOM (End of Message) marker
     * 
     * EOM consists of 4 flush frames with:
     * - Data portion: all zeros (tribit 0 → gray → scramble)
     * - Probe portion: normal scrambled probes
     * 
     * The scrambler continues from where data encoding left off,
     * which is why we need data_symbol_count to sync.
     * 
     * @param mode_id Current mode
     * @param data_symbol_count Number of data+probe symbols already sent
     * @return EOM symbols (4 frames)
     */
    std::vector<complex_t> generate_eom(ModeId mode_id, size_t data_symbol_count) {
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        int unknown_len = mode_cfg.unknown_data_len;
        int known_len = mode_cfg.known_data_len;
        
        // M75 modes have no probes/EOM structure
        if (unknown_len == 0 || known_len == 0) {
            return {};
        }
        
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
        
        // MGD3[0] = 0 - zero data maps to symbol 0 before scrambling
        static const int ZERO_GRAY = 0;
        
        // EOM = 4 flush frames
        static const int EOM_FRAMES = 4;
        
        int pattern_len = unknown_len + known_len;
        std::vector<complex_t> output;
        output.reserve(EOM_FRAMES * pattern_len);
        
        // Continue scrambler from where data encoding left off
        DataScramblerFixed scrambler;
        for (size_t i = 0; i < data_symbol_count; i++) {
            scrambler.next();
        }
        
        // Generate 4 flush frames
        for (int frame = 0; frame < EOM_FRAMES; frame++) {
            // Data portion: all zeros (tribit 0 → gray 0 → scrambled)
            for (int i = 0; i < unknown_len; i++) {
                int scr = scrambler.next();
                int sym_idx = (ZERO_GRAY + scr) & 7;
                output.push_back(PSK8[sym_idx]);
            }
            
            // Probe portion: scrambler only (same as normal probes)
            for (int i = 0; i < known_len; i++) {
                int sym_idx = scrambler.next();
                output.push_back(PSK8[sym_idx]);
            }
        }
        
        return output;
    }
    
    /**
     * Modulate symbols with RRC pulse shaping
     * 
     * Uses Square Root Raised Cosine (SRRC) pulse shaping for:
     * - Improved spectral efficiency
     * - ISI-free transmission when matched with RX filter
     * - MS-DMT compatibility
     */
    Samples modulate_with_rrc(const std::vector<complex_t>& symbols) {
        // Create upsampled baseband signal with impulses at symbol positions
        std::vector<complex_t> baseband(symbols.size() * sps_, complex_t(0, 0));
        
        for (size_t i = 0; i < symbols.size(); i++) {
            baseband[i * sps_] = symbols[i];
        }
        
        // Apply RRC pulse shaping filter
        std::vector<complex_t> shaped(baseband.size() + rrc_taps_.size() - 1, complex_t(0, 0));
        
        for (size_t i = 0; i < baseband.size(); i++) {
            if (std::abs(baseband[i]) > 1e-10f) {  // Only process non-zero samples
                for (size_t j = 0; j < rrc_taps_.size(); j++) {
                    shaped[i + j] += baseband[i] * rrc_taps_[j];
                }
            }
        }
        
        // Keep the full signal including filter tails
        // The RX will handle timing synchronization
        Samples output;
        output.reserve(shaped.size());
        
        for (const auto& sample : shaped) {
            auto carrier = nco_.next();
            float rf = sample.real() * carrier.real() - sample.imag() * carrier.imag();
            output.push_back(rf * config_.amplitude);
        }
        
        return output;
    }
    
    /**
     * Simple modulation without pulse shaping (for testing)
     */
    Samples modulate_simple(const std::vector<complex_t>& symbols) {
        Samples output;
        output.reserve(symbols.size() * sps_);
        
        for (const auto& sym : symbols) {
            for (int i = 0; i < sps_; i++) {
                auto carrier = nco_.next();
                float sample = sym.real() * carrier.real() - 
                              sym.imag() * carrier.imag();
                output.push_back(sample * config_.amplitude);
            }
        }
        
        return output;
    }
};

// ============================================================
// ModemTX Public Interface
// ============================================================

ModemTX::ModemTX(const TxConfig& config)
    : impl_(std::make_unique<ModemTXImpl>(config)) {
}

ModemTX::~ModemTX() = default;

ModemTX::ModemTX(ModemTX&&) noexcept = default;
ModemTX& ModemTX::operator=(ModemTX&&) noexcept = default;

const TxConfig& ModemTX::config() const {
    return impl_->config();
}

Result<void> ModemTX::set_config(const TxConfig& config) {
    return impl_->set_config(config);
}

Result<void> ModemTX::set_mode(Mode mode) {
    return impl_->set_mode(mode);
}

Result<Samples> ModemTX::encode(const std::vector<uint8_t>& data) {
    return impl_->encode(data);
}

Result<Samples> ModemTX::encode(const std::string& text) {
    std::vector<uint8_t> data(text.begin(), text.end());
    return impl_->encode(data);
}

Result<Samples> ModemTX::generate_preamble() {
    return impl_->generate_preamble();
}

Result<Samples> ModemTX::generate_tone(float duration, float freq) {
    return impl_->generate_tone(duration, freq);
}

ModemStats ModemTX::stats() const {
    return impl_->stats();
}

void ModemTX::reset_stats() {
    impl_->reset_stats();
}

float ModemTX::calculate_duration(size_t data_bytes) const {
    return impl_->calculate_duration(data_bytes);
}

size_t ModemTX::max_data_size() const {
    return impl_->max_data_size();
}

} // namespace api
} // namespace m110a
