#ifndef M110A_RX_H
#define M110A_RX_H

#include "common/types.h"
#include "common/constants.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "sync/preamble_detector.h"
#include "sync/timing_recovery.h"
#include "sync/carrier_recovery.h"
#include "equalizer/dfe.h"
#include "modem/scrambler.h"
#include "modem/symbol_mapper.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <memory>
#include <functional>
#include <cmath>

namespace m110a {

/**
 * M110A Demodulator/Receiver
 * 
 * Complete receiver chain:
 *   RF → Downconvert → Match Filter → Preamble Detect →
 *   Timing Recovery → Carrier Recovery → Equalizer →
 *   Soft Demap → Deinterleave → Viterbi → Descramble → Data
 */
class M110A_Rx {
public:
    enum class State {
        SEARCHING,      // Looking for preamble
        ACQUIRING,      // Locking timing/carrier loops
        SYNCHRONIZED,   // Decoding data
        LOST_SYNC       // Lost synchronization
    };
    
    using DataCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    struct Config {
        float sample_rate;
        float carrier_freq;
        InterleaveMode interleave_mode;
        int data_rate;
        float preamble_threshold;
        float timing_bandwidth;
        float carrier_bandwidth;
        
        Config()
            : sample_rate(SAMPLE_RATE)
            , carrier_freq(CARRIER_FREQ)
            , interleave_mode(InterleaveMode::SHORT)
            , data_rate(2400)
            , preamble_threshold(0.35f)
            , timing_bandwidth(0.01f)
            , carrier_bandwidth(0.02f) {}
    };
    
    struct Stats {
        int samples_processed;
        int symbols_recovered;
        int frames_decoded;
        int bytes_decoded;
        float freq_offset_hz;
        float timing_offset;
        float snr_estimate;
        
        Stats() : samples_processed(0), symbols_recovered(0),
                  frames_decoded(0), bytes_decoded(0),
                  freq_offset_hz(0.0f), timing_offset(0.0f),
                  snr_estimate(0.0f) {}
    };
    
    explicit M110A_Rx(const Config& config = Config{})
        : config_(config)
        , state_(State::SEARCHING)
        , stats_()
        , frame_symbol_count_(0)
        , interleave_block_count_(0)
        , acquire_count_(0)
        , preamble_skip_count_(0)
        , prev_symbol_(1.0f, 0.0f) {
        
        initialize();
    }
    
    void reset() {
        state_ = State::SEARCHING;
        stats_ = Stats();
        frame_symbol_count_ = 0;
        interleave_block_count_ = 0;
        acquire_count_ = 0;
        preamble_skip_count_ = 0;
        prev_symbol_ = complex_t(1.0f, 0.0f);
        
        downconvert_nco_->reset();
        matched_filter_->reset();
        preamble_detector_->reset();
        timing_recovery_->reset();
        carrier_recovery_->reset();
        equalizer_->reset();
        viterbi_->reset();
        descrambler_->reset(SCRAMBLER_INIT_DATA);
        
        baseband_buffer_.clear();
        symbol_buffer_.clear();
        soft_bits_.clear();
        decoded_data_.clear();
    }
    
    /**
     * Process real RF samples
     */
    int process(const std::vector<float>& samples) {
        int bytes_decoded = 0;
        
        for (float s : samples) {
            bytes_decoded += process_sample(complex_t(s, 0.0f));
        }
        
        return bytes_decoded;
    }
    
    /**
     * Process complex baseband samples
     */
    int process(const std::vector<complex_t>& samples) {
        int bytes_decoded = 0;
        
        for (const auto& sample : samples) {
            bytes_decoded += process_sample(sample);
        }
        
        return bytes_decoded;
    }
    
    State state() const { return state_; }
    bool is_synchronized() const { return state_ == State::SYNCHRONIZED; }
    
    std::vector<uint8_t> get_decoded_data() {
        auto data = std::move(decoded_data_);
        decoded_data_.clear();
        return data;
    }
    
    void set_data_callback(DataCallback callback) {
        data_callback_ = callback;
    }
    
    const Stats& stats() const { return stats_; }
    float frequency_offset() const { return carrier_recovery_->frequency_offset(); }
    float timing_phase() const { return timing_recovery_->mu(); }

private:
    Config config_;
    State state_;
    Stats stats_;
    
    // DSP components
    std::unique_ptr<NCO> downconvert_nco_;
    std::unique_ptr<ComplexFirFilter> matched_filter_;
    std::unique_ptr<PreambleDetector> preamble_detector_;
    std::unique_ptr<TimingRecovery> timing_recovery_;
    std::unique_ptr<CarrierRecovery> carrier_recovery_;
    std::unique_ptr<DFE> equalizer_;
    
