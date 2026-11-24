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
#include <complex>

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
        bool use_nlms;        // Use Normalized LMS (power-normalized step)
        float nlms_delta;     // NLMS regularization (prevents div-by-zero)
        int lookahead;        // Decision delay: process() takes x[n+lookahead], returns y[n]
        
        Config()
            : ff_taps(11)      // Center tap + 5 on each side
            , fb_taps(5)       // 5 feedback taps
            , mu_ff(0.01f)     // Conservative step size
            , mu_fb(0.005f)    // Smaller for feedback
            , leak(0.0001f)    // Small leak for stability
            , use_nlms(false)  // Default: standard LMS
            , nlms_delta(0.001f) // NLMS regularization
            , lookahead(0) {}    // Causal: the newest input is the current symbol
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
    
    /// Change the LMS step sizes, e.g. a larger step for preamble pretraining.
    void set_step(float mu_ff, float mu_fb) { config_.mu_ff = mu_ff; config_.mu_fb = mu_fb; }

    /// Shift an input into the feed-forward delay line: no output, no adaptation,
    /// and the feedback line is left as it is. Use it only for the lookahead inputs
    /// before the first output, when no earlier symbol is known (clear_lines()).
    void push(complex_t input) { ff_delay_[ff_idx_] = input; ff_idx_ = (ff_idx_ + 1) % config_.ff_taps; }

    /// Shift an input into the feed-forward line and a known decision into the
    /// feedback line: no output, no adaptation. With lookahead L the decision is for
    /// the symbol L before the input. Fills the lines from known symbols before the
    /// first output, so that output sees a past as full as every later one does.
    void prime(complex_t input, complex_t decision) {
        push(input);
        fb_delay_[fb_idx_] = decision;
        fb_idx_ = (fb_idx_ + 1) % config_.fb_taps;
    }

    /// Zero both delay lines and keep the taps: a stream starts with no past.
    void clear_lines() {
        std::fill(ff_delay_.begin(), ff_delay_.end(), complex_t(0.0f, 0.0f));
        std::fill(fb_delay_.begin(), fb_delay_.end(), complex_t(0.0f, 0.0f));
        ff_idx_ = 0;
        fb_idx_ = 0;
    }

    /**
     * Process one symbol known to be +ref or -ref, without adapting: the feedback
     * line gets whichever of the two the output is nearer. A 110A probe at an
     * interleaver block boundary is the zero probe or a D1/D2 Walsh pattern, so
     * each of its symbols is the zero-probe symbol rotated by 0 or 180 degrees.
     * @param input Received symbol x[n+lookahead]
     * @param ref The zero-probe symbol for n
     * @return Equalized symbol y[n]
     */
    complex_t process_antipodal(complex_t input, complex_t ref) {
        ff_delay_[ff_idx_] = input;
        complex_t output = filter_output();
        complex_t decision = (std::real(output * std::conj(ref)) >= 0.0f) ? ref : -ref;
        fb_delay_[fb_idx_] = decision;
        fb_idx_ = (fb_idx_ + 1) % config_.fb_taps;
        ff_idx_ = (ff_idx_ + 1) % config_.ff_taps;
        symbol_count_++;
        return output;
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
     * @param input Received symbol x[n+lookahead] (after timing/carrier recovery)
     * @param training_symbol Known symbol for training (ignored if training=false)
     * @param training If true, use training_symbol for adaptation
     * @return Equalized symbol y[n]
     */
    complex_t process(complex_t input, complex_t training_symbol = complex_t(0,0), 
                      bool training = false) {
        // Push input into feedforward delay line at current position
        ff_delay_[ff_idx_] = input;
        complex_t output = filter_output();
        
        // Make decision
        complex_t decision = training ? training_symbol : hard_decision(output);
        
        // Compute error
        complex_t error = decision - output;
        
        // LMS adaptation
        adapt(error);
        
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
     * Set the taps to the least-squares fit over a known stretch: minimise
     * sum |ref[n] - y[n]|^2 for n in [first, end), where y[n] reads x[n - center]
     * to x[n + center] through the feed-forward taps (x indexed like ref, not fed
     * ahead) and ref[n-1] to ref[n-fb_taps] through the feedback taps. Inputs
     * outside x and references before ref[0] count as 0. delta adds
     * delta * trace / taps to the diagonal (ridge), so a short or flat stretch
     * still gives a well-posed fit. The delay lines are left as they are.
     *
     * The LMS gets to the same taps only with a large step, and leaves them
     * carrying that step's noise into the data: excess error that a flat
     * low-SNR channel shows as lower decode quality.
     * @return false when the fit is singular (taps unchanged)
     */
    bool fit_least_squares(const std::vector<complex_t>& x, const std::vector<complex_t>& ref,
                           int first, int end, float delta = 1e-3f) {
        const int nf = config_.ff_taps, nb = config_.fb_taps, m = nf + nb;
        end = std::min(end, static_cast<int>(ref.size()));
        if (first < 0 || end - first < m) return false;
        std::vector<std::complex<double>> R(m * m), b(m), u(m);
        for (int n = first; n < end; n++) {
            for (int i = 0; i < nf; i++) {
                const long k = static_cast<long>(n) + config_.lookahead - tap_delay(i);
                u[i] = (k >= 0 && k < static_cast<long>(x.size())) ? std::complex<double>(x[k]) : 0.0;
            }
            for (int i = 0; i < nb; i++) u[nf + i] = n - 1 - i >= 0 ? std::complex<double>(ref[n - 1 - i]) : 0.0;
            for (int r = 0; r < m; r++) {
                for (int c = 0; c < m; c++) R[r * m + c] += std::conj(u[r]) * u[c];
                b[r] += std::conj(u[r]) * std::complex<double>(ref[n]);
            }
        }
        double tr = 0.0;
        for (int r = 0; r < m; r++) tr += R[r * m + r].real();
        for (int r = 0; r < m; r++) R[r * m + r] += delta * tr / m;
        // Gaussian elimination with partial pivoting
        for (int c = 0; c < m; c++) {
            int p = c;
            for (int r = c + 1; r < m; r++) if (std::abs(R[r * m + c]) > std::abs(R[p * m + c])) p = r;
            if (std::abs(R[p * m + c]) < 1e-12) return false;
            if (p != c) { for (int k = 0; k < m; k++) std::swap(R[c * m + k], R[p * m + k]); std::swap(b[c], b[p]); }
            for (int r = c + 1; r < m; r++) {
                const std::complex<double> f = R[r * m + c] / R[c * m + c];
                for (int k = c; k < m; k++) R[r * m + k] -= f * R[c * m + k];
                b[r] -= f * b[c];
            }
        }
        for (int c = m - 1; c >= 0; c--) {
            std::complex<double> acc = b[c];
            for (int k = c + 1; k < m; k++) acc -= R[c * m + k] * b[k];
            b[c] = acc / R[c * m + c];
        }
        for (int i = 0; i < nf; i++) ff_taps_[i] = complex_t(b[i]);
        for (int i = 0; i < nb; i++) fb_taps_[i] = complex_t(b[nf + i]);
        return true;
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
     * Check if equalizer is converged (based on center tap dominance). The center
     * tap multiplies the current symbol x[n] (see tap_delay()).
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
     * Delay-line age of the input feed-forward tap i multiplies. The newest input,
     * at age 0, is x[n+lookahead], so tap i multiplies x[n + i - center]: the center
     * tap multiplies the current symbol x[n], and with lookahead = ff_taps / 2 the
     * taps span x[n-center] to x[n+center]. With lookahead 0 the taps after the
     * center wrap to the oldest inputs (ages ff_taps-1 down to center+1), the
     * original causal arrangement.
     */
    int tap_delay(int i) const {
        const int center = config_.ff_taps / 2;
        return ((config_.lookahead + center - i) % config_.ff_taps + config_.ff_taps) % config_.ff_taps;
    }

    /// Ring index of the input feed-forward tap i multiplies.
    int ff_tap_index(int i) const {
        return (ff_idx_ - tap_delay(i) + config_.ff_taps) % config_.ff_taps;
    }

    /// Feed-forward plus feedback output for the input now at ff_idx_.
    complex_t filter_output() const {
        complex_t ff_out(0.0f, 0.0f);
        for (int i = 0; i < config_.ff_taps; i++) ff_out += ff_taps_[i] * ff_delay_[ff_tap_index(i)];
        complex_t fb_out(0.0f, 0.0f);
        for (int i = 0; i < config_.fb_taps; i++) {
            // Feedback tap i multiplies decision at delay (i+1)
            int idx = (fb_idx_ - i - 1 + config_.fb_taps) % config_.fb_taps;
            fb_out += fb_taps_[i] * fb_delay_[idx];
        }
        return ff_out + fb_out;
    }

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
     * LMS/NLMS tap adaptation
     * 
     * Standard LMS: w[k] += μ * error * conj(x[k])
     * NLMS:         w[k] += μ / (δ + ||x||²) * error * conj(x[k])
     * 
     * NLMS normalizes by input power, giving faster convergence
     * on time-varying channels and better stability.
     */
    void adapt(complex_t error) {
        float mu_ff_eff = config_.mu_ff;
        float mu_fb_eff = config_.mu_fb;
        
        if (config_.use_nlms) {
            // Compute feedforward input power
            float ff_power = 0.0f;
            for (int i = 0; i < config_.ff_taps; i++) {
                ff_power += std::norm(ff_delay_[i]);
            }
            mu_ff_eff = config_.mu_ff / (config_.nlms_delta + ff_power);
            
            // Compute feedback input power  
            float fb_power = 0.0f;
            for (int i = 0; i < config_.fb_taps; i++) {
                fb_power += std::norm(fb_delay_[i]);
            }
            mu_fb_eff = config_.mu_fb / (config_.nlms_delta + fb_power);
        }
        
        // Feedforward adaptation: w[k] += mu_eff * error * conj(x[delay])
        for (int i = 0; i < config_.ff_taps; i++) {
            ff_taps_[i] += mu_ff_eff * error * std::conj(ff_delay_[ff_tap_index(i)]);
            ff_taps_[i] *= (1.0f - config_.leak);  // Leaky LMS
        }
        
        // Feedback adaptation
        for (int i = 0; i < config_.fb_taps; i++) {
            int idx = (fb_idx_ - i - 1 + config_.fb_taps) % config_.fb_taps;
            fb_taps_[i] += mu_fb_eff * error * std::conj(fb_delay_[idx]);
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
