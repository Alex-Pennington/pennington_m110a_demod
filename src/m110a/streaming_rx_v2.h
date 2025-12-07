#ifndef M110A_STREAMING_RX_V2_H
#define M110A_STREAMING_RX_V2_H

/**
 * Streaming Receiver V2 - Decimate-First Architecture
 * 
 * Signal flow:
 *   48kHz RF → Downconvert → Decimate by 5 → 9600 Hz baseband (SPS=4)
 *            → SRRC Match Filter → Timing Recovery → Symbols → Decode
 * 
 * This architecture keeps SPS=4 for timing recovery, where the Gardner TED
 * works correctly with the sample history buffer.
 */

#include "common/types.h"
#include "common/constants.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "dsp/resampler.h"
#include "sync/timing_recovery.h"
#include "sync/freq_search_detector.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <vector>
#include <memory>
#include <functional>

namespace m110a {

class StreamingRxV2 {
public:
    // Internal processing rate (integer SPS)
    static constexpr float INTERNAL_RATE = 9600.0f;  // 9600/2400 = SPS=4
    static constexpr int INTERNAL_SPS = 4;
    static constexpr int DECIMATION_FACTOR = 5;      // 48000/9600 = 5
    
    struct Config {
        float input_sample_rate = SAMPLE_RATE_48K;   // Hardware capture rate
        float symbol_rate = SYMBOL_RATE;
        float carrier_freq = CARRIER_FREQ;
        InterleaveMode interleave_mode = InterleaveMode::SHORT;
        bool verbose = false;
    };
    
    struct Stats {
        int samples_processed = 0;
        int symbols_decoded = 0;
        int frames_decoded = 0;
        int bytes_decoded = 0;
        float freq_offset_hz = 0.0f;
        float timing_offset = 0.0f;
        bool synchronized = false;
    };
    
    using DataCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    explicit StreamingRxV2(const Config& cfg = Config{})
        : config_(cfg)
        , state_(State::SEARCHING)
        , decimation_factor_(static_cast<int>(cfg.input_sample_rate / INTERNAL_RATE)) {
        
        // Verify decimation gives integer result
        float actual_internal = cfg.input_sample_rate / decimation_factor_;
        if (std::abs(actual_internal - INTERNAL_RATE) > 0.1f) {
            // Fallback for non-48k rates
            decimation_factor_ = 1;
            internal_rate_ = cfg.input_sample_rate;
        } else {
            internal_rate_ = INTERNAL_RATE;
        }
        
        internal_sps_ = internal_rate_ / cfg.symbol_rate;
        
        // Initialize components
        reset();
    }
    
    void reset() {
        state_ = State::SEARCHING;
        stats_ = Stats{};
        
        // NCO for downconversion (at input rate)
        input_nco_ = std::make_unique<NCO>(config_.input_sample_rate, -config_.carrier_freq);
        
        // Decimation filter (complex)
        if (decimation_factor_ > 1) {
            float cutoff = 1.0f / decimation_factor_;
            auto lp_taps = generate_lowpass_taps(63, cutoff);
            decim_filter_i_ = std::make_unique<FirFilter<float>>(lp_taps);
            decim_filter_q_ = std::make_unique<FirFilter<float>>(lp_taps);
        }
        decim_count_ = 0;
        
        // SRRC matched filter (at internal rate)
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, internal_sps_);
        match_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps);
        
        // Timing recovery (at internal rate, SPS=4)
        TimingRecovery::Config tr_cfg;
        tr_cfg.samples_per_symbol = internal_sps_;
        tr_cfg.loop_bandwidth = 0.01f;
        tr_cfg.damping = 1.0f;
        timing_ = std::make_unique<TimingRecovery>(tr_cfg);
        
        // Detection buffer
        detect_buffer_.clear();
        detect_buffer_.reserve(10000);
        
        // Decode state
        soft_bits_.clear();
        prev_symbol_ = complex_t(1.0f, 0.0f);
        frame_symbol_count_ = 0;
        samples_to_skip_ = 0;
        
