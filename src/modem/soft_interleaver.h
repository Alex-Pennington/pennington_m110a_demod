/**
 * @file soft_interleaver.h
 * @brief Soft Interleaver/Deinterleaver for Turbo Equalization
 * 
 * Reorders soft values (LLRs) while preserving magnitudes.
 * Uses same block interleaver structure as MIL-STD-188-110A.
 * 
 * Block interleaver: writes row-wise, reads column-wise
 * - Rows = interleaver depth (mode dependent)
 * - Cols = symbols per block
 */

#ifndef SOFT_INTERLEAVER_H
#define SOFT_INTERLEAVER_H

#include <vector>
#include <algorithm>

namespace m110a {

class SoftInterleaver {
public:
    /**
     * Create interleaver with given dimensions
     * 
     * @param rows Interleaver depth (typically 40 for short, varies for long)
     * @param cols Symbols per interleaver row
     */
    SoftInterleaver(int rows, int cols) 
        : rows_(rows), cols_(cols), size_(rows * cols) {
        
        // Build permutation tables
        perm_.resize(size_);
        inv_perm_.resize(size_);
        
        // Standard block interleaver: write rows, read columns
        for (int r = 0; r < rows_; r++) {
            for (int c = 0; c < cols_; c++) {
                int write_idx = r * cols_ + c;        // Row-major write
                int read_idx = c * rows_ + r;         // Column-major read
                perm_[write_idx] = read_idx;
                inv_perm_[read_idx] = write_idx;
            }
        }
    }
    
    /**
     * Interleave soft values (encoder side / before channel)
     * 
     * @param input LLRs in natural order
     * @return LLRs in interleaved order
     */
    std::vector<float> interleave(const std::vector<float>& input) const {
        std::vector<float> output(input.size());
        
        size_t full_blocks = input.size() / size_;
        size_t remainder = input.size() % size_;
        
        // Process full blocks
        for (size_t b = 0; b < full_blocks; b++) {
            size_t base = b * size_;
            for (int i = 0; i < size_; i++) {
                output[base + perm_[i]] = input[base + i];
            }
        }
        
        // Pass through remainder (partial block)
        size_t base = full_blocks * size_;
        for (size_t i = 0; i < remainder; i++) {
            output[base + i] = input[base + i];
        }
        
        return output;
    }
    
    /**
     * Deinterleave soft values (decoder side / after channel)
     * 
     * @param input LLRs in interleaved order
     * @return LLRs in natural order
     */
    std::vector<float> deinterleave(const std::vector<float>& input) const {
        std::vector<float> output(input.size());
        
        size_t full_blocks = input.size() / size_;
        size_t remainder = input.size() % size_;
        
        // Process full blocks
        for (size_t b = 0; b < full_blocks; b++) {
            size_t base = b * size_;
            for (int i = 0; i < size_; i++) {
                output[base + inv_perm_[i]] = input[base + i];
            }
        }
        
        // Pass through remainder
        size_t base = full_blocks * size_;
        for (size_t i = 0; i < remainder; i++) {
            output[base + i] = input[base + i];
        }
        
        return output;
    }
    
    /**
     * Get interleaver dimensions
     */
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int size() const { return size_; }
    
    /**
     * Create interleaver for MIL-STD-188-110A mode
     * 
     * @param short_interleave true for SHORT modes, false for LONG
     * @param bits_per_symbol 3 for 8-PSK
     */
    static SoftInterleaver for_mode(bool short_interleave, int bits_per_symbol = 3) {
        // MIL-STD-188-110A interleaver parameters
        // Short: 40 rows × (variable cols based on rate)
        // Long: 40× longer
        
        int rows = short_interleave ? 40 : 40 * 9;  // 40 or 360
        int cols = 72;  // Typical for 2400 bps
        
        // Adjust cols based on bits_per_symbol
        // Each symbol carries bits_per_symbol bits
        // Interleaver operates on bits
        
        return SoftInterleaver(rows, cols * bits_per_symbol);
    }

private:
    int rows_;
    int cols_;
    int size_;
    std::vector<int> perm_;      // Forward permutation
    std::vector<int> inv_perm_;  // Inverse permutation
};

} // namespace m110a

#endif // SOFT_INTERLEAVER_H