    // Decoder components
    std::unique_ptr<ViterbiDecoder> viterbi_;
    std::unique_ptr<Scrambler> descrambler_;
    std::unique_ptr<BlockInterleaver> deinterleaver_;
    SymbolMapper mapper_;
    
    // Buffers
    std::vector<float> baseband_buffer_;
    std::vector<complex_t> symbol_buffer_;
    std::vector<soft_bit_t> soft_bits_;  // int8_t soft bits
    std::vector<uint8_t> decoded_data_;
    
    // State tracking
    int frame_symbol_count_;
    int interleave_block_count_;
    int acquire_count_;
    int preamble_skip_count_;  // Symbols to skip after acquisition
    
    // Differential demodulation state
    complex_t prev_symbol_;
    
    // Probe reference for equalizer training
    std::vector<complex_t> probe_ref_;
    
    // Callback
    DataCallback data_callback_;
    
    void initialize() {
        // Downconverter NCO
        downconvert_nco_ = std::make_unique<NCO>(
            config_.sample_rate, -config_.carrier_freq);
        
        // Matched filter (SRRC)
        auto srrc_taps = generate_srrc_taps(
            SRRC_ALPHA, SRRC_SPAN_SYMBOLS, 
            config_.sample_rate / SYMBOL_RATE);
        matched_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps);
        
        // Preamble detector
        PreambleDetector::Config pd_config;
        pd_config.detection_threshold = config_.preamble_threshold;
        preamble_detector_ = std::make_unique<PreambleDetector>(pd_config);
        
        // Timing recovery
        TimingRecovery::Config tr_config;
        tr_config.loop_bandwidth = config_.timing_bandwidth;
        timing_recovery_ = std::make_unique<TimingRecovery>(tr_config);
        
        // Carrier recovery
        CarrierRecovery::Config cr_config;
        cr_config.loop_bandwidth = config_.carrier_bandwidth;
        carrier_recovery_ = std::make_unique<CarrierRecovery>(cr_config);
        
        // Equalizer
        DFE::Config dfe_config;
        equalizer_ = std::make_unique<DFE>(dfe_config);
        
        // Viterbi decoder
        viterbi_ = std::make_unique<ViterbiDecoder>();
        
        // Descrambler
        descrambler_ = std::make_unique<Scrambler>(SCRAMBLER_INIT_DATA);
        
        // Deinterleaver
        BlockInterleaver::Config int_config;
        int_config.mode = config_.interleave_mode;
        int_config.data_rate = config_.data_rate;
        deinterleaver_ = std::make_unique<BlockInterleaver>(int_config);
        
        // Generate probe reference sequence
        generate_probe_reference();
    }
    
    void generate_probe_reference() {
        // Probe symbols use preamble scrambler init
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
        
        probe_ref_.clear();
        probe_ref_.reserve(PROBE_SYMBOLS_PER_FRAME);
        
        for (int i = 0; i < PROBE_SYMBOLS_PER_FRAME; i++) {
            probe_ref_.push_back(mapper_.map(scr.next_tribit()));
        }
    }
    
    /**
     * Process one sample through the receiver chain
     */
    int process_sample(complex_t sample) {
        stats_.samples_processed++;
        
        // State machine determines processing
        switch (state_) {
            case State::SEARCHING:
                // Preamble detector needs raw RF samples
                return process_searching(sample.real());
                
            case State::ACQUIRING:
            case State::SYNCHRONIZED: {
                // Downconvert to baseband
                complex_t baseband = downconvert_nco_->mix(sample);
                // Matched filter
                complex_t filtered = matched_filter_->process(baseband);
                
                if (state_ == State::ACQUIRING) {
                    return process_acquiring(filtered);
                } else {
                    return process_synchronized(filtered);
                }
            }
                
            case State::LOST_SYNC:
                state_ = State::SEARCHING;
                preamble_detector_->reset();
                acquire_count_ = 0;
                return 0;
        }
        return 0;
    }
    
    /**
     * SEARCHING state - look for preamble
     */
    int process_searching(float rf_sample) {
        // Buffer RF samples for preamble detector
        baseband_buffer_.push_back(rf_sample);
        
        // Process in chunks for efficiency
        if (baseband_buffer_.size() >= 64) {
            auto result = preamble_detector_->process(baseband_buffer_);
            baseband_buffer_.clear();
            
            if (result.acquired) {
                // Preamble detected!
                state_ = State::ACQUIRING;
                stats_.freq_offset_hz = result.freq_offset_hz;
                
                // Adjust NCO for detected frequency offset
                downconvert_nco_->set_frequency(
                    -config_.carrier_freq - result.freq_offset_hz);
                
                // Reset synchronization loops
                timing_recovery_->reset();
                carrier_recovery_->reset();
                equalizer_->reset();
                
                // Reset decode chain
                viterbi_->reset();
                descrambler_->reset(SCRAMBLER_INIT_DATA);
                // Deinterleaver is stateless - no reset needed
                
                // Clear buffers
                symbol_buffer_.clear();
                soft_bits_.clear();
                
                frame_symbol_count_ = 0;
                interleave_block_count_ = 0;
                acquire_count_ = 0;
            }
        }
        
        return 0;
    }
    
