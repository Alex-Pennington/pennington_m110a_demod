#ifndef M110A_DFE_H
#define M110A_DFE_H

#include "common/types.h"
#include "common/constants.h"
#include "modem/scrambler.h"
#include "modem/symbol_mapper.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <array>

namespace m110a {

/**
 * Decision Feedback Equalizer (DFE) for M110A
 * 
 * The DFE uses feedforward taps to cancel precursor ISI and
 * feedback taps to cancel postcursor ISI using past decisions.
 * 
 * Structure:
 *   Feedforward filter (FFF): Operates on received samples
 *   Feedback filter (FBF): Operates on past symbol decisions
 *   
 *   y[n] = sum(ff[k] * x[n-k]) + sum(fb[k] * d[n-k])
 *   
 * Training uses the known probe symbols in M110A frames.
 * Decision-directed mode uses hard decisions on data symbols.
 */
class DFE {
public:
    struct Config {
        int ff_taps;          // Number of feedforward taps
        int fb_taps;          // Number of feedback taps
        float mu_ff;          // LMS step size for feedforward
        float mu_fb;          // LMS step size for feedback
        float leak;           // Leaky LMS coefficient (0 = no leak)
        
        Config()
            : ff_taps(11)      // Center tap + 5 on each side
            , fb_taps(5)       // 5 feedback taps
            , mu_ff(0.01f)     // Conservative step size
            , mu_fb(0.005f)    // Smaller for feedback
            , leak(0.0001f) {} // Small leak for stability
    };
    
    explicit DFE(const Config& config = Config{})
        : config_(config)
        , ff_taps_(config.ff_taps, complex_t(0.0f, 0.0f))
        , fb_taps_(config.fb_taps, complex_t(0.0f, 0.0f))
        , ff_delay_(config.ff_taps, complex_t(0.0f, 0.0f))
        , fb_delay_(config.fb_taps, complex_t(0.0f, 0.0f))
        , ff_idx_(0)
        , fb_idx_(0)
        , training_mode_(false)
        , symbol_count_(0) {
        
        // Initialize center tap to unity
        int center = config_.ff_taps / 2;
        ff_taps_[center] = complex_t(1.0f, 0.0f);
    }
    
    void reset() {
        std::fill(ff_taps_.begin(), ff_taps_.end(), complex_t(0.0f, 0.0f));
        std::fill(fb_taps_.begin(), fb_taps_.end(), complex_t(0.0f, 0.0f));
        std::fill(ff_delay_.begin(), ff_delay_.end(), complex_t(0.0f, 0.0f));
        std::fill(fb_delay_.begin(), fb_delay_.end(), complex_t(0.0f, 0.0f));
        
        // Re-initialize center tap
        int center = config_.ff_taps / 2;
        ff_taps_[center] = complex_t(1.0f, 0.0f);
        
        ff_idx_ = 0;
        fb_idx_ = 0;
        training_mode_ = false;
        symbol_count_ = 0;
    }
    
    /**
     * Process one symbol with optional training
     * @param input Received symbol (after timing/carrier recovery)
     * @param training_symbol Known symbol for training (ignored if training=false)
     * @param training If true, use training_symbol for adaptation
     * @return Equalized symbol
     */
    complex_t process(complex_t input, complex_t training_symbol = complex_t(0,0), 
                      bool training = false) {
        // Push input into feedforward delay line at current position
        ff_delay_[ff_idx_] = input;
        
        // Compute feedforward output
        // The center tap (ff_taps/2) should multiply the current input
        // Tap 0 multiplies oldest, Tap N-1 multiplies newest
        complex_t ff_out(0.0f, 0.0f);
        int center = config_.ff_taps / 2;
        
        for (int i = 0; i < config_.ff_taps; i++) {
            // For tap i, we need to read the sample at delay (center - i)
            // delay 0 = current input at ff_idx_
            // delay 1 = previous at ff_idx_-1
            int delay = center - i;
            int idx = (ff_idx_ - delay + config_.ff_taps) % config_.ff_taps;
            ff_out += ff_taps_[i] * ff_delay_[idx];
        }
        
        // Compute feedback output (using past decisions)
        complex_t fb_out(0.0f, 0.0f);
        for (int i = 0; i < config_.fb_taps; i++) {
            // Feedback tap i multiplies decision at delay (i+1)
            int idx = (fb_idx_ - i - 1 + config_.fb_taps) % config_.fb_taps;
            fb_out += fb_taps_[i] * fb_delay_[idx];
        }
        
        // Equalizer output
        complex_t output = ff_out + fb_out;
        
        // Make decision
        complex_t decision = training ? training_symbol : hard_decision(output);
        
        // Compute error
        complex_t error = decision - output;
        
        // LMS adaptation
        adapt(error, center);
        
        // Update feedback delay with decision
        fb_delay_[fb_idx_] = decision;
        fb_idx_ = (fb_idx_ + 1) % config_.fb_taps;
        
        // Advance feedforward index
        ff_idx_ = (ff_idx_ + 1) % config_.ff_taps;
        
        training_mode_ = training;
        symbol_count_++;
        
        return output;
    }
    
