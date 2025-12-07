#ifndef M110A_MLSE_ADVANCED_H
#define M110A_MLSE_ADVANCED_H

/**
 * Advanced MLSE Features
 * 
 * Phase 4: Reduced-state techniques
 * - DDFSE (Delayed Decision Feedback Sequence Estimation)
 * - M-algorithm (beam search with M best states)
 * 
 * Phase 5: Soft-Output Viterbi Algorithm (SOVA)
 * - Generates reliability information for FEC
 * - Path metric differences for soft decisions
 * 
 * Phase 6: SIMD Optimization
 * - Vectorized ACS operations
 * - Parallel metric computation
 * 
 * References:
 * - Hagenauer & Hoeher, "A Viterbi algorithm with soft-decision outputs", IEEE GLOBECOM 1989
 * - Duel-Hallen & Heegard, "Delayed Decision-Feedback Sequence Estimation", IEEE Trans COM 1989
 */

#include "common/types.h"
#include "common/constants.h"
#include "dsp/mlse_equalizer.h"
#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <algorithm>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace m110a {

// ============================================================================
// Soft-Output Viterbi Algorithm (SOVA)
// ============================================================================

/**
 * Soft output for one symbol decision
 */
struct SoftSymbol {
    int hard_decision;      // Most likely symbol (0-7)
    float reliability;      // Log-likelihood ratio magnitude
    std::array<float, 8> symbol_llrs;  // LLR for each possible symbol
    
    SoftSymbol() : hard_decision(0), reliability(0.0f) {
        symbol_llrs.fill(0.0f);
    }
};

/**
 * SOVA Configuration
 */
struct SOVAConfig {
    int channel_memory = 3;
    int traceback_depth = 25;
    float noise_variance = 0.1f;  // For LLR scaling
    bool normalize_llrs = true;   // Normalize LLRs to [-1, 1] range
};

/**
 * Soft-Output Viterbi Algorithm Equalizer
 * 
 * Extends standard MLSE with soft output generation.
 * Computes reliability information based on path metric differences
 * between the ML path and competing paths.
 */
class SOVAEqualizer {
public:
    explicit SOVAEqualizer(const SOVAConfig& config = SOVAConfig())
        : config_(config)
        , num_states_(compute_num_states(config.channel_memory))
        , channel_taps_(config.channel_memory, complex_t(1, 0)) {
        
        initialize();
    }
    
    /**
     * Set channel estimate
     */
    void set_channel(const std::vector<complex_t>& taps) {
        channel_taps_ = taps;
        channel_taps_.resize(config_.channel_memory, complex_t(0, 0));
        update_expected_outputs();
    }
    
    /**
     * Estimate channel from known symbols
     */
    void estimate_channel(const std::vector<complex_t>& known_symbols,
                         const std::vector<complex_t>& received);
    
    /**
     * Equalize with soft outputs
     */
    std::vector<SoftSymbol> equalize_soft(const std::vector<complex_t>& received);
    
    /**
     * Get hard decisions only (for compatibility)
     */
    std::vector<int> equalize(const std::vector<complex_t>& received) {
        auto soft = equalize_soft(received);
        std::vector<int> hard;
        hard.reserve(soft.size());
        for (const auto& s : soft) {
            hard.push_back(s.hard_decision);
        }
        return hard;
    }
    
    /**
     * Reset state
     */
    void reset();
    
    const SOVAConfig& config() const { return config_; }

private:
    SOVAConfig config_;
    int num_states_;
    std::vector<complex_t> channel_taps_;
    
    // State transition table
    struct Transition {
        int next_state;
        int input_symbol;
        complex_t expected_output;
    };
    std::vector<std::vector<Transition>> transitions_;
    
    // Extended Viterbi state for SOVA
    struct SOVAState {
        float path_metric = std::numeric_limits<float>::infinity();
        int survivor_input = -1;
        int survivor_state = -1;
        float delta_metric = 0.0f;  // Difference to next best path
    };
    
