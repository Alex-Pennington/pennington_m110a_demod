/**
 * Viterbi Trellis Debug
 */

#include "modem/viterbi.h"
#include <iostream>
#include <iomanip>

using namespace m110a;

// MS-DMT convention
void msdmt_parity(int state, int* bit1, int* bit2) {
    int count = 0;
    if (state & 0x01) count++;
    if (state & 0x02) count++;
    if (state & 0x08) count++;
    if (state & 0x10) count++;
    if (state & 0x40) count++;
    *bit1 = count & 1;
    
    count = 0;
    if (state & 0x01) count++;
    if (state & 0x08) count++;
    if (state & 0x10) count++;
    if (state & 0x20) count++;
    if (state & 0x40) count++;
    *bit2 = count & 1;
}

int main() {
    std::cout << "=== Viterbi Trellis Analysis ===" << std::endl;
    
    // Check state transitions
    std::cout << "\n6-bit state transitions (64 states):" << std::endl;
    std::cout << "Format: state -> next_state (input 0), next_state (input 1)" << std::endl;
    
    // MS-DMT convention: encoder state right-shifts
    // encoder_state = (encoder_state >> 1) | (input << 6)
    // So for 6-bit decoder state:
    // next_state = (state >> 1) | (input << 5)
    
    std::cout << "\nMS-DMT style (right-shift, new bit at MSB):" << std::endl;
    for (int state = 0; state < 8; state++) {
        int next0 = (state >> 1) | (0 << 5);
        int next1 = (state >> 1) | (1 << 5);
        
        // Outputs for each transition
        int enc_state0 = (state >> 1) | (0 << 6);  // 7-bit encoder state after input 0
        int enc_state1 = (state >> 1) | (1 << 6);  // 7-bit encoder state after input 1
        
        int b1_0, b2_0, b1_1, b2_1;
        msdmt_parity(enc_state0, &b1_0, &b2_0);
        msdmt_parity(enc_state1, &b1_1, &b2_1);
        
        std::cout << "State " << state << ": ";
        std::cout << state << " --(0)--> " << next0 << " [" << b1_0 << b2_0 << "], ";
        std::cout << state << " --(1)--> " << next1 << " [" << b1_1 << b2_1 << "]" << std::endl;
    }
    
    // Now check what our decoder computes
    std::cout << "\n\nOur decoder transitions:" << std::endl;
    
    ViterbiDecoder dec;
    
    // Dump the decoder's internal transition table
    std::cout << "\nDecoder internal transition table (first 8 states):" << std::endl;
    // Can't access private members, so let's verify by encoding
    
    std::cout << "\n\nTesting decode of known sequence:" << std::endl;
    
    // Encode 0, 1, 0, 1
    std::vector<uint8_t> test_input = {0, 1, 0, 1};
    ConvEncoder enc;
    std::vector<uint8_t> test_encoded;
    enc.encode(test_input, test_encoded, true);  // with flush
    
    std::cout << "Input: ";
    for (auto b : test_input) std::cout << (int)b;
    std::cout << std::endl;
    
    std::cout << "Encoded (" << test_encoded.size() << " bits): ";
    for (auto b : test_encoded) std::cout << (int)b;
    std::cout << std::endl;
    
    // Convert to soft (positive = 1, negative = 0)
    std::vector<soft_bit_t> soft;
    for (uint8_t b : test_encoded) {
        soft.push_back(b ? 100 : -100);
    }
    
    // Decode
    std::vector<uint8_t> decoded;
    dec.decode_block(soft, decoded, true);
    
    std::cout << "Decoded (" << decoded.size() << " bits): ";
    for (auto b : decoded) std::cout << (int)b;
    std::cout << std::endl;
    
    // Compare
    std::cout << "Match: " << (test_input == std::vector<uint8_t>(decoded.begin(), decoded.begin() + test_input.size()) ? "YES" : "NO") << std::endl;
    
    // Access internal state through testing
    // We'll manually check a few states
    
    // Test encode/decode pair for first few bits
    std::cout << "\n\nEncode test:" << std::endl;
    ConvEncoder enc;
    
    std::vector<uint8_t> input = {0, 1, 0, 1, 0, 1, 0, 1};
    std::vector<uint8_t> encoded;
    enc.encode(input, encoded, false);
    
    std::cout << "Input:   ";
    for (auto b : input) std::cout << (int)b;
    std::cout << std::endl;
    
    std::cout << "Encoded: ";
    for (auto b : encoded) std::cout << (int)b;
    std::cout << std::endl;
    
    // Expected from MS-DMT for input 01010101:
    // State progression (7-bit encoder):
    // Initial: 0000000
    // After 0: 0000000, output = parity(0000000) = 00
    // After 1: 1000000, output = parity(1000000) = 11
    // After 0: 0100000, output = parity(0100000) = 00
    // After 1: 1010000, output = parity(1010000) = 11
    // etc.
    
    std::cout << "\nExpected: ";
    int state = 0;
    for (int b : input) {
        state = (state >> 1) | (b << 6);
        int b1, b2;
        msdmt_parity(state, &b1, &b2);
        std::cout << b1 << b2;
    }
    std::cout << std::endl;
    
    return 0;
}
