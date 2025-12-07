#ifndef M110A_STREAMING_RX_V3_H
#define M110A_STREAMING_RX_V3_H

/**
 * Streaming Receiver V3 - Full Adaptive Recovery
 * 
 * Signal flow:
 *   48kHz RF → Downconvert → Decimate by 5 → 9600Hz (SPS=4)
 *           → SRRC Match Filter → Adaptive Timing Recovery
 *           → Carrier Recovery → Channel Estimation → Decode
 */

#include "common/types.h"
#include "common/constants.h"
#include "dsp/nco.h"
#include "dsp/fir_filter.h"
#include "dsp/resampler.h"
#include "sync/timing_recovery_v2.h"
#include "sync/freq_search_detector.h"
#include "channel/channel_estimator.h"
#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"
#include "modem/interleaver.h"
#include <vector>
#include <memory>
#include <functional>

namespace m110a {

class StreamingRxV3 {
public:
    static constexpr float INPUT_RATE = 48000.0f;
    static constexpr float INTERNAL_RATE = 9600.0f;
    static constexpr int DECIM_FACTOR = 5;
    static constexpr float INTERNAL_SPS = 4.0f;
    
    struct Config {
        float input_sample_rate = INPUT_RATE;
        float carrier_freq = CARRIER_FREQ;
        InterleaveMode interleave_mode = InterleaveMode::SHORT;
        bool use_channel_est = true;
        bool verbose = false;
    };
    
    struct Stats {
        int samples_processed = 0;
        int symbols_decoded = 0;
        int frames_decoded = 0;
        int bytes_decoded = 0;
        float freq_offset_hz = 0.0f;
        float timing_mu = 0.0f;
        float snr_db = 0.0f;
        bool timing_locked = false;
        bool synchronized = false;
    };
    
    using DataCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    explicit StreamingRxV3(const Config& cfg = Config{})
        : config_(cfg)
        , state_(State::SEARCHING)
        , decim_count_(0)
        , frame_symbol_count_(0) {
        
        initialize();
    }
    
    void reset() {
        state_ = State::SEARCHING;
        stats_ = Stats{};
        
        input_nco_->reset();
        decim_filter_i_->reset();
        decim_filter_q_->reset();
        match_filter_->reset();
        timing_->reset();
        channel_tracker_.reset();
        
        decim_count_ = 0;
        frame_symbol_count_ = 0;
        detect_buffer_.clear();
        soft_bits_.clear();
        decoded_data_.clear();
        prev_symbol_ = complex_t(1.0f, 0.0f);
    }
    
    /**
     * Process input samples
     * @return Number of new decoded bytes
     */
    int process(const std::vector<float>& samples) {
        int bytes_before = decoded_data_.size();
        
        for (float s : samples) {
            process_sample(s);
        }
        
        return decoded_data_.size() - bytes_before;
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
    
    // Downconversion
    std::unique_ptr<NCO> input_nco_;
    
    // Decimation (48k → 9600)
    std::unique_ptr<FirFilter<float>> decim_filter_i_;
    std::unique_ptr<FirFilter<float>> decim_filter_q_;
    int decim_count_;
    
    // Matched filter
    std::unique_ptr<ComplexFirFilter> match_filter_;
    
    // Timing recovery
    std::unique_ptr<TimingRecoveryV2> timing_;
    
    // Channel estimation
    ChannelTracker channel_tracker_;
    
    // Decode
    std::unique_ptr<BlockInterleaver> deinterleaver_;
    std::vector<soft_bit_t> soft_bits_;
    std::vector<uint8_t> decoded_data_;
    complex_t prev_symbol_;
    int frame_symbol_count_;
    
    // Detection
    std::vector<complex_t> detect_buffer_;
    
    DataCallback data_callback_;
    
    void initialize() {
        // NCO for downconversion
        input_nco_ = std::make_unique<NCO>(config_.input_sample_rate, -config_.carrier_freq);
        
        // Decimation filter
        auto lp_taps = generate_lowpass_taps(63, 1.0f / DECIM_FACTOR);
        decim_filter_i_ = std::make_unique<FirFilter<float>>(lp_taps);
        decim_filter_q_ = std::make_unique<FirFilter<float>>(lp_taps);
        
        // SRRC matched filter at internal rate
        auto srrc_taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, INTERNAL_SPS);
        match_filter_ = std::make_unique<ComplexFirFilter>(srrc_taps);
        
        // Timing recovery
        TimingRecoveryV2::Config tr_cfg;
        tr_cfg.samples_per_symbol = INTERNAL_SPS;
        tr_cfg.acq_bandwidth = 0.05f;
        tr_cfg.track_bandwidth = 0.01f;
        timing_ = std::make_unique<TimingRecoveryV2>(tr_cfg);
        
        // Deinterleaver
        BlockInterleaver::Config il_cfg;
        il_cfg.mode = config_.interleave_mode;
        il_cfg.data_rate = static_cast<int>(SYMBOL_RATE);
        deinterleaver_ = std::make_unique<BlockInterleaver>(il_cfg);
        
        prev_symbol_ = complex_t(1.0f, 0.0f);
    }
    
