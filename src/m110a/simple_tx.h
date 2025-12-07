#ifndef M110A_SIMPLE_TX_H
#define M110A_SIMPLE_TX_H

/**
 * Simplified M110A Transmitter for Loopback Testing
 * 
 * This version generates RF samples at a fixed rate (integer SPS)
 * for testing with SimpleRx.
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

class SimpleTx {
public:
    struct Config {
        float sample_rate;
        float symbol_rate;
        float carrier_freq;
        float output_amplitude;
        InterleaveMode interleave_mode;
        
        // Default to 48kHz for hardware compatibility (SPS=20, integer)
        Config() : sample_rate(SAMPLE_RATE_48K), symbol_rate(SYMBOL_RATE), 
                   carrier_freq(CARRIER_FREQ), output_amplitude(0.8f),
                   interleave_mode(InterleaveMode::ZERO) {}
    };
    
    struct TxResult {
        std::vector<float> rf_samples;
        int num_symbols;
    };
    
    explicit SimpleTx(const Config& config = Config{})
        : config_(config)
        , sps_(config.sample_rate / config.symbol_rate) {
        
        // Generate SRRC filter
        srrc_taps_ = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps_);
        
        // Initialize DSP components
        tx_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps_);
        tx_nco_ = std::make_unique<NCO>(config_.sample_rate, config_.carrier_freq);
        
        // Initialize interleaver
        BlockInterleaver::Config il_cfg;
        il_cfg.mode = config_.interleave_mode;
        il_cfg.data_rate = static_cast<int>(config_.symbol_rate);  // Coded bit rate
        interleaver_ = std::make_unique<BlockInterleaver>(il_cfg);
    }
    
    /**
     * Transmit a message
     * @param data Bytes to transmit
     * @return TxResult with RF samples and symbol count
     */
    TxResult transmit(const std::vector<uint8_t>& data) {
        TxResult result;
        
        // Reset components
        tx_filter_->reset();
        tx_nco_->reset();
        
        // Unpack to bits
        std::vector<uint8_t> data_bits;
        for (uint8_t byte : data) {
            for (int i = 7; i >= 0; i--) {
                data_bits.push_back((byte >> i) & 1);
            }
        }
        
        // Scramble
        Scrambler tx_scr(SCRAMBLER_INIT_DATA);
        std::vector<uint8_t> scrambled;
        for (uint8_t b : data_bits) {
            scrambled.push_back(b ^ tx_scr.next_bit());
        }
        
        // FEC encode
        ConvEncoder encoder;
        std::vector<uint8_t> coded;
        encoder.encode(scrambled, coded, true);
        
        // Interleave coded bits
        auto interleaved = interleaver_->interleave(coded);
        
        // Map to symbols
        SymbolMapper mapper;
        std::vector<complex_t> symbols;
        
        for (size_t i = 0; i + 2 < interleaved.size(); i += 3) {
            uint8_t tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
            symbols.push_back(mapper.map(tribit));
        }
        
        result.num_symbols = symbols.size();
        
        // Pulse shape and upconvert
        float gain = std::sqrt(sps_);
        int sps_int = static_cast<int>(sps_);
        
        for (auto& sym : symbols) {
            complex_t shaped = tx_filter_->process(sym * gain);
            result.rf_samples.push_back((shaped * tx_nco_->next()).real() * config_.output_amplitude);
            
            for (int i = 1; i < sps_int; i++) {
                shaped = tx_filter_->process(complex_t(0,0));
                result.rf_samples.push_back((shaped * tx_nco_->next()).real() * config_.output_amplitude);
            }
        }
        
        // Flush filter
        for (size_t i = 0; i < srrc_taps_.size(); i++) {
            complex_t shaped = tx_filter_->process(complex_t(0,0));
            result.rf_samples.push_back((shaped * tx_nco_->next()).real() * config_.output_amplitude);
        }
        
        return result;
    }

private:
    Config config_;
    float sps_;
    
    std::vector<float> srrc_taps_;
    std::unique_ptr<ComplexFirFilter> tx_filter_;
    std::unique_ptr<NCO> tx_nco_;
    std::unique_ptr<BlockInterleaver> interleaver_;
};

} // namespace m110a

#endif // M110A_SIMPLE_TX_H