    /**
     * ACQUIRING state - lock timing and carrier loops
     */
    int process_acquiring(complex_t sample) {
        // Run timing recovery
        if (timing_recovery_->process(sample)) {
            complex_t timed = timing_recovery_->get_symbol();
            
            // Run carrier recovery
            complex_t synced = carrier_recovery_->process(timed);
            
            stats_.symbols_recovered++;
            acquire_count_++;
            
            // Allow loops to settle (about 50 symbols)
            // Then transition to synchronized state
            if (acquire_count_ > 50) {
                state_ = State::SYNCHRONIZED;
                frame_symbol_count_ = 0;
                
                // Skip remaining preamble symbols before decoding data
                // Preamble detector fires at ~40% through preamble (~557 symbols)
                // After 50 symbol acquisition, need to skip ~700 more to reach data
                preamble_skip_count_ = 700;
                
                // Initialize differential demod reference
                prev_symbol_ = synced;
                
                // Start fresh for data
                symbol_buffer_.clear();
                soft_bits_.clear();
            }
        }
        return 0;
    }
    
    /**
     * SYNCHRONIZED state - decode data
     */
    int process_synchronized(complex_t sample) {
        // Run timing recovery
        if (!timing_recovery_->process(sample)) {
            return 0;  // No symbol ready yet
        }
        
        complex_t timed = timing_recovery_->get_symbol();
        
        // Run carrier recovery
        complex_t synced = carrier_recovery_->process(timed);
        
        stats_.symbols_recovered++;
        stats_.timing_offset = timing_recovery_->mu();
        stats_.freq_offset_hz = carrier_recovery_->frequency_offset();
        
        // Skip remaining preamble symbols before decoding
        if (preamble_skip_count_ > 0) {
            preamble_skip_count_--;
            // Keep updating differential reference
            prev_symbol_ = synced;
            return 0;
        }
        
        // Frame structure: 32 data + 16 probe = 48 symbols per frame
        int frame_pos = frame_symbol_count_ % FRAME_SYMBOLS;
        frame_symbol_count_++;
        
        if (frame_pos < DATA_SYMBOLS_PER_FRAME) {
            // Data symbol - equalize and buffer
            complex_t eq = equalizer_->process(synced);
            symbol_buffer_.push_back(eq);
            
            // Soft demap immediately
            auto soft = soft_demap_8psk(eq);
            soft_bits_.insert(soft_bits_.end(), soft.begin(), soft.end());
            
        } else {
            // Probe symbol - use for equalizer training
            int probe_idx = frame_pos - DATA_SYMBOLS_PER_FRAME;
            if (probe_idx < (int)probe_ref_.size()) {
                equalizer_->process(synced, probe_ref_[probe_idx], true);
            }
            
            // End of frame
            if (frame_pos == FRAME_SYMBOLS - 1) {
                stats_.frames_decoded++;
            }
        }
        
        // Check if we should decode
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            // ZERO interleave: decode continuously as data arrives
            return decode_continuous();
        } else {
            // SHORT/LONG interleave: wait for full block
            int block_size = deinterleaver_->block_size();
            if (block_size > 0 && (int)soft_bits_.size() >= block_size) {
                return decode_block();
            }
        }
        
