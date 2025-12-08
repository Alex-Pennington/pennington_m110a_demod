#ifndef M110A_MULTIMODE_INTERLEAVER_H
#define M110A_MULTIMODE_INTERLEAVER_H

/**
 * Multi-Mode Block Interleaver for MIL-STD-188-110A
 * 
 * Implements the standard helical interleaver with mode-specific parameters.
 * 
 * The interleaver writes data row-by-row with stride row_inc,
 * and reads column-by-column with stride col_inc.
 * This spreads burst errors across multiple codewords.
 */

#include "common/types.h"
#include "m110a/mode_config.h"
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace m110a {

/**
 * Helical Block Interleaver
 * 
 * Write pattern: row = (row + row_inc) % rows
 * Read pattern:  col = (col + col_inc) % cols
 */
class MultiModeInterleaver {
public:
    /**
     * Construct interleaver for specific mode
     */
    explicit MultiModeInterleaver(ModeId mode)
        : mode_(mode) {
        
        const auto& cfg = ModeDatabase::get(mode);
        params_ = cfg.interleaver;
        matrix_.resize(params_.rows * params_.cols);
    }
    
    /**
     * Construct interleaver from parameters
     */
    explicit MultiModeInterleaver(const InterleaverParams& params)
        : params_(params) {
        
        matrix_.resize(params_.rows * params_.cols);
    }
    
    /**
     * Get interleaver parameters
     */
    const InterleaverParams& params() const { return params_; }
    int block_size() const { return params_.rows * params_.cols; }
    int rows() const { return params_.rows; }
    int cols() const { return params_.cols; }
    
    /**
     * Check if this is a passthrough (no-op) interleaver
     */
    bool is_passthrough() const {
        return params_.row_inc == 0 && params_.col_inc == 0;
    }
    
    /**
     * Interleave a block of soft bits (TX side)
     * Input: coded bits in sequential order
     * Output: interleaved bits for transmission
     * 
     * Uses exact MS-DMT algorithm:
     * - Load: write at [row][col], row = (row + row_inc) % rows; if row==0: col++
     * - Fetch: read at [row][col], row++, col = (col + col_inc) % cols; if row==0: col = col_last+1
     */
    std::vector<soft_bit_t> interleave(const std::vector<soft_bit_t>& input) {
        if (static_cast<int>(input.size()) != block_size()) {
            throw std::runtime_error("Interleaver: input size mismatch");
        }
        
        // Passthrough for voice modes with row_inc=0, col_inc=0
        if (is_passthrough()) {
            return input;
        }
        
        // Clear matrix
        std::fill(matrix_.begin(), matrix_.end(), 0);
        
        // Load phase (MS-DMT load_interleaver)
        int row = 0, col = 0;
        for (int i = 0; i < block_size(); i++) {
            matrix_[row * params_.cols + col] = input[i];
            row = (row + params_.row_inc) % params_.rows;
            if (row == 0) {
                col = (col + 1) % params_.cols;
            }
        }
        
        // Fetch phase (MS-DMT fetch_interleaver)
        std::vector<soft_bit_t> output(block_size());
        row = 0; col = 0;
        int col_last = 0;
        for (int i = 0; i < block_size(); i++) {
            output[i] = matrix_[row * params_.cols + col];
            row = (row + 1) % params_.rows;
            col = (col + params_.col_inc) % params_.cols;
            if (row == 0) {
                col = (col_last + 1) % params_.cols;
                col_last = col;
            }
        }
        
        return output;
    }
    
    /**
     * Deinterleave a block of soft bits (RX side)
     * Reverses the interleaving operation
     * 
     * Uses exact MS-DMT algorithm:
     * - Load: write using fetch pattern (row++, col += col_inc, col_last tracking)
     * - Fetch: read using load pattern (row += row_inc, col++ on wrap)
     */
    std::vector<soft_bit_t> deinterleave(const std::vector<soft_bit_t>& input) {
        if (static_cast<int>(input.size()) != block_size()) {
            throw std::runtime_error("Deinterleaver: input size mismatch");
        }
        
        // Passthrough for voice modes with row_inc=0, col_inc=0
        if (is_passthrough()) {
            return input;
        }
        
        // Clear matrix
        std::fill(matrix_.begin(), matrix_.end(), 0);
        
        // Load phase (MS-DMT load_deinterleaver - uses fetch pattern)
        int row = 0, col = 0;
        int col_last = 0;
        for (int i = 0; i < block_size(); i++) {
            matrix_[row * params_.cols + col] = input[i];
            row = (row + 1) % params_.rows;
            col = (col + params_.col_inc) % params_.cols;
            if (row == 0) {
                col = (col_last + 1) % params_.cols;
                col_last = col;
            }
        }
        
        // Fetch phase (MS-DMT fetch_deinterleaver - uses load pattern)
        std::vector<soft_bit_t> output(block_size());
        row = 0; col = 0;
        for (int i = 0; i < block_size(); i++) {
            output[i] = matrix_[row * params_.cols + col];
            row = (row + params_.row_inc) % params_.rows;
            if (row == 0) {
                col = (col + 1) % params_.cols;
            }
        }
        
        return output;
    }
    
    /**
     * Interleave float LLRs (for turbo equalization)
     */
    std::vector<float> interleave_float(const std::vector<float>& input) {
        if (static_cast<int>(input.size()) != block_size()) {
            throw std::runtime_error("Interleaver: input size mismatch");
        }
        
        if (is_passthrough()) {
            return input;
        }
        
        std::vector<float> matrix(block_size());
        
        // Load phase
        int row = 0, col = 0;
        for (int i = 0; i < block_size(); i++) {
            matrix[row * params_.cols + col] = input[i];
            row = (row + params_.row_inc) % params_.rows;
            if (row == 0) {
                col = (col + 1) % params_.cols;
            }
        }
        
        // Fetch phase
        std::vector<float> output(block_size());
        row = 0; col = 0;
        int col_last = 0;
        for (int i = 0; i < block_size(); i++) {
            output[i] = matrix[row * params_.cols + col];
            row = (row + 1) % params_.rows;
            col = (col + params_.col_inc) % params_.cols;
            if (row == 0) {
                col = (col_last + 1) % params_.cols;
                col_last = col;
            }
        }
        
        return output;
    }
    
    /**
     * Deinterleave float LLRs (for turbo equalization)
     */
    std::vector<float> deinterleave_float(const std::vector<float>& input) {
        if (static_cast<int>(input.size()) != block_size()) {
            throw std::runtime_error("Deinterleaver: input size mismatch");
        }
        
        if (is_passthrough()) {
            return input;
        }
        
        std::vector<float> matrix(block_size());
        
        // Load phase (uses fetch pattern)
        int row = 0, col = 0;
        int col_last = 0;
        for (int i = 0; i < block_size(); i++) {
            matrix[row * params_.cols + col] = input[i];
            row = (row + 1) % params_.rows;
            col = (col + params_.col_inc) % params_.cols;
            if (row == 0) {
                col = (col_last + 1) % params_.cols;
                col_last = col;
            }
        }
        
        // Fetch phase (uses load pattern)
        std::vector<float> output(block_size());
        row = 0; col = 0;
        for (int i = 0; i < block_size(); i++) {
            output[i] = matrix[row * params_.cols + col];
            row = (row + params_.row_inc) % params_.rows;
            if (row == 0) {
                col = (col + 1) % params_.cols;
            }
        }
        
        return output;
    }
    
    /**
     * Interleave hard bits (for TX)
     */
    std::vector<uint8_t> interleave_hard(const std::vector<uint8_t>& input) {
        std::vector<soft_bit_t> soft(input.begin(), input.end());
        auto interleaved = interleave(soft);
        return std::vector<uint8_t>(interleaved.begin(), interleaved.end());
    }
    
    /**
     * Deinterleave hard bits
     */
    std::vector<uint8_t> deinterleave_hard(const std::vector<uint8_t>& input) {
        std::vector<soft_bit_t> soft(input.begin(), input.end());
        auto deinterleaved = deinterleave(soft);
        return std::vector<uint8_t>(deinterleaved.begin(), deinterleaved.end());
    }

private:
    ModeId mode_;
    InterleaverParams params_;
    std::vector<soft_bit_t> matrix_;
};

/**
 * Streaming interleaver for continuous data
 * Handles block boundaries automatically
 */
class StreamingInterleaver {
public:
    explicit StreamingInterleaver(ModeId mode)
        : interleaver_(mode)
        , block_count_(0) {}
    
    /**
     * Add bits to interleaver, return complete interleaved blocks
     */
    std::vector<soft_bit_t> process(const std::vector<soft_bit_t>& input) {
        buffer_.insert(buffer_.end(), input.begin(), input.end());
        
        std::vector<soft_bit_t> output;
        int bs = interleaver_.block_size();
        
        while (static_cast<int>(buffer_.size()) >= bs) {
            std::vector<soft_bit_t> block(buffer_.begin(), buffer_.begin() + bs);
            buffer_.erase(buffer_.begin(), buffer_.begin() + bs);
            
            auto interleaved = interleaver_.interleave(block);
            output.insert(output.end(), interleaved.begin(), interleaved.end());
            block_count_++;
        }
        
        return output;
    }
    
    /**
     * Flush remaining data (pad with zeros if needed)
     */
    std::vector<soft_bit_t> flush() {
        if (buffer_.empty()) return {};
        
        int bs = interleaver_.block_size();
        while (static_cast<int>(buffer_.size()) < bs) {
            buffer_.push_back(0);  // Pad with zeros
        }
        
        auto interleaved = interleaver_.interleave(buffer_);
        buffer_.clear();
        block_count_++;
        
        return interleaved;
    }
    
    int block_count() const { return block_count_; }
    int pending_bits() const { return buffer_.size(); }
    void reset() { buffer_.clear(); block_count_ = 0; }

private:
    MultiModeInterleaver interleaver_;
    std::vector<soft_bit_t> buffer_;
    int block_count_;
};

/**
 * Streaming deinterleaver for RX
 */
class StreamingDeinterleaver {
public:
    explicit StreamingDeinterleaver(ModeId mode)
        : deinterleaver_(mode)
        , block_count_(0) {}
    
    std::vector<soft_bit_t> process(const std::vector<soft_bit_t>& input) {
        buffer_.insert(buffer_.end(), input.begin(), input.end());
        
        std::vector<soft_bit_t> output;
        int bs = deinterleaver_.block_size();
        
        while (static_cast<int>(buffer_.size()) >= bs) {
            std::vector<soft_bit_t> block(buffer_.begin(), buffer_.begin() + bs);
            buffer_.erase(buffer_.begin(), buffer_.begin() + bs);
            
            auto deinterleaved = deinterleaver_.deinterleave(block);
            output.insert(output.end(), deinterleaved.begin(), deinterleaved.end());
            block_count_++;
        }
        
        return output;
    }
    
    int block_count() const { return block_count_; }
    int pending_bits() const { return buffer_.size(); }
    void reset() { buffer_.clear(); block_count_ = 0; }

private:
    MultiModeInterleaver deinterleaver_;
    std::vector<soft_bit_t> buffer_;
    int block_count_;
};

} // namespace m110a

#endif // M110A_MULTIMODE_INTERLEAVER_H
