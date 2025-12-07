#ifndef M110A_CLI_RX_V2_H
#define M110A_CLI_RX_V2_H

/**
 * CLI Receiver V2 for M110A - With Probe Processing
 * 
 * Enhanced receiver that properly handles the frame structure:
 *   32 data symbols + 16 probe symbols = 48 per frame
 * 
 * Uses probe symbols for:
 *   - Channel estimation (amplitude/phase correction)
 *   - SNR estimation (soft bit scaling)
 *   - Fine frequency tracking
 * 
 * Architecture:
 *   RF → Downconvert → SRRC → Symbol Sample → Frame Process → Decode
 */

#include "common/types.h"
#include "common/constants.h"
#include "sync/freq_search_detector.h"
#include "channel/channel_estimator.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <vector>
#include <cmath>
#include <memory>
#include <iostream>

namespace m110a {

class CliRxV2 {
public:
    struct Config {
        float sample_rate;
        float symbol_rate;
        float carrier_freq;
        InterleaveMode interleave_mode;
        bool verbose;
        bool use_probes;           // Enable probe processing
        
        Config() 
            : sample_rate(SAMPLE_RATE_48K)
            , symbol_rate(SYMBOL_RATE)
            , carrier_freq(CARRIER_FREQ)
            , interleave_mode(InterleaveMode::SHORT)
            , verbose(false)
            , use_probes(true) {}
    };
    
    struct Result {
        bool success;
        std::vector<uint8_t> data;
        float freq_offset_hz;
        int symbols_decoded;
        int frames_decoded;
        float snr_db;
        float channel_amplitude;
        float channel_phase_deg;
        
        Result() 
            : success(false)
            , freq_offset_hz(0.0f)
            , symbols_decoded(0)
            , frames_decoded(0)
            , snr_db(0.0f)
            , channel_amplitude(1.0f)
            , channel_phase_deg(0.0f) {}
    };
    
    explicit CliRxV2(const Config& cfg) : config_(cfg) {}
    
    /**
     * Decode a complete PCM file with probe processing
     */
    Result decode(const std::vector<float>& rf_samples) {
        Result result;
        
        float sps = config_.sample_rate / config_.symbol_rate;
        int sps_int = static_cast<int>(sps);
        
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
            std::cerr << "Preamble: freq=" << sync.freq_offset_hz 
                      << " Hz, peak=" << sync.correlation_peak << "\n";
        }
        
        // Step 2: Calculate data start
        int preamble_symbols = (config_.interleave_mode == InterleaveMode::LONG) ? 11520 : 1440;
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
        int filter_delay = srrc_taps.size() - 1;
        int preamble_samples = static_cast<int>(preamble_symbols * sps) + srrc_taps.size();
        
        if (preamble_samples >= static_cast<int>(rf_samples.size())) {
            if (config_.verbose) std::cerr << "Not enough samples after preamble\n";
            return result;
        }
        
        // Step 3: Downconvert and filter
        NCO rx_nco(config_.sample_rate, -config_.carrier_freq - sync.freq_offset_hz);
        ComplexFirFilter rx_filter(srrc_taps);
        
