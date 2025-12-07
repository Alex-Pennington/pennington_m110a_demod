#ifndef M110A_MLSE_EQUALIZER_H
#define M110A_MLSE_EQUALIZER_H

/**
 * MLSE Equalizer - Maximum Likelihood Sequence Estimation
 * 
 * Viterbi algorithm-based equalizer for severe multipath channels.
 * Replaces/augments DFE for improved performance on CCIR Moderate/Poor.
 * 
 * Implementation follows phased approach:
 * - Phase 1: L=2 (8 states) - proof of concept
 * - Phase 2: L=3 (64 states) - CCIR Moderate target
 * - Phase 3: L=4+ with reduced-state techniques
 * 
 * Reference: Forney, "Maximum-Likelihood Sequence Estimation", IEEE 1972
 */

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <array>
#include <cmath>
#include <limits>

namespace m110a {

// ============================================================================
// Configuration
// ============================================================================

struct MLSEConfig {
    int channel_memory = 2;          // L: number of channel taps (2-5)
    int traceback_depth = 20;        // Symbols before making decision
    bool adaptive_channel = false;   // Update channel estimate during data
    float adaptation_rate = 0.01f;   // LMS step size for adaptation
    
    // Derived parameters (computed in constructor)
    int num_states() const { 
        // States = M^(L-1) where M=8 for 8-PSK
        int s = 1;
        for (int i = 0; i < channel_memory - 1; i++) s *= 8;
        return s;
    }
    
    int num_transitions() const {
        return num_states() * 8;  // Each state has 8 outgoing transitions
    }
};

// ============================================================================
// Phase 1: Core Data Structures
// ============================================================================

/**
 * 8-PSK constellation points (same as modulator)
 * Indexed 0-7 for symbols
 */
inline const std::array<complex_t, 8>& get_8psk_constellation() {
    static std::array<complex_t, 8> constellation;
    static bool initialized = false;
    
    if (!initialized) {
        for (int i = 0; i < 8; i++) {
            float angle = i * PI / 4.0f;  // 0° + i*45° (matches modem)
            constellation[i] = complex_t(std::cos(angle), std::sin(angle));
        }
        initialized = true;
    }
    return constellation;
}

/**
 * State in the Viterbi trellis
 */
struct ViterbiState {
    float path_metric = std::numeric_limits<float>::infinity();
    int survivor_input = -1;  // Which input led to this state
    int survivor_state = -1;  // Previous state on survivor path
    
    void reset() {
        path_metric = std::numeric_limits<float>::infinity();
        survivor_input = -1;
        survivor_state = -1;
    }
};

/**
 * Survivor history entry for traceback
 */
struct SurvivorEntry {
    int input;       // Input symbol that led to this state
    int prev_state;  // Previous state
};

/**
 * Pre-computed state transition
 */
struct StateTransition {
    int next_state;           // State after this transition
    int input_symbol;         // Input symbol (0-7)
    complex_t expected_output; // Expected received value (computed per channel)
};

// ============================================================================
// MLSE Equalizer Class
// ============================================================================

class MLSEEqualizer {
public:
    explicit MLSEEqualizer(const MLSEConfig& config = MLSEConfig())
        : config_(config)
        , num_states_(config.num_states())
        , channel_taps_(config.channel_memory, complex_t(0, 0)) {
        
        // Initialize to identity channel: h[0] = 1, h[k] = 0 for k > 0
        channel_taps_[0] = complex_t(1, 0);
        
        initialize_trellis();
        update_expected_outputs();  // Compute expected outputs for initial channel
        reset();
    }
    
    /**
     * Estimate channel from known preamble symbols
     * Uses Least Squares estimation
     * 
     * @param known_symbols  Known transmitted symbols
     * @param received       Received samples (same length as known_symbols)
     */
    void estimate_channel(const std::vector<complex_t>& known_symbols,
                         const std::vector<complex_t>& received);
    
    /**
     * Set channel taps directly (for testing)
     */
    void set_channel(const std::vector<complex_t>& taps) {
        channel_taps_ = taps;
        channel_taps_.resize(config_.channel_memory, complex_t(0, 0));
        update_expected_outputs();
    }
    
    /**
     * Get current channel estimate
     */
    const std::vector<complex_t>& get_channel() const { return channel_taps_; }
    
    /**
     * Process received symbols through Viterbi algorithm
     * 
     * @param received  Received complex symbols (after matched filter)
     * @return Decoded symbol indices (0-7 for 8-PSK)
     */
    std::vector<int> equalize(const std::vector<complex_t>& received);
    