    std::vector<SOVAState> current_states_;
    std::vector<SOVAState> next_states_;
    
    // History for soft output computation
    struct HistoryEntry {
        int input;
        int prev_state;
        float delta;  // Path metric difference at this point
    };
    std::vector<std::vector<HistoryEntry>> history_;
    int history_idx_ = 0;
    int symbols_processed_ = 0;
    
    static int compute_num_states(int L) {
        int s = 1;
        for (int i = 0; i < L - 1; i++) s *= 8;
        return s;
    }
    
    void initialize();
    void update_expected_outputs();
    void acs_step_sova(complex_t received);
    SoftSymbol traceback_soft();
    std::vector<SoftSymbol> flush_soft();
    
    float branch_metric(complex_t received, complex_t expected) const {
        complex_t diff = received - expected;
        return std::norm(diff);
    }
    
    void state_to_symbols(int state, std::vector<int>& symbols) const;
};

// ============================================================================
// DDFSE - Delayed Decision Feedback Sequence Estimation
// ============================================================================

/**
 * DDFSE Configuration
 */
struct DDFSEConfig {
    int mlse_taps = 3;        // Number of taps handled by MLSE (L')
    int dfe_taps = 2;         // Number of taps handled by DFE
    int traceback_depth = 20;
    float dfe_mu = 0.01f;     // DFE adaptation rate
};

/**
 * DDFSE Equalizer
 * 
 * Hybrid MLSE/DFE approach:
 * - MLSE handles first L' taps (recent ISI)
 * - DFE handles remaining taps (older ISI) using decisions
 * 
 * Complexity: O(8^(L'-1)) instead of O(8^(L-1))
 * For L=5, L'=3: 64 states instead of 4096
 */
class DDFSEEqualizer {
public:
    explicit DDFSEEqualizer(const DDFSEConfig& config = DDFSEConfig())
        : config_(config)
        , num_states_(compute_num_states(config.mlse_taps))
        , channel_taps_(config.mlse_taps + config.dfe_taps, complex_t(0, 0))
        , dfe_taps_(config.dfe_taps, complex_t(0, 0))
        , decision_buffer_(config.dfe_taps, 0) {
        
        channel_taps_[0] = complex_t(1, 0);
        initialize();
    }
    
    /**
     * Set full channel estimate
     * First mlse_taps are handled by Viterbi, rest by DFE
     */
    void set_channel(const std::vector<complex_t>& taps) {
        int total = config_.mlse_taps + config_.dfe_taps;
        channel_taps_.resize(total, complex_t(0, 0));
        
        for (size_t i = 0; i < std::min(taps.size(), static_cast<size_t>(total)); i++) {
            channel_taps_[i] = taps[i];
        }
        
        // Extract DFE portion
        for (int i = 0; i < config_.dfe_taps; i++) {
            if (config_.mlse_taps + i < static_cast<int>(channel_taps_.size())) {
                dfe_taps_[i] = channel_taps_[config_.mlse_taps + i];
            }
        }
        
        update_expected_outputs();
    }
    
    /**
     * Equalize received symbols
     */
    std::vector<int> equalize(const std::vector<complex_t>& received);
    
    /**
     * Reset state
     */
    void reset();
    
    /**
     * Get effective complexity
     */
    int num_states() const { return num_states_; }
    int full_states() const {
        return compute_num_states(config_.mlse_taps + config_.dfe_taps);
    }

private:
    DDFSEConfig config_;
    int num_states_;
    std::vector<complex_t> channel_taps_;
    std::vector<complex_t> dfe_taps_;
    std::vector<int> decision_buffer_;  // Past decisions for DFE
    
    struct Transition {
        int next_state;
        int input_symbol;
        complex_t base_expected;  // Without DFE contribution
    };
    std::vector<std::vector<Transition>> transitions_;
    
    struct State {
        float path_metric = std::numeric_limits<float>::infinity();
        int survivor_input = -1;
        int survivor_state = -1;
    };
    std::vector<State> current_states_;
    std::vector<State> next_states_;
    
