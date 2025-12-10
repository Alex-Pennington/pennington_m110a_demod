#ifndef M110A_BRAIN_DECODER_H
#define M110A_BRAIN_DECODER_H

/**
 * Brain Modem Compatible Decoder
 * 
 * Implements verified decode algorithm for MIL-STD-188-110A signals:
 * - RRC matched filtering with fine-grained timing
 * - Preamble correlation-based synchronization
 * - D1/D2 mode detection via Walsh correlation
 * - Compatible with Brain Modem reference implementation
 * 
 * Based on verified analysis of reference WAV files:
 * - 2400 baud rate for all modes
 * - 1800 Hz carrier
 * - 0.35 RRC alpha
 * - Sample-level timing optimization
 */

#include "common/types.h"
#include <cstdio>
#include <cstdlib>
#include "common/constants.h"
#include "m110a/brain_preamble.h"
#include "modem/scrambler.h"
#include "dsp/fir_filter.h"
#include "dsp/nco.h"
#include "sync/fft_afc.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace m110a {

/**
 * Brain Modem Decoder Configuration
 */
struct BrainDecoderConfig {
    float sample_rate = 48000.0f;
    float carrier_freq = 1800.0f;
    float baud_rate = 2400.0f;
    float rrc_alpha = 0.35f;
    int rrc_span = 6;  // symbols
    int max_search_symbols = 500;  // preamble search range
    float freq_search_range = 10.0f;  // Hz, search Â± this range for carrier
    float freq_search_step = 1.0f;    // Hz, step size for frequency search
    bool use_fft_coarse_afc = true;   // Enable two-stage AFC (FFT coarse + preamble fine)
    float coarse_search_range = 12.0f; // Hz, FFT coarse AFC search range
    float fine_search_range = 2.5f;    // Hz, preamble fine search range around coarse estimate
    bool verbose = false;
    
    // Mode-specific frame structure (default M2400S)
    int unknown_data_len = 32;  // Data symbols per mini-frame
    int known_data_len = 16;    // Probe symbols per mini-frame
    int preamble_symbols = 1440; // Preamble length in symbols (3 frames Ã— 480)
};

/**
 * Brain Modem Decode Result
 */
struct BrainDecodeResult {
    bool preamble_found = false;
    float correlation = 0.0f;
    float accuracy = 0.0f;  // Hard decision accuracy on preamble
    int start_sample = 0;
    float phase_offset = 0.0f;
    float freq_offset_hz = 0.0f;  // Detected frequency offset from nominal carrier
    
    // Mode detection
    int d1 = -1;
    int d2 = -1;
    float d1_corr = 0.0f;
    float d2_corr = 0.0f;
    std::string mode_name = "UNKNOWN";
    
    // Extracted symbols
    std::vector<complex_t> preamble_symbols;
    std::vector<complex_t> data_symbols;
    
    // Decoded data
    std::vector<uint8_t> data;
};

/**
 * Brain Modem Compatible Decoder
 */
class BrainDecoder {
public:
    explicit BrainDecoder(const BrainDecoderConfig& cfg = BrainDecoderConfig())
        : config_(cfg) {
        
        // Pre-compute samples per symbol
        sps_ = static_cast<int>(cfg.sample_rate / cfg.baud_rate);
        
        // Generate RRC filter
        rrc_taps_ = generate_srrc_taps(cfg.rrc_alpha, cfg.rrc_span, static_cast<float>(sps_));
        
        // Generate expected common preamble pattern (288 symbols)
        generate_common_pattern();
    }
    
