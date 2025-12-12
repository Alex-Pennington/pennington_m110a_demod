#ifndef M110A_MULTICHANNEL_AFC_H
#define M110A_MULTICHANNEL_AFC_H

/**
 * Multi-Channel AFC for MIL-STD-188-110A
 * 
 * Implements Brain Core's proven ±80 Hz frequency acquisition approach:
 * 
 * Stage 1: Three parallel correlation channels at -50, 0, +50 Hz offsets
 *          Pick channel with best preamble correlation
 * 
 * Stage 2: Fine frequency estimation within winning channel using
 *          phase rotation between preamble segments (±31 Hz range)
 * 
 * Total acquisition range: ±81 Hz (vs ±15 Hz with FFT-based approach)
 * 
 * Based on Brain Core rxm110a.cpp find_frequency_error_and_start_of_preamble()
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/brain_preamble.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace m110a {

/**
 * Multi-Channel AFC Configuration
 */
struct MultiChannelAFCConfig {
    float sample_rate = 48000.0f;
    float baud_rate = 2400.0f;
    float channel_spacing_hz = 50.0f;  // Spacing between parallel channels
    int num_channels = 3;               // -50, 0, +50 Hz
    float fine_resolution_hz = 0.5f;    // Final frequency resolution
    float max_freq_offset_hz = 81.0f;   // Maximum detectable offset
    bool verbose = false;
};

/**
 * AFC Result
 */
struct AFCResult {
    bool success = false;
    float freq_offset_hz = 0.0f;
    float correlation = 0.0f;
    int winning_channel = 0;  // -1, 0, +1 for lo, on, hi
    int start_sample = 0;
};

/**
 * Multi-Channel Automatic Frequency Control
 * 
 * Three-channel parallel correlator with fine frequency estimation.
 * Matches Brain Core's implementation for ±80 Hz acquisition range.
 */
class MultiChannelAFC {
public:
    explicit MultiChannelAFC(const MultiChannelAFCConfig& cfg = MultiChannelAFCConfig())
        : config_(cfg) {
        
        sps_ = static_cast<int>(cfg.sample_rate / cfg.baud_rate);
        
        // Pre-compute frequency offset in radians per sample for each channel
        // Channel spacing: typically 50 Hz
        float offset_rad = 2.0f * PI * cfg.channel_spacing_hz / cfg.sample_rate;
        channel_offsets_[0] = -offset_rad;  // Low channel (-50 Hz)
        channel_offsets_[1] = 0.0f;          // On-frequency channel
        channel_offsets_[2] = +offset_rad;   // High channel (+50 Hz)
        
        // Generate reference preamble (common 288 symbols)
        generate_reference_preamble();
    }
    
    /**
     * Estimate frequency offset from baseband samples
     * 
     * @param filtered Baseband samples (after downconversion to nominal carrier)
     * @return AFCResult with frequency offset estimate
     */
    AFCResult estimate(const std::vector<complex_t>& filtered) {
        AFCResult result;
        
        if (filtered.size() < 288 * sps_ + sps_ * 100) {
            return result;  // Too short
        }
        
        // Stage 1: Parallel channel correlation
        // Search each channel for best preamble correlation
        
        struct ChannelResult {
            float correlation = 0.0f;
            int start_sample = 0;
            float fine_freq_offset = 0.0f;
        };
        
        std::array<ChannelResult, 3> channel_results;
        
        // Generate frequency-shifted versions of the input
        std::array<std::vector<complex_t>, 3> shifted;
        for (int ch = 0; ch < 3; ch++) {
            shifted[ch] = frequency_shift(filtered, channel_offsets_[ch]);
        }
        
        // Correlate each channel
        int search_range = std::min(static_cast<int>(filtered.size()) - 288 * sps_, 
                                    200 * sps_);  // Search first 200 symbols
        
        for (int ch = 0; ch < 3; ch++) {
            auto [corr, start] = find_best_correlation(shifted[ch], search_range);
            channel_results[ch].correlation = corr;
            channel_results[ch].start_sample = start;
        }
        
        // Pick winning channel
        int best_ch = 0;
        float best_corr = channel_results[0].correlation;
        for (int ch = 1; ch < 3; ch++) {
            if (channel_results[ch].correlation > best_corr) {
                best_corr = channel_results[ch].correlation;
                best_ch = ch;
            }
        }
        
        if (best_corr < 0.5f) {
            // No preamble found in any channel
            return result;
        }
        
        // Stage 2: Fine frequency estimation within winning channel
        // Use phase rotation between preamble segments
        int start = channel_results[best_ch].start_sample;
        float fine_offset = calculate_fine_frequency_error(shifted[best_ch], start);
        
        // Total frequency offset = channel offset + fine offset
        float channel_hz = (best_ch - 1) * config_.channel_spacing_hz;  // -50, 0, +50
        float total_offset_hz = channel_hz + fine_offset;
        
        // Reject if outside valid range
        if (std::abs(total_offset_hz) > config_.max_freq_offset_hz) {
            if (config_.verbose) {
                std::cerr << "[AFC] Rejected: offset " << total_offset_hz 
                          << " Hz exceeds max " << config_.max_freq_offset_hz << " Hz\n";
            }
            return result;
        }
        
        result.success = true;
        result.freq_offset_hz = total_offset_hz;
        result.correlation = best_corr;
        result.winning_channel = best_ch - 1;  // -1, 0, +1
        result.start_sample = start;
        
        if (config_.verbose) {
            std::cerr << "[AFC] Channel " << (best_ch - 1) * 50 << " Hz won, "
                      << "fine=" << fine_offset << " Hz, "
                      << "total=" << total_offset_hz << " Hz, "
                      << "corr=" << best_corr << "\n";
        }
        
        return result;
    }
    
