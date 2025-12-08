#ifndef M110A_VITERBI_H
#define M110A_VITERBI_H

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>

namespace m110a {

/**
 * Convolutional Encoder and Viterbi Decoder
 * 
 * Implementation based on MIL-STD-188-110A Appendix C:
 *   Section C.3.3: Convolutional Encoder
 *   Table C-III: Encoder Polynomials
 * 
 * Encoder: K=7, rate 1/2
 * Generator polynomials (octal):
 *   G1 = 155 (0x6D) = 1101101
 *   G2 = 117 (0x4F) = 1001111
 * 
 * For each input bit, outputs 2 coded bits.
 */
class ConvEncoder {
public:
    ConvEncoder() : state_(0) {}
    
    void reset() { state_ = 0; }
    
    /**
     * Encode one input bit
     * @param bit Input bit (0 or 1)
     * @return Pair of output bits {g1, g2}
     * 
     * Uses MS-DMT convention: right-shift, new bit enters at MSB (bit 6)
     */
    std::pair<uint8_t, uint8_t> encode_bit(uint8_t bit) {
        // MS-DMT: encode_state = encode_state >> 1; if (in) encode_state |= 0x40;
        state_ = state_ >> 1;
        if (bit & 1) state_ |= 0x40;
        
        // Compute outputs using generator polynomials
        uint8_t g1 = parity(state_ & VITERBI_G1);
        uint8_t g2 = parity(state_ & VITERBI_G2);
        
        return {g1, g2};
    }
    
    /**
     * Encode a block of bits
     * @param input Input bits
     * @param output Output bits (will be 2x input size + tail)
     * @param flush If true, flush encoder with K-1 zeros
     */
    void encode(const std::vector<uint8_t>& input,
                std::vector<uint8_t>& output,
                bool flush = true) {
        
        output.clear();
        output.reserve(input.size() * 2 + (flush ? (VITERBI_K - 1) * 2 : 0));
        
        for (uint8_t bit : input) {
            auto [g1, g2] = encode_bit(bit);
            output.push_back(g1);
            output.push_back(g2);
        }
        
        // Flush with zeros to return to state 0
        if (flush) {
            for (int i = 0; i < VITERBI_K - 1; i++) {
                auto [g1, g2] = encode_bit(0);
                output.push_back(g1);
                output.push_back(g2);
            }
        }
    }
    
    uint8_t state() const { return state_; }

private:
    uint8_t state_;  // 7-bit shift register state
    
    // Count 1s in byte (parity)
    static uint8_t parity(uint8_t x) {
        x ^= x >> 4;
        x ^= x >> 2;
        x ^= x >> 1;
        return x & 1;
    }
};

/**
 * Viterbi Decoder (K=7, rate 1/2)
 * 
 * Supports both hard and soft decision decoding.
 * Uses fixed-point metrics for efficiency.
 * 
 * Soft decisions use signed 8-bit values:
 *   -127 = strong 0
 *   +127 = strong 1
 *   0 = erasure/unknown
 */
class ViterbiDecoder {
public:
    using metric_t = int32_t;
    
    static constexpr int K = VITERBI_K;
    static constexpr int NUM_STATES = VITERBI_STATES;
    static constexpr int TRACEBACK_LENGTH = 5 * K;
    static constexpr metric_t METRIC_MAX = 1000000;
    
    struct Config {
        int traceback_length;
        
        Config() : traceback_length(TRACEBACK_LENGTH) {}
    };
    
    explicit ViterbiDecoder(const Config& config = Config{})
        : config_(config)
        , bits_decoded_(0) {
        
        precompute_transitions();
        reset();
    }
    
    void reset() {
        for (int s = 0; s < NUM_STATES; s++) {
            path_metrics_[s] = METRIC_MAX;
        }
        path_metrics_[0] = 0;
        
        history_.clear();
        bits_decoded_ = 0;
    }
    