    /**
     * Process single received symbol (streaming mode)
     * Returns decoded symbol if traceback depth reached, -1 otherwise
     */
    int process_symbol(complex_t received);
    
    /**
     * Flush remaining symbols at end of block
     */
    std::vector<int> flush();
    
    /**
     * Reset equalizer state
     */
    void reset();
    
    /**
     * Get configuration
     */
    const MLSEConfig& config() const { return config_; }
    
    /**
     * Get path metric for best state (for diagnostics)
     */
    float get_best_metric() const;

private:
    MLSEConfig config_;
    int num_states_;
    
    // Channel model
    std::vector<complex_t> channel_taps_;
    
    // Trellis structure
    std::vector<std::vector<StateTransition>> transitions_;  // [state][input]
    
    // Viterbi state
    std::vector<ViterbiState> current_states_;
    std::vector<ViterbiState> next_states_;
    
    // Survivor path history for traceback
    std::vector<std::vector<SurvivorEntry>> survivor_history_;  // [time][state]
    int history_write_idx_ = 0;
    int symbols_processed_ = 0;
    std::vector<int> output_buffer_;  // Decoded symbols waiting to be output
    
    // Initialize trellis structure
    void initialize_trellis();
    
    // Update expected outputs after channel change
    void update_expected_outputs();
    
    // Compute branch metric (Euclidean distance)
    float branch_metric(complex_t received, complex_t expected) const {
        complex_t diff = received - expected;
        return std::norm(diff);  // |diff|²
    }
    
    // Add-Compare-Select for one symbol time
    void acs_step(complex_t received);
    
    // Traceback to recover symbol sequence
    int traceback_one();
    
    // Convert state index to symbol history
    void state_to_symbols(int state, std::vector<int>& symbols) const;
    
    // Convert symbol history to state index
    int symbols_to_state(const std::vector<int>& symbols) const;
};

// ============================================================================
// Implementation - Phase 1
// ============================================================================

inline void MLSEEqualizer::initialize_trellis() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.channel_memory;
    
    // Allocate transitions: [num_states][8 inputs]
    transitions_.resize(num_states_);
    for (int s = 0; s < num_states_; s++) {
        transitions_[s].resize(8);
    }
    
    // For L=2: state = previous symbol (0-7)
    // For L=3: state = prev_symbol * 8 + prev_prev_symbol
    // etc.
    
    // Build transition table
    for (int state = 0; state < num_states_; state++) {
        for (int input = 0; input < 8; input++) {
            StateTransition& trans = transitions_[state][input];
            trans.input_symbol = input;
            
            // Next state: shift in new symbol
            // next_state = (state * 8 + input) mod num_states
            // But we need to drop oldest symbol
            if (L == 2) {
                trans.next_state = input;  // Only remember most recent
            } else {
                // state encodes [s_{n-1}, s_{n-2}, ..., s_{n-L+1}]
                // next encodes [input, s_{n-1}, ..., s_{n-L+2}]
                trans.next_state = (input * (num_states_ / 8)) + (state / 8);
            }
            
            // Expected output computed when channel is set
            trans.expected_output = complex_t(0, 0);
        }
    }
    
    // Allocate Viterbi states
    current_states_.resize(num_states_);
    next_states_.resize(num_states_);
    
    // Allocate survivor history
    int history_size = config_.traceback_depth + 10;
    survivor_history_.resize(history_size);
    for (auto& h : survivor_history_) {
        h.resize(num_states_);
        for (auto& e : h) {
            e.input = -1;
            e.prev_state = -1;
        }
    }
}

inline void MLSEEqualizer::update_expected_outputs() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.channel_memory;
    
    // For each state and input, compute expected received value
    for (int state = 0; state < num_states_; state++) {
        // Decode state into previous symbols
        std::vector<int> prev_symbols(L - 1);
        state_to_symbols(state, prev_symbols);
        
        for (int input = 0; input < 8; input++) {
            // Expected output: r = h[0]*s[n] + h[1]*s[n-1] + h[2]*s[n-2] + ...
            complex_t expected = channel_taps_[0] * constellation[input];
            
            for (int k = 1; k < L && k <= static_cast<int>(prev_symbols.size()); k++) {
                expected += channel_taps_[k] * constellation[prev_symbols[k-1]];
            }
            
            transitions_[state][input].expected_output = expected;
        }
    }
}

inline void MLSEEqualizer::state_to_symbols(int state, std::vector<int>& symbols) const {
    // Decode state index into symbol history
    // state = s[n-1] * 8^(L-2) + s[n-2] * 8^(L-3) + ... + s[n-L+1]
    int divisor = num_states_ / 8;
    for (size_t i = 0; i < symbols.size(); i++) {
        symbols[i] = (state / divisor) % 8;
        divisor /= 8;
        if (divisor == 0) divisor = 1;
    }
}