    /**
     * Decode RF samples
     */
    BrainDecodeResult decode(const std::vector<float>& rf_samples) {
        BrainDecodeResult result;
        
        // Step 1: Two-stage AFC (if enabled)
        float coarse_freq_offset = 0.0f;
        float best_freq_offset = 0.0f;
        float best_preamble_corr = 0.0f;
        std::vector<complex_t> best_filtered;
        
        if (config_.use_fft_coarse_afc && config_.coarse_search_range > 0.0f) {
            // STAGE 1: Delay-multiply coarse frequency estimation
            // Do initial downconversion at nominal carrier frequency
            auto initial_filtered = downconvert_and_filter(rf_samples);
            if (!initial_filtered.empty() && initial_filtered.size() >= 288 * sps_) {
                // Setup delay-multiply coarse AFC
                CoarseAFC::Config afc_config;
                afc_config.sample_rate = config_.sample_rate;
                afc_config.baud_rate = config_.baud_rate;
                afc_config.search_range_hz = config_.coarse_search_range;
                afc_config.delay_samples = 10;         // 10 symbols delay
                afc_config.integration_symbols = 200;  // Integrate over 200 symbols
                afc_config.min_power_db = -20.0f;
                
                CoarseAFC coarse_afc(afc_config);
                
                // Estimate coarse frequency offset (no preamble needed)
                coarse_freq_offset = coarse_afc.estimate_frequency_offset(initial_filtered, 0);
                
                if (config_.verbose && coarse_freq_offset != 0.0f) {
                    printf("Delay-Multiply Coarse AFC: %.2f Hz\n", coarse_freq_offset);
                }
            }
            
            // STAGE 2: Fine preamble-based search around coarse estimate
            // Search Â±fine_search_range around the coarse estimate
            float search_start = coarse_freq_offset - config_.fine_search_range;
            float search_end = coarse_freq_offset + config_.fine_search_range;
            
            for (float freq_off = search_start; 
                 freq_off <= search_end; 
                 freq_off += config_.freq_search_step) {
                
                auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                if (filtered.size() < 288 * sps_) continue;
                
                float corr = quick_preamble_correlation(filtered, freq_off);
                
                if (corr > best_preamble_corr) {
                    best_preamble_corr = corr;
                    best_freq_offset = freq_off;
                    best_filtered = std::move(filtered);
                }
            }
            
            if (best_filtered.empty()) {
                // Fine search failed, try full range fallback
                if (config_.verbose) {
                    printf("Fine AFC failed, trying full range search\n");
                }
                for (float freq_off = -config_.freq_search_range; 
                     freq_off <= config_.freq_search_range; 
                     freq_off += config_.freq_search_step) {
                    
                    auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                    if (filtered.size() < 288 * sps_) continue;
                    
                    float corr = quick_preamble_correlation(filtered, freq_off);
                    
                    if (corr > best_preamble_corr) {
                        best_preamble_corr = corr;
                        best_freq_offset = freq_off;
                        best_filtered = std::move(filtered);
                    }
                }
            }
            
            result.freq_offset_hz = best_freq_offset;
            
        } else if (config_.freq_search_range > 0.0f) {
            // Legacy single-stage preamble-only AFC
            for (float freq_off = -config_.freq_search_range; 
                 freq_off <= config_.freq_search_range; 
                 freq_off += config_.freq_search_step) {
                
                auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                if (filtered.size() < 288 * sps_) continue;
                
                float corr = quick_preamble_correlation(filtered, freq_off);
                
                if (corr > best_preamble_corr) {
                    best_preamble_corr = corr;
                    best_freq_offset = freq_off;
                    best_filtered = std::move(filtered);
                }
            }
            
            if (best_filtered.empty()) {
                return result;  // No valid frequency found
            }
            
            result.freq_offset_hz = best_freq_offset;
        } else {
            // No frequency search
            best_filtered = downconvert_and_filter(rf_samples);
            if (best_filtered.empty() || best_filtered.size() < 288 * sps_) {
                return result;  // Signal too short
            }
        }

        if (best_filtered.empty()) {
            return result;  // AFC failed
        }

        auto& filtered = best_filtered;        // Step 2: Find preamble with fine-grained timing
        find_preamble(filtered, result);
        if (!result.preamble_found) {
            return result;
        }
        
        // Step 3: Extract preamble symbols
        extract_preamble_symbols(filtered, result);
        
        // Step 4: Detect mode from D1/D2
        detect_mode(filtered, result);
        
        // Step 5: Extract data symbols
        extract_data_symbols(filtered, result);
        
        return result;
    }
    
