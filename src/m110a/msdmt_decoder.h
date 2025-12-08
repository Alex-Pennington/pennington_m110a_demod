#ifndef M110A_MSDMT_DECODER_H
#define M110A_MSDMT_DECODER_H

/**
 * MS-DMT Compatible Decoder
 * 
 * Implements verified decode algorithm for MIL-STD-188-110A signals:
 * - RRC matched filtering with fine-grained timing
 * - Preamble correlation-based synchronization
 * - D1/D2 mode detection via Walsh correlation
 * - Compatible with MS-DMT reference implementation
 * 
 * Based on verified analysis of reference WAV files:
 * - 2400 baud rate for all modes
 * - 1800 Hz carrier
 * - 0.35 RRC alpha
 * - Sample-level timing optimization
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/msdmt_preamble.h"
#include "modem/scrambler.h"
#include "dsp/fir_filter.h"
#include "dsp/nco.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace m110a {

/**
 * MS-DMT Decoder Configuration
 */
struct MSDMTDecoderConfig {
    float sample_rate = 48000.0f;
    float carrier_freq = 1800.0f;
    float baud_rate = 2400.0f;
    float rrc_alpha = 0.35f;
    int rrc_span = 6;  // symbols
    int max_search_symbols = 500;  // preamble search range
    float freq_search_range = 10.0f;  // Hz, search ± this range for carrier
    float freq_search_step = 1.0f;    // Hz, step size for frequency search
    bool verbose = false;
    
    // Mode-specific frame structure (default M2400S)
    int unknown_data_len = 32;  // Data symbols per mini-frame
    int known_data_len = 16;    // Probe symbols per mini-frame
    int preamble_symbols = 1440; // Preamble length in symbols (3 frames × 480)
};

/**
 * MS-DMT Decode Result
 */
