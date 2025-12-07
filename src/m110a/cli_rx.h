#ifndef M110A_CLI_RX_H
#define M110A_CLI_RX_H

/**
 * CLI Receiver for M110A
 * 
 * Uses SimpleTx/SimpleRx approach with preamble detection.
 * Works with PCM files and handles the full decode chain.
 * 
 * Limitations:
 * - Does not process probe symbols (for channel estimation)
 * - Uses hard symbol timing (no adaptive timing recovery)
 * - Best for loopback/clean channel testing
 */

#include "common/types.h"
#include "common/constants.h"
#include "sync/freq_search_detector.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <vector>
#include <cmath>
#include <memory>
#include <functional>
#include <iostream>

namespace m110a {

class CliRx {
public:
    struct Config {
        float sample_rate;
        float symbol_rate;
        float carrier_freq;
        InterleaveMode interleave_mode;
        bool verbose;
        
        Config() : sample_rate(SAMPLE_RATE), symbol_rate(SYMBOL_RATE),
                   carrier_freq(CARRIER_FREQ), interleave_mode(InterleaveMode::SHORT),
                   verbose(false) {}
    };
    
    struct Result {
        bool success;
        std::vector<uint8_t> data;
        float freq_offset_hz;
        int symbols_decoded;
        
        Result() : success(false), freq_offset_hz(0.0f), symbols_decoded(0) {}
    };
    
    explicit CliRx(const Config& cfg) : config_(cfg) {}
    
    /**
     * Decode a complete PCM file
     * Returns decoded data and status
     */
    Result decode(const std::vector<float>& rf_samples) {
        Result result;
        
        float sps = config_.sample_rate / config_.symbol_rate;
        int sps_int = static_cast<int>(sps);  // Use integer SPS like SimpleTx/SimpleRx
        
        // Step 1: Detect preamble
        FreqSearchDetector::Config pd_cfg;
        pd_cfg.sample_rate = config_.sample_rate;
        pd_cfg.carrier_freq = config_.carrier_freq;
        pd_cfg.freq_search_range = 50.0f;
        pd_cfg.freq_step = 5.0f;
        pd_cfg.detection_threshold = 0.3f;
        
        FreqSearchDetector detector(pd_cfg);
        auto sync = detector.detect(rf_samples);
        
        if (!sync.acquired) {
            if (config_.verbose) {
                std::cerr << "No preamble detected\n";
            }
            return result;
        }
        
        result.freq_offset_hz = sync.freq_offset_hz;
        
        if (config_.verbose) {
            std::cerr << "Preamble detected: freq=" << sync.freq_offset_hz 
                      << " Hz, peak=" << sync.correlation_peak << "\n";
        }
        
        // Step 2: Calculate where data starts
        // Preamble is generated with fractional SPS, data with integer SPS
        int preamble_symbols = (config_.interleave_mode == InterleaveMode::LONG) ? 11520 : 1440;
        
        // M110A_Tx preamble uses fractional timing + filter flush
        // Calculate actual preamble length from sample count if possible
        // For now, estimate based on the detection offset
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
        int filter_delay = srrc_taps.size() - 1;
        
        // The preamble from M110A_Tx is about preamble_symbols * sps + filter_taps samples
        int preamble_samples = static_cast<int>(preamble_symbols * sps) + srrc_taps.size();
        
        if (config_.verbose) {
            std::cerr << "Estimated preamble end: " << preamble_samples << " samples\n";
        }
        
        if (preamble_samples >= static_cast<int>(rf_samples.size())) {
            if (config_.verbose) {
                std::cerr << "Not enough samples after preamble\n";
            }
            return result;
        }
        
        // Step 3: Downconvert and filter
        NCO rx_nco(config_.sample_rate, -config_.carrier_freq - sync.freq_offset_hz);
        ComplexFirFilter rx_filter(srrc_taps);
        
        std::vector<complex_t> filtered;
        for (float s : rf_samples) {
            filtered.push_back(rx_filter.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        // Step 4: Sample data symbols using INTEGER SPS (like SimpleRx)
        // Start after preamble, accounting for filter delay
        int data_start = preamble_samples + filter_delay;
        
        std::vector<complex_t> data_symbols;
        for (int sample_idx = data_start; sample_idx < static_cast<int>(filtered.size()); sample_idx += sps_int) {
            data_symbols.push_back(filtered[sample_idx]);
        }
        
        if (data_symbols.empty()) {
            return result;
        }
        
        if (config_.verbose) {
            std::cerr << "Data symbols: " << data_symbols.size() << "\n";
        }
        
        // Step 5: Differential decode (start with (1,0) like SimpleRx)
        complex_t prev(1.0f, 0.0f);
        std::vector<int> tribits;
        for (const auto& sym : data_symbols) {
            complex_t diff = sym * std::conj(prev);
            prev = sym;
            
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2 * PI;
            int tribit = (static_cast<int>(std::round(phase / (PI / 4)))) % 8;
            tribits.push_back(tribit);
        }
        
        result.symbols_decoded = tribits.size();
        
        // Step 6: Convert to soft bits
        std::vector<soft_bit_t> soft_bits;
        for (int t : tribits) {
            soft_bits.push_back(((t >> 2) & 1) ? 127 : -127);
            soft_bits.push_back(((t >> 1) & 1) ? 127 : -127);
            soft_bits.push_back(((t >> 0) & 1) ? 127 : -127);
        }
        
        // Step 7: Deinterleave (skip for ZERO mode)
        std::vector<soft_bit_t> deinterleaved;
        
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            // No interleaving - use soft bits directly
            deinterleaved = soft_bits;
        } else {
            BlockInterleaver::Config il_cfg;
            il_cfg.mode = config_.interleave_mode;
            il_cfg.data_rate = static_cast<int>(config_.symbol_rate);
            BlockInterleaver deint(il_cfg);
            
            int block_size = deint.block_size();
            if (static_cast<int>(soft_bits.size()) < block_size) {
                if (config_.verbose) {
                    std::cerr << "Not enough soft bits: " << soft_bits.size() 
                              << " < " << block_size << "\n";
                }
                return result;
            }
            
            std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.begin() + block_size);
            deinterleaved = deint.deinterleave_soft(block);
        }
        
        // Step 8: Viterbi decode
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        viterbi.decode_block(deinterleaved, decoded_bits, true);
        
        // Step 9: Descramble
        Scrambler scr(SCRAMBLER_INIT_DATA);
        for (auto& b : decoded_bits) {
            b ^= scr.next_bit();
        }
        
        // Step 10: Pack to bytes
        for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | decoded_bits[i + j];
            }
            result.data.push_back(byte);
        }
        
        result.success = !result.data.empty();
        return result;
    }

private:
    Config config_;
};

} // namespace m110a

#endif // M110A_CLI_RX_H