inline int MLSEEqualizer::symbols_to_state(const std::vector<int>& symbols) const {
    int state = 0;
    int multiplier = num_states_ / 8;
    for (size_t i = 0; i < symbols.size() && multiplier >= 1; i++) {
        state += symbols[i] * multiplier;
        multiplier /= 8;
    }
    return state;
}

inline void MLSEEqualizer::reset() {
    // Initialize state 0 with metric 0, all others infinity
    for (int s = 0; s < num_states_; s++) {
        current_states_[s].reset();
    }
    current_states_[0].path_metric = 0.0f;
    
    // Clear history
    for (auto& h : survivor_history_) {
        for (auto& e : h) {
            e.input = -1;
            e.prev_state = -1;
        }
    }
    history_write_idx_ = 0;
    symbols_processed_ = 0;
    output_buffer_.clear();
}

inline void MLSEEqualizer::estimate_channel(
    const std::vector<complex_t>& known_symbols,
    const std::vector<complex_t>& received) {
    
    int L = config_.channel_memory;
    int N = std::min(known_symbols.size(), received.size());
    
    if (N < L + 10) {
        // Not enough samples, use default channel
        channel_taps_.assign(L, complex_t(0, 0));
        channel_taps_[0] = complex_t(1, 0);
        update_expected_outputs();
        return;
    }
    
    // Least Squares channel estimation using normal equations
    // Model: r[n] = sum_k h[k] * s[n-k]
    // Solution: h = (S^H * S)^(-1) * S^H * r
    
    // Compute S^H * S (L x L matrix) and S^H * r (L x 1 vector)
    std::vector<std::vector<complex_t>> SHS(L, std::vector<complex_t>(L, complex_t(0,0)));
    std::vector<complex_t> SHr(L, complex_t(0,0));
    
    for (int n = L - 1; n < N; n++) {
        // Build row of S for this time instant
        std::vector<complex_t> s_row(L);
        for (int k = 0; k < L; k++) {
            s_row[k] = known_symbols[n - k];
        }
        
        // Accumulate S^H * S
        for (int i = 0; i < L; i++) {
            for (int j = 0; j < L; j++) {
                SHS[i][j] += std::conj(s_row[i]) * s_row[j];
            }
        }
        
        // Accumulate S^H * r
        for (int i = 0; i < L; i++) {
            SHr[i] += std::conj(s_row[i]) * received[n];
        }
    }
    
    // Solve SHS * h = SHr using Gaussian elimination with partial pivoting
    // Augmented matrix [SHS | SHr]
    std::vector<std::vector<complex_t>> aug(L, std::vector<complex_t>(L + 1));
    std::vector<bool> singular_col(L, false);
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < L; j++) {
            aug[i][j] = SHS[i][j];
        }
        aug[i][L] = SHr[i];
    }
    
    // Forward elimination
    for (int col = 0; col < L; col++) {
        // Find pivot
        int pivot_row = col;
        float pivot_mag = std::abs(aug[col][col]);
        for (int row = col + 1; row < L; row++) {
            if (std::abs(aug[row][col]) > pivot_mag) {
                pivot_mag = std::abs(aug[row][col]);
                pivot_row = row;
            }
        }
        
        // Swap rows if needed
        if (pivot_row != col) {
            std::swap(aug[col], aug[pivot_row]);
        }
        
        // Check for singular matrix
        if (pivot_mag < 1e-10f) {
            singular_col[col] = true;
            continue;
        }
        
        // Eliminate column
        for (int row = col + 1; row < L; row++) {
            complex_t factor = aug[row][col] / aug[col][col];
            for (int j = col; j <= L; j++) {
                aug[row][j] -= factor * aug[col][j];
            }
        }
    }
    
    // Back substitution
    for (int i = L - 1; i >= 0; i--) {
        if (singular_col[i] || std::abs(aug[i][i]) < 1e-10f) {
            // Singular: use default (identity channel for tap 0, zero otherwise)
            channel_taps_[i] = (i == 0) ? complex_t(1, 0) : complex_t(0, 0);
        } else {
            complex_t sum = aug[i][L];
            for (int j = i + 1; j < L; j++) {
                sum -= aug[i][j] * channel_taps_[j];
            }
            channel_taps_[i] = sum / aug[i][i];
        }
    }
    
    update_expected_outputs();
}