struct MSDMTDecodeResult {
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
 * MS-DMT Compatible Decoder
 */
class MSDMTDecoder {
public:
    explicit MSDMTDecoder(const MSDMTDecoderConfig& cfg = MSDMTDecoderConfig())
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
    MSDMTDecodeResult decode(const std::vector<float>& rf_samples) {
        MSDMTDecodeResult result;
        
        // Step 1: Frequency search - try multiple carrier offsets
        // Always search all frequencies and pick the best one
        float best_freq_offset = 0.0f;
        float best_preamble_corr = 0.0f;
        std::vector<complex_t> best_filtered;
        
        if (config_.freq_search_range > 0.0f) {
            // Search all frequencies including zero
            for (float freq_off = -config_.freq_search_range; 
                 freq_off <= config_.freq_search_range; 
                 freq_off += config_.freq_search_step) {
                
                auto filtered = downconvert_and_filter_with_offset(rf_samples, freq_off);
                if (filtered.size() < 288 * sps_) continue;
                
                float corr = quick_preamble_correlation(filtered);
                
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
            if (best_filtered.size() < 288 * sps_) {
                return result;  // Signal too short
            }
        }
        
        auto& filtered = best_filtered;
        
        // Step 2: Find preamble with fine-grained timing
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
    MSDMTDecoderConfig config_;
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
            uint8_t d_val = msdmt::p_c_seq[i];
            for (int j = 0; j < 32; j++) {
                uint8_t base = msdmt::psymbol[d_val][j % 8];
                uint8_t scrambled = (base + msdmt::pscramble[scram_idx % 32]) % 8;
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
     * Uses phase consistency between first and second half of preamble
     * to detect frequency offset
     */
    float quick_preamble_correlation(const std::vector<complex_t>& filtered) {
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
        const int HALF_LEN = 144;  // Use 2 halves of 144 symbols each
        std::vector<complex_t> expected;
        for (int i = 0; i < 2 * HALF_LEN && i < (int)common_pattern_.size(); i++) {
            expected.push_back(PSK8[common_pattern_[i]]);
        }
        
        // Search for best correlation with phase consistency check
        float best_metric = 0.0f;
        int max_search = std::min((int)filtered.size() - 2 * HALF_LEN * sps_, 200 * sps_);
        
        for (int start = 0; start < max_search; start += sps_ * 8) {  // Every 8 symbols
            // Compute correlation on first half
            complex_t corr1(0, 0);
            float power1 = 0.0f;
            for (int i = 0; i < HALF_LEN; i++) {
                int idx = start + i * sps_;
                if (idx < (int)filtered.size()) {
                    corr1 += filtered[idx] * std::conj(expected[i]);
                    power1 += std::norm(filtered[idx]);
                }
            }
            
            // Compute correlation on second half
            complex_t corr2(0, 0);
            float power2 = 0.0f;
            for (int i = HALF_LEN; i < 2 * HALF_LEN; i++) {
                int idx = start + i * sps_;
                if (idx < (int)filtered.size()) {
                    corr2 += filtered[idx] * std::conj(expected[i]);
                    power2 += std::norm(filtered[idx]);
                }
            }
            
            // Correlation magnitudes (how well preamble matches)
            float mag1 = std::abs(corr1) / std::sqrt(power1 + 1e-10f);
            float mag2 = std::abs(corr2) / std::sqrt(power2 + 1e-10f);
            
            // Phase difference between halves (should be ~0 if frequency is correct)
            // Frequency offset causes phase to drift: delta_phase = 2*pi*df*dt
            // For HALF_LEN=144 symbols at 2400 baud, dt = 60ms
            // At 1 Hz offset, delta_phase = 2*pi*1*0.06 = 21.6 degrees
            float phase1 = std::arg(corr1);
            float phase2 = std::arg(corr2);
            float phase_diff = std::abs(phase2 - phase1);
            if (phase_diff > M_PI) phase_diff = 2*M_PI - phase_diff;
            
            // Metric: high correlation AND small phase difference
            // Phase penalty: cos(phase_diff) ranges from 1 (0 diff) to -1 (180 diff)
            float phase_factor = std::cos(phase_diff);
            float metric = (mag1 + mag2) * 0.5f * std::max(0.0f, phase_factor);
            
            if (metric > best_metric) {
                best_metric = metric;
            }
        }
        
        return best_metric;
    }
    
    /**
     * Find preamble with sample-level timing optimization
     */
    void find_preamble(const std::vector<complex_t>& filtered, MSDMTDecodeResult& result) {
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
        const float early_stop_threshold = 0.90f;
        bool found_strong = false;
        
        // Search over sample positions
        for (int start = 0; start < max_search; start++) {
            // Compute correlation without rotation
            complex_t corr(0, 0);
            float power = 0;
            
            for (int i = 0; i < 288; i++) {
                int idx = start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                
                complex_t ref(msdmt::psk8_i[common_pattern_[i]], 
                              msdmt::psk8_q[common_pattern_[i]]);
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
                            complex_t ref(msdmt::psk8_i[common_pattern_[i]], 
                                          msdmt::psk8_q[common_pattern_[i]]);
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
                                   MSDMTDecodeResult& result) {
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
    void detect_mode(const std::vector<complex_t>& filtered, MSDMTDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // D1 starts at symbol 288, D2 at 320
        int d1_start = result.start_sample + 288 * sps_;
        int d2_start = result.start_sample + 320 * sps_;
        
        float best_d1_corr = 0.0f;
        float best_d2_corr = 0.0f;
        int d1 = 0, d2 = 0;
        
        for (int d = 0; d < 8; d++) {
            // D1 correlation
            complex_t corr1(0, 0);
            float pow1 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (msdmt::psymbol[d][i % 8] + msdmt::pscramble[(288 + i) % 32]) % 8;
                int idx = d1_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(msdmt::psk8_i[pattern], msdmt::psk8_q[pattern]);
                corr1 += sym * std::conj(ref);
                pow1 += std::norm(filtered[idx]);
            }
            float c1 = std::abs(corr1) / std::sqrt(pow1 * 32 + 0.0001f);
            if (c1 > best_d1_corr) { best_d1_corr = c1; d1 = d; }
            
            // D2 correlation
            complex_t corr2(0, 0);
            float pow2 = 0;
            for (int i = 0; i < 32; i++) {
                uint8_t pattern = (msdmt::psymbol[d][i % 8] + msdmt::pscramble[(320 + i) % 32]) % 8;
                int idx = d2_start + i * sps_;
                if (idx >= static_cast<int>(filtered.size())) break;
                complex_t sym = filtered[idx] * rot;
                complex_t ref(msdmt::psk8_i[pattern], msdmt::psk8_q[pattern]);
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
        // Mode table from MS-DMT
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
                               MSDMTDecodeResult& result) {
        complex_t rot(std::cos(result.phase_offset), std::sin(result.phase_offset));
        
        // Determine preamble length based on mode
        // Short interleave: 3 frames × 480 = 1440 symbols
        // Long interleave: 24 frames × 480 = 11520 symbols
        int preamble_symbols = 1440;  // Default to short
        if (result.mode_name.back() == 'L') {
            preamble_symbols = 11520;
        }
        
        // Data starts after preamble
        int data_start = result.start_sample + preamble_symbols * sps_;
        
        // Extract data symbols
        for (int idx = data_start; idx < static_cast<int>(filtered.size()); idx += sps_) {
            result.data_symbols.push_back(filtered[idx] * rot);
        }
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
                
                // Descramble: rotate by -scr_val * 45°
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
     * Uses inverse Gray code: position → tribit
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

#endif // M110A_MSDMT_DECODER_H