        return 0;
    }
    
    /**
     * Soft demap differential 8-PSK symbol to 3 soft bits
     * 
     * In MIL-STD-188-110A, tribits encode PHASE INCREMENTS:
     *   tribit 0 (000) → 0° increment
     *   tribit 1 (001) → 45° increment
     *   tribit 2 (010) → 90° increment
     *   etc.
     * 
     * Returns soft bits in int8_t format (-127 to +127)
     * Positive = bit 0 more likely, Negative = bit 1 more likely
     */
    std::vector<soft_bit_t> soft_demap_8psk(complex_t symbol) {
        // Compute phase difference from previous symbol
        // diff = symbol * conj(prev_symbol) gives relative phase
        complex_t diff = symbol * std::conj(prev_symbol_);
        
        // Update previous symbol for next time
        prev_symbol_ = symbol;
        
        // Normalize the difference (in case of amplitude variations)
        float mag = std::abs(diff);
        if (mag > 0.01f) {
            diff /= mag;
        }
        
        // Phase increment possibilities (natural binary mapping)
        static const float phase_increments[8] = {
            0.0f,               // 000 → 0°
            PI / 4.0f,          // 001 → 45°
            PI / 2.0f,          // 010 → 90°
            3.0f * PI / 4.0f,   // 011 → 135°
            PI,                 // 100 → 180°
            5.0f * PI / 4.0f,   // 101 → 225°
            3.0f * PI / 2.0f,   // 110 → 270°
            7.0f * PI / 4.0f    // 111 → 315°
        };
        
        // Reference symbols for each tribit
        static complex_t ref_symbols[8];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i < 8; i++) {
                ref_symbols[i] = complex_t(
                    std::cos(phase_increments[i]),
                    std::sin(phase_increments[i])
                );
            }
            initialized = true;
        }
        
        // Calculate distances to all possible phase increments
        float distances[8];
        for (int i = 0; i < 8; i++) {
            float dr = diff.real() - ref_symbols[i].real();
            float di = diff.imag() - ref_symbols[i].imag();
            distances[i] = dr*dr + di*di;
        }
        
        // Estimate noise variance
        float min_dist = distances[0];
        for (int i = 1; i < 8; i++) {
            if (distances[i] < min_dist) min_dist = distances[i];
        }
        float noise_var = std::max(min_dist, 0.01f);
        
        // Calculate soft bits (LLRs) using natural binary mapping
        // tribit = (bit2, bit1, bit0) where bit2 is MSB
        std::vector<soft_bit_t> soft(3);
        
        for (int bit = 0; bit < 3; bit++) {
            float min_d0 = 1e10f;  // Min distance where bit=0
            float min_d1 = 1e10f;  // Min distance where bit=1
            
            for (int tribit = 0; tribit < 8; tribit++) {
                // Natural binary: check the appropriate bit
                int b = (tribit >> (2 - bit)) & 1;
                
                if (b == 0) {
                    if (distances[tribit] < min_d0) min_d0 = distances[tribit];
                } else {
                    if (distances[tribit] < min_d1) min_d1 = distances[tribit];
                }
            }
            
            // LLR = (d1 - d0) / (2 * noise_var)
            // Positive LLR means bit=0 more likely
            // But Viterbi expects: bit=0 → negative, bit=1 → positive
            // So we negate the LLR
            float llr = (min_d0 - min_d1) / (2.0f * noise_var);
            
            // Scale and clip to int8 range
            int scaled = static_cast<int>(llr * 32.0f);
            scaled = std::max(-127, std::min(127, scaled));
            
            soft[bit] = static_cast<soft_bit_t>(scaled);
        }
        
        return soft;
    }
    
    /**
     * Decode a complete interleave block
     */
    int decode_block() {
        int block_size = deinterleaver_->block_size();
        if ((int)soft_bits_.size() < block_size) {
            return 0;
        }
        
        // Extract block
        std::vector<soft_bit_t> block(soft_bits_.begin(), 
                                       soft_bits_.begin() + block_size);
        soft_bits_.erase(soft_bits_.begin(), 
                         soft_bits_.begin() + block_size);
        
        // Deinterleave
        std::vector<soft_bit_t> deint = deinterleaver_->deinterleave_soft(block);
        
        // Viterbi decode - outputs scrambled bits
        std::vector<uint8_t> scrambled_bits;
        viterbi_->decode_block(deint, scrambled_bits, true);
        
        // Descramble bits and assemble into bytes
        std::vector<uint8_t> data = descrambler_->descramble_bits_to_bytes(scrambled_bits);
        
        // Store decoded data
        decoded_data_.insert(decoded_data_.end(), data.begin(), data.end());
        stats_.bytes_decoded += data.size();
        
        // Callback if registered
        if (data_callback_ && !data.empty()) {
            data_callback_(data);
        }
        
        interleave_block_count_++;
        
        return data.size();
    }
    
    /**
     * Continuous decode for ZERO interleave mode
     */
    int decode_continuous() {
        // Need at least 48 soft bits (16 tribits)
        const int MIN_BITS = 48;
        
        if ((int)soft_bits_.size() < MIN_BITS) {
            return 0;
        }
        
        // Process available soft bits
        std::vector<soft_bit_t> to_decode = soft_bits_;
        soft_bits_.clear();
        
        // Viterbi decode - outputs scrambled bits
        std::vector<uint8_t> scrambled_bits;
        viterbi_->decode_block(to_decode, scrambled_bits, false);  // Don't flush
        
        if (scrambled_bits.empty()) {
            return 0;
        }
        
        // Descramble bits and assemble into bytes
        std::vector<uint8_t> data = descrambler_->descramble_bits_to_bytes(scrambled_bits);
        
        // Store decoded data
        decoded_data_.insert(decoded_data_.end(), data.begin(), data.end());
        stats_.bytes_decoded += data.size();
        
        // Callback if registered
        if (data_callback_ && !data.empty()) {
            data_callback_(data);
        }
        
        return data.size();
    }
};

} // namespace m110a

#endif // M110A_RX_H