    /**
     * Train on a block of known symbols (e.g., probe sequence)
     * @param inputs Received symbols
     * @param reference Known transmitted symbols
     * @return MSE after training
     */
    float train(const std::vector<complex_t>& inputs,
                const std::vector<complex_t>& reference) {
        
        float mse = 0.0f;
        size_t n = std::min(inputs.size(), reference.size());
        
        for (size_t i = 0; i < n; i++) {
            complex_t out = process(inputs[i], reference[i], true);
            complex_t err = reference[i] - out;
            mse += std::norm(err);
        }
        
        return mse / n;
    }
    
    /**
     * Equalize a block of data symbols (decision-directed)
     * @param inputs Received symbols
     * @param outputs Equalized symbols (appended)
     * @return Number of symbols processed
     */
    int equalize(const std::vector<complex_t>& inputs,
                 std::vector<complex_t>& outputs) {
        
        for (const auto& in : inputs) {
            outputs.push_back(process(in));
        }
        return inputs.size();
    }
    
    /**
     * Get current feedforward taps
     */
    const std::vector<complex_t>& ff_taps() const { return ff_taps_; }
    
    /**
     * Get current feedback taps
     */
    const std::vector<complex_t>& fb_taps() const { return fb_taps_; }
    
    /**
     * Get tap magnitudes for analysis
     */
    std::vector<float> ff_tap_magnitudes() const {
        std::vector<float> mags;
        for (const auto& t : ff_taps_) {
            mags.push_back(std::abs(t));
        }
        return mags;
    }
    
    /**
     * Check if equalizer is converged (based on center tap dominance)
     */
    bool is_converged() const {
        int center = config_.ff_taps / 2;
        float center_mag = std::abs(ff_taps_[center]);
        
        // Check that center tap is dominant
        float sum_others = 0.0f;
        for (int i = 0; i < config_.ff_taps; i++) {
            if (i != center) {
                sum_others += std::abs(ff_taps_[i]);
            }
        }
        
        return center_mag > 0.5f && sum_others < center_mag;
    }
    
    /**
     * Get symbol count
     */
    int symbol_count() const { return symbol_count_; }

private:
    Config config_;
    
    std::vector<complex_t> ff_taps_;    // Feedforward tap coefficients
    std::vector<complex_t> fb_taps_;    // Feedback tap coefficients
    std::vector<complex_t> ff_delay_;   // Feedforward delay line
    std::vector<complex_t> fb_delay_;   // Feedback delay line (decisions)
    
    int ff_idx_;                        // Current position in FF delay line
    int fb_idx_;                        // Current position in FB delay line
    
    bool training_mode_;
    int symbol_count_;
    
    /**
     * 8-PSK hard decision
     */
    complex_t hard_decision(complex_t symbol) const {
        float mag = std::abs(symbol);
        if (mag < 0.01f) return complex_t(1.0f, 0.0f);  // Default
        
        float phase = std::arg(symbol);
        float step = PI / 4.0f;
        int sector = static_cast<int>(std::round(phase / step)) & 0x7;
        
        return std::polar(1.0f, sector * step);
    }
    
    /**
     * LMS tap adaptation
     */
    void adapt(complex_t error, int center) {
        // Feedforward adaptation: w[k] += mu * error * conj(x[delay])
        for (int i = 0; i < config_.ff_taps; i++) {
            int delay = center - i;
            int idx = (ff_idx_ - delay + config_.ff_taps) % config_.ff_taps;
            ff_taps_[i] += config_.mu_ff * error * std::conj(ff_delay_[idx]);
            ff_taps_[i] *= (1.0f - config_.leak);  // Leaky LMS
        }
        
        // Feedback adaptation
        for (int i = 0; i < config_.fb_taps; i++) {
            int idx = (fb_idx_ - i - 1 + config_.fb_taps) % config_.fb_taps;
            fb_taps_[i] += config_.mu_fb * error * std::conj(fb_delay_[idx]);
            fb_taps_[i] *= (1.0f - config_.leak);
        }
    }
};

/**
 * M110A Frame Equalizer
 * 
 * Handles the M110A frame structure with interleaved data and probe symbols.
 * Each frame: 32 data symbols + 16 probe symbols = 48 symbols
 * 
 * Uses probe symbols for training, then equalizes data symbols
 * in decision-directed mode.
 */
class FrameEqualizer {
public:
    struct Config {
        DFE::Config dfe_config;
        int data_symbols;     // Data symbols per frame (32)
        int probe_symbols;    // Probe symbols per frame (16)
        
        Config()
            : data_symbols(DATA_SYMBOLS_PER_FRAME)
            , probe_symbols(PROBE_SYMBOLS_PER_FRAME) {}
    };
    
    explicit FrameEqualizer(const Config& config = Config{})
        : config_(config)
        , dfe_(config.dfe_config)
        , frame_count_(0) {
        
        // Generate probe symbol reference
        generate_probe_reference();
    }
    
    void reset() {
        dfe_.reset();
        frame_count_ = 0;
    }
    