    struct HistoryEntry {
        int input;
        int prev_state;
    };
    std::vector<std::vector<HistoryEntry>> history_;
    int history_idx_ = 0;
    int symbols_processed_ = 0;
    
    static int compute_num_states(int L) {
        int s = 1;
        for (int i = 0; i < L - 1; i++) s *= 8;
        return s;
    }
    
    void initialize();
    void update_expected_outputs();
    void acs_step(complex_t received);
    int traceback_one();
    std::vector<int> flush();
    
    void state_to_symbols(int state, std::vector<int>& symbols) const;
    
    // Compute DFE contribution from past decisions
    complex_t compute_dfe_contribution() const {
        if (decision_buffer_.empty() || config_.dfe_taps == 0) {
            return complex_t(0, 0);
        }
        const auto& constellation = get_8psk_constellation();
        complex_t dfe_out(0, 0);
        for (int i = 0; i < config_.dfe_taps; i++) {
            size_t buf_idx = (decision_buffer_.size() - 1 - i) % decision_buffer_.size();
            int past_sym = decision_buffer_[buf_idx];
            dfe_out += dfe_taps_[i] * constellation[past_sym];
        }
        return dfe_out;
    }
};

// ============================================================================
// SIMD-Optimized Branch Metric Computation
// ============================================================================

#ifdef __SSE2__
/**
 * SSE2-optimized Euclidean distance computation
 * Computes 4 complex distances in parallel
 */
inline void compute_branch_metrics_sse2(
    const complex_t* received,   // Single received symbol (broadcast)
    const complex_t* expected,   // 4 expected values
    float* metrics,              // 4 output metrics
    int count) {
    
    // Load received symbol and broadcast
    __m128 rx_real = _mm_set1_ps(received->real());
    __m128 rx_imag = _mm_set1_ps(received->imag());
    
    for (int i = 0; i < count; i += 4) {
        // Load 4 expected values (interleaved real/imag)
        __m128 exp0 = _mm_loadu_ps(reinterpret_cast<const float*>(&expected[i]));
        __m128 exp1 = _mm_loadu_ps(reinterpret_cast<const float*>(&expected[i+2]));
        
        // Deinterleave to separate real and imag
        __m128 exp_real = _mm_shuffle_ps(exp0, exp1, _MM_SHUFFLE(2, 0, 2, 0));
        __m128 exp_imag = _mm_shuffle_ps(exp0, exp1, _MM_SHUFFLE(3, 1, 3, 1));
        
        // Compute differences
        __m128 diff_real = _mm_sub_ps(rx_real, exp_real);
        __m128 diff_imag = _mm_sub_ps(rx_imag, exp_imag);
        
        // Compute |diff|^2 = real^2 + imag^2
        __m128 sq_real = _mm_mul_ps(diff_real, diff_real);
        __m128 sq_imag = _mm_mul_ps(diff_imag, diff_imag);
        __m128 result = _mm_add_ps(sq_real, sq_imag);
        
        // Store results
        _mm_storeu_ps(&metrics[i], result);
    }
}
#endif

#ifdef __AVX2__
/**
 * AVX2-optimized Euclidean distance computation
 * Computes 8 complex distances in parallel
 */