        // Interleaver
        BlockInterleaver::Config il_cfg;
        il_cfg.mode = config_.interleave_mode;
        il_cfg.data_rate = static_cast<int>(config_.symbol_rate);
        deinterleaver_ = std::make_unique<BlockInterleaver>(il_cfg);
    }
    
    /**
     * Process a block of input samples
     * @param samples RF samples at input_sample_rate
     * @return Number of decoded bytes (call get_decoded_data() to retrieve)
     */
    int process(const std::vector<float>& samples) {
        int bytes_before = decoded_data_.size();
        
        for (float s : samples) {
            process_sample(s);
        }
        
        return decoded_data_.size() - bytes_before;
    }
    
    /**
     * Process a single input sample
     */
    void process_sample(float sample) {
        stats_.samples_processed++;
        
        // Downconvert to baseband
        complex_t bb = input_nco_->mix(complex_t(sample, 0));
        
        // Decimate if needed
        complex_t decimated;
        bool have_decimated = false;
        
        if (decimation_factor_ > 1) {
            float filt_i = decim_filter_i_->process(bb.real());
            float filt_q = decim_filter_q_->process(bb.imag());
            
            if (++decim_count_ >= decimation_factor_) {
                decim_count_ = 0;
                decimated = complex_t(filt_i, filt_q);
                have_decimated = true;
            }
        } else {
            decimated = bb;
            have_decimated = true;
        }
        
        if (!have_decimated) return;
        
        // Now at internal rate (9600 Hz, SPS=4)
        switch (state_) {
            case State::SEARCHING:
                process_searching(decimated);
                break;
            case State::SYNCING:
                process_syncing(decimated);
                break;
            case State::DECODING:
                process_decoding(decimated);
                break;
            case State::DONE:
                break;
        }
    }
    
    std::vector<uint8_t> get_decoded_data() {
        auto data = std::move(decoded_data_);
        decoded_data_.clear();
        return data;
    }
    
    const Stats& stats() const { return stats_; }
    bool is_synchronized() const { return stats_.synchronized; }
    
    void set_data_callback(DataCallback cb) { data_callback_ = cb; }