    /**
     * Process one complete frame (data + probe symbols)
     * @param frame Input frame symbols (48 total)
     * @param data_out Equalized data symbols (32 output)
     * @return true if frame processed successfully
     */
    bool process_frame(const std::vector<complex_t>& frame,
                       std::vector<complex_t>& data_out) {
        
        int frame_size = config_.data_symbols + config_.probe_symbols;
        if (static_cast<int>(frame.size()) < frame_size) {
            return false;
        }
        
        // Extract data and probe portions
        std::vector<complex_t> data_in(frame.begin(), 
                                       frame.begin() + config_.data_symbols);
        std::vector<complex_t> probe_in(frame.begin() + config_.data_symbols,
                                        frame.begin() + frame_size);
        
        // Train on probe symbols first
        dfe_.train(probe_in, probe_ref_);
        
        // Now equalize data symbols (decision-directed)
        dfe_.equalize(data_in, data_out);
        
        frame_count_++;
        return true;
    }
    
    /**
     * Process continuous stream of symbols
     * Automatically detects frame boundaries using probe correlation
     * @param symbols Input symbols
     * @param data_out Equalized data symbols
     * @return Number of complete frames processed
     */
    int process_stream(const std::vector<complex_t>& symbols,
                       std::vector<complex_t>& data_out) {
        
        int frame_size = config_.data_symbols + config_.probe_symbols;
        int frames = 0;
        
        for (size_t i = 0; i + frame_size <= symbols.size(); i += frame_size) {
            std::vector<complex_t> frame(symbols.begin() + i,
                                         symbols.begin() + i + frame_size);
            
            if (process_frame(frame, data_out)) {
                frames++;
            }
        }
        
        return frames;
    }
    
    /**
     * Get current DFE state
     */
    const DFE& dfe() const { return dfe_; }
    DFE& dfe() { return dfe_; }
    
    /**
     * Get frame count
     */
    int frame_count() const { return frame_count_; }
    
    /**
     * Get probe reference symbols
     */
    const std::vector<complex_t>& probe_reference() const { return probe_ref_; }

private:
    Config config_;
    DFE dfe_;
    int frame_count_;
    
    std::vector<complex_t> probe_ref_;  // Known probe symbol sequence
    
    void generate_probe_reference() {
        // Probe symbols use the same scrambler sequence as preamble
        Scrambler scr(SCRAMBLER_INIT_PREAMBLE);
        SymbolMapper mapper;
        
        probe_ref_.clear();
        probe_ref_.reserve(config_.probe_symbols);
        
        for (int i = 0; i < config_.probe_symbols; i++) {
            uint8_t tribit = scr.next_tribit();
            probe_ref_.push_back(mapper.map(tribit));
        }
    }
};

/**
 * Simple channel model for testing
 * Implements a multipath channel with configurable taps
 */
class MultipathChannel {
public:
    struct Config {
        std::vector<complex_t> taps;  // Channel impulse response
        float noise_std;              // AWGN noise standard deviation
        
        Config() : noise_std(0.0f) {
            // Default: mild ISI
            taps = {
                complex_t(1.0f, 0.0f),      // Main path
                complex_t(0.3f, 0.1f),      // First echo
                complex_t(0.1f, -0.05f)     // Second echo
            };
        }
    };
    
    explicit MultipathChannel(const Config& config = Config{})
        : config_(config)
        , delay_line_(config.taps.size(), complex_t(0.0f, 0.0f))
        , write_idx_(0)
        , rng_state_(12345) {}
    
    void reset() {
        std::fill(delay_line_.begin(), delay_line_.end(), complex_t(0.0f, 0.0f));
        write_idx_ = 0;
    }
    
    complex_t process(complex_t input) {
        // Store input
        delay_line_[write_idx_] = input;
        
        // Convolve with channel taps
        complex_t output(0.0f, 0.0f);
        int idx = write_idx_;
        
        for (size_t i = 0; i < config_.taps.size(); i++) {
            output += config_.taps[i] * delay_line_[idx];
            idx--;
            if (idx < 0) idx = delay_line_.size() - 1;
        }
        
        // Add noise
        if (config_.noise_std > 0.0f) {
            output += complex_t(gaussian() * config_.noise_std,
                               gaussian() * config_.noise_std);
        }
        
        write_idx_ = (write_idx_ + 1) % delay_line_.size();
        
        return output;
    }
    
    std::vector<complex_t> process_block(const std::vector<complex_t>& input) {
        std::vector<complex_t> output;
        output.reserve(input.size());
        
        for (const auto& s : input) {
            output.push_back(process(s));
        }
        
        return output;
    }

private:
    Config config_;
    std::vector<complex_t> delay_line_;
    int write_idx_;
    uint32_t rng_state_;
    
    float gaussian() {
        // Box-Muller transform
        float u1 = uniform();
        float u2 = uniform();
        if (u1 < 1e-10f) u1 = 1e-10f;
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * PI * u2);
    }
    
    float uniform() {
        rng_state_ = rng_state_ * 1664525 + 1013904223;
        return static_cast<float>(rng_state_) / 4294967296.0f;
    }
};

} // namespace m110a

#endif // M110A_DFE_H