    /**
     * Decode soft decision symbols
     */
    int decode_soft(soft_bit_t soft1, soft_bit_t soft2) {
        metric_t new_metrics[NUM_STATES];
        // Store both previous state AND input for proper traceback
        std::array<std::pair<uint8_t, uint8_t>, NUM_STATES> new_history;
        
        for (int s = 0; s < NUM_STATES; s++) {
            new_metrics[s] = METRIC_MAX;
            new_history[s] = {0, 0};
        }
        
        // ACS: for each state, find best incoming path
        for (int state = 0; state < NUM_STATES; state++) {
            if (path_metrics_[state] >= METRIC_MAX) continue;
            
            for (int input = 0; input < 2; input++) {
                int next = next_state_[state][input];
                int output = branch_output_[state][input];
                
                // Branch metric
                // MS-DMT convention: +soft = logic 0, -soft = logic 1
                // For minimum metric when bits match:
                // - Expected 0, soft positive → low metric (good)
                // - Expected 1, soft negative → low metric (good)
                int b1 = (output >> 1) & 1;
                int b2 = output & 1;
                metric_t m1 = (b1 == 0) ? (127 - soft1) : (127 + soft1);
                metric_t m2 = (b2 == 0) ? (127 - soft2) : (127 + soft2);
                metric_t branch = m1 + m2;
                
                metric_t candidate = path_metrics_[state] + branch;
                
                if (candidate < new_metrics[next]) {
                    new_metrics[next] = candidate;
                    new_history[next] = {static_cast<uint8_t>(state), 
                                         static_cast<uint8_t>(input)};
                }
            }
        }
        
        // Update metrics
        metric_t min_metric = METRIC_MAX;
        for (int s = 0; s < NUM_STATES; s++) {
            path_metrics_[s] = new_metrics[s];
            if (new_metrics[s] < min_metric) {
                min_metric = new_metrics[s];
            }
        }
        
        // Normalize
        if (min_metric > 10000) {
            for (int s = 0; s < NUM_STATES; s++) {
                if (path_metrics_[s] < METRIC_MAX) {
                    path_metrics_[s] -= min_metric;
                }
            }
        }
        
        // Store history (previous state and input for each destination state)
        history_.push_back(new_history);
        bits_decoded_++;
        
        // Output if we have enough history
        if ((int)history_.size() >= config_.traceback_length) {
            return traceback_one();
        }
        
        return -1;
    }
    
    int decode_hard(uint8_t bit1, uint8_t bit2) {
        return decode_soft(bit1 ? 127 : -127, bit2 ? 127 : -127);
    }
    
    int decode_block(const std::vector<soft_bit_t>& soft_bits,
                     std::vector<uint8_t>& output,
                     bool flush = true) {
        
        int count = 0;
        
        for (size_t i = 0; i + 1 < soft_bits.size(); i += 2) {
            int bit = decode_soft(soft_bits[i], soft_bits[i + 1]);
            if (bit >= 0) {
                output.push_back(bit);
                count++;
            }
        }
        
        if (flush) {
            auto remaining = flush_decoder();
            for (uint8_t b : remaining) {
                output.push_back(b);
                count++;
            }
        }
        
        return count;
    }
    
    int decode_block_hard(const std::vector<uint8_t>& hard_bits,
                          std::vector<uint8_t>& output,
                          bool flush = true) {
        
        std::vector<soft_bit_t> soft;
        for (uint8_t bit : hard_bits) {
            // MS-DMT convention: +soft = bit 0, -soft = bit 1
            soft.push_back(bit ? -127 : 127);
        }
        return decode_block(soft, output, flush);
    }
    
    std::vector<uint8_t> flush_decoder() {
        std::vector<uint8_t> output;
        
        while (!history_.empty()) {
            int bit = traceback_one();
            if (bit >= 0) {
                output.push_back(bit);
            }
        }
        
        return output;
    }
    
    int bits_decoded() const { return bits_decoded_; }
    
    int best_state() const {
        int best = 0;
        metric_t best_metric = path_metrics_[0];
        for (int s = 1; s < NUM_STATES; s++) {
            if (path_metrics_[s] < best_metric) {
                best_metric = path_metrics_[s];
                best = s;
            }
        }
        return best;
    }
    
    metric_t path_metric(int state) const {
        return path_metrics_[state];
    }

private:
    Config config_;
    
    metric_t path_metrics_[NUM_STATES];
    
    // History: for each time step, for each state: (previous_state, input)
    std::vector<std::array<std::pair<uint8_t, uint8_t>, NUM_STATES>> history_;
    
    int bits_decoded_;
    
    int next_state_[NUM_STATES][2];
    int branch_output_[NUM_STATES][2];
    
