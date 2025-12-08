#ifndef M110A_INTERLEAVER_H
#define M110A_INTERLEAVER_H

#include "common/types.h"
#include "common/constants.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace m110a {

/**
 * Block Interleaver/Deinterleaver for MIL-STD-188-110A
 * 
 * Implementation based on MIL-STD-188-110A Appendix C:
 *   Section C.3.5: Interleaver
 * 
 * Block interleaver spreads burst errors across the codeword,
 * making them easier for the Viterbi decoder to correct.
 * 
 * Interleave modes:
 *   ZERO:  No interleaving
 *   SHORT: 0.6 second block (for low latency)
 *   LONG:  4.8 second block (maximum protection)
 * 
 * The interleaver writes data into a matrix row-by-row and reads
 * column-by-column (or vice versa for deinterleaving).
 */
class BlockInterleaver {
public:
    struct Config {
        InterleaveMode mode;
        int data_rate;        // bits per second
        
        Config() : mode(InterleaveMode::SHORT), data_rate(2400) {}
    };
    
    explicit BlockInterleaver(const Config& config = Config{})
        : config_(config) {
        configure(config);
    }
    
    void configure(const Config& config) {
        config_ = config;
        
        // Calculate block size based on mode and data rate
        float duration = 0.0f;
        switch (config.mode) {
            case InterleaveMode::ZERO:
                rows_ = 1;
                cols_ = 1;
                return;
            case InterleaveMode::SHORT:
                duration = PREAMBLE_DURATION_SHORT;  // 0.6 seconds
                break;
            case InterleaveMode::LONG:
                duration = PREAMBLE_DURATION_LONG;   // 4.8 seconds
                break;
        }
        
        // Block size in bits
        int block_bits = static_cast<int>(duration * config.data_rate);
        
        // Matrix dimensions - use near-square for best burst protection
        // Rows = number of coded bits in one "depth" of interleaving
        // For rate 1/2 code, each info bit produces 2 coded bits
        
        // Standard approach: make rows = constraint length factor
        // and cols = block_size / rows
        rows_ = 40;  // Standard depth
        cols_ = block_bits / rows_;
        
        if (cols_ < 1) cols_ = 1;
        
        buffer_.resize(rows_ * cols_);
    }
    
    /**
     * Interleave a block of data
     * Writes row-by-row, reads column-by-column
     */
    std::vector<uint8_t> interleave(const std::vector<uint8_t>& input) {
        if (config_.mode == InterleaveMode::ZERO) {
            return input;  // Pass through
        }
        
        int block_size = rows_ * cols_;
        std::vector<uint8_t> output;
        output.reserve(input.size());
        
        // Process full blocks
        for (size_t offset = 0; offset < input.size(); offset += block_size) {
            // Fill buffer row-by-row
            std::fill(buffer_.begin(), buffer_.end(), 0);
            
            for (int i = 0; i < block_size && offset + i < input.size(); i++) {
                buffer_[i] = input[offset + i];
            }
            
            // Read column-by-column
            for (int c = 0; c < cols_; c++) {
                for (int r = 0; r < rows_; r++) {
                    int idx = r * cols_ + c;
                    output.push_back(buffer_[idx]);
                }
            }
        }
        
        return output;
    }
    
    /**
     * Deinterleave a block of data
     * Writes column-by-column, reads row-by-row
     */
    std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& input) {
        if (config_.mode == InterleaveMode::ZERO) {
            return input;
        }
        
        int block_size = rows_ * cols_;
        std::vector<uint8_t> output;
        output.reserve(input.size());
        
        for (size_t offset = 0; offset < input.size(); offset += block_size) {
            // Fill buffer column-by-column
            std::fill(buffer_.begin(), buffer_.end(), 0);
            
            int idx = 0;
            for (int c = 0; c < cols_ && idx < block_size; c++) {
                for (int r = 0; r < rows_ && idx < block_size; r++) {
                    if (offset + idx < input.size()) {
                        buffer_[r * cols_ + c] = input[offset + idx];
                    }
                    idx++;
                }
            }
            
            // Read row-by-row
            for (int i = 0; i < block_size; i++) {
                output.push_back(buffer_[i]);
            }
        }
        
        return output;
    }
    
    /**
     * Deinterleave soft decisions
     */
    std::vector<soft_bit_t> deinterleave_soft(const std::vector<soft_bit_t>& input) {
        if (config_.mode == InterleaveMode::ZERO) {
            return input;
        }
        
        int block_size = rows_ * cols_;
        std::vector<soft_bit_t> output;
        output.reserve(input.size());
        
        std::vector<soft_bit_t> soft_buffer(block_size);
        
        for (size_t offset = 0; offset < input.size(); offset += block_size) {
            // Fill buffer column-by-column
            std::fill(soft_buffer.begin(), soft_buffer.end(), 0);
            
            int idx = 0;
            for (int c = 0; c < cols_ && idx < block_size; c++) {
                for (int r = 0; r < rows_ && idx < block_size; r++) {
                    if (offset + idx < input.size()) {
                        soft_buffer[r * cols_ + c] = input[offset + idx];
                    }
                    idx++;
                }
            }
            
            // Read row-by-row
            for (int i = 0; i < block_size; i++) {
                output.push_back(soft_buffer[i]);
            }
        }
        
        return output;
    }
    
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int block_size() const { return rows_ * cols_; }
    InterleaveMode mode() const { return config_.mode; }

private:
    Config config_;
    int rows_;
    int cols_;
    std::vector<uint8_t> buffer_;
};

} // namespace m110a

#endif // M110A_INTERLEAVER_H
