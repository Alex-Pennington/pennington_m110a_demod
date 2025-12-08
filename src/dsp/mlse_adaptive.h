/**
 * @file mlse_adaptive.h
 * @brief Per-Frame Adaptive MLSE Equalizer with Turbo Equalization
 * 
 * Improvements over basic MLSE:
 * 1. Per-frame channel tracking using probe symbols
 * 2. LMS-style continuous adaptation during decision-directed mode
 * 3. Soft output generation for turbo equalization
 * 4. Iterative decoder-equalizer feedback (turbo)
 * 
 * Reference: Douillard et al., "Iterative Correction of Intersymbol Interference"
 */

#ifndef MLSE_ADAPTIVE_H
#define MLSE_ADAPTIVE_H

#include <vector>
#include <complex>
#include <cmath>
#include <array>
#include <algorithm>
#include "../modem/scrambler_fixed.h"

namespace m110a {

using complex_t = std::complex<float>;

// 8-PSK constellation
static const std::array<complex_t, 8> MLSE_PSK8 = {{
    complex_t( 1.000f,  0.000f),
    complex_t( 0.707f,  0.707f),
    complex_t( 0.000f,  1.000f),
    complex_t(-0.707f,  0.707f),
    complex_t(-1.000f,  0.000f),
    complex_t(-0.707f, -0.707f),
    complex_t( 0.000f, -1.000f),
    complex_t( 0.707f, -0.707f)
}};

struct AdaptiveMLSEConfig {
    int channel_memory = 2;          // L: channel taps (2 = 8 states, 3 = 64 states)
    int traceback_depth = 20;        // Viterbi traceback
    float adaptation_rate = 0.01f;   // LMS step size for channel tracking
    bool track_during_data = true;   // DD adaptation during data symbols
    float noise_variance = 0.1f;     // For soft output scaling
    int turbo_iterations = 0;        // 0 = no turbo, >0 = iterative
};

/**
 * Soft output from MLSE (for turbo equalization)
 */
struct SoftSymbol {
    int hard_decision;           // Most likely symbol (0-7)
    float reliability;           // Log-likelihood ratio magnitude
    std::array<float, 8> probs;  // Probability for each symbol
};

/**
 * Adaptive MLSE with Per-Frame Channel Tracking
 */
class AdaptiveMLSE {
public:
    explicit AdaptiveMLSE(const AdaptiveMLSEConfig& cfg = AdaptiveMLSEConfig())
        : cfg_(cfg)
        , L_(cfg.channel_memory)
        , num_states_(compute_num_states(L_))
    {
        reset();
    }
    
    void reset() {
        // Initialize channel to identity
        h_.assign(L_, complex_t(0, 0));
        h_[0] = complex_t(1, 0);
        
        // Initialize Viterbi states
        path_metrics_.assign(num_states_, 1e30f);
        path_metrics_[0] = 0;  // Start in state 0
        
        // Survivor memory
        int depth = cfg_.traceback_depth + 10;
        survivors_.assign(depth, std::vector<int>(num_states_, 0));
        survivor_states_.assign(depth, std::vector<int>(num_states_, 0));
        time_idx_ = 0;
        
        // Precompute transitions
        build_trellis();
    }
    
    /**
     * Estimate channel from known training sequence
     */
    void estimate_channel(const std::vector<complex_t>& received,
                         const std::vector<complex_t>& known) {
        if (received.size() < (size_t)L_ || known.size() < (size_t)L_) return;
        
        int N = std::min(received.size(), known.size());
        
        // Least squares: h = (S^H S)^-1 S^H r
        // For small L, direct solve is fine
        
        // Build S matrix (Toeplitz from known symbols)
        // S[n][k] = known[n-k] for k=0..L-1
        
        // Compute S^H * S (L x L)
        std::vector<std::vector<complex_t>> SHS(L_, std::vector<complex_t>(L_, 0));
        std::vector<complex_t> SHr(L_, 0);
        
        for (int n = L_ - 1; n < N; n++) {
            for (int i = 0; i < L_; i++) {
                complex_t si = known[n - i];
                SHr[i] += std::conj(si) * received[n];
                for (int j = 0; j < L_; j++) {
                    complex_t sj = known[n - j];
                    SHS[i][j] += std::conj(si) * sj;
                }
            }
        }
        
        // Add regularization
        for (int i = 0; i < L_; i++) {
            SHS[i][i] += complex_t(0.001f, 0);
        }
        
        // Solve via Gaussian elimination
        solve_linear(SHS, SHr, h_);
        
        // Update expected outputs
        update_expected_outputs();
    }
    
