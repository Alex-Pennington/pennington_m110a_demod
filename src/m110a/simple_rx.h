#ifndef M110A_SIMPLE_RX_H
#define M110A_SIMPLE_RX_H

/**
 * Simplified M110A Receiver for Loopback Testing
 * 
 * This version uses fixed sampling (no timing recovery) for testing
 * the core signal processing chain. Works perfectly in loopback mode
 * where TX and RX are synchronized.
 * 
 * For real-world use, timing recovery needs to be properly implemented.
 */

#include "common/types.h"
#include "common/constants.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <vector>
#include <cmath>
#include <memory>

namespace m110a {

class SimpleRx {
public:
    struct Config {
        float sample_rate;
        float symbol_rate;
        float carrier_freq;
        InterleaveMode interleave_mode;
        
        // Default to 48kHz for hardware compatibility (SPS=20, integer)
        Config() : sample_rate(SAMPLE_RATE_48K), symbol_rate(SYMBOL_RATE), 
                   carrier_freq(CARRIER_FREQ), interleave_mode(InterleaveMode::ZERO) {}
    };
    
    explicit SimpleRx(const Config& config = Config{})
        : config_(config)
        , sps_(config.sample_rate / config.symbol_rate) {
        
        // Generate SRRC matched filter
        srrc_taps_ = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps_);
        filter_delay_ = srrc_taps_.size() - 1;  // Combined TX+RX delay
        
        // Initialize DSP components
        rx_nco_ = std::make_unique<NCO>(config_.sample_rate, -config_.carrier_freq);
        rx_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps_);
        
        // Initialize interleaver for deinterleaving
        BlockInterleaver::Config il_cfg;
        il_cfg.mode = config_.interleave_mode;
        il_cfg.data_rate = static_cast<int>(config_.symbol_rate);
        interleaver_ = std::make_unique<BlockInterleaver>(il_cfg);
        
        reset();
    }
    
    void reset() {
        rx_nco_->reset();
        rx_filter_->reset();
        filtered_.clear();
        sample_count_ = 0;
    }
    
    /**
     * Process RF samples and decode data
     * @param rf_samples Input RF samples at sample_rate
     * @param num_symbols Expected number of symbols (from TX)
     * @return Decoded bytes
     */
    std::vector<uint8_t> decode(const std::vector<float>& rf_samples, int num_symbols) {
        // Downconvert and match filter
        filtered_.clear();
        filtered_.reserve(rf_samples.size());
        
        for (float s : rf_samples) {
            complex_t bb = rx_nco_->mix(complex_t(s, 0));
            filtered_.push_back(rx_filter_->process(bb));
        }
        
        // Sample at optimal points
        std::vector<complex_t> rx_symbols;
        for (int sym = 0; sym < num_symbols; sym++) {
            int sample_idx = filter_delay_ + sym * static_cast<int>(sps_);
            if (sample_idx < static_cast<int>(filtered_.size())) {
                rx_symbols.push_back(filtered_[sample_idx]);
            }
        }
        
        // Differential decode
        complex_t prev(1.0f, 0.0f);
        std::vector<int> tribits;
        
        for (auto& sym : rx_symbols) {
            complex_t diff = sym * std::conj(prev);
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2 * PI;
            int tribit = (static_cast<int>(std::round(phase / (PI / 4)))) % 8;
            tribits.push_back(tribit);
            prev = sym;
        }
        
        // Convert to soft bits
        std::vector<soft_bit_t> soft_bits;
        for (int t : tribits) {
            soft_bits.push_back(((t >> 2) & 1) ? 127 : -127);
            soft_bits.push_back(((t >> 1) & 1) ? 127 : -127);
            soft_bits.push_back(((t >> 0) & 1) ? 127 : -127);
        }
        
        // Deinterleave soft bits
        auto deinterleaved = interleaver_->deinterleave_soft(soft_bits);
        
        // Viterbi decode
        ViterbiDecoder decoder;
        std::vector<uint8_t> decoded_bits;
        decoder.decode_block(deinterleaved, decoded_bits, true);
        
        // Descramble
        Scrambler rx_scr(SCRAMBLER_INIT_DATA);
        std::vector<uint8_t> descrambled;
        for (auto b : decoded_bits) {
            descrambled.push_back(b ^ rx_scr.next_bit());
        }
        
        // Pack to bytes
        std::vector<uint8_t> result;
        for (size_t i = 0; i + 7 < descrambled.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | descrambled[i + j];
            }
            result.push_back(byte);
        }
        
        return result;
    }

private:
    Config config_;
    float sps_;
    int filter_delay_;
    
    std::vector<float> srrc_taps_;
    std::unique_ptr<NCO> rx_nco_;
    std::unique_ptr<ComplexFirFilter> rx_filter_;
    std::unique_ptr<BlockInterleaver> interleaver_;
    
    std::vector<complex_t> filtered_;
    int sample_count_;
};

} // namespace m110a

#endif // M110A_SIMPLE_RX_H
