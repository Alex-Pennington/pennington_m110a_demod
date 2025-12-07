#ifndef M110A_MULTIMODE_TX_H
#define M110A_MULTIMODE_TX_H

/**
 * Multi-Mode MIL-STD-188-110A Transmitter
 * 
 * Supports all standard data rates from 75 bps to 4800 bps.
 * 
 * Signal chain:
 *   Data → FEC Encode → Interleave → Scramble → PSK Map
 *        → Insert Probes → Prepend Preamble → Pulse Shape → Upconvert
 */

#include "common/types.h"
#include "common/constants.h"
#include "m110a/mode_config.h"
#include "m110a/msdmt_preamble.h"
#include "modem/multimode_mapper.h"
#include "modem/multimode_interleaver.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include <vector>
#include <memory>
#include <iostream>

namespace m110a {

class MultiModeTx {
public:
    struct Config {
        ModeId mode;
        float sample_rate;
        float carrier_freq;
        float output_amplitude;
        bool verbose;
        
        Config() 
            : mode(ModeId::M2400S)
            , sample_rate(48000.0f)
            , carrier_freq(1800.0f)
            , output_amplitude(0.8f)
            , verbose(false) {}
    };
    
    struct TxResult {
        std::vector<float> rf_samples;
        int num_symbols;
        int num_data_bits;
        float duration_sec;
    };
    
    explicit MultiModeTx(const Config& cfg = Config{})
        : config_(cfg)
        , mode_cfg_(ModeDatabase::get(cfg.mode))
        , mapper_(mode_cfg_.modulation)
        , interleaver_(cfg.mode) {
        
        initialize();
    }
    
    void set_mode(ModeId mode) {
        config_.mode = mode;
        mode_cfg_ = ModeDatabase::get(mode);
        mapper_.set_modulation(mode_cfg_.modulation);
        interleaver_ = MultiModeInterleaver(mode);
    }
    
    const ModeConfig& mode_config() const { return mode_cfg_; }
    
    /**
     * Transmit data with preamble
     * 
     * MIL-STD-188-110A specifies:
     * - Preamble is ALWAYS at 2400 baud (8PSK)
     * - Data is at mode-specific symbol rate
     */
    TxResult transmit(const std::vector<uint8_t>& data) {
        TxResult result;
        
        // Generate preamble symbols (at 2400 baud)
        auto preamble = generate_preamble();
        
        // Encode data symbols (at mode symbol rate)
        auto data_symbols = encode_data(data);
        
        // Create carrier NCO for continuous phase
        NCO carrier(config_.sample_rate, config_.carrier_freq);
        
        // Modulate preamble at 2400 baud
        auto preamble_samples = modulate_with_carrier(preamble, 2400.0f, carrier);
        
        // Modulate data at mode symbol rate (carrier phase continues)
        auto data_samples = modulate_with_carrier(data_symbols, mode_cfg_.symbol_rate, carrier);
        
        // Combine
        result.rf_samples = std::move(preamble_samples);
        result.rf_samples.insert(result.rf_samples.end(), 
                                 data_samples.begin(), data_samples.end());
        
        result.num_symbols = preamble.size() + data_symbols.size();
        result.num_data_bits = data.size() * 8;
        result.duration_sec = result.rf_samples.size() / config_.sample_rate;
        
        return result;
    }
    