    /**
     * Update channel estimate with single known symbol (LMS)
     */
    void adapt_channel(complex_t received, complex_t known, 
                      const std::vector<complex_t>& history) {
        if (history.size() < (size_t)L_) return;
        
        // Compute expected received
        complex_t expected(0, 0);
        for (int i = 0; i < L_; i++) {
            expected += h_[i] * history[history.size() - 1 - i];
        }
        
        // Error
        complex_t error = received - expected;
        
        // LMS update: h += mu * error * conj(x)
        float mu = cfg_.adaptation_rate;
        for (int i = 0; i < L_; i++) {
            h_[i] += mu * error * std::conj(history[history.size() - 1 - i]);
        }
        
        update_expected_outputs();
    }
    
    /**
     * Equalize with per-frame probe-based channel tracking
     * 
     * @param received Input symbols
     * @param unknown_len Data symbols per frame
     * @param known_len Probe symbols per frame  
     * @return Equalized symbol indices
     */
    std::vector<int> equalize_with_tracking(
            const std::vector<complex_t>& received,
            int unknown_len, int known_len) {
        
        std::vector<int> output;
        output.reserve(received.size());
        
        int frame_len = unknown_len + known_len;
        size_t idx = 0;
        int frame = 0;
        
        // History for channel tracking
        std::vector<complex_t> symbol_history;
        symbol_history.reserve(received.size());
        
        while (idx + frame_len <= received.size()) {
            // === Process data symbols (decision-directed) ===
            for (int i = 0; i < unknown_len && idx + i < received.size(); i++) {
                int sym = process_symbol(received[idx + i]);
                if (sym >= 0) {
                    output.push_back(sym);
                    symbol_history.push_back(MLSE_PSK8[sym]);
                    
                    // DD channel adaptation
                    if (cfg_.track_during_data && symbol_history.size() >= (size_t)L_) {
                        adapt_channel(received[idx + i], MLSE_PSK8[sym], symbol_history);
                    }
                }
            }
            
            // === Process probe symbols (training) ===
            // Generate expected probes using scrambler
            DataScramblerFixed scrambler;
            int scr_pos = frame * frame_len + unknown_len;
            for (int i = 0; i < scr_pos; i++) scrambler.next();
            
            // Collect probe pairs for batch channel update
            std::vector<complex_t> probe_rx, probe_tx;
            for (int i = 0; i < known_len && idx + unknown_len + i < received.size(); i++) {
                int scr = scrambler.next();
                complex_t expected = MLSE_PSK8[scr];
                
                probe_rx.push_back(received[idx + unknown_len + i]);
                probe_tx.push_back(expected);
                
                // Also process through MLSE (using known training)
                int sym = process_symbol_training(received[idx + unknown_len + i], expected);
                if (sym >= 0) {
                    output.push_back(sym);
                    symbol_history.push_back(expected);
                }
            }
            
            // Batch channel update from probes
            if (probe_rx.size() >= (size_t)L_) {
                update_channel_from_probes(probe_rx, probe_tx);
            }
            
            idx += frame_len;
            frame++;
        }
        
        // Process remaining
        while (idx < received.size()) {
            int sym = process_symbol(received[idx++]);
            if (sym >= 0) output.push_back(sym);
        }
        
        // Flush
        auto remaining = flush();
        output.insert(output.end(), remaining.begin(), remaining.end());
        
        return output;
    }
    
