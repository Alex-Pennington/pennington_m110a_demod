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
#include "common/constants.h"
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
    }
    
    const TxConfig& config() const { return config_; }
    
    Result<void> set_config(const TxConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = config.validate();
        if (!result.ok()) return result;
        config_ = config;
        codec_.set_mode(api_to_internal_mode(config.mode));
        nco_ = NCO(config.sample_rate, config.carrier_freq);
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
        
        // Build complete symbol stream (preamble + data with probes)
        std::vector<complex_t> all_symbols;
        
        // Generate preamble if requested
        if (config_.include_preamble) {
            auto preamble_symbols = generate_preamble(mode_id);
            all_symbols.insert(all_symbols.end(), preamble_symbols.begin(), preamble_symbols.end());
        }
        
        // Encode data to symbols using M110ACodec with probes integrated
        auto data_symbols = codec_.encode_with_probes(data);
        all_symbols.insert(all_symbols.end(), data_symbols.begin(), data_symbols.end());
        
        // Modulate entire stream in one pass (continuous carrier phase)
        auto output = modulate_continuous(all_symbols);
        
        // Update stats
        stats_.bytes_transmitted += data.size();
        stats_.frames_transmitted++;
        
        return output;
    }
    
    Result<Samples> generate_preamble() {
        std::lock_guard<std::mutex> lock(mutex_);
        ModeId mode_id = api_to_internal_mode(config_.mode);
        auto symbols = generate_preamble(mode_id);
        return modulate_continuous(symbols);
    }
    
    Result<Samples> generate_tone(float duration, float freq) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freq == 0.0f) freq = config_.carrier_freq;
        
        size_t num_samples = static_cast<size_t>(duration * config_.sample_rate);
        Samples output(num_samples);
        
        float phase = 0.0f;
        float phase_inc = 2.0f * PI * freq / config_.sample_rate;
        
        for (size_t i = 0; i < num_samples; i++) {
            output[i] = std::cos(phase) * config_.amplitude;
            phase += phase_inc;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
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
        MSDMTPreambleEncoder encoder;
        
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
    
    Samples modulate_continuous(const std::vector<complex_t>& symbols) {
        // 2400 baud
        float sps = config_.sample_rate / 2400.0f;
        int sps_int = static_cast<int>(sps);
        
        // Create fresh NCO for each transmission
        NCO carrier(config_.sample_rate, config_.carrier_freq);
        
        // Simple modulation without pulse shaping (for debugging)
        // This should match what MSDMT decoder expects from reference files
        Samples output;
        output.reserve(symbols.size() * sps_int);
        
        for (const auto& sym : symbols) {
            for (int i = 0; i < sps_int; i++) {
                complex_t c = carrier.next();
                float sample = sym.real() * c.real() - sym.imag() * c.imag();
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