inline void MLSEEqualizer::acs_step(complex_t received) {
    // Reset next states
    for (int s = 0; s < num_states_; s++) {
        next_states_[s].reset();
    }
    
    // Add-Compare-Select
    for (int state = 0; state < num_states_; state++) {
        if (current_states_[state].path_metric >= std::numeric_limits<float>::infinity() / 2) {
            continue;  // Skip unreachable states
        }
        
        for (int input = 0; input < 8; input++) {
            const StateTransition& trans = transitions_[state][input];
            
            // Branch metric
            float bm = branch_metric(received, trans.expected_output);
            
            // Path metric
            float pm = current_states_[state].path_metric + bm;
            
            // Compare-Select
            if (pm < next_states_[trans.next_state].path_metric) {
                next_states_[trans.next_state].path_metric = pm;
                next_states_[trans.next_state].survivor_input = input;
                next_states_[trans.next_state].survivor_state = state;
            }
        }
    }
    
    // Store survivor history
    int hist_idx = history_write_idx_ % survivor_history_.size();
    for (int s = 0; s < num_states_; s++) {
        survivor_history_[hist_idx][s].input = next_states_[s].survivor_input;
        survivor_history_[hist_idx][s].prev_state = next_states_[s].survivor_state;
    }
    history_write_idx_++;
    
    // Swap current/next
    std::swap(current_states_, next_states_);
    
    symbols_processed_++;
}

inline int MLSEEqualizer::traceback_one() {
    if (symbols_processed_ < config_.traceback_depth) {
        return -1;  // Not enough history yet
    }
    
    // Find best current state
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    // Traceback through history to find symbol from traceback_depth ago
    int state = best_state;
    int oldest_symbol = -1;
    int history_size = static_cast<int>(survivor_history_.size());
    
    // Trace back traceback_depth steps
    for (int i = 0; i < config_.traceback_depth; i++) {
        // Current position in circular buffer
        int hist_idx = (history_write_idx_ - 1 - i + history_size) % history_size;
        
        const SurvivorEntry& entry = survivor_history_[hist_idx][state];
        
        if (i == config_.traceback_depth - 1) {
            // This is the oldest symbol we want
            oldest_symbol = entry.input;
        }
        
        // Move to previous state
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    return oldest_symbol;
}

inline int MLSEEqualizer::process_symbol(complex_t received) {
    acs_step(received);
    return traceback_one();
}

inline std::vector<int> MLSEEqualizer::equalize(const std::vector<complex_t>& received) {
    reset();
    
    std::vector<int> output;
    output.reserve(received.size());
    
    // Process all symbols
    for (const auto& r : received) {
        int symbol = process_symbol(r);
        if (symbol >= 0) {
            output.push_back(symbol);
        }
    }
    
    // Flush remaining
    auto remaining = flush();
    output.insert(output.end(), remaining.begin(), remaining.end());
    
    return output;
}

inline std::vector<int> MLSEEqualizer::flush() {
    std::vector<int> output;
    
    if (symbols_processed_ == 0) return output;
    
    // Find best final state
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    // Calculate how many symbols haven't been output yet
    // Streaming outputs symbols 0 to (symbols_processed - traceback_depth - 1) when >= traceback_depth
    // Number of streaming outputs = max(0, symbols_processed - traceback_depth + 1)
    // Remaining = symbols_processed - streaming_outputs = min(symbols_processed, traceback_depth - 1)
    int streaming_outputs = std::max(0, symbols_processed_ - config_.traceback_depth + 1);
    int remaining = symbols_processed_ - streaming_outputs;
    
    if (remaining <= 0) return output;
    
    // Traceback remaining symbols (the most recent ones that weren't output)
    std::vector<int> reversed;
    reversed.reserve(remaining);
    
    int state = best_state;
    int history_size = static_cast<int>(survivor_history_.size());
    
    for (int i = 0; i < remaining; i++) {
        int hist_idx = (history_write_idx_ - 1 - i + history_size) % history_size;
        const SurvivorEntry& entry = survivor_history_[hist_idx][state];
        
        if (entry.input >= 0) {
            reversed.push_back(entry.input);
        }
        
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    // Reverse to get correct order
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        output.push_back(*it);
    }
    
    return output;
}

inline float MLSEEqualizer::get_best_metric() const {
    float best = std::numeric_limits<float>::infinity();
    for (int s = 0; s < num_states_; s++) {
        if (current_states_[s].path_metric < best) {
            best = current_states_[s].path_metric;
        }
    }
    return best;
}

} // namespace m110a

#endif // M110A_MLSE_EQUALIZER_H