    void precompute_transitions() {
        // MS-DMT Viterbi transitions:
        // next_state = (state >> 1) | (input << 5)
        // output = parity(state | (input << 6))
        for (int state = 0; state < NUM_STATES; state++) {
            for (int input = 0; input < 2; input++) {
                // State transition: right-shift, input at bit 5
                int next = (state >> 1) | (input << 5);
                next_state_[state][input] = next;
                
                // Output: parity of (state | input<<6) which is the full 7-bit encoder state
                int full_state = state | (input << 6);
                int g1 = __builtin_parity(full_state & VITERBI_G1);
                int g2 = __builtin_parity(full_state & VITERBI_G2);
                branch_output_[state][input] = (g1 << 1) | g2;
            }
        }
    }
    
    int traceback_one() {
        if (history_.empty()) return -1;
        
        // Find best ending state
        int state = best_state();
        
        // Traceback through history to find the oldest input
        std::vector<uint8_t> inputs;
        for (int i = history_.size() - 1; i >= 0; i--) {
            auto [prev_state, input] = history_[i][state];
            inputs.push_back(input);
            state = prev_state;
        }
        
        // The oldest input is the one to output
        int output_bit = inputs.back();
        
        // Remove the oldest history entry
        history_.erase(history_.begin());
        
        return output_bit;
    }
};

/**
 * Soft symbol to soft bit converter for 8-PSK
 * 
 * Converts complex 8-PSK symbols to soft bit values for the Viterbi decoder.
 * Each 8-PSK symbol carries 3 bits, so we need to generate 3 soft outputs.
 */
class SoftDemapper8PSK {
public:
    /**
     * Compute soft bits from 8-PSK symbol
     * @param symbol Received symbol (after equalization)
     * @param noise_var Estimated noise variance
     * @param soft_bits Output soft bits (3 values)
     */
    static void demap(complex_t symbol, float noise_var,
                      std::array<soft_bit_t, 3>& soft_bits) {
        
        float mag = std::abs(symbol);
        if (mag < 0.01f) {
            // Very weak symbol - erasure
            soft_bits = {0, 0, 0};
            return;
        }
        
        // Get phase
        float phase = std::arg(symbol);
        
        // Scale factor based on SNR
        float scale = mag / (noise_var + 0.01f);
        scale = std::min(scale, 20.0f);  // Limit to prevent saturation
        
        // For Gray-coded 8-PSK, compute LLRs for each bit
        // Bit 0 (LSB): distinguishes between even/odd sectors
        // Bit 1: distinguishes quadrant
        // Bit 2 (MSB): distinguishes hemisphere
        
        // Simple approximation using phase
        // Wrap phase to [0, 2π]
        if (phase < 0) phase += 2.0f * PI;
        
        // Sector (0-7)
        float sector_f = phase / (PI / 4.0f);
        int sector = static_cast<int>(sector_f) & 7;
        float phase_in_sector = sector_f - sector;  // [0, 1)
        
        // Compute soft decisions based on distance to decision boundaries
        // Using simplified approach based on phase
        
        // Bit 2 (MSB): 0 for sectors 0-3, 1 for sectors 4-7
        float dist_b2 = (sector < 4) ? 
                        (sector_f - 4.0f) : (sector_f - 4.0f);
        soft_bits[2] = clamp_soft(dist_b2 * scale * 30.0f);
        
        // Bit 1: pattern depends on sector
        float dist_b1;
        if (sector == 0 || sector == 1 || sector == 4 || sector == 5) {
            dist_b1 = (phase_in_sector - 0.5f) * 2.0f;
        } else {
            dist_b1 = (0.5f - phase_in_sector) * 2.0f;
        }
        soft_bits[1] = clamp_soft(dist_b1 * scale * 30.0f);
        
        // Bit 0 (LSB): alternates each sector
        float dist_b0 = (phase_in_sector - 0.5f) * 2.0f;
        if (sector & 1) dist_b0 = -dist_b0;
        soft_bits[0] = clamp_soft(dist_b0 * scale * 30.0f);
    }
    
private:
    static soft_bit_t clamp_soft(float value) {
        if (value > 127.0f) return 127;
        if (value < -127.0f) return -127;
        return static_cast<soft_bit_t>(value);
    }
};

} // namespace m110a

#endif // M110A_VITERBI_H