    /**
     * Get expected preamble pattern (for external use)
     */
    const std::vector<uint8_t>& common_pattern() const {
        return common_pattern_;
    }

private:
    BrainDecoderConfig config_;
    int sps_;  // Samples per symbol
    std::vector<float> rrc_taps_;
    std::vector<uint8_t> common_pattern_;  // 288-symbol expected pattern
    
    /**
     * Generate expected common preamble pattern
     */
    void generate_common_pattern() {
        common_pattern_.clear();
        int scram_idx = 0;
        
        for (int i = 0; i < 9; i++) {
            uint8_t d_val = brain::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = brain::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + brain::pscramble[scram_idx % 32]) % 8;
                common_pattern_.push_back(scrambled);
                scram_idx++;
            }
        }
    }
    
    /**
     * Downconvert to baseband and apply RRC matched filter
     */
    std::vector<complex_t> downconvert_and_filter(const std::vector<float>& rf_samples) {
        // Downconvert
        std::vector<complex_t> bb(rf_samples.size());
        float phase = 0.0f;
        float phase_inc = 2.0f * PI * config_.carrier_freq / config_.sample_rate;
        
        for (size_t i = 0; i < rf_samples.size(); i++) {
            bb[i] = complex_t(rf_samples[i] * std::cos(phase), 
                              -rf_samples[i] * std::sin(phase));
            phase += phase_inc;
            if (phase > 2*PI) phase -= 2*PI;
        }
        
        // Apply matched filter
        int half = rrc_taps_.size() / 2;
        std::vector<complex_t> filtered(bb.size());
        
        for (size_t i = 0; i < bb.size(); i++) {
            complex_t sum(0, 0);
            for (size_t j = 0; j < rrc_taps_.size(); j++) {
                int idx = static_cast<int>(i) - half + j;
                if (idx >= 0 && idx < static_cast<int>(bb.size())) {
                    sum += bb[idx] * rrc_taps_[j];
                }
            }
            filtered[i] = sum;
        }
        
        return filtered;
    }
    
    /**
     * Downconvert with frequency offset and apply RRC matched filter
     */
    std::vector<complex_t> downconvert_and_filter_with_offset(
            const std::vector<float>& rf_samples, float freq_offset_hz) {
        // Downconvert with offset
        std::vector<complex_t> bb(rf_samples.size());
        float phase = 0.0f;
        float phase_inc = 2.0f * PI * (config_.carrier_freq + freq_offset_hz) / config_.sample_rate;
        
        for (size_t i = 0; i < rf_samples.size(); i++) {
            bb[i] = complex_t(rf_samples[i] * std::cos(phase), 
                              -rf_samples[i] * std::sin(phase));
            phase += phase_inc;
            if (phase > 2*PI) phase -= 2*PI;
        }
        
        // Apply matched filter
        int half = rrc_taps_.size() / 2;
        std::vector<complex_t> filtered(bb.size());
        
        for (size_t i = 0; i < bb.size(); i++) {
            complex_t sum(0, 0);
            for (size_t j = 0; j < rrc_taps_.size(); j++) {
                int idx = static_cast<int>(i) - half + j;
                if (idx >= 0 && idx < static_cast<int>(bb.size())) {
                    sum += bb[idx] * rrc_taps_[j];
                }
            }
            filtered[i] = sum;
        }
        
        return filtered;
    }
    
    /**
     * Quick preamble correlation for frequency search
     * Returns correlation metric (higher = better frequency match)
     * Uses phase consistency across multiple segments of preamble
     * to discriminate between frequency candidates
     * 
     * @param filtered Baseband samples after downconversion with trial freq offset
     * @param trial_freq_offset_hz The trial frequency offset that was applied (Hz)
     * @return Metric value (higher = better match, strongly prefers correct frequency)
     */
    float quick_preamble_correlation(const std::vector<complex_t>& filtered, 
                                     float trial_freq_offset_hz = 0.0f) {
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
        
        // Generate expected preamble symbols
        const int SEGMENT_LEN = 72;  // Use 4 segments of 72 symbols each
        const int NUM_SEGMENTS = 4;
        std::vector<complex_t> expected;
        for (int i = 0; i < SEGMENT_LEN * NUM_SEGMENTS && i < (int)common_pattern_.size(); i++) {
            expected.push_back(PSK8[common_pattern_[i]]);
        }
        
        // Search for best correlation with multi-segment phase consistency
        float best_metric = 0.0f;
        int max_search = std::min((int)filtered.size() - SEGMENT_LEN * NUM_SEGMENTS * sps_, 200 * sps_);
        
        for (int start = 0; start < max_search; start += sps_ * 8) {  // Every 8 symbols
            // Compute correlation for each segment
            std::vector<complex_t> segment_corr(NUM_SEGMENTS);
            std::vector<float> segment_power(NUM_SEGMENTS, 0.0f);
            
            for (int seg = 0; seg < NUM_SEGMENTS; seg++) {
                complex_t corr(0, 0);
                float power = 0.0f;
                
                for (int i = 0; i < SEGMENT_LEN; i++) {
                    int idx = start + (seg * SEGMENT_LEN + i) * sps_;
                    if (idx < (int)filtered.size()) {
                        int pattern_idx = seg * SEGMENT_LEN + i;
                        corr += filtered[idx] * std::conj(expected[pattern_idx]);
                        power += std::norm(filtered[idx]);
                    }
                }
                
                segment_corr[seg] = corr;
                segment_power[seg] = power;
            }
            
            // Average correlation magnitude across segments
            float total_correlation = 0.0f;
            for (int seg = 0; seg < NUM_SEGMENTS; seg++) {
                total_correlation += std::abs(segment_corr[seg]) / std::sqrt(segment_power[seg] + 1e-10f);
            }
            float avg_correlation = total_correlation / NUM_SEGMENTS;
            
            // Simple metric - just use correlation
            // With two-stage AFC, we're already searching a narrow range
            // so frequency discrimination is less critical
            float metric = avg_correlation;            if (metric > best_metric) {
                best_metric = metric;
            }
        }
        
        return best_metric;
    }
    
    /**
     * Find preamble with sample-level timing optimization
     */
    void find_preamble(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        float best_corr = 0.0f;
        int best_start = 0;
        float best_phase = 0.0f;
        
        int max_search = std::min(
            static_cast<int>(filtered.size()) - 288 * sps_,
            config_.max_search_symbols * sps_
        );
        if (max_search < 0) return;
        
        // Use "first strong peak" detection to avoid false peaks from noise
        // Once we find correlation > threshold, refine locally and stop
        // Lower threshold (0.80) ensures we stop at first frame, not a slightly
        // higher peak at frame 1 (which would cause 1-frame timing error)
        const float early_stop_threshold = 0.80f;
        bool found_strong = false;
        
        // Search over sample positions
        for (int start = 0; start < max_search; start++) {
            // Compute correlation without rotation
            complex_t corr(0, 0);
            float power = 0;
            
            for (int i = 0; i < 288; i++) {
                int idx = start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                
                complex_t ref(brain::psk8_i[common_pattern_[i]], 
                              brain::psk8_q[common_pattern_[i]]);
                corr += filtered[idx] * std::conj(ref);
                power += std::norm(filtered[idx]);
            }
            
            float c = std::abs(corr) / std::sqrt(power * 288 + 0.0001f);
            if (c > best_corr) {
                best_corr = c;
                best_start = start;
                // Extract phase directly from correlation using atan2
                // This gives continuous phase estimate robust to quantization
                best_phase = -std::atan2(corr.imag(), corr.real());
                
                // Early termination: first strong peak wins
                // This prevents later spurious noise peaks from winning
                if (c > early_stop_threshold) {
                    found_strong = true;
                    // Search a small window around this peak to find true maximum
                    int local_end = std::min(start + sps_ * 2, max_search);
                    for (int s2 = start + 1; s2 < local_end; s2++) {
                        complex_t c2(0, 0);
                        float p2 = 0;
                        for (int i = 0; i < 288; i++) {
                            int idx = s2 + i * sps_;
                            if (idx >= static_cast<int>(filtered.size())) break;
                            complex_t ref(brain::psk8_i[common_pattern_[i]], 
                                          brain::psk8_q[common_pattern_[i]]);
                            c2 += filtered[idx] * std::conj(ref);
                            p2 += std::norm(filtered[idx]);
                        }
                        float corr2 = std::abs(c2) / std::sqrt(p2 * 288 + 0.0001f);
                        if (corr2 > best_corr) {
                            best_corr = corr2;
                            best_start = s2;
                            best_phase = -std::atan2(c2.imag(), c2.real());
                        }
                    }
                    break;  // Stop searching
                }
            }
        }
        
        result.correlation = best_corr;
        result.start_sample = best_start;
        result.phase_offset = best_phase;
        result.preamble_found = (best_corr > 0.7f);
        
        // Compute hard decision accuracy
        if (result.preamble_found) {
            complex_t rot(std::cos(best_phase), std::sin(best_phase));
            int matches = 0;
            
            for (int i = 0; i < 288; i++) {
                int idx = best_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                
                complex_t sym = filtered[idx] * rot;
                float ph = std::atan2(sym.imag(), sym.real());
                int rcv = ((static_cast<int>(std::round(ph * 4.0f / PI)) + 8) % 8);
                if (rcv == common_pattern_[i]) matches++;
            }
            
            result.accuracy = 100.0f * matches / 288.0f;
        }
    }
    
    /**
     * Extract preamble symbols for further processing
     */
    void extract_preamble_symbols(const std::vector<complex_t>& filtered, 
                                   BrainDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // Extract all 480 symbols of first preamble frame
        for (int i = 0; i < 480; i++) {
            int idx = result.start_sample + i * sps_;
            if (idx >= static_cast<int>(filtered.size())) break;
            result.preamble_symbols.push_back(filtered[idx] * rot);
        }
    }
    
    /**
     * Detect mode from D1/D2 patterns
     */
    void detect_mode(const std::vector<complex_t>& filtered, BrainDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // D1 starts at symbol 320, D2 at 352 (per MIL-STD-188-110A section 5.2.2)
        int d1_start = result.start_sample + 320 * sps_;
        int d2_start = result.start_sample + 352 * sps_;
        
        float best_d1_corr = 0.0f;
        float best_d2_corr = 0.0f;
        int d1 = 0, d2 = 0;
        
        for (int d = 0; d < 8; d++) {
            // D1 correlation
            complex_t corr1(0, 0);
            float pow1 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (brain::psymbol[d][i % 8] + brain::pscramble[i % 32]) % 8;
                int idx = d1_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(brain::psk8_i[pattern], brain::psk8_q[pattern]);
                corr1 += sym * std::conj(ref);
                pow1 += std::norm(filtered[idx]);
            }
            float c1 = std::abs(corr1) / std::sqrt(pow1 * 32 + 0.0001f);
            if (c1 > best_d1_corr) { best_d1_corr = c1; d1 = d; }
            
            // D2 correlation
            complex_t corr2(0, 0);
            float pow2 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (brain::psymbol[d][i % 8] + brain::pscramble[i % 32]) % 8;
                int idx = d2_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(brain::psk8_i[pattern], brain::psk8_q[pattern]);
                corr2 += sym * std::conj(ref);
                pow2 += std::norm(filtered[idx]);
            }
            float c2 = std::abs(corr2) / std::sqrt(pow2 * 32 + 0.0001f);
            if (c2 > best_d2_corr) { best_d2_corr = c2; d2 = d; }
        }
        
        result.d1 = d1;
        result.d2 = d2;
        result.d1_corr = best_d1_corr;
        result.d2_corr = best_d2_corr;
        
        // Look up mode name
        result.mode_name = lookup_mode_name(d1, d2);
    }
    
    /**
     * Look up mode name from D1/D2 values
     */
    std::string lookup_mode_name(int d1, int d2) {
        // Mode table from Brain Modem
        struct ModeEntry { int d1; int d2; const char* name; };
        static const ModeEntry modes[] = {
            {0, 0, "M75N"},    // 75bps no interleave (special case)
            {7, 4, "M150S"},
            {5, 4, "M150L"},
            {6, 7, "M300S"},
            {4, 7, "M300L"},
            {6, 6, "M600S"},
            {4, 6, "M600L"},
            {6, 5, "M1200S"},
            {4, 5, "M1200L"},
            {6, 4, "M2400S"},
            {4, 4, "M2400L"},
            {7, 6, "M4800S"},
        };
        
        for (const auto& m : modes) {
            if (m.d1 == d1 && m.d2 == d2) {
                return m.name;
            }
        }
        return "UNKNOWN";
    }
    
    /**
     * Extract data symbols (after preamble)
     */
    void extract_data_symbols(const std::vector<complex_t>& filtered,
                               BrainDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // Use configured preamble length (set by caller based on known mode)
        // This is critical for interoperability - don't rely on D1/D2 detection
        // which may not match the configured mode exactly
        int preamble_symbols = config_.preamble_symbols;
        
        // Data starts after preamble
        int data_start = result.start_sample + preamble_symbols * sps_;
        
        // Debug output for comparing modes
        std::cout << "[RX] extract_data: preamble_symbols=" << preamble_symbols
                  << " start_sample=" << result.start_sample
                  << " data_start=" << data_start 
                  << " filtered.size()=" << filtered.size()
                  << " sps=" << sps_ << std::endl;
        
        // Extract data symbols
        for (int idx = data_start; idx < static_cast<int>(filtered.size()); idx += sps_) {
            result.data_symbols.push_back(filtered[idx] * rot);
        }
        
        std::cout << "[RX] extracted " << result.data_symbols.size() << " data symbols" << std::endl;
    }
    
    /**
     * Descramble and demap data symbols to soft bits
     * 
     * Process:
     * 1. Apply descrambler (complex conjugate multiply)
     * 2. Find nearest constellation point
     * 3. Apply inverse Gray code
     * 4. Generate soft decisions
     */
    std::vector<float> descramble_and_demap(const std::vector<complex_t>& symbols,
                                             int unknown_len, int known_len) {
        std::vector<float> soft_bits;
        
        // Initialize scrambler
        RefScrambler scr;
        
        int pattern_len = unknown_len + known_len;
        int sym_idx = 0;
        
        while (sym_idx + pattern_len <= static_cast<int>(symbols.size())) {
            // Process unknown (data) symbols
            for (int i = 0; i < unknown_len; i++) {
                complex_t sym = symbols[sym_idx + i];
                uint8_t scr_val = scr.next_tribit();
                
                // Descramble: rotate by -scr_val * 45Â°
                float scr_phase = -scr_val * (PI / 4.0f);
                sym *= std::polar(1.0f, scr_phase);
                
                // Soft demap to 3 bits
                auto soft = soft_demap_8psk(sym);
                soft_bits.insert(soft_bits.end(), soft.begin(), soft.end());
            }
            
            // Skip known (probe) symbols - still need to advance scrambler
            for (int i = 0; i < known_len; i++) {
                scr.next_tribit();
            }
            
            sym_idx += pattern_len;
        }
        
        return soft_bits;
    }
    
    /**
     * Soft demap 8-PSK symbol to 3 soft bits
     * Uses inverse Gray code: position â†’ tribit
     */
    std::vector<float> soft_demap_8psk(complex_t sym) {
        // Inverse Gray code mapping (position to tribit)
        static const int inv_gray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        
        // Find angle and nearest position
        float angle = std::atan2(sym.imag(), sym.real());
        int pos = static_cast<int>(std::round(angle * 4.0f / PI));
        pos = ((pos % 8) + 8) % 8;
        
        // Get tribit from inverse Gray code
        int tribit = inv_gray[pos];
        
        // Calculate soft decisions based on distance
        float mag = std::abs(sym);
        float confidence = mag * 10.0f;  // Scale factor
        
        std::vector<float> soft(3);
        soft[0] = (tribit & 4) ? confidence : -confidence;
        soft[1] = (tribit & 2) ? confidence : -confidence;
        soft[2] = (tribit & 1) ? confidence : -confidence;
        
        return soft;
    }
};

} // namespace m110a

#endif // M110A_BRAIN_DECODER_H
