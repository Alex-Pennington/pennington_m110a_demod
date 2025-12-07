#ifndef M110A_MULTIMODE_RX_H
#define M110A_MULTIMODE_RX_H

/**
 * Multi-Mode MIL-STD-188-110A Receiver
 * 
 * Supports all standard data rates from 75 bps to 4800 bps.
 * 
 * Signal chain:
 *   RF → Downconvert → Match Filter → Timing Recovery → Carrier Recovery
 *      → Demap → Deinterleave → Viterbi Decode → Descramble → Data
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/mode_config.h"
#include "m110a/mode_detector.h"
#include "modem/multimode_mapper.h"
#include "modem/multimode_interleaver.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "equalizer/dfe.h"
#include "dsp/mlse_equalizer.h"
#include "sync/freq_search_detector.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include <vector>
#include <memory>
#include <functional>
#include <iostream>

namespace m110a {

class MultiModeRx {
public:
    struct Config {
        ModeId mode;
        float sample_rate;
        float carrier_freq;
        float freq_search_range;   // Frequency search range in Hz (0 = no search)
        bool verbose;
        bool enable_dfe;           // Enable DFE equalizer
        bool enable_mlse;          // Enable MLSE equalizer (alternative to DFE)
        bool auto_detect;          // Auto-detect mode from D1/D2 preamble
        DFE::Config dfe_config;    // DFE parameters
        MLSEConfig mlse_config;    // MLSE parameters
        
        Config()
            : mode(ModeId::M2400S)
            , sample_rate(48000.0f)
            , carrier_freq(1800.0f)
            , freq_search_range(0.0f)  // Default no search for loopback
            , verbose(false)
            , enable_dfe(false)        // Default off for backward compatibility
            , enable_mlse(false)       // Default off
            , auto_detect(false) {}    // Default off - use specified mode
    };
    
    struct RxResult {
        bool success;
        std::vector<uint8_t> data;
        float freq_offset_hz;
        int symbols_decoded;
        int frames_decoded;
        float snr_estimate;
        bool mode_detected;        // True if auto-detection succeeded
        ModeId detected_mode;      // Detected mode (if auto_detect enabled)
        int d1_confidence;         // D1 detection confidence (0-96)
        int d2_confidence;         // D2 detection confidence (0-96)
        
        RxResult() : success(false), freq_offset_hz(0), symbols_decoded(0),
                     frames_decoded(0), snr_estimate(0), mode_detected(false),
                     detected_mode(ModeId::M2400S), d1_confidence(0), d2_confidence(0) {}
    };
    
    explicit MultiModeRx(const Config& cfg = Config{})
        : config_(cfg)
        , mode_cfg_(ModeDatabase::get(cfg.mode))
        , mapper_(mode_cfg_.modulation)
        , deinterleaver_(cfg.mode) {}
    
    void set_mode(ModeId mode) {
        config_.mode = mode;
        mode_cfg_ = ModeDatabase::get(mode);
        mapper_.set_modulation(mode_cfg_.modulation);
        deinterleaver_ = MultiModeInterleaver(mode);
    }
    
    const ModeConfig& mode_config() const { return mode_cfg_; }
    
    /**
     * Decode RF samples
     */
    RxResult decode(const std::vector<float>& rf_samples) {
        RxResult result;
        
        float sps = config_.sample_rate / mode_cfg_.symbol_rate;
        int sps_int = static_cast<int>(sps);
        
        // Preamble detection
        // Note: Frequency search during preamble is unreliable, so we detect at nominal
        // frequency and use probe-based AFC during data reception
        FreqSearchDetector::Config pd_cfg;
        pd_cfg.sample_rate = config_.sample_rate;
        pd_cfg.carrier_freq = config_.carrier_freq;
        pd_cfg.freq_search_range = 0.0f;  // Disable search - use probe AFC instead
        pd_cfg.freq_step = 5.0f;
        pd_cfg.detection_threshold = 0.08f;
        pd_cfg.confirmation_threshold = 0.08f;
        pd_cfg.required_peaks = 2;
        
        // segment_symbols is one preamble frame, not total preamble
        pd_cfg.segment_symbols = mode_cfg_.preamble_symbols() / mode_cfg_.preamble_frames;
        
        FreqSearchDetector detector(pd_cfg);
        auto sync = detector.detect(rf_samples);
        
        if (!sync.acquired) {
            if (config_.verbose) {
                std::cerr << "No preamble detected\n";
            }
            return result;
        }
        
        // Use detected frequency offset for AFC
        result.freq_offset_hz = sync.freq_offset_hz;
        
        if (config_.verbose) {
            std::cerr << "Preamble: freq_offset=" << sync.freq_offset_hz << " Hz\n";
        }
        
        // Calculate data start - skip preamble samples plus TX filter tail plus filter delays
        int preamble_syms = mode_cfg_.preamble_symbols();
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
        int filter_delay = (srrc_taps.size() - 1) / 2;  // Group delay of one filter
        int filter_taps = static_cast<int>(srrc_taps.size());
        
        // For auto-detect mode, use minimum preamble (3 frames = 1440 symbols)
        // We'll recalculate after detecting the actual mode
        if (config_.auto_detect) {
            preamble_syms = 3 * 480;  // Minimum 3 frames for SHORT interleave modes
        }
        
        // Preamble output includes: preamble_syms * sps + filter_taps (from TX filter flush)
        int preamble_output_samples = static_cast<int>(preamble_syms * sps) + filter_taps;
        
        // Data starts at: preamble_output + TX_filter_delay + RX_filter_delay
        // Both filters contribute to the total group delay
        int data_start = preamble_output_samples + 2 * filter_delay;
        
        // For auto-detect, don't bail out early - we'll recalculate after detection
        if (!config_.auto_detect && data_start >= static_cast<int>(rf_samples.size())) {
            return result;
        }
        
        // Downconvert and filter
        // Use detected frequency offset for carrier frequency correction
        // Note: For ideal loopback, freq_offset should be ~0 but preamble detector may report noise
        float applied_freq_offset = (config_.freq_search_range > 0) ? sync.freq_offset_hz : 0;
        NCO rx_nco(config_.sample_rate, -config_.carrier_freq - applied_freq_offset);
        ComplexFirFilter rx_filter(srrc_taps);
        
        std::vector<complex_t> filtered;
        for (float s : rf_samples) {
            filtered.push_back(rx_filter.process(rx_nco.mix(complex_t(s, 0))));
        }
        
        // Mode auto-detection from preamble D1/D2 sequences
        if (config_.auto_detect) {
            // Extract preamble symbols for mode detection
            // Preamble symbols start at 2*filter_delay (TX + RX filter group delays)
            int preamble_start = 2 * filter_delay;
            
            std::vector<complex_t> preamble_symbols;
            for (int idx = preamble_start; idx < preamble_output_samples && 
                 idx < static_cast<int>(filtered.size()); idx += sps_int) {
                preamble_symbols.push_back(filtered[idx]);
            }
            
            if (preamble_symbols.size() >= 576) {
                // Phase correction from first 20 preamble symbols
                Scrambler phase_scr(SCRAMBLER_INIT_PREAMBLE);
                MultiModeMapper phase_mapper(Modulation::PSK8);
                
                float phase_sum = 0.0f;
                int phase_count = 0;
                for (int i = 0; i < 20 && i < static_cast<int>(preamble_symbols.size()); i++) {
                    uint8_t expected_tribit = phase_scr.next_tribit();
                    complex_t expected_sym = phase_mapper.map(expected_tribit);
                    complex_t received_sym = preamble_symbols[i];
                    
                    float expected_angle = std::atan2(expected_sym.imag(), expected_sym.real());
                    float received_angle = std::atan2(received_sym.imag(), received_sym.real());
                    float diff = received_angle - expected_angle;
                    while (diff > M_PI) diff -= 2 * M_PI;
                    while (diff < -M_PI) diff += 2 * M_PI;
                    phase_sum += diff;
                    phase_count++;
                }
                
                if (phase_count > 0) {
                    float phase_offset = phase_sum / phase_count;
                    complex_t phase_corr = std::polar(1.0f, -phase_offset);
                    for (auto& sym : preamble_symbols) {
                        sym *= phase_corr;
                    }
                }
                
                // Detect mode
                ModeDetector detector;
                auto detect_result = detector.detect(preamble_symbols);
                
                result.mode_detected = detect_result.detected;
                result.detected_mode = detect_result.mode;
                result.d1_confidence = detect_result.d1_confidence;
                result.d2_confidence = detect_result.d2_confidence;
                
                if (detect_result.detected && 
                    detect_result.d1_confidence >= ModeDetector::min_confidence() &&
                    detect_result.d2_confidence >= ModeDetector::min_confidence()) {
                    
                    // Switch to detected mode
                    if (config_.verbose) {
                        std::cerr << "Mode detected: " << ModeDatabase::get(detect_result.mode).name
                                  << " (D1=" << detect_result.d1 << ", D2=" << detect_result.d2
                                  << ", conf=" << detect_result.d1_confidence << "/" 
                                  << detect_result.d2_confidence << ")\n";
                    }
                    
                    // Update mode configuration
                    mode_cfg_ = ModeDatabase::get(detect_result.mode);
                    mapper_.set_modulation(mode_cfg_.modulation);
                    deinterleaver_ = MultiModeInterleaver(detect_result.mode);
                    
                    // Recalculate data_start for new mode's preamble length
                    preamble_syms = mode_cfg_.preamble_symbols();
                    preamble_output_samples = static_cast<int>(preamble_syms * sps) + filter_taps;
                    data_start = preamble_output_samples + 2 * filter_delay;
                    
                    // Check if signal is long enough for detected mode
                    if (data_start >= static_cast<int>(filtered.size())) {
                        if (config_.verbose) {
                            std::cerr << "Signal too short for detected mode\n";
                        }
                        return result;
                    }
                }
            }
        }
        
        // Final size check (for non-auto-detect mode)
        if (data_start >= static_cast<int>(filtered.size())) {
            return result;
        }
        
        // Sample symbols at data_start
        std::vector<complex_t> all_symbols;
        for (int idx = data_start; idx < static_cast<int>(filtered.size()); idx += sps_int) {
            all_symbols.push_back(filtered[idx]);
        }
        
        if (config_.verbose) {
            std::cerr << "RX: all_symbols=" << all_symbols.size() 
                      << ", data_start=" << data_start 
                      << ", filtered=" << filtered.size() << "\n";
        }
        
        if (all_symbols.empty()) return result;
        
        // Process frames - extract data symbols, skip probe symbols
        int unknown_len = mode_cfg_.unknown_data_len;
        int known_len = mode_cfg_.known_data_len;
        int pattern_len = unknown_len + known_len;
        
        std::vector<complex_t> data_symbols;
        
        if (config_.enable_dfe && unknown_len > 0 && known_len > 0) {
            // === DFE EQUALIZATION PATH ===
            // Generate probe reference symbols (same as TX)
            std::vector<complex_t> probe_ref;
            Scrambler probe_scr_ref(SCRAMBLER_INIT_PREAMBLE);
            for (int i = 0; i < known_len; i++) {
                int sym_idx = probe_scr_ref.next_tribit();
                probe_ref.push_back(PSK8_CONSTELLATION[sym_idx]);
            }
            
            // Create DFE with configured parameters
            DFE dfe(config_.dfe_config);
            
            // Process each pattern: train on probes, equalize data
            size_t sym_idx = 0;
            int pattern_count = 0;
            
            while (sym_idx + pattern_len <= all_symbols.size()) {
                // Extract probe symbols for this pattern
                std::vector<complex_t> probe_in;
                for (int i = 0; i < known_len; i++) {
                    probe_in.push_back(all_symbols[sym_idx + unknown_len + i]);
                }
                
                // Regenerate probe reference for this pattern
                // (scrambler continues from where we left off)
                std::vector<complex_t> pattern_probe_ref;
                Scrambler pattern_scr(SCRAMBLER_INIT_PREAMBLE);
                // Advance scrambler to match pattern position
                for (int skip = 0; skip < pattern_count * known_len; skip++) {
                    pattern_scr.next_tribit();
                }
                for (int i = 0; i < known_len; i++) {
                    int idx = pattern_scr.next_tribit();
                    pattern_probe_ref.push_back(PSK8_CONSTELLATION[idx]);
                }
                
                // Train DFE on probe symbols
                float mse = dfe.train(probe_in, pattern_probe_ref);
                
                if (config_.verbose && pattern_count < 3) {
                    std::cerr << "DFE: pattern " << pattern_count 
                              << " MSE=" << mse 
                              << " converged=" << dfe.is_converged() << "\n";
                }
                
                // Equalize data symbols
                std::vector<complex_t> data_in;
                for (int i = 0; i < unknown_len; i++) {
                    data_in.push_back(all_symbols[sym_idx + i]);
                }
                
                dfe.equalize(data_in, data_symbols);
                
                sym_idx += pattern_len;
                result.frames_decoded++;
                pattern_count++;
            }
            
            if (config_.verbose) {
                auto mags = dfe.ff_tap_magnitudes();
                std::cerr << "DFE FF taps: ";
                for (float m : mags) std::cerr << m << " ";
                std::cerr << "\n";
            }
        } else if (config_.enable_mlse && unknown_len > 0 && known_len > 0) {
            // === MLSE EQUALIZATION PATH ===
            // Uses Viterbi algorithm to find ML symbol sequence given channel estimate
            
            // Create MLSE equalizer
            MLSEConfig mlse_cfg = config_.mlse_config;
            if (mlse_cfg.channel_memory == 0) {
                mlse_cfg.channel_memory = 3;  // Default L=3 for multipath
            }
            if (mlse_cfg.traceback_depth == 0) {
                mlse_cfg.traceback_depth = 20;
            }
            MLSEEqualizer mlse(mlse_cfg);
            
            // Process each pattern: estimate channel from probes, equalize data
            size_t sym_idx = 0;
            int pattern_count = 0;
            
            // For channel estimation, we need received probes and reference probes
            std::vector<complex_t> all_probe_rx;
            std::vector<complex_t> all_probe_ref;
            
            // First pass: collect initial probe symbols for channel estimation
            int init_patterns = std::min(3, static_cast<int>((all_symbols.size() / pattern_len)));
            Scrambler init_scr(SCRAMBLER_INIT_PREAMBLE);
            
            for (int p = 0; p < init_patterns; p++) {
                size_t pat_start = p * pattern_len;
                
                // Collect received probes
                for (int i = 0; i < known_len; i++) {
                    all_probe_rx.push_back(all_symbols[pat_start + unknown_len + i]);
                }
                
                // Generate reference probes
                for (int i = 0; i < known_len; i++) {
                    int idx = init_scr.next_tribit();
                    all_probe_ref.push_back(PSK8_CONSTELLATION[idx]);
                }
            }
            
            // Initial channel estimation from collected probes
            mlse.estimate_channel(all_probe_ref, all_probe_rx);
            
            if (config_.verbose) {
                auto ch = mlse.get_channel();
                std::cerr << "MLSE initial channel: ";
                for (const auto& h : ch) std::cerr << h << " ";
                std::cerr << "\n";
            }
            
            // Second pass: equalize all data using MLSE
            Scrambler eq_scr(SCRAMBLER_INIT_PREAMBLE);
            
            while (sym_idx + pattern_len <= all_symbols.size()) {
                // Every few patterns, update channel estimate from probes
                if (pattern_count > 0 && pattern_count % 5 == 0) {
                    // Re-estimate channel using recent probes
                    std::vector<complex_t> recent_probe_rx;
                    std::vector<complex_t> recent_probe_ref;
                    
                    Scrambler update_scr(SCRAMBLER_INIT_PREAMBLE);
                    // Advance to current pattern
                    for (int skip = 0; skip < (pattern_count - 2) * known_len; skip++) {
                        update_scr.next_tribit();
                    }
                    
                    // Collect last few patterns' probes
                    for (int p = std::max(0, pattern_count - 3); p < pattern_count; p++) {
                        size_t pat_start = p * pattern_len;
                        for (int i = 0; i < known_len; i++) {
                            if (pat_start + unknown_len + i < all_symbols.size()) {
                                recent_probe_rx.push_back(all_symbols[pat_start + unknown_len + i]);
                                int idx = update_scr.next_tribit();
                                recent_probe_ref.push_back(PSK8_CONSTELLATION[idx]);
                            }
                        }
                    }
                    
                    if (!recent_probe_rx.empty()) {
                        mlse.estimate_channel(recent_probe_ref, recent_probe_rx);
                    }
                }
                
                // Extract data symbols for this pattern
                std::vector<complex_t> data_in;
                for (int i = 0; i < unknown_len; i++) {
                    data_in.push_back(all_symbols[sym_idx + i]);
                }
                
                // Equalize using MLSE
                auto decoded_indices = mlse.equalize(data_in);
                
                // Convert symbol indices to complex symbols
                for (int idx : decoded_indices) {
                    if (idx >= 0 && idx < 8) {
                        data_symbols.push_back(PSK8_CONSTELLATION[idx]);
                    }
                }
                
                // Advance scrambler past this pattern's probes (for tracking)
                for (int i = 0; i < known_len; i++) {
                    eq_scr.next_tribit();
                }
                
                sym_idx += pattern_len;
                result.frames_decoded++;
                pattern_count++;
            }
            
            if (config_.verbose) {
                std::cerr << "MLSE: processed " << pattern_count << " patterns, "
                          << data_symbols.size() << " data symbols\n";
            }
        } else if (unknown_len > 0 && known_len > 0) {
            // === PROBE-BASED PHASE AND FREQUENCY ESTIMATION ===
            // Estimate carrier phase AND frequency offset using PROBE symbols
            
            // First pass: collect phase estimates at each probe pattern
            std::vector<float> pattern_phases;
            std::vector<int> pattern_positions;  // Symbol index of each pattern start
            
            Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
            
            int num_patterns = 0;
            for (size_t p = 0; (p + 1) * pattern_len <= all_symbols.size(); p++) {
                num_patterns++;
            }
            
            // Estimate phase for each pattern using its probe symbols
            for (int p = 0; p < num_patterns; p++) {
                int probe_start = p * pattern_len + unknown_len;
                float sum_sin = 0, sum_cos = 0;
                int count = 0;
                
                // Reset scrambler for each pattern
                Scrambler pattern_scr(SCRAMBLER_INIT_PREAMBLE);
                for (int skip = 0; skip < p * known_len; skip++) {
                    pattern_scr.next_tribit();  // Advance to this pattern's probe sequence
                }
                
                for (int i = 0; i < known_len && probe_start + i < static_cast<int>(all_symbols.size()); i++) {
                    complex_t sym = all_symbols[probe_start + i];
                    float mag = std::abs(sym);
                    if (mag < 0.01f) continue;
                    sym /= mag;
                    
                    int expected_idx = pattern_scr.next_tribit();
                    float expected_phase = expected_idx * (PI / 4.0f);
                    float received_phase = std::arg(sym);
                    
                    float diff = received_phase - expected_phase;
                    while (diff > PI) diff -= 2 * PI;
                    while (diff < -PI) diff += 2 * PI;
                    
                    sum_cos += std::cos(diff);
                    sum_sin += std::sin(diff);
                    count++;
                }
                
                if (count > 0) {
                    float phase = std::atan2(sum_sin, sum_cos);
                    pattern_phases.push_back(phase);
                    pattern_positions.push_back(p * pattern_len + unknown_len + known_len / 2);  // Mid-probe position
                }
            }
            
            // Estimate frequency offset from phase slope (if enough patterns)
            float freq_offset_rad_per_symbol = 0.0f;
            float phase_at_origin = 0.0f;
            
            if (pattern_phases.size() >= 2) {
                // Unwrap phases
                std::vector<float> unwrapped = pattern_phases;
                for (size_t i = 1; i < unwrapped.size(); i++) {
                    float diff = unwrapped[i] - unwrapped[i-1];
                    while (diff > PI) diff -= 2 * PI;
                    while (diff < -PI) diff += 2 * PI;
                    unwrapped[i] = unwrapped[i-1] + diff;
                }
                
                // Linear regression: phase = a + b * position
                float sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
                int n = static_cast<int>(unwrapped.size());
                
                for (int i = 0; i < n; i++) {
                    float x = static_cast<float>(pattern_positions[i]);
                    float y = unwrapped[i];
                    sum_x += x;
                    sum_y += y;
                    sum_xx += x * x;
                    sum_xy += x * y;
                }
                
                float denom = n * sum_xx - sum_x * sum_x;
                if (std::abs(denom) > 1e-6f) {
                    freq_offset_rad_per_symbol = (n * sum_xy - sum_x * sum_y) / denom;
                    phase_at_origin = (sum_y - freq_offset_rad_per_symbol * sum_x) / n;
                } else if (!pattern_phases.empty()) {
                    phase_at_origin = pattern_phases[0];
                }
                
                if (config_.verbose) {
                    float freq_hz = freq_offset_rad_per_symbol * mode_cfg_.symbol_rate / (2 * PI);
                    std::cerr << "RX: AFC estimate: " << freq_hz << " Hz offset, "
                              << "phase_0=" << phase_at_origin * 180.0f / PI << " deg"
                              << " (from " << n << " patterns)\n";
                }
                
                // Store AFC estimate in result
                result.freq_offset_hz = freq_offset_rad_per_symbol * mode_cfg_.symbol_rate / (2 * PI);
            } else if (!pattern_phases.empty()) {
                phase_at_origin = pattern_phases[0];
            }
            
            // Apply linear phase correction to all symbols
            for (size_t i = 0; i < all_symbols.size(); i++) {
                float phase_correction = phase_at_origin + freq_offset_rad_per_symbol * i;
                all_symbols[i] *= std::polar(1.0f, -phase_correction);
            }
            
            // Extract data symbols (skip probes)
            size_t sym_idx = 0;
            while (sym_idx + pattern_len <= all_symbols.size()) {
                for (int i = 0; i < unknown_len; i++) {
                    data_symbols.push_back(all_symbols[sym_idx + i]);
                }
                sym_idx += pattern_len;
                result.frames_decoded++;
            }
        } else {
            // 75bps modes: no probes, all symbols are data
            data_symbols = all_symbols;
            result.frames_decoded = 1;
        }
        
        result.symbols_decoded = data_symbols.size();
        
        int rep = mode_cfg_.symbol_repetition;
        int bps = mode_cfg_.bits_per_symbol;
        
        std::vector<soft_bit_t> soft_bits;
        Scrambler sym_scr(SCRAMBLER_INIT_DATA);  // Must match TX scrambler
        
        if (rep > 1) {
            // LOW RATE MODE: BPSK demapping with repetition combining
            soft_bits.reserve(data_symbols.size());
            
            for (const auto& sym : data_symbols) {
                float mag = std::abs(sym);
                complex_t norm_sym = (mag > 0.01f) ? sym / mag : complex_t(1.0f, 0.0f);
                
                // Get scrambler value - BPSK positions are at scr_val and (scr_val+4)%8
                int scr_val = sym_scr.next_tribit();
                int pos0 = scr_val % 8;
                int pos1 = (scr_val + 4) % 8;
                
                // Compute distances to both possible symbol positions
                float d0 = std::norm(norm_sym - PSK8_CONSTELLATION[pos0]);
                float d1 = std::norm(norm_sym - PSK8_CONSTELLATION[pos1]);
                
                // LLR: positive means bit=1, negative means bit=0
                float noise_var = 0.1f;
                float llr = (d0 - d1) / (2.0f * noise_var);
                int soft = static_cast<int>(llr * 32.0f);
                soft = std::max(-127, std::min(127, soft));
                
                soft_bits.push_back(static_cast<soft_bit_t>(soft));
            }
        } else {
            // HIGH RATE MODE: Native modulation demapping (QPSK/8PSK)
            soft_bits.reserve(data_symbols.size() * bps);
            
            for (const auto& sym : data_symbols) {
                float mag = std::abs(sym);
                complex_t norm_sym = (mag > 0.01f) ? sym / mag : complex_t(1.0f, 0.0f);
                
                // Get scrambler value to undo symbol rotation
                int scr_val = sym_scr.next_tribit();
                
                // Rotate back by -scr_val (equivalent to adding 8-scr_val)
                // This un-does the TX scrambler rotation
                float scr_phase = -scr_val * (PI / 4.0f);
                norm_sym *= std::polar(1.0f, scr_phase);
                
                // Soft demap using absolute PSK
                auto soft = mapper_.soft_demap_absolute(norm_sym, 0.1f);
                soft_bits.insert(soft_bits.end(), soft.begin(), soft.end());
            }
        }
        
        // Deinterleave
        int block_size = deinterleaver_.block_size();
        if (static_cast<int>(soft_bits.size()) < block_size) {
            if (config_.verbose) {
                std::cerr << "Not enough soft bits: " << soft_bits.size() 
                          << " < " << block_size << "\n";
            }
            return result;
        }
        
        std::vector<soft_bit_t> deinterleaved;
        for (size_t i = 0; i + block_size <= soft_bits.size(); i += block_size) {
            std::vector<soft_bit_t> block(soft_bits.begin() + i,
                                          soft_bits.begin() + i + block_size);
            auto dil = deinterleaver_.deinterleave(block);
            deinterleaved.insert(deinterleaved.end(), dil.begin(), dil.end());
        }
        
        // Combine repeated bits AFTER deinterleaving (only for low-rate modes)
        std::vector<soft_bit_t> unrepeated;
        
        if (rep > 1) {
            // Combine each set of rep*2 bits back to 2 bits
            for (size_t i = 0; i + rep * 2 <= deinterleaved.size(); i += rep * 2) {
                // First bit of pair
                int sum0 = 0;
                for (int r = 0; r < rep; r++) {
                    sum0 += deinterleaved[i + r * 2];
                }
                sum0 = std::max(-127, std::min(127, sum0 / rep));
                unrepeated.push_back(static_cast<soft_bit_t>(sum0));
                
                // Second bit of pair
                int sum1 = 0;
                for (int r = 0; r < rep; r++) {
                    sum1 += deinterleaved[i + r * 2 + 1];
                }
                sum1 = std::max(-127, std::min(127, sum1 / rep));
                unrepeated.push_back(static_cast<soft_bit_t>(sum1));
            }
        } else {
            unrepeated = deinterleaved;
        }
        
        // Viterbi decode
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        viterbi.decode_block(unrepeated, decoded_bits, true);
        
        // Descramble
        Scrambler scr(SCRAMBLER_INIT_DATA);
        for (auto& b : decoded_bits) {
            b ^= scr.next_bit();
        }
        
        // Pack to bytes
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
    ModeConfig mode_cfg_;
    MultiModeMapper mapper_;
    MultiModeInterleaver deinterleaver_;
};

} // namespace m110a

#endif // M110A_MULTIMODE_RX_H