    /**
     * Generate preamble with D1/D2 mode identification
     * 
     * Per MIL-STD-188-110A, preamble structure per frame (480 symbols):
     *   Frame 1:
     *     - Symbols 0-287:   Scrambled sync
     *     - Symbols 288-335: D1 (mode ID, 48 symbols)
     *     - Symbols 336-383: D1 repeated
     *     - Symbols 384-479: Scrambled sync
     *   Frame 2:
     *     - Symbols 0-47:    D2 (interleave ID, 48 symbols)
     *     - Symbols 48-95:   D2 repeated
     *     - Symbols 96-479:  Scrambled sync
     *   Frames 3+: All scrambled sync
     */
    std::vector<complex_t> generate_preamble() {
        std::vector<complex_t> symbols;
        
        int preamble_syms = mode_cfg_.preamble_symbols();
        int num_frames = mode_cfg_.preamble_frames;
        int frame_size = 480;  // symbols per frame
        
        symbols.reserve(preamble_syms);
        
        // D1 and D2 values for this mode
        int d1 = mode_cfg_.d1_sequence;
        int d2 = mode_cfg_.d2_sequence;
        
        // Use MS-DMT preamble encoder
        MSDMTPreambleEncoder encoder;
        
        // Generate each frame
        for (int frame = 0; frame < num_frames; frame++) {
            // Frame structure: Common(288) + Mode(64) + Count(96) + Zero(32) = 480
            
            // 1. Common segment (288 symbols = 9 x 32)
            // Uses p_c_seq pattern: D0,D1,D0,D1,D0,D1,D0,D1,D0 with pscramble
            for (int seg = 0; seg < 9; seg++) {
                uint8_t d_val = msdmt::p_c_seq[seg];
                for (int i = 0; i < 32; i++) {
                    uint8_t base = msdmt::psymbol[d_val][i % 8];
                    uint8_t scrambled = (base + msdmt::pscramble[i]) % 8;
                    symbols.push_back(complex_t(msdmt::psk8_i[scrambled], 
                                                 msdmt::psk8_q[scrambled]));
                }
            }
            
            // 2. Mode segment (64 symbols = D1 x 32 + D2 x 32)
            // D1 segment
            for (int i = 0; i < 32; i++) {
                uint8_t base = msdmt::psymbol[d1][i % 8];
                uint8_t scrambled = (base + msdmt::pscramble[i]) % 8;
                symbols.push_back(complex_t(msdmt::psk8_i[scrambled], 
                                             msdmt::psk8_q[scrambled]));
            }
            // D2 segment
            for (int i = 0; i < 32; i++) {
                uint8_t base = msdmt::psymbol[d2][i % 8];
                uint8_t scrambled = (base + msdmt::pscramble[i]) % 8;
                symbols.push_back(complex_t(msdmt::psk8_i[scrambled], 
                                             msdmt::psk8_q[scrambled]));
            }
            
            // 3. Count segment (96 symbols = 3 x 32)
            // Countdown value: num_frames - frame - 1 (so frame 0 = 2, frame 1 = 1, frame 2 = 0)
            int countdown = num_frames - frame - 1;
            uint8_t count_d = countdown % 8;
            for (int rep = 0; rep < 3; rep++) {
                for (int i = 0; i < 32; i++) {
                    uint8_t base = msdmt::psymbol[count_d][i % 8];
                    uint8_t scrambled = (base + msdmt::pscramble[i]) % 8;
                    symbols.push_back(complex_t(msdmt::psk8_i[scrambled], 
                                                 msdmt::psk8_q[scrambled]));
                }
            }
            
            // 4. Zero segment (32 symbols = D0 pattern)
            for (int i = 0; i < 32; i++) {
                uint8_t base = msdmt::psymbol[0][i % 8];  // D0 = all zeros
                uint8_t scrambled = (base + msdmt::pscramble[i]) % 8;
                symbols.push_back(complex_t(msdmt::psk8_i[scrambled], 
                                             msdmt::psk8_q[scrambled]));
            }
        }
        
        return symbols;
    }
    