    /**
     * Re-estimate with extended search (5 channels)
     * Use if initial 3-channel search fails
     * Extends to ±100 Hz range with 5 channels
     */
    AFCResult estimate_extended(const std::vector<complex_t>& filtered) {
        // Try standard 3-channel first
        auto result = estimate(filtered);
        if (result.success) return result;
        
        // Extended search: add ±100 Hz channels
        float offset_rad = 2.0f * PI * config_.channel_spacing_hz * 2.0f / config_.sample_rate;
        
        std::array<std::vector<complex_t>, 2> extended_shifted;
        extended_shifted[0] = frequency_shift(filtered, -offset_rad);  // -100 Hz
        extended_shifted[1] = frequency_shift(filtered, +offset_rad);  // +100 Hz
        
        int search_range = std::min(static_cast<int>(filtered.size()) - 288 * sps_, 
                                    200 * sps_);
        
        for (int ch = 0; ch < 2; ch++) {
            auto [corr, start] = find_best_correlation(extended_shifted[ch], search_range);
            
            if (corr > 0.5f) {
                float fine_offset = calculate_fine_frequency_error(extended_shifted[ch], start);
                float channel_hz = (ch == 0) ? -100.0f : +100.0f;
                float total_offset_hz = channel_hz + fine_offset;
                
                if (std::abs(total_offset_hz) <= 125.0f) {  // Extended max
                    result.success = true;
                    result.freq_offset_hz = total_offset_hz;
                    result.correlation = corr;
                    result.winning_channel = (ch == 0) ? -2 : +2;
                    result.start_sample = start;
                    
                    if (config_.verbose) {
                        std::cerr << "[AFC] Extended channel " << channel_hz << " Hz won, "
                                  << "total=" << total_offset_hz << " Hz\n";
                    }
                    return result;
                }
            }
        }
        
        return result;  // Still failed
    }

private:
    MultiChannelAFCConfig config_;
    int sps_;
    std::array<float, 3> channel_offsets_;  // Radians per sample
    std::vector<complex_t> ref_preamble_;   // Reference preamble symbols
    