        std::vector<complex_t> filtered;
        for (float s : rf_samples) {
            filtered.push_back(rx_filter.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        // Step 4: Sample ALL symbols (data + probes)
        int data_start = preamble_samples + filter_delay;
        
        std::vector<complex_t> all_symbols;
        for (int idx = data_start; idx < static_cast<int>(filtered.size()); idx += sps_int) {
            all_symbols.push_back(filtered[idx]);
        }
        
        if (all_symbols.empty()) return result;
        
        if (config_.verbose) {
            std::cerr << "Total symbols: " << all_symbols.size() << "\n";
        }
        
        // Step 5: Process frame by frame
        ChannelTracker tracker;
        std::vector<complex_t> data_symbols;
        std::vector<complex_t> compensated_data;
        
        int frame_count = 0;
        size_t sym_idx = 0;
        
        // Differential decode state
        complex_t prev(1.0f, 0.0f);
        
        while (sym_idx + FRAME_SYMBOLS <= all_symbols.size()) {
            // Extract one frame
            std::vector<complex_t> frame(
                all_symbols.begin() + sym_idx,
                all_symbols.begin() + sym_idx + FRAME_SYMBOLS);
            
            if (config_.use_probes) {
                // Process probes and compensate data
                std::vector<complex_t> frame_data;
                bool ok = tracker.process_frame(frame, frame_data);
                
                if (ok) {
                    // Use compensated data symbols
                    for (const auto& s : frame_data) {
                        compensated_data.push_back(s);
                    }
                    frame_count++;
                }
            } else {
                // Just take data symbols without compensation
                for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
                    compensated_data.push_back(frame[i]);
                }
                frame_count++;
            }
            
            sym_idx += FRAME_SYMBOLS;
        }
        
        // Handle any remaining partial frame (data only, no probes)
        while (sym_idx < all_symbols.size()) {
            compensated_data.push_back(all_symbols[sym_idx]);
            sym_idx++;
        }
        
        result.frames_decoded = frame_count;
        
        if (config_.use_probes) {
            result.snr_db = tracker.estimate().snr_db;
            result.channel_amplitude = tracker.estimate().amplitude;
            result.channel_phase_deg = tracker.estimate().phase_offset * 180.0f / PI;
            
            if (config_.verbose) {
                std::cerr << "Channel: amp=" << result.channel_amplitude
                          << ", phase=" << result.channel_phase_deg << "°"
                          << ", SNR=" << result.snr_db << " dB\n";
            }
        }
        
        // Step 6: Differential decode
        prev = complex_t(1.0f, 0.0f);
        std::vector<int> tribits;
        
        for (const auto& sym : compensated_data) {
            complex_t diff = sym * std::conj(prev);
            prev = sym;
            
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2 * PI;
            int tribit = (static_cast<int>(std::round(phase / (PI / 4)))) % 8;
            tribits.push_back(tribit);
        }
        
        result.symbols_decoded = tribits.size();
        
        // Step 7: Convert to soft bits with SNR scaling
        float soft_scale = 1.0f;
        if (config_.use_probes && result.snr_db > 0) {
            // Higher SNR = more confident soft bits
            soft_scale = std::min(4.0f, std::sqrt(std::pow(10.0f, result.snr_db / 10.0f) / 10.0f));
        }
        
        std::vector<soft_bit_t> soft_bits;
        for (int t : tribits) {
            // Base soft values
            int s2 = ((t >> 2) & 1) ? 64 : -64;
            int s1 = ((t >> 1) & 1) ? 64 : -64;
            int s0 = ((t >> 0) & 1) ? 64 : -64;
            
            // Scale by SNR confidence
            s2 = static_cast<int>(s2 * soft_scale);
            s1 = static_cast<int>(s1 * soft_scale);
            s0 = static_cast<int>(s0 * soft_scale);
            
            // Clip to valid range
            soft_bits.push_back(std::max(-127, std::min(127, s2)));
            soft_bits.push_back(std::max(-127, std::min(127, s1)));
            soft_bits.push_back(std::max(-127, std::min(127, s0)));
        }
        
        // Step 8: Deinterleave
        std::vector<soft_bit_t> deinterleaved;
        
        if (config_.interleave_mode == InterleaveMode::ZERO) {
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
        
        // Step 9: Viterbi decode
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        viterbi.decode_block(deinterleaved, decoded_bits, true);
        
        // Step 10: Descramble
        Scrambler scr(SCRAMBLER_INIT_DATA);
        for (auto& b : decoded_bits) {
            b ^= scr.next_bit();
        }
        
        // Step 11: Pack to bytes
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

#endif // M110A_CLI_RX_V2_H
