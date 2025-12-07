#ifndef M110A_SCRAMBLER_FIXED_H
#define M110A_SCRAMBLER_FIXED_H

#include <vector>
#include <cstdint>

namespace m110a {

/**
 * Fixed Data Scrambler for MIL-STD-188-110A
 * 
 * The scrambler generates a 160-symbol sequence that is used cyclically.
 * This matches the reference modem behavior where:
 *   tx_data_scrambler_offset = ++tx_data_scrambler_offset % 160
 */
class DataScramblerFixed {
public:
    DataScramblerFixed() : offset_(0) {
        generate_sequence();
    }
    
    void reset() {
        offset_ = 0;
    }
    
    /**
     * Get next scrambler value (0-7)
     * Wraps at 160 symbols
     */
    int next() {
        int val = sequence_[offset_];
        offset_ = (offset_ + 1) % 160;
        return val;
    }
    
    /**
     * Get scrambler value at specific offset
     */
    int at(int pos) const {
        return sequence_[pos % 160];
    }
    
private:
    void generate_sequence() {
        // Initial state: 1011 0101 1101 (0xBAD)
        int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
        
        sequence_.resize(160);
        
        for (int i = 0; i < 160; i++) {
            // Clock 8 times per symbol
            for (int j = 0; j < 8; j++) {
                int c = sreg[11];
                for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
                sreg[0] = c;
                // Polynomial: x^12 + x^7 + x^5 + x^2 + 1
                sreg[6] ^= c;  // x^7 tap
                sreg[4] ^= c;  // x^5 tap  
                sreg[1] ^= c;  // x^2 tap
            }
            // 3-bit output
            sequence_[i] = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
        }
    }
    
    std::vector<int> sequence_;
    int offset_;
};

} // namespace m110a

#endif // M110A_SCRAMBLER_FIXED_H