    /**
     * Generate reference preamble (288 symbols, first 9 segments)
     * Uses Brain Core's known preamble pattern
     */
    void generate_reference_preamble() {
        ref_preamble_.clear();
        ref_preamble_.reserve(288);
        
        int scram_idx = 0;
        for (int i = 0; i < 9; i++) {
            uint8_t d_val = brain::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = brain::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                
                // Convert to complex symbol
                ref_preamble_.push_back(complex_t(
                    brain::psk8_i[scrambled],
                    brain::psk8_q[scrambled]
                ));
                scram_idx++;
            }
        }
    }
    
    /**
     * Apply frequency shift to signal
     * Equivalent to Brain Core's translate_seq_in_freq()
     */
    std::vector<complex_t> frequency_shift(const std::vector<complex_t>& in, 
                                            float delta_rad_per_sample) {
        if (std::abs(delta_rad_per_sample) < 1e-10f) {
            return in;  // No shift needed
        }
        
        std::vector<complex_t> out(in.size());
        float acc = 0.0f;
        
        for (size_t i = 0; i < in.size(); i++) {
            complex_t osc(std::cos(acc), -std::sin(acc));
            out[i] = in[i] * osc;
            acc -= delta_rad_per_sample;
            
            // Wrap phase
            if (acc >= 2.0f * PI) acc -= 2.0f * PI;
            if (acc <= -2.0f * PI) acc += 2.0f * PI;
        }
        
        return out;
    }
    
    /**
     * Find best preamble correlation in signal
     * Returns (correlation, start_sample)
     */
    std::pair<float, int> find_best_correlation(const std::vector<complex_t>& signal,
                                                 int search_range) {
        float best_corr = 0.0f;
        int best_start = 0;
        
        // Correlate at symbol rate (every sps_ samples)
        for (int start = 0; start < search_range; start += sps_) {
            float corr = correlate_preamble(signal, start);
            
            if (corr > best_corr) {
                best_corr = corr;
                best_start = start;
                
                // Early exit if very strong correlation found
                if (corr > 0.90f) {
                    // Refine locally
                    for (int s = std::max(0, start - sps_); 
                         s <= std::min(search_range, start + sps_); s++) {
                        float c2 = correlate_preamble(signal, s);
                        if (c2 > best_corr) {
                            best_corr = c2;
                            best_start = s;
                        }
                    }
                    break;
                }
            }
        }
        
        return {best_corr, best_start};
    }
    
    /**
     * Correlate signal against reference preamble at given start position
     * Returns normalized correlation magnitude
     * 
     * Uses segmented correlation like Brain Core's correlate_common_preamble()
     */
    float correlate_preamble(const std::vector<complex_t>& signal, int start) {
        if (start + 288 * sps_ > static_cast<int>(signal.size())) {
            return 0.0f;
        }
        
        // Correlate in 9 segments of 32 symbols for better off-frequency detection
        float total_mag = 0.0f;
        
        for (int seg = 0; seg < 9; seg++) {
            complex_t corr(0.0f, 0.0f);
            float power = 0.0f;
            
            for (int i = 0; i < 32; i++) {
                int sym_idx = seg * 32 + i;
                int sample_idx = start + sym_idx * sps_;
                
                // Subsample correlation (every 2 samples like Brain Core)
                corr += signal[sample_idx] * std::conj(ref_preamble_[sym_idx]);
                power += std::norm(signal[sample_idx]);
            }
            
            // Accumulate normalized segment magnitude
            total_mag += std::abs(corr) / std::sqrt(power + 1e-10f);
        }
        
        // Normalize by number of segments and symbol count
        return total_mag / (9.0f * std::sqrt(32.0f));
    }
    
    /**
     * Calculate fine frequency error using phase rotation between segments
     * 
     * Based on Brain Core's calculate_frequency_error():
     * - Descramble against expected pattern
     * - Measure phase rotation over 32-symbol intervals
     * - Average to get frequency estimate
     * 
     * Range: approximately ±31 Hz
     */
    float calculate_fine_frequency_error(const std::vector<complex_t>& signal, int start) {
        if (start + 192 * sps_ > static_cast<int>(signal.size())) {
            return 0.0f;
        }
        
        // Descramble first 192 symbols against expected pattern
        std::vector<complex_t> descrambled(192);
        for (int i = 0; i < 192; i++) {
            int sample_idx = start + i * sps_;
            descrambled[i] = signal[sample_idx] * std::conj(ref_preamble_[i]);
        }
        
        // Calculate phase rotation over 32-symbol segments
        // This gives frequency error with 32 symbol integration
        complex_t sum(0.0f, 0.0f);
        
        for (int i = 0; i < 160; i++) {
            sum += descrambled[i] * std::conj(descrambled[i + 32]);
        }
        
        // Phase change over 32 symbols
        float phase_delta = std::atan2(sum.imag(), sum.real());
        
        // Convert to frequency: delta_phase / (32 symbols * symbol_period)
        // = delta_phase * baud_rate / 32 / (2*pi)
        float freq_hz = phase_delta * config_.baud_rate / (32.0f * 2.0f * PI);
        
        return freq_hz;
    }
};

} // namespace m110a

#endif // M110A_MULTICHANNEL_AFC_H