private:
    enum class State { SEARCHING, SYNCING, DECODING, DONE };
    
    Config config_;
    State state_;
    Stats stats_;
    
    // Sample rate conversion
    int decimation_factor_;
    float internal_rate_;
    float internal_sps_;
    int decim_count_ = 0;
    
    // DSP components
    std::unique_ptr<NCO> input_nco_;
    std::unique_ptr<FirFilter<float>> decim_filter_i_;
    std::unique_ptr<FirFilter<float>> decim_filter_q_;
    std::unique_ptr<ComplexFirFilter> match_filter_;
    std::unique_ptr<TimingRecovery> timing_;
    std::unique_ptr<BlockInterleaver> deinterleaver_;
    
    // Detection
    std::vector<complex_t> detect_buffer_;
    
    // Decode state
    std::vector<soft_bit_t> soft_bits_;
    std::vector<uint8_t> decoded_data_;
    complex_t prev_symbol_;
    int frame_symbol_count_ = 0;
    int samples_to_skip_ = 0;
    
    DataCallback data_callback_;
    
    void process_searching(complex_t sample) {
        // Buffer samples for preamble detection
        detect_buffer_.push_back(sample);
        
        // Need enough samples for detection (at internal rate)
        int preamble_symbols = (config_.interleave_mode == InterleaveMode::LONG) ? 11520 : 1440;
        int min_samples = static_cast<int>(preamble_symbols * internal_sps_ * 0.3f);
        
        if (detect_buffer_.size() >= static_cast<size_t>(min_samples) && detect_buffer_.size() % 500 == 0) {
            // Try detection using magnitude correlation
            if (try_detect_preamble()) {
                state_ = State::SYNCING;
                stats_.synchronized = true;
                
                if (config_.verbose) {
                    std::cerr << "SYNC acquired at sample " << stats_.samples_processed << "\n";
                }
            }
        }
        
        // Prevent unbounded buffer growth
        if (detect_buffer_.size() > 100000) {
            detect_buffer_.erase(detect_buffer_.begin(), detect_buffer_.begin() + 50000);
        }
    }
    
    bool try_detect_preamble() {
        // Simple energy-based detection
        // Look for consistent symbol energy in the preamble region
        
        if (detect_buffer_.size() < 1000) return false;
        
        // Compute average magnitude
        float sum_mag = 0;
        for (size_t i = detect_buffer_.size() - 1000; i < detect_buffer_.size(); i++) {
            sum_mag += std::abs(detect_buffer_[i]);
        }
        float avg_mag = sum_mag / 1000.0f;
        
        // Check for reasonable signal level
        if (avg_mag < 0.01f) return false;
        
        // For now, simple threshold detection
        // A proper implementation would use the FreqSearchDetector
        return avg_mag > 0.05f;
    }
    
    void process_syncing(complex_t sample) {
        // Skip to end of preamble
        int preamble_symbols = (config_.interleave_mode == InterleaveMode::LONG) ? 11520 : 1440;
        int preamble_samples = static_cast<int>(preamble_symbols * internal_sps_);
        
        // We've been buffering, now skip remaining preamble
        int buffered = detect_buffer_.size();
        samples_to_skip_ = preamble_samples - buffered;
        
        if (samples_to_skip_ <= 0) {
            // Already past preamble, start decoding
            state_ = State::DECODING;
            
            // Get reference from last buffered sample
            if (!detect_buffer_.empty()) {
                prev_symbol_ = detect_buffer_.back();
            }
            
            // Process any samples past preamble
            int start = buffered + samples_to_skip_;
            if (start < 0) start = 0;
            for (size_t i = start; i < detect_buffer_.size(); i++) {
                process_decoding(detect_buffer_[i]);
            }
            detect_buffer_.clear();
        } else {
            state_ = State::DECODING;
            // Will skip samples_to_skip_ samples before actual decoding
        }
    }
    
    void process_decoding(complex_t sample) {
        // Skip remaining preamble samples
        if (samples_to_skip_ > 0) {
            samples_to_skip_--;
            // Update reference symbol
            complex_t filtered = match_filter_->process(sample);
            prev_symbol_ = filtered;
            return;
        }
        
        // Matched filter
        complex_t filtered = match_filter_->process(sample);
        
        // Timing recovery
        if (timing_->process(filtered)) {
            // Got a symbol
            decode_symbol(timing_->get_symbol());
        }
    }
    
    void decode_symbol(complex_t symbol) {
        // Differential decode
        complex_t diff = symbol * std::conj(prev_symbol_);
        prev_symbol_ = symbol;
        
        float phase = std::atan2(diff.imag(), diff.real());
        if (phase < 0) phase += 2 * PI;
        int tribit = (static_cast<int>(std::round(phase / (PI / 4)))) % 8;
        
        stats_.symbols_decoded++;
        
        // Frame structure: 32 data + 16 probe
        int frame_pos = frame_symbol_count_ % FRAME_SYMBOLS;
        frame_symbol_count_++;
        
        // Only collect data symbols (first 32 of each frame)
        if (frame_pos < DATA_SYMBOLS_PER_FRAME) {
            soft_bits_.push_back(((tribit >> 2) & 1) ? 127 : -127);
            soft_bits_.push_back(((tribit >> 1) & 1) ? 127 : -127);
            soft_bits_.push_back(((tribit >> 0) & 1) ? 127 : -127);
            
            try_decode();
        }
    }
    
    void try_decode() {
        int block_size = deinterleaver_->block_size();
        
        // For ZERO interleave, process continuously
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            block_size = soft_bits_.size();
            if (block_size < 6) return;  // Need at least a few bits
            block_size = (block_size / 6) * 6;  // Round to complete coded symbols
        }
        
        if (static_cast<int>(soft_bits_.size()) < block_size) return;
        
        // Extract block
        std::vector<soft_bit_t> block(soft_bits_.begin(), soft_bits_.begin() + block_size);
        soft_bits_.erase(soft_bits_.begin(), soft_bits_.begin() + block_size);
        
        // Deinterleave
        std::vector<soft_bit_t> deinterleaved;
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            deinterleaved = block;
        } else {
            deinterleaved = deinterleaver_->deinterleave_soft(block);
        }
        
        // Viterbi decode
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        viterbi.decode_block(deinterleaved, decoded_bits, true);
        
        // Descramble
        Scrambler scr(SCRAMBLER_INIT_DATA);
        for (auto& b : decoded_bits) {
            b ^= scr.next_bit();
        }
        
        // Pack to bytes
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | decoded_bits[i + j];
            }
            bytes.push_back(byte);
        }
        
        if (!bytes.empty()) {
            stats_.frames_decoded++;
            stats_.bytes_decoded += bytes.size();
            
            decoded_data_.insert(decoded_data_.end(), bytes.begin(), bytes.end());
            
            if (data_callback_) {
                data_callback_(bytes);
            }
        }
    }
};

} // namespace m110a

#endif // M110A_STREAMING_RX_V2_H