    void process_sample(float sample) {
        stats_.samples_processed++;
        
        // Downconvert
        complex_t bb = input_nco_->mix(complex_t(sample, 0.0f));
        
        // Decimate
        float fi = decim_filter_i_->process(bb.real());
        float fq = decim_filter_q_->process(bb.imag());
        
        if (++decim_count_ < DECIM_FACTOR) return;
        decim_count_ = 0;
        
        complex_t decimated(fi, fq);
        
        // Matched filter
        complex_t filtered = match_filter_->process(decimated);
        
        // State machine
        switch (state_) {
            case State::SEARCHING:
                process_searching(filtered);
                break;
            case State::SYNCING:
                process_syncing(filtered);
                break;
            case State::DECODING:
                process_decoding(filtered);
                break;
            case State::DONE:
                break;
        }
    }
    
    void process_searching(complex_t sample) {
        detect_buffer_.push_back(sample);
        
        // Simple energy detection
        if (detect_buffer_.size() >= 500) {
            float energy = 0.0f;
            for (size_t i = detect_buffer_.size() - 500; i < detect_buffer_.size(); i++) {
                energy += std::norm(detect_buffer_[i]);
            }
            energy /= 500.0f;
            
            if (energy > 0.01f) {
                state_ = State::SYNCING;
                stats_.synchronized = true;
            }
        }
        
        // Limit buffer size
        if (detect_buffer_.size() > 50000) {
            detect_buffer_.erase(detect_buffer_.begin(), detect_buffer_.begin() + 25000);
        }
    }
    
    void process_syncing(complex_t sample) {
        // Feed to timing recovery, wait for lock
        if (timing_->process(sample)) {
            if (timing_->is_locked()) {
                state_ = State::DECODING;
                detect_buffer_.clear();
            }
        }
        
        stats_.timing_locked = timing_->is_locked();
        stats_.timing_mu = timing_->mu();
    }
    
    void process_decoding(complex_t sample) {
        // Timing recovery
        if (!timing_->process(sample)) return;
        
        complex_t symbol = timing_->get_symbol();
        stats_.symbols_decoded++;
        stats_.timing_mu = timing_->mu();
        stats_.timing_locked = timing_->is_locked();
        
        // Frame tracking
        int frame_pos = frame_symbol_count_ % FRAME_SYMBOLS;
        
        if (frame_pos < DATA_SYMBOLS_PER_FRAME) {
            // Data symbol
            complex_t compensated = symbol;
            if (config_.use_channel_est) {
                compensated = channel_tracker_.process(symbol, false);
            }
            
            // Differential decode
            complex_t diff = compensated * std::conj(prev_symbol_);
            prev_symbol_ = compensated;
            
            float phase = std::atan2(diff.imag(), diff.real());
            if (phase < 0) phase += 2.0f * PI;
            int tribit = static_cast<int>(std::round(phase / (PI/4.0f))) % 8;
            
            // To soft bits
            soft_bits_.push_back(((tribit >> 2) & 1) ? 64 : -64);
            soft_bits_.push_back(((tribit >> 1) & 1) ? 64 : -64);
            soft_bits_.push_back(((tribit >> 0) & 1) ? 64 : -64);
            
            try_decode();
            
        } else {
            // Probe symbol
            if (config_.use_channel_est) {
                int probe_idx = frame_pos - DATA_SYMBOLS_PER_FRAME;
                auto ref = channel_tracker_.estimator().get_probe_reference(0);
                if (probe_idx < static_cast<int>(ref.size())) {
                    channel_tracker_.process(symbol, true, ref[probe_idx]);
                }
            }
            
            if (frame_pos == FRAME_SYMBOLS - 1) {
                stats_.frames_decoded++;
                stats_.snr_db = channel_tracker_.estimate().snr_db;
            }
        }
        
        frame_symbol_count_++;
    }
    
    void try_decode() {
        int block_size = deinterleaver_->block_size();
        
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            block_size = (soft_bits_.size() / 6) * 6;
            if (block_size < 48) return;
        }
        
        if (static_cast<int>(soft_bits_.size()) < block_size) return;
        
        // Extract and decode block
        std::vector<soft_bit_t> block(soft_bits_.begin(), soft_bits_.begin() + block_size);
        soft_bits_.erase(soft_bits_.begin(), soft_bits_.begin() + block_size);
        
        std::vector<soft_bit_t> deint;
        if (config_.interleave_mode == InterleaveMode::ZERO) {
            deint = block;
        } else {
            deint = deinterleaver_->deinterleave_soft(block);
        }
        
        ViterbiDecoder viterbi;
        std::vector<uint8_t> bits;
        viterbi.decode_block(deint, bits, true);
        
        Scrambler scr(SCRAMBLER_INIT_DATA);
        for (auto& b : bits) b ^= scr.next_bit();
        
        // Pack bytes
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 7 < bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | bits[i + j];
            }
            bytes.push_back(byte);
        }
        
        if (!bytes.empty()) {
            stats_.bytes_decoded += bytes.size();
            decoded_data_.insert(decoded_data_.end(), bytes.begin(), bytes.end());
            if (data_callback_) data_callback_(bytes);
        }
    }
};

} // namespace m110a

#endif // M110A_STREAMING_RX_V3_H
