#ifndef M110A_TX_H
#define M110A_TX_H

#include "common/types.h"
#include "common/constants.h"
#include "modem/scrambler.h"
#include "modem/symbol_mapper.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include <vector>
#include <cmath>
#include <memory>

namespace m110a {

/**
 * M110A Transmitter Configuration
 */
struct TxConfig {
    int data_rate_bps = 1200;                    // 75, 150, 300, 600, 1200, 2400
    InterleaveMode interleave = InterleaveMode::SHORT;
    float sample_rate = SAMPLE_RATE;
    float carrier_freq = CARRIER_FREQ;
    float output_amplitude = 0.6f;               // Output level (0.0 to 1.0)
};

/**
 * MIL-STD-188-110A Transmitter
 * 
 * Generates valid 110A waveforms for testing the receiver.
 * 
 * Signal flow:
 *   Data → [FEC Encode] → [Interleave] → Scramble → 8-PSK Map
 *        → Insert Probes → Prepend Preamble → SRRC Filter → Upconvert
 */
class M110A_Tx {
public:
    explicit M110A_Tx(const TxConfig& config = TxConfig{});
    
    /**
     * Generate complete transmission including preamble
     * @param data Raw data bytes to transmit
     * @return PCM samples (real, suitable for sound card)
     */
    std::vector<sample_t> transmit(const std::vector<uint8_t>& data);
    
    /**
     * Generate preamble only (for sync testing)
     * @param long_preamble Use LONG interleave preamble (4.8s vs 0.6s)
     * @return PCM samples
     */
    std::vector<sample_t> generate_preamble(bool long_preamble = false);
    
    /**
     * Generate baseband preamble symbols (complex, before pulse shaping)
     */
    std::vector<complex_t> generate_preamble_symbols(bool long_preamble = false);
    
    /**
     * Generate channel probe symbols (for equalizer testing)
     * @param count Number of probe symbols
     * @return Complex baseband symbols
     */
    std::vector<complex_t> generate_probe_symbols(int count);
    
    /**
     * Generate a test pattern: preamble + repeated known data
     * Good for BER testing
     */
    std::vector<sample_t> generate_test_pattern(int num_frames = 10);
    
    /**
     * Modulate baseband symbols to PCM output
     * Applies SRRC filtering and upconversion
     */
    std::vector<sample_t> modulate(const std::vector<complex_t>& symbols);
    
    /**
     * Get current configuration
     */
    const TxConfig& config() const { return config_; }
    
private:
    TxConfig config_;
    
    // DSP components
    std::unique_ptr<ComplexFirFilter> srrc_filter_;
    std::vector<float> srrc_taps_;
    
    // Generate symbols for one 0.2s preamble segment
    std::vector<complex_t> generate_preamble_segment();
    
    // Upsample symbols to sample rate (insert zeros, filter)
    std::vector<complex_t> pulse_shape(const std::vector<complex_t>& symbols);
    
    // Upconvert to carrier frequency
    std::vector<sample_t> upconvert(const std::vector<complex_t>& baseband);
    
    // Get samples per symbol (may be fractional, handled by interpolation)
    float samples_per_symbol() const {
        return config_.sample_rate / SYMBOL_RATE;
    }
};

// ============================================================================
// Implementation
// ============================================================================

inline M110A_Tx::M110A_Tx(const TxConfig& config) : config_(config) {
    // Generate SRRC filter taps
    srrc_taps_ = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, samples_per_symbol());
    srrc_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps_);
}

inline std::vector<complex_t> M110A_Tx::generate_preamble_segment() {
    // One segment = 0.2 seconds = 480 symbols at 2400 baud
    constexpr int SEGMENT_SYMBOLS = 480;
    
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> symbols;
    symbols.reserve(SEGMENT_SYMBOLS);
    
    for (int i = 0; i < SEGMENT_SYMBOLS; i++) {
        uint8_t tribit = scr.next_tribit();
        symbols.push_back(mapper.map(tribit));
    }
    
    return symbols;
}

inline std::vector<complex_t> M110A_Tx::generate_preamble_symbols(bool long_preamble) {
    // SHORT/ZERO preamble: 3 segments (0.6s) = 1440 symbols
    // LONG preamble: 24 segments (4.8s) = 11520 symbols
    
    int num_segments = long_preamble ? 24 : 3;
    int total_symbols = num_segments * 480;
    
    std::vector<complex_t> symbols;
    symbols.reserve(total_symbols);
    
    // Each segment is generated fresh with scrambler reset
    // This creates the repeating pattern used for correlation
    for (int seg = 0; seg < num_segments; seg++) {
        auto segment = generate_preamble_segment();
        symbols.insert(symbols.end(), segment.begin(), segment.end());
    }
    
    return symbols;
}

inline std::vector<complex_t> M110A_Tx::generate_probe_symbols(int count) {
    Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    std::vector<complex_t> symbols;
    symbols.reserve(count);
    
    for (int i = 0; i < count; i++) {
        uint8_t tribit = scr.next_tribit();
        symbols.push_back(mapper.map(tribit));
    }
    
    return symbols;
}