    /**
     * Generate soft outputs for turbo equalization
     */
    std::vector<SoftSymbol> equalize_soft(const std::vector<complex_t>& received) {
        std::vector<SoftSymbol> output;
        output.reserve(received.size());
        
        // For each received symbol, compute soft probability for each PSK point
        // based on distance metric with channel estimation
        
        for (size_t t = 0; t < received.size(); t++) {
            SoftSymbol soft;
            complex_t r = received[t];
            
            // Compute distance to each constellation point
            // Account for ISI from previous symbols using channel estimate
            float noise_var = cfg_.noise_variance;
            float max_log_prob = -1e30f;
            
            for (int s = 0; s < 8; s++) {
                // Expected received for symbol s (including channel)
                complex_t expected = h_[0] * MLSE_PSK8[s];
                
                // Add ISI contribution from estimated previous symbols
                // Use hard decisions from previous outputs
                for (int k = 1; k < L_ && k <= static_cast<int>(t); k++) {
                    if (k <= static_cast<int>(t) && t >= static_cast<size_t>(k)) {
                        int prev_sym = output[t - k].hard_decision;
                        expected += h_[k] * MLSE_PSK8[prev_sym];
                    }
                }
                
                // Distance metric
                float dist_sq = std::norm(r - expected);
                
                // Log probability (Gaussian noise model)
                float log_prob = -dist_sq / (2.0f * noise_var);
                soft.probs[s] = log_prob;
                max_log_prob = std::max(max_log_prob, log_prob);
            }
            
            // Convert to probabilities (normalized)
            float sum = 0;
            for (int s = 0; s < 8; s++) {
                soft.probs[s] = std::exp(soft.probs[s] - max_log_prob);
                sum += soft.probs[s];
            }
            for (int s = 0; s < 8; s++) {
                soft.probs[s] /= sum;
            }
            
            // Hard decision
            soft.hard_decision = 0;
            float best_prob = soft.probs[0];
            for (int s = 1; s < 8; s++) {
                if (soft.probs[s] > best_prob) {
                    best_prob = soft.probs[s];
                    soft.hard_decision = s;
                }
            }
            
            // Reliability
            soft.reliability = std::log(best_prob + 1e-10f) - 
                              std::log((1.0f - best_prob) / 7.0f + 1e-10f);
            
            output.push_back(soft);
        }
        
        return output;
    }
    
    /**
     * Turbo equalization iteration
     * Uses decoder soft outputs as priors for next equalization pass
     */
    std::vector<SoftSymbol> turbo_iteration(
            const std::vector<complex_t>& received,
            const std::vector<SoftSymbol>& decoder_output) {
        
        // Incorporate decoder priors into branch metrics
        // This modifies the MLSE to use extrinsic information
        
        std::vector<SoftSymbol> output;
        output.reserve(received.size());
        
        reset();
        
        for (size_t t = 0; t < received.size(); t++) {
            complex_t r = received[t];
            
            // Modified ACS with priors
            std::vector<float> next_metrics(num_states_, 1e30f);
            std::vector<int> next_survivors(num_states_, 0);
            std::vector<int> next_states(num_states_, 0);
            
            for (int state = 0; state < num_states_; state++) {
                if (path_metrics_[state] > 1e29f) continue;
                
                for (int input = 0; input < 8; input++) {
                    // Branch metric from channel
                    complex_t expected = expected_outputs_[state][input];
                    float bm = std::norm(r - expected);
                    
                    // Add prior from decoder (extrinsic info)
                    if (t < decoder_output.size()) {
                        float prior = -std::log(decoder_output[t].probs[input] + 1e-10f);
                        bm += 0.5f * prior;  // Weight factor
                    }
                    
                    float pm = path_metrics_[state] + bm;
                    int next = next_state_[state][input];
                    
                    if (pm < next_metrics[next]) {
                        next_metrics[next] = pm;
                        next_survivors[next] = input;
                        next_states[next] = state;
                    }
                }
            }
            
            path_metrics_ = next_metrics;
            
            int hist_idx = time_idx_ % survivors_.size();
            survivors_[hist_idx] = next_survivors;
            survivor_states_[hist_idx] = next_states;
            time_idx_++;
            
            // Generate soft output
            SoftSymbol soft;
            float min_metric = *std::min_element(path_metrics_.begin(), path_metrics_.end());
            
            // Compute symbol probabilities from path metrics
            float sum = 0;
            for (int s = 0; s < 8; s++) {
                float best_metric_for_s = 1e30f;
                for (int state = 0; state < num_states_; state++) {
                    if (next_survivors[state] == s) {
                        best_metric_for_s = std::min(best_metric_for_s, next_metrics[state]);
                    }
                }
                soft.probs[s] = std::exp(-(best_metric_for_s - min_metric) / (2 * cfg_.noise_variance));
                sum += soft.probs[s];
            }
            
            for (int s = 0; s < 8; s++) soft.probs[s] /= sum;
            
            soft.hard_decision = std::max_element(soft.probs.begin(), soft.probs.end()) - soft.probs.begin();
            soft.reliability = std::log(soft.probs[soft.hard_decision] + 1e-10f);
            
            output.push_back(soft);
        }
        
        return output;
    }
    