inline void compute_branch_metrics_avx2(
    const complex_t* received,
    const complex_t* expected,
    float* metrics,
    int count) {
    
    __m256 rx_real = _mm256_set1_ps(received->real());
    __m256 rx_imag = _mm256_set1_ps(received->imag());
    
    for (int i = 0; i < count; i += 8) {
        // Load 8 expected values
        __m256 exp0 = _mm256_loadu_ps(reinterpret_cast<const float*>(&expected[i]));
        __m256 exp1 = _mm256_loadu_ps(reinterpret_cast<const float*>(&expected[i+4]));
        
        // Deinterleave (more complex with AVX)
        __m256 exp_real = _mm256_shuffle_ps(exp0, exp1, _MM_SHUFFLE(2, 0, 2, 0));
        __m256 exp_imag = _mm256_shuffle_ps(exp0, exp1, _MM_SHUFFLE(3, 1, 3, 1));
        exp_real = _mm256_permutevar8x32_ps(exp_real, _mm256_setr_epi32(0,1,4,5,2,3,6,7));
        exp_imag = _mm256_permutevar8x32_ps(exp_imag, _mm256_setr_epi32(0,1,4,5,2,3,6,7));
        
        // Compute differences and squared magnitudes
        __m256 diff_real = _mm256_sub_ps(rx_real, exp_real);
        __m256 diff_imag = _mm256_sub_ps(rx_imag, exp_imag);
        __m256 result = _mm256_fmadd_ps(diff_real, diff_real,
                                        _mm256_mul_ps(diff_imag, diff_imag));
        
        _mm256_storeu_ps(&metrics[i], result);
    }
}
#endif

/**
 * Scalar fallback for branch metric computation
 */
inline void compute_branch_metrics_scalar(
    const complex_t* received,
    const complex_t* expected,
    float* metrics,
    int count) {
    
    for (int i = 0; i < count; i++) {
        complex_t diff = *received - expected[i];
        metrics[i] = std::norm(diff);
    }
}

/**
 * Auto-dispatch to best available SIMD
 */
inline void compute_branch_metrics(
    const complex_t* received,
    const complex_t* expected,
    float* metrics,
    int count) {
    
#ifdef __AVX2__
    if (count >= 8) {
        int aligned_count = (count / 8) * 8;
        compute_branch_metrics_avx2(received, expected, metrics, aligned_count);
        // Handle remainder
        for (int i = aligned_count; i < count; i++) {
            complex_t diff = *received - expected[i];
            metrics[i] = std::norm(diff);
        }
        return;
    }
#endif

#ifdef __SSE2__
    if (count >= 4) {
        int aligned_count = (count / 4) * 4;
        compute_branch_metrics_sse2(received, expected, metrics, aligned_count);
        for (int i = aligned_count; i < count; i++) {
            complex_t diff = *received - expected[i];
            metrics[i] = std::norm(diff);
        }
        return;
    }
#endif
    
    compute_branch_metrics_scalar(received, expected, metrics, count);
}

// ============================================================================
// SOVA Implementation
// ============================================================================

inline void SOVAEqualizer::initialize() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.channel_memory;
    
    // Allocate transitions
    transitions_.resize(num_states_);
    for (int s = 0; s < num_states_; s++) {
        transitions_[s].resize(8);
    }
    
    // Build transition table
    for (int state = 0; state < num_states_; state++) {
        for (int input = 0; input < 8; input++) {
            Transition& trans = transitions_[state][input];
            trans.input_symbol = input;
            
            if (L == 2) {
                trans.next_state = input;
            } else {
                trans.next_state = (input * (num_states_ / 8)) + (state / 8);
            }
            
            trans.expected_output = complex_t(0, 0);
        }
    }
    
    // Allocate states
    current_states_.resize(num_states_);
    next_states_.resize(num_states_);
    
    // Allocate history
    int history_size = config_.traceback_depth + 10;
    history_.resize(history_size);
    for (auto& h : history_) {
        h.resize(num_states_);
    }
    
    reset();
}

inline void SOVAEqualizer::update_expected_outputs() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.channel_memory;
    
    for (int state = 0; state < num_states_; state++) {
        std::vector<int> prev_symbols(L - 1);
        state_to_symbols(state, prev_symbols);
        
        for (int input = 0; input < 8; input++) {
            complex_t expected = channel_taps_[0] * constellation[input];
            
            for (int k = 1; k < L && k <= static_cast<int>(prev_symbols.size()); k++) {
                expected += channel_taps_[k] * constellation[prev_symbols[k-1]];
            }
            
            transitions_[state][input].expected_output = expected;
        }
    }
}

inline void SOVAEqualizer::state_to_symbols(int state, std::vector<int>& symbols) const {
    int divisor = num_states_ / 8;
    for (size_t i = 0; i < symbols.size(); i++) {
        symbols[i] = (state / divisor) % 8;
        divisor /= 8;
        if (divisor == 0) divisor = 1;
    }
}

