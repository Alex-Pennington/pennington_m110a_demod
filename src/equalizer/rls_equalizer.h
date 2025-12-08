/**
 * @file rls_equalizer.h
 * @brief Recursive Least Squares (RLS) Decision Feedback Equalizer
 * 
 * RLS provides faster convergence than LMS, critical for:
 * - Rapid HF fading channels
 * - Short preambles
 * - Time-varying multipath
 * 
 * Algorithm:
 *   k = P*x / (λ + x'*P*x)
 *   y = w'*x
 *   e = d - y
 *   w = w + k*e
 *   P = (P - k*x'*P) / λ
 */

#ifndef RLS_EQUALIZER_H
#define RLS_EQUALIZER_H

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace m110a {

using complex_t = std::complex<float>;

/**
 * RLS DFE Configuration
 */
struct RLSConfig {
    int ff_taps = 11;          // Feedforward filter length
    int fb_taps = 5;           // Feedback filter length
    float lambda = 0.99f;      // Forgetting factor (0.95-0.999)
    float delta = 0.01f;       // Initial P matrix scaling
    int center_tap = 5;        // Center tap of FF filter (cursor position)
};

/**
 * RLS Decision Feedback Equalizer
 * 
 * Faster convergence than LMS/NLMS, but O(N²) complexity
 */
class RLSEqualizer {
public:
    explicit RLSEqualizer(const RLSConfig& config = RLSConfig())
        : config_(config)
        , ff_weights_(config.ff_taps, complex_t(0, 0))
        , fb_weights_(config.fb_taps, complex_t(0, 0))
        , ff_delay_(config.ff_taps, complex_t(0, 0))
        , fb_delay_(config.fb_taps, complex_t(0, 0))
        , P_ff_(config.ff_taps * config.ff_taps)
        , P_fb_(config.fb_taps * config.fb_taps)
    {
        reset();
    }
    
    /**
     * Reset equalizer state
     */
    void reset() {
        // Clear weights
        std::fill(ff_weights_.begin(), ff_weights_.end(), complex_t(0, 0));
        std::fill(fb_weights_.begin(), fb_weights_.end(), complex_t(0, 0));
        
        // Initialize center tap
        if (config_.center_tap < config_.ff_taps) {
            ff_weights_[config_.center_tap] = complex_t(1, 0);
        }
        
        // Clear delay lines
        std::fill(ff_delay_.begin(), ff_delay_.end(), complex_t(0, 0));
        std::fill(fb_delay_.begin(), fb_delay_.end(), complex_t(0, 0));
        
        // Initialize P matrices to δ*I (scaled identity)
        std::fill(P_ff_.begin(), P_ff_.end(), complex_t(0, 0));
        std::fill(P_fb_.begin(), P_fb_.end(), complex_t(0, 0));
        
        float delta_inv = 1.0f / config_.delta;
        for (int i = 0; i < config_.ff_taps; i++) {
            P_ff_[i * config_.ff_taps + i] = complex_t(delta_inv, 0);
        }
        for (int i = 0; i < config_.fb_taps; i++) {
            P_fb_[i * config_.fb_taps + i] = complex_t(delta_inv, 0);
        }
    }
    
    /**
     * Process one input sample
     * @param input New input sample
     * @param training Optional known symbol for training (nullptr for DD mode)
     * @return Equalized output
     */
    complex_t process(complex_t input, const complex_t* training = nullptr) {
        // Shift input into FF delay line
        for (int i = config_.ff_taps - 1; i > 0; i--) {
            ff_delay_[i] = ff_delay_[i - 1];
        }
        ff_delay_[0] = input;
        
        // Compute FF output
        complex_t ff_out(0, 0);
        for (int i = 0; i < config_.ff_taps; i++) {
            ff_out += ff_weights_[i] * ff_delay_[i];
        }
        
        // Compute FB output (ISI cancellation)
        complex_t fb_out(0, 0);
        for (int i = 0; i < config_.fb_taps; i++) {
            fb_out += fb_weights_[i] * fb_delay_[i];
        }
        
        // Combined output
        complex_t output = ff_out - fb_out;
        
        // Decision or training
        complex_t decision;
        if (training) {
            decision = *training;
        } else {
            decision = hard_decision_8psk(output);
        }
        
        // Error
        complex_t error = decision - output;
        
        // RLS adaptation
        adapt_rls(error);
        
        // Update FB delay line with decision
        for (int i = config_.fb_taps - 1; i > 0; i--) {
            fb_delay_[i] = fb_delay_[i - 1];
        }
        if (config_.fb_taps > 0) {
            fb_delay_[0] = decision;
        }
        
        return output;
    }
    
    /**
     * Train on known sequence
     * @param inputs Input samples
     * @param training Known symbol sequence
     * @param passes Number of training passes (2 recommended)
     */
    void train(const std::vector<complex_t>& inputs,
               const std::vector<complex_t>& training,
               int passes = 2) {
        
        size_t len = std::min(inputs.size(), training.size());
        
        for (int pass = 0; pass < passes; pass++) {
            // Reset delay lines but keep weights
            std::fill(ff_delay_.begin(), ff_delay_.end(), complex_t(0, 0));
            std::fill(fb_delay_.begin(), fb_delay_.end(), complex_t(0, 0));
            
            for (size_t i = 0; i < len; i++) {
                process(inputs[i], &training[i]);
            }
        }
    }
    
    /**
     * Equalize a block of symbols
     * @param inputs Input symbols
     * @param outputs Output buffer
     * @param training Optional training sequence for first symbols
     */
    void equalize_block(const std::vector<complex_t>& inputs,
                        std::vector<complex_t>& outputs,
                        const std::vector<complex_t>* training = nullptr) {
        
        outputs.resize(inputs.size());
        
        for (size_t i = 0; i < inputs.size(); i++) {
            const complex_t* train_ptr = nullptr;
            if (training && i < training->size()) {
                train_ptr = &(*training)[i];
            }
            outputs[i] = process(inputs[i], train_ptr);
        }
    }
    
    /**
     * Get current feedforward weights
     */
    const std::vector<complex_t>& ff_weights() const { return ff_weights_; }
    
    /**
     * Get current feedback weights
     */
    const std::vector<complex_t>& fb_weights() const { return fb_weights_; }

private:
    RLSConfig config_;
    
    // Filter weights
    std::vector<complex_t> ff_weights_;
    std::vector<complex_t> fb_weights_;
    
    // Delay lines
    std::vector<complex_t> ff_delay_;
    std::vector<complex_t> fb_delay_;
    
    // Inverse correlation matrices (stored as flat vectors)
    std::vector<complex_t> P_ff_;
    std::vector<complex_t> P_fb_;
    
    /**
     * RLS weight update
     */
    void adapt_rls(complex_t error) {
        // Update FF weights using RLS
        update_weights_rls(ff_weights_, ff_delay_, P_ff_, 
                           config_.ff_taps, error);
        
        // Update FB weights using RLS
        update_weights_rls(fb_weights_, fb_delay_, P_fb_,
                           config_.fb_taps, error);
    }
    
    /**
     * RLS weight update for one filter
     * 
     * k = P*x / (λ + x'*P*x)
     * w = w + k*conj(e)
     * P = (P - k*x'*P) / λ
     */
    void update_weights_rls(std::vector<complex_t>& w,
                            const std::vector<complex_t>& x,
                            std::vector<complex_t>& P,
                            int N,
                            complex_t error) {
        
        if (N == 0) return;
        
        // k = P*x
        std::vector<complex_t> k(N);
        for (int i = 0; i < N; i++) {
            k[i] = complex_t(0, 0);
            for (int j = 0; j < N; j++) {
                k[i] += P[i * N + j] * x[j];
            }
        }
        
        // denom = λ + x'*P*x = λ + x'*k
        complex_t denom(config_.lambda, 0);
        for (int i = 0; i < N; i++) {
            denom += std::conj(x[i]) * k[i];
        }
        
        // k = k / denom
        complex_t denom_inv = complex_t(1, 0) / denom;
        for (int i = 0; i < N; i++) {
            k[i] *= denom_inv;
        }
        
        // w = w + k*conj(error)
        complex_t error_conj = std::conj(error);
        for (int i = 0; i < N; i++) {
            w[i] += k[i] * error_conj;
        }
        
        // P = (P - k*x'*P) / λ
        // First compute x'*P (row vector)
        std::vector<complex_t> xHP(N);
        for (int j = 0; j < N; j++) {
            xHP[j] = complex_t(0, 0);
            for (int i = 0; i < N; i++) {
                xHP[j] += std::conj(x[i]) * P[i * N + j];
            }
        }
        
        // P = P - k*xHP
        float lambda_inv = 1.0f / config_.lambda;
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                P[i * N + j] = (P[i * N + j] - k[i] * xHP[j]) * lambda_inv;
            }
        }
    }
    
    /**
     * 8-PSK hard decision
     */
    static complex_t hard_decision_8psk(complex_t sym) {
        float angle = std::atan2(sym.imag(), sym.real());
        if (angle < 0) angle += 2 * M_PI;
        
        int idx = static_cast<int>(std::round(angle / (M_PI / 4))) % 8;
        
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
        
        return PSK8[idx];
    }
};

} // namespace m110a

#endif // RLS_EQUALIZER_H