    /**
     * Encode data (FEC, optionally bit repetition, interleave, PSK map with scrambler)
     * 
     * Per MIL-STD-188-110A / MS-DMT:
     * - Low rate modes (with repetition): bit-level repetition + BPSK mapping
     * - High rate modes (no repetition): native modulation (QPSK/8PSK)
     * 
     * Flow:
     * 1. Data scramble (XOR with scrambler)
     * 2. FEC encode (rate 1/2)
     * 3. If repetition > 1: Bit-level repetition, then BPSK mapping
     *    If repetition = 1: Group bits per modulation (2 for QPSK, 3 for 8PSK)
     * 4. Interleave (before symbol mapping for low rates, after for high rates)
     * 5. Data scrambler rotates symbol index (phase diversity)
     * 6. Map to constellation
     */
    std::vector<complex_t> encode_data(const std::vector<uint8_t>& data) {
        // Convert bytes to bits
        std::vector<uint8_t> bits;
        bits.reserve(data.size() * 8);
        for (uint8_t byte : data) {
            for (int i = 7; i >= 0; i--) {
                bits.push_back((byte >> i) & 1);
            }
        }
        
        // Scramble data bits
        Scrambler data_scr(SCRAMBLER_INIT_DATA);
        for (auto& b : bits) {
            b ^= data_scr.next_bit();
        }
        
        // FEC encode (rate 1/2, K=7)
        ConvEncoder encoder;
        std::vector<uint8_t> coded;
        encoder.encode(bits, coded, true);
        
        int rep = mode_cfg_.symbol_repetition;
        int bps = mode_cfg_.bits_per_symbol;
        
        std::vector<complex_t> symbols;
        Scrambler sym_scr(SCRAMBLER_INIT_DATA);  // Symbol-level data scrambler
        
        if (rep > 1) {
            // LOW RATE MODE: bit-level repetition + BPSK mapping
            // Each coded bit is repeated 'rep' times, each becomes a BPSK symbol
            
            // Bit-level repetition
            std::vector<uint8_t> repeated;
            repeated.reserve(coded.size() * rep);
            
            // For each coded bit pair (from rate 1/2 FEC), repeat it rep times
            for (size_t i = 0; i + 1 < coded.size(); i += 2) {
                for (int r = 0; r < rep; r++) {
                    repeated.push_back(coded[i]);
                    repeated.push_back(coded[i + 1]);
                }
            }
            if (coded.size() % 2 == 1) {
                for (int r = 0; r < rep; r++) {
                    repeated.push_back(coded[coded.size() - 1]);
                }
            }
            
            // Convert to soft bits for interleaver
            std::vector<soft_bit_t> soft_coded(repeated.begin(), repeated.end());
            
            // Pad to block size
            int bs = interleaver_.block_size();
            while (soft_coded.size() % bs != 0) {
                soft_coded.push_back(0);
            }
            
            // Interleave
            std::vector<soft_bit_t> interleaved;
            for (size_t i = 0; i < soft_coded.size(); i += bs) {
                std::vector<soft_bit_t> block(soft_coded.begin() + i, 
                                              soft_coded.begin() + i + bs);
                auto il = interleaver_.interleave(block);
                interleaved.insert(interleaved.end(), il.begin(), il.end());
            }
            
            // Map each bit to BPSK symbol with scrambler
            symbols.reserve(interleaved.size());
            for (const auto& bit : interleaved) {
                int sym_idx = (bit > 0) ? 4 : 0;  // BPSK: bit 0 → symbol 0, bit 1 → symbol 4
                int scr_val = sym_scr.next_tribit();
                sym_idx = (sym_idx + scr_val) % 8;
                symbols.push_back(MultiModeMapper::symbol_to_complex(sym_idx));
            }
        } else {
            // HIGH RATE MODE: native modulation (no repetition)
            // Group bits by bits_per_symbol and map using mode's modulation
            
            // Convert to soft bits for interleaver
            std::vector<soft_bit_t> soft_coded(coded.begin(), coded.end());
            
            // Pad to block size
            int bs = interleaver_.block_size();
            while (soft_coded.size() % bs != 0) {
                soft_coded.push_back(0);
            }
            
            // Interleave
            std::vector<soft_bit_t> interleaved;
            for (size_t i = 0; i < soft_coded.size(); i += bs) {
                std::vector<soft_bit_t> block(soft_coded.begin() + i, 
                                              soft_coded.begin() + i + bs);
                auto il = interleaver_.interleave(block);
                interleaved.insert(interleaved.end(), il.begin(), il.end());
            }
            
            // Group bits and map to symbols
            symbols.reserve(interleaved.size() / bps);
            for (size_t i = 0; i + bps <= interleaved.size(); i += bps) {
                int sym_idx = 0;
                for (int j = 0; j < bps; j++) {
                    sym_idx = (sym_idx << 1) | (interleaved[i + j] > 0 ? 1 : 0);
                }
                // Convert to constellation index based on modulation
                sym_idx = mapper_.map_to_symbol_index(sym_idx);
                
                // Apply data scrambler rotation
                int scr_val = sym_scr.next_tribit();
                sym_idx = (sym_idx + scr_val) % 8;
                symbols.push_back(MultiModeMapper::symbol_to_complex(sym_idx));
            }
        }
        
        // Insert probe symbols
        symbols = insert_probes(symbols);
        
        return symbols;
    }
    