inline void SOVAEqualizer::reset() {
    for (int s = 0; s < num_states_; s++) {
        current_states_[s].path_metric = std::numeric_limits<float>::infinity();
        current_states_[s].survivor_input = -1;
        current_states_[s].survivor_state = -1;
        current_states_[s].delta_metric = 0.0f;
    }
    current_states_[0].path_metric = 0.0f;
    
    for (auto& h : history_) {
        for (auto& e : h) {
            e.input = -1;
            e.prev_state = -1;
            e.delta = 0.0f;
        }
    }
    history_idx_ = 0;
    symbols_processed_ = 0;
}

inline void SOVAEqualizer::estimate_channel(
    const std::vector<complex_t>& known_symbols,
    const std::vector<complex_t>& received) {
    
    int L = config_.channel_memory;
    int N = std::min(known_symbols.size(), received.size());
    
    if (N < L + 10) {
        channel_taps_.assign(L, complex_t(0, 0));
        channel_taps_[0] = complex_t(1, 0);
        update_expected_outputs();
        return;
    }
    
    // Least Squares with Gaussian elimination (same as MLSEEqualizer)
    std::vector<std::vector<complex_t>> SHS(L, std::vector<complex_t>(L, complex_t(0,0)));
    std::vector<complex_t> SHr(L, complex_t(0,0));
    
    for (int n = L - 1; n < N; n++) {
        std::vector<complex_t> s_row(L);
        for (int k = 0; k < L; k++) {
            s_row[k] = known_symbols[n - k];
        }
        
        for (int i = 0; i < L; i++) {
            for (int j = 0; j < L; j++) {
                SHS[i][j] += std::conj(s_row[i]) * s_row[j];
            }
            SHr[i] += std::conj(s_row[i]) * received[n];
        }
    }
    
    // Gaussian elimination
    std::vector<std::vector<complex_t>> aug(L, std::vector<complex_t>(L + 1));
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < L; j++) aug[i][j] = SHS[i][j];
        aug[i][L] = SHr[i];
    }
    
    for (int col = 0; col < L; col++) {
        int pivot_row = col;
        float pivot_mag = std::abs(aug[col][col]);
        for (int row = col + 1; row < L; row++) {
            if (std::abs(aug[row][col]) > pivot_mag) {
                pivot_mag = std::abs(aug[row][col]);
                pivot_row = row;
            }
        }
        if (pivot_row != col) std::swap(aug[col], aug[pivot_row]);
        
        if (pivot_mag < 1e-10f) continue;
        
        for (int row = col + 1; row < L; row++) {
            complex_t factor = aug[row][col] / aug[col][col];
            for (int j = col; j <= L; j++) {
                aug[row][j] -= factor * aug[col][j];
            }
        }
    }
    
    for (int i = L - 1; i >= 0; i--) {
        if (std::abs(aug[i][i]) < 1e-10f) {
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

inline void SOVAEqualizer::acs_step_sova(complex_t received) {
    // Reset next states
    for (int s = 0; s < num_states_; s++) {
        next_states_[s].path_metric = std::numeric_limits<float>::infinity();
        next_states_[s].survivor_input = -1;
        next_states_[s].survivor_state = -1;
        next_states_[s].delta_metric = std::numeric_limits<float>::infinity();
    }
    
    // Compute all branch metrics (could use SIMD here)
    std::vector<float> all_metrics(num_states_ * 8);
    for (int state = 0; state < num_states_; state++) {
        for (int input = 0; input < 8; input++) {
            all_metrics[state * 8 + input] = 
                branch_metric(received, transitions_[state][input].expected_output);
        }
    }
    
    // ACS with tracking second-best path
    for (int state = 0; state < num_states_; state++) {
        if (current_states_[state].path_metric >= std::numeric_limits<float>::infinity() / 2) {
            continue;
        }
        
        for (int input = 0; input < 8; input++) {
            const Transition& trans = transitions_[state][input];
            float bm = all_metrics[state * 8 + input];
            float pm = current_states_[state].path_metric + bm;
            
            if (pm < next_states_[trans.next_state].path_metric) {
                // New best path - old best becomes second best
                next_states_[trans.next_state].delta_metric = 
                    next_states_[trans.next_state].path_metric - pm;
                next_states_[trans.next_state].path_metric = pm;
                next_states_[trans.next_state].survivor_input = input;
                next_states_[trans.next_state].survivor_state = state;
            } else {
                // Update delta if this is second best
                float delta = pm - next_states_[trans.next_state].path_metric;
                if (delta < next_states_[trans.next_state].delta_metric) {
                    next_states_[trans.next_state].delta_metric = delta;
                }
            }
        }
    }
    
    // Store history with delta metrics
    int hist_idx = history_idx_ % history_.size();
    for (int s = 0; s < num_states_; s++) {
        history_[hist_idx][s].input = next_states_[s].survivor_input;
        history_[hist_idx][s].prev_state = next_states_[s].survivor_state;
        history_[hist_idx][s].delta = next_states_[s].delta_metric;
    }
    history_idx_++;
    
    std::swap(current_states_, next_states_);
    symbols_processed_++;
}

inline SoftSymbol SOVAEqualizer::traceback_soft() {
    SoftSymbol result;
    
    if (symbols_processed_ < config_.traceback_depth) {
        result.hard_decision = -1;
        return result;
    }
    
    // Find best state
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    // Traceback and accumulate reliability
    int state = best_state;
    float min_delta = std::numeric_limits<float>::infinity();
    int target_symbol = -1;
    int history_size = static_cast<int>(history_.size());
    
    for (int i = 0; i < config_.traceback_depth; i++) {
        int hist_idx = (history_idx_ - 1 - i + history_size) % history_size;
        const HistoryEntry& entry = history_[hist_idx][state];
        
        if (i == config_.traceback_depth - 1) {
            target_symbol = entry.input;
        }
        
        // Track minimum delta along path (determines reliability)
        if (entry.delta < min_delta) {
            min_delta = entry.delta;
        }
        
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    result.hard_decision = (target_symbol >= 0) ? target_symbol : 0;
    
    // Convert delta to LLR-like reliability
    // Higher delta = more reliable decision
    if (min_delta < std::numeric_limits<float>::infinity()) {
        result.reliability = min_delta / (2.0f * config_.noise_variance);
        if (config_.normalize_llrs) {
            result.reliability = std::tanh(result.reliability);  // Normalize to [-1, 1]
        }
    } else {
        result.reliability = 1.0f;  // Maximum reliability
    }
    
    // For now, just set the decided symbol LLR high, others low
    // A more sophisticated implementation would track competing symbols
    for (int i = 0; i < 8; i++) {
        result.symbol_llrs[i] = (i == result.hard_decision) ? result.reliability : -result.reliability;
    }
    
    return result;
}

inline std::vector<SoftSymbol> SOVAEqualizer::equalize_soft(const std::vector<complex_t>& received) {
    reset();
    
    std::vector<SoftSymbol> output;
    output.reserve(received.size());
    
    for (const auto& r : received) {
        acs_step_sova(r);
        auto soft = traceback_soft();
        if (soft.hard_decision >= 0) {
            output.push_back(soft);
        }
    }
    
    auto remaining = flush_soft();
    output.insert(output.end(), remaining.begin(), remaining.end());
    
    return output;
}

inline std::vector<SoftSymbol> SOVAEqualizer::flush_soft() {
    std::vector<SoftSymbol> output;
    
    if (symbols_processed_ == 0) return output;
    
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    int streaming_outputs = std::max(0, symbols_processed_ - config_.traceback_depth + 1);
    int remaining = symbols_processed_ - streaming_outputs;
    
    std::vector<SoftSymbol> reversed;
    reversed.reserve(remaining);
    
    int state = best_state;
    int history_size = static_cast<int>(history_.size());
    float cumulative_delta = std::numeric_limits<float>::infinity();
    
    for (int i = 0; i < remaining; i++) {
        int hist_idx = (history_idx_ - 1 - i + history_size) % history_size;
        const HistoryEntry& entry = history_[hist_idx][state];
        
        if (entry.input >= 0) {
            SoftSymbol soft;
            soft.hard_decision = entry.input;
            
            if (entry.delta < cumulative_delta) {
                cumulative_delta = entry.delta;
            }
            
            soft.reliability = (cumulative_delta < std::numeric_limits<float>::infinity()) ?
                std::tanh(cumulative_delta / (2.0f * config_.noise_variance)) : 1.0f;
            
            for (int j = 0; j < 8; j++) {
                soft.symbol_llrs[j] = (j == soft.hard_decision) ? soft.reliability : -soft.reliability;
            }
            
            reversed.push_back(soft);
        }
        
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        output.push_back(*it);
    }
    
    return output;
}

// ============================================================================
// DDFSE Implementation
// ============================================================================

inline void DDFSEEqualizer::initialize() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.mlse_taps;
    
    transitions_.resize(num_states_);
    for (int s = 0; s < num_states_; s++) {
        transitions_[s].resize(8);
    }
    
    for (int state = 0; state < num_states_; state++) {
        for (int input = 0; input < 8; input++) {
            Transition& trans = transitions_[state][input];
            trans.input_symbol = input;
            
            if (L == 2) {
                trans.next_state = input;
            } else {
                trans.next_state = (input * (num_states_ / 8)) + (state / 8);
            }
            
            trans.base_expected = complex_t(0, 0);
        }
    }
    
    current_states_.resize(num_states_);
    next_states_.resize(num_states_);
    
    int history_size = config_.traceback_depth + 10;
    history_.resize(history_size);
    for (auto& h : history_) {
        h.resize(num_states_);
    }
    
    reset();
}

inline void DDFSEEqualizer::update_expected_outputs() {
    const auto& constellation = get_8psk_constellation();
    int L = config_.mlse_taps;
    
    for (int state = 0; state < num_states_; state++) {
        std::vector<int> prev_symbols(L - 1);
        state_to_symbols(state, prev_symbols);
        
        for (int input = 0; input < 8; input++) {
            // Only MLSE portion
            complex_t expected = channel_taps_[0] * constellation[input];
            
            for (int k = 1; k < L && k <= static_cast<int>(prev_symbols.size()); k++) {
                expected += channel_taps_[k] * constellation[prev_symbols[k-1]];
            }
            
            transitions_[state][input].base_expected = expected;
        }
    }
}

inline void DDFSEEqualizer::state_to_symbols(int state, std::vector<int>& symbols) const {
    int divisor = num_states_ / 8;
    for (size_t i = 0; i < symbols.size(); i++) {
        symbols[i] = (state / divisor) % 8;
        divisor /= 8;
        if (divisor == 0) divisor = 1;
    }
}

inline void DDFSEEqualizer::reset() {
    for (int s = 0; s < num_states_; s++) {
        current_states_[s].path_metric = std::numeric_limits<float>::infinity();
        current_states_[s].survivor_input = -1;
        current_states_[s].survivor_state = -1;
    }
    current_states_[0].path_metric = 0.0f;
    
    for (auto& h : history_) {
        for (auto& e : h) {
            e.input = -1;
            e.prev_state = -1;
        }
    }
    history_idx_ = 0;
    symbols_processed_ = 0;
    
    std::fill(decision_buffer_.begin(), decision_buffer_.end(), 0);
}

inline void DDFSEEqualizer::acs_step(complex_t received) {
    // Compute DFE contribution from past decisions
    complex_t dfe_contrib = compute_dfe_contribution();
    
    // Subtract DFE contribution from received
    complex_t adjusted_received = received - dfe_contrib;
    
    // Reset next states
    for (int s = 0; s < num_states_; s++) {
        next_states_[s].path_metric = std::numeric_limits<float>::infinity();
        next_states_[s].survivor_input = -1;
        next_states_[s].survivor_state = -1;
    }
    
    // Standard ACS on adjusted signal
    for (int state = 0; state < num_states_; state++) {
        if (current_states_[state].path_metric >= std::numeric_limits<float>::infinity() / 2) {
            continue;
        }
        
        for (int input = 0; input < 8; input++) {
            const Transition& trans = transitions_[state][input];
            
            complex_t diff = adjusted_received - trans.base_expected;
            float bm = std::norm(diff);
            float pm = current_states_[state].path_metric + bm;
            
            if (pm < next_states_[trans.next_state].path_metric) {
                next_states_[trans.next_state].path_metric = pm;
                next_states_[trans.next_state].survivor_input = input;
                next_states_[trans.next_state].survivor_state = state;
            }
        }
    }
    
    // Store history
    int hist_idx = history_idx_ % history_.size();
    for (int s = 0; s < num_states_; s++) {
        history_[hist_idx][s].input = next_states_[s].survivor_input;
        history_[hist_idx][s].prev_state = next_states_[s].survivor_state;
    }
    history_idx_++;
    
    std::swap(current_states_, next_states_);
    symbols_processed_++;
}

inline int DDFSEEqualizer::traceback_one() {
    if (symbols_processed_ < config_.traceback_depth) {
        return -1;
    }
    
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    int state = best_state;
    int oldest_symbol = -1;
    int history_size = static_cast<int>(history_.size());
    
    for (int i = 0; i < config_.traceback_depth; i++) {
        int hist_idx = (history_idx_ - 1 - i + history_size) % history_size;
        const HistoryEntry& entry = history_[hist_idx][state];
        
        if (i == config_.traceback_depth - 1) {
            oldest_symbol = entry.input;
        }
        
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    // Update decision buffer for DFE
    if (oldest_symbol >= 0 && !decision_buffer_.empty()) {
        // Shift buffer and add new decision
        for (size_t i = 0; i < decision_buffer_.size() - 1; i++) {
            decision_buffer_[i] = decision_buffer_[i + 1];
        }
        decision_buffer_.back() = oldest_symbol;
    }
    
    return oldest_symbol;
}

inline std::vector<int> DDFSEEqualizer::flush() {
    std::vector<int> output;
    
    if (symbols_processed_ == 0) return output;
    
    int best_state = 0;
    float best_metric = current_states_[0].path_metric;
    for (int s = 1; s < num_states_; s++) {
        if (current_states_[s].path_metric < best_metric) {
            best_metric = current_states_[s].path_metric;
            best_state = s;
        }
    }
    
    int streaming_outputs = std::max(0, symbols_processed_ - config_.traceback_depth + 1);
    int remaining = symbols_processed_ - streaming_outputs;
    
    std::vector<int> reversed;
    reversed.reserve(remaining);
    
    int state = best_state;
    int history_size = static_cast<int>(history_.size());
    
    for (int i = 0; i < remaining; i++) {
        int hist_idx = (history_idx_ - 1 - i + history_size) % history_size;
        const HistoryEntry& entry = history_[hist_idx][state];
        
        if (entry.input >= 0) {
            reversed.push_back(entry.input);
        }
        
        if (entry.prev_state >= 0) {
            state = entry.prev_state;
        } else {
            break;
        }
    }
    
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        output.push_back(*it);
    }
    
    return output;
}

inline std::vector<int> DDFSEEqualizer::equalize(const std::vector<complex_t>& received) {
    reset();
    
    std::vector<int> output;
    output.reserve(received.size());
    
    for (const auto& r : received) {
        acs_step(r);
        int symbol = traceback_one();
        if (symbol >= 0) {
            output.push_back(symbol);
        }
    }
    
    auto remaining = flush();
    output.insert(output.end(), remaining.begin(), remaining.end());
    
    return output;
}

} // namespace m110a

#endif // M110A_MLSE_ADVANCED_H