    const std::vector<complex_t>& channel() const { return h_; }

private:
    AdaptiveMLSEConfig cfg_;
    int L_;  // Channel memory
    int num_states_;
    
    std::vector<complex_t> h_;  // Channel taps
    
    // Viterbi state
    std::vector<float> path_metrics_;
    std::vector<std::vector<int>> survivors_;
    std::vector<std::vector<int>> survivor_states_;
    int time_idx_ = 0;
    
    // Trellis structure
    std::vector<std::vector<int>> next_state_;        // [state][input] -> next state
    std::vector<std::vector<complex_t>> expected_outputs_;  // [state][input] -> expected rx
    
    static int compute_num_states(int L) {
        int s = 1;
        for (int i = 0; i < L - 1; i++) s *= 8;
        return s;
    }
    
    void build_trellis() {
        next_state_.assign(num_states_, std::vector<int>(8));
        expected_outputs_.assign(num_states_, std::vector<complex_t>(8));
        
        for (int state = 0; state < num_states_; state++) {
            for (int input = 0; input < 8; input++) {
                // Next state: shift in new symbol
                if (L_ == 2) {
                    next_state_[state][input] = input;
                } else {
                    next_state_[state][input] = (state * 8 + input) % num_states_;
                }
            }
        }
        
        update_expected_outputs();
    }
    
    void update_expected_outputs() {
        for (int state = 0; state < num_states_; state++) {
            // Decode state to get symbol history
            std::vector<int> history;
            int s = state;
            for (int i = 0; i < L_ - 1; i++) {
                history.push_back(s % 8);
                s /= 8;
            }
            std::reverse(history.begin(), history.end());
            
            for (int input = 0; input < 8; input++) {
                // Expected output = h[0]*x[n] + h[1]*x[n-1] + ...
                complex_t expected = h_[0] * MLSE_PSK8[input];
                for (int i = 0; i < (int)history.size() && i + 1 < L_; i++) {
                    expected += h_[i + 1] * MLSE_PSK8[history[history.size() - 1 - i]];
                }
                expected_outputs_[state][input] = expected;
            }
        }
    }
    
    void acs_step(complex_t received) {
        std::vector<float> next_metrics(num_states_, 1e30f);
        std::vector<int> next_survivors(num_states_, 0);
        std::vector<int> next_surv_states(num_states_, 0);
        
        for (int state = 0; state < num_states_; state++) {
            if (path_metrics_[state] > 1e29f) continue;
            
            for (int input = 0; input < 8; input++) {
                complex_t expected = expected_outputs_[state][input];
                float bm = std::norm(received - expected);
                float pm = path_metrics_[state] + bm;
                
                int next = next_state_[state][input];
                if (pm < next_metrics[next]) {
                    next_metrics[next] = pm;
                    next_survivors[next] = input;
                    next_surv_states[next] = state;
                }
            }
        }
        
        path_metrics_ = next_metrics;
        
        int hist_idx = time_idx_ % survivors_.size();
        survivors_[hist_idx] = next_survivors;
        survivor_states_[hist_idx] = next_surv_states;
        time_idx_++;
    }
    
    int process_symbol(complex_t received) {
        acs_step(received);
        return traceback();
    }
    
    int process_symbol_training(complex_t received, complex_t known) {
        // Force known symbol in trellis
        int known_idx = 0;
        float min_dist = 1e30f;
        for (int i = 0; i < 8; i++) {
            float d = std::norm(known - MLSE_PSK8[i]);
            if (d < min_dist) { min_dist = d; known_idx = i; }
        }
        
        // Modified ACS using known symbol
        std::vector<float> next_metrics(num_states_, 1e30f);
        std::vector<int> next_survivors(num_states_, 0);
        std::vector<int> next_surv_states(num_states_, 0);
        
        for (int state = 0; state < num_states_; state++) {
            if (path_metrics_[state] > 1e29f) continue;
            
            // Only allow transitions with known input
            int input = known_idx;
            complex_t expected = expected_outputs_[state][input];
            float bm = std::norm(received - expected);
            float pm = path_metrics_[state] + bm;
            
            int next = next_state_[state][input];
            if (pm < next_metrics[next]) {
                next_metrics[next] = pm;
                next_survivors[next] = input;
                next_surv_states[next] = state;
            }
        }
        
        path_metrics_ = next_metrics;
        
        int hist_idx = time_idx_ % survivors_.size();
        survivors_[hist_idx] = next_survivors;
        survivor_states_[hist_idx] = next_surv_states;
        time_idx_++;
        
        return traceback();
    }
    