    /**
     * Insert probe (known) symbols for channel estimation
     * Per MS-DMT: unknown_data_len followed by known_data_len, repeating
     * 
     * Note: Probes use the same mapper as data to maintain phase continuity.
     * For BPSK/QPSK modes, probes still use 8PSK - this is achieved by
     * always calling map_8psk() for probes.
     */
    std::vector<complex_t> insert_probes(const std::vector<complex_t>& data_symbols) {
        int unknown_len = mode_cfg_.unknown_data_len;
        int known_len = mode_cfg_.known_data_len;
        
        // 75bps modes don't have probe symbols
        if (unknown_len == 0 || known_len == 0) {
            return data_symbols;
        }
        
        std::vector<complex_t> output;
        Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
        
        size_t data_idx = 0;
        int pattern_count = 0;
        while (data_idx < data_symbols.size()) {
            // Copy unknown_data_len data symbols
            int to_copy = std::min(unknown_len, 
                                   static_cast<int>(data_symbols.size() - data_idx));
            for (int i = 0; i < to_copy; i++) {
                output.push_back(data_symbols[data_idx++]);
            }
            
            // Pad with zeros if incomplete pattern
            for (int i = to_copy; i < unknown_len; i++) {
                output.push_back(mapper_.map(0));
            }
            
            // Add known_data_len probe symbols (8PSK always for probes)
            // Use mapper_.map_8psk() to maintain phase continuity
            for (int i = 0; i < known_len; i++) {
                uint8_t tribit = probe_scr.next_tribit();
                complex_t probe_sym = mapper_.map_8psk(tribit);
                output.push_back(probe_sym);
            }
            pattern_count++;
        }
        
        return output;
    }
    
    /**
     * Pulse shape and upconvert to RF with external carrier NCO (for phase continuity)
     */
    std::vector<float> modulate_with_carrier(const std::vector<complex_t>& symbols, 
                                              float symbol_rate, NCO& carrier) {
        float sps = config_.sample_rate / symbol_rate;
        int sps_int = static_cast<int>(sps);
        
        // SRRC pulse shaping
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, sps);
        ComplexFirFilter srrc(srrc_taps);
        
        // Upsample and filter
        std::vector<complex_t> baseband;
        
        for (const auto& sym : symbols) {
            baseband.push_back(srrc.process(sym));
            for (int i = 1; i < sps_int; i++) {
                baseband.push_back(srrc.process(complex_t(0, 0)));
            }
        }
        
        // Flush filter
        for (size_t i = 0; i < srrc_taps.size(); i++) {
            baseband.push_back(srrc.process(complex_t(0, 0)));
        }
        
        // Find peak for normalization
        float peak = 0.0f;
        for (const auto& bb : baseband) {
            peak = std::max(peak, std::abs(bb.real()));
            peak = std::max(peak, std::abs(bb.imag()));
        }
        
        // Normalize to avoid clipping, apply output amplitude
        float scale = (peak > 0.0f) ? config_.output_amplitude / peak : config_.output_amplitude;
        
        // Upconvert using provided carrier NCO
        std::vector<float> rf;
        rf.reserve(baseband.size());
        
        for (const auto& bb : baseband) {
            complex_t scaled = bb * scale;
            complex_t up = scaled * carrier.next();
            rf.push_back(up.real());
        }
        
        return rf;
    }
    
    /**
     * Pulse shape and upconvert to RF at specified symbol rate
     */
    std::vector<float> modulate_at_rate(const std::vector<complex_t>& symbols, float symbol_rate) {
        NCO carrier(config_.sample_rate, config_.carrier_freq);
        return modulate_with_carrier(symbols, symbol_rate, carrier);
    }
    
    /**
     * Pulse shape and upconvert to RF (uses mode symbol rate)
     */
    std::vector<float> modulate(const std::vector<complex_t>& symbols) {
        return modulate_at_rate(symbols, mode_cfg_.symbol_rate);
    }

private:
    Config config_;
    ModeConfig mode_cfg_;
    MultiModeMapper mapper_;
    MultiModeInterleaver interleaver_;
    
    void initialize() {
        // Already initialized in constructor initializer list
    }
};

} // namespace m110a

#endif // M110A_MULTIMODE_TX_H