inline std::vector<complex_t> M110A_Tx::pulse_shape(const std::vector<complex_t>& symbols) {
    // Upsample by inserting zeros, then filter with SRRC
    int sps = static_cast<int>(samples_per_symbol() + 0.5f);  // Round to nearest int
    
    // Reset filter state
    srrc_filter_->reset();
    
    // Gain factor: compensate for zero-insertion and filter gain
    // SRRC is energy-normalized, so we need sqrt(sps) gain for amplitude preservation
    float gain = std::sqrt(static_cast<float>(sps));
    
    std::vector<complex_t> output;
    output.reserve(symbols.size() * sps + srrc_taps_.size());
    
    for (const auto& sym : symbols) {
        // Insert symbol followed by (sps-1) zeros
        output.push_back(srrc_filter_->process(sym * gain));
        for (int i = 1; i < sps; i++) {
            output.push_back(srrc_filter_->process(complex_t(0.0f, 0.0f)));
        }
    }
    
    // Flush filter (process zeros to get tail)
    for (size_t i = 0; i < srrc_taps_.size(); i++) {
        output.push_back(srrc_filter_->process(complex_t(0.0f, 0.0f)));
    }
    
    return output;
}

inline std::vector<sample_t> M110A_Tx::upconvert(const std::vector<complex_t>& baseband) {
    NCO carrier(config_.sample_rate, config_.carrier_freq);
    
    std::vector<sample_t> output;
    output.reserve(baseband.size());
    
    for (const auto& bb : baseband) {
        // Multiply by carrier: real output = Re{baseband * exp(j*2*pi*fc*t)}
        complex_t carrier_sample = carrier.next();
        complex_t modulated = bb * carrier_sample;
        
        // Take real part, scale by amplitude
        output.push_back(modulated.real() * config_.output_amplitude);
    }
    
    return output;
}

inline std::vector<sample_t> M110A_Tx::modulate(const std::vector<complex_t>& symbols) {
    auto baseband = pulse_shape(symbols);
    return upconvert(baseband);
}

inline std::vector<sample_t> M110A_Tx::generate_preamble(bool long_preamble) {
    auto symbols = generate_preamble_symbols(long_preamble);
    return modulate(symbols);
}

inline std::vector<sample_t> M110A_Tx::generate_test_pattern(int num_frames) {
    std::vector<complex_t> symbols;
    
    // Start with preamble
    auto preamble = generate_preamble_symbols(false);
    symbols.insert(symbols.end(), preamble.begin(), preamble.end());
    
    // Add data frames with probe symbols
    // Each frame: 32 data symbols + 16 probe symbols
    Scrambler data_scr(SCRAMBLER_INIT_DATA);
    Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
    SymbolMapper mapper;
    
    // Continue phase from end of preamble
    // (In real 110A, there's a specific transition, but this is for testing)
    
    for (int frame = 0; frame < num_frames; frame++) {
        // 32 data symbols (using scrambled test pattern: incrementing bytes)
        for (int i = 0; i < DATA_SYMBOLS_PER_FRAME; i++) {
            // Generate pseudo-random data tribits
            uint8_t data_tribit = (frame * 32 + i) % 8;  // Simple pattern
            uint8_t scrambled = data_tribit ^ data_scr.next_tribit();
            symbols.push_back(mapper.map(scrambled));
        }
        
        // 16 probe symbols (known sequence for equalizer)
        for (int i = 0; i < PROBE_SYMBOLS_PER_FRAME; i++) {
            uint8_t probe_tribit = probe_scr.next_tribit();
            symbols.push_back(mapper.map(probe_tribit));
        }
    }
    
    return modulate(symbols);
}

inline std::vector<sample_t> M110A_Tx::transmit(const std::vector<uint8_t>& data) {
    // For now, implement simplified version without full FEC/interleaving
    // This generates valid waveform structure for sync testing
    
    std::vector<complex_t> symbols;
    
    // Preamble
    auto preamble = generate_preamble_symbols(config_.interleave == InterleaveMode::LONG);
    symbols.insert(symbols.end(), preamble.begin(), preamble.end());
    
    // Convert data bytes to tribits and modulate
    Scrambler scr(SCRAMBLER_INIT_DATA);
    SymbolMapper mapper;
    
    // Simple bit packing: 8 bits → 2 tribits + 2 bits left over
    // For proper 110A, would need FEC encoding first
    
    size_t bit_idx = 0;
    std::vector<uint8_t> bits;
    
    // Unpack bytes to bits
    for (uint8_t byte : data) {
        for (int i = 7; i >= 0; i--) {
            bits.push_back((byte >> i) & 1);
        }
    }
    
    // Pack bits to tribits
    int symbol_count = 0;
    while (bit_idx + 3 <= bits.size()) {
        uint8_t tribit = (bits[bit_idx] << 2) | (bits[bit_idx+1] << 1) | bits[bit_idx+2];
        bit_idx += 3;
        
        uint8_t scrambled = tribit ^ scr.next_tribit();
        symbols.push_back(mapper.map(scrambled));
        symbol_count++;
        
        // Insert probe every 32 data symbols
        if (symbol_count % DATA_SYMBOLS_PER_FRAME == 0) {
            Scrambler probe_scr(SCRAMBLER_INIT_PREAMBLE);
            // Advance probe scrambler to correct position
            for (int i = 0; i < (symbol_count / DATA_SYMBOLS_PER_FRAME - 1) * PROBE_SYMBOLS_PER_FRAME; i++) {
                probe_scr.next_tribit();
            }
            for (int i = 0; i < PROBE_SYMBOLS_PER_FRAME; i++) {
                symbols.push_back(mapper.map(probe_scr.next_tribit()));
            }
        }
    }
    
    return modulate(symbols);
}

} // namespace m110a

#endif // M110A_TX_H