    int traceback() {
        if (time_idx_ < cfg_.traceback_depth) return -1;
        
        // Find best current state
        int best_state = 0;
        float best_metric = path_metrics_[0];
        for (int s = 1; s < num_states_; s++) {
            if (path_metrics_[s] < best_metric) {
                best_metric = path_metrics_[s];
                best_state = s;
            }
        }
        
        // Traceback
        int state = best_state;
        int oldest_symbol = -1;
        int history_size = survivors_.size();
        
        for (int i = 0; i < cfg_.traceback_depth; i++) {
            int hist_idx = (time_idx_ - 1 - i + history_size) % history_size;
            
            if (i == cfg_.traceback_depth - 1) {
                oldest_symbol = survivors_[hist_idx][state];
            }
            
            int prev_state = survivor_states_[hist_idx][state];
            if (prev_state >= 0) state = prev_state;
            else break;
        }
        
        return oldest_symbol;
    }
    
    std::vector<int> flush() {
        std::vector<int> output;
        
        int best_state = 0;
        float best_metric = path_metrics_[0];
        for (int s = 1; s < num_states_; s++) {
            if (path_metrics_[s] < best_metric) {
                best_metric = path_metrics_[s];
                best_state = s;
            }
        }
        
        int remaining = std::min(time_idx_, cfg_.traceback_depth - 1);
        if (remaining <= 0) return output;
        
        std::vector<int> reversed;
        int state = best_state;
        int history_size = survivors_.size();
        
        for (int i = 0; i < remaining; i++) {
            int hist_idx = (time_idx_ - 1 - i + history_size) % history_size;
            int sym = survivors_[hist_idx][state];
            if (sym >= 0) reversed.push_back(sym);
            
            int prev = survivor_states_[hist_idx][state];
            if (prev >= 0) state = prev;
            else break;
        }
        
        for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
            output.push_back(*it);
        }
        
        return output;
    }
    
    void update_channel_from_probes(const std::vector<complex_t>& rx,
                                   const std::vector<complex_t>& tx) {
        // Quick LMS-style update from probe block
        for (size_t i = L_ - 1; i < rx.size(); i++) {
            complex_t expected(0, 0);
            for (int k = 0; k < L_; k++) {
                expected += h_[k] * tx[i - k];
            }
            complex_t error = rx[i] - expected;
            
            float mu = cfg_.adaptation_rate;
            for (int k = 0; k < L_; k++) {
                h_[k] += mu * error * std::conj(tx[i - k]);
            }
        }
        update_expected_outputs();
    }
    
    void solve_linear(std::vector<std::vector<complex_t>>& A,
                     std::vector<complex_t>& b,
                     std::vector<complex_t>& x) {
        int n = A.size();
        x.resize(n);
        
        // Gaussian elimination with partial pivoting
        for (int col = 0; col < n; col++) {
            // Find pivot
            int pivot = col;
            float pivot_mag = std::abs(A[col][col]);
            for (int row = col + 1; row < n; row++) {
                if (std::abs(A[row][col]) > pivot_mag) {
                    pivot_mag = std::abs(A[row][col]);
                    pivot = row;
                }
            }
            
            if (pivot != col) {
                std::swap(A[col], A[pivot]);
                std::swap(b[col], b[pivot]);
            }
            
            if (pivot_mag < 1e-10f) continue;
            
            // Eliminate
            for (int row = col + 1; row < n; row++) {
                complex_t factor = A[row][col] / A[col][col];
                for (int j = col; j < n; j++) {
                    A[row][j] -= factor * A[col][j];
                }
                b[row] -= factor * b[col];
            }
        }
        
        // Back substitution
        for (int i = n - 1; i >= 0; i--) {
            if (std::abs(A[i][i]) < 1e-10f) {
                x[i] = (i == 0) ? complex_t(1, 0) : complex_t(0, 0);
            } else {
                complex_t sum = b[i];
                for (int j = i + 1; j < n; j++) {
                    sum -= A[i][j] * x[j];
                }
                x[i] = sum / A[i][i];
            }
        }
    }
};

} // namespace m110a

#endif // MLSE_ADAPTIVE_H
