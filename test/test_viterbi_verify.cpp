/**
 * MS-DMT Viterbi Verification Test
 * 
 * Verifies our Viterbi implementation matches MS-DMT exactly.
 */

#include "modem/viterbi.h"
#include <iostream>
#include <vector>
#include <cstdint>

using namespace m110a;

// MS-DMT parity masks
#define C10MASK 0x0001 
#define C11MASK 0x0002 
#define C13MASK 0x0008 
#define C14MASK 0x0010 
#define C16MASK 0x0040 

#define C20MASK 0x0001 
#define C23MASK 0x0008 
#define C24MASK 0x0010 
#define C25MASK 0x0020 
#define C26MASK 0x0040 

// MS-DMT parity function
void msdmt_parity(int state, int* bit1, int* bit2) {
    int count;
    
    count = 0;
    if (state & C10MASK) count++;
    if (state & C11MASK) count++;
    if (state & C13MASK) count++;
    if (state & C14MASK) count++;
    if (state & C16MASK) count++;
    
    *bit1 = count & 1;
    
    count = 0;
    if (state & C20MASK) count++;
    if (state & C23MASK) count++;
    if (state & C24MASK) count++;
    if (state & C25MASK) count++;
    if (state & C26MASK) count++;
    
    *bit2 = count & 1;
}

// MS-DMT encoder
class MSDMTViterbiEncoder {
public:
    MSDMTViterbiEncoder() : encode_state(0) {}
    
    void reset() { encode_state = 0; }
    
    void encode(int in, int* bit1, int* bit2) {
        encode_state = encode_state >> 1;
        if (in) encode_state |= 0x40;
        
        msdmt_parity(encode_state, bit1, bit2);
    }
    
private:
    int encode_state;
};

int main() {
    std::cout << "=== MS-DMT Viterbi Verification ===" << std::endl;
    
    // Check generator polynomials
    // MS-DMT: bit1 uses taps 0,1,3,4,6 = C10MASK|C11MASK|C13MASK|C14MASK|C16MASK = 0x5B
    // MS-DMT: bit2 uses taps 0,3,4,5,6 = C20MASK|C23MASK|C24MASK|C25MASK|C26MASK = 0x79
    
    std::cout << "\nGenerator Polynomial Verification:" << std::endl;
    std::cout << "G1 mask: 0x" << std::hex << (C10MASK|C11MASK|C13MASK|C14MASK|C16MASK) 
              << " (expected 0x5B)" << std::endl;
    std::cout << "G2 mask: 0x" << std::hex << (C20MASK|C23MASK|C24MASK|C25MASK|C26MASK) 
              << " (expected 0x79)" << std::endl;
    std::cout << std::dec;
    
    // Test encoding
    std::cout << "\nEncode Test:" << std::endl;
    
    // Test data
    std::vector<int> test_bits = {1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1};
    
    // MS-DMT encoding
    MSDMTViterbiEncoder msdmt_enc;
    std::vector<int> msdmt_out;
    for (int bit : test_bits) {
        int b1, b2;
        msdmt_enc.encode(bit, &b1, &b2);
        msdmt_out.push_back(b1);
        msdmt_out.push_back(b2);
    }
    
    // Our encoding
    ConvEncoder our_enc;
    std::vector<uint8_t> test_input(test_bits.begin(), test_bits.end());
    std::vector<uint8_t> our_out;
    our_enc.encode(test_input, our_out, false);
    
    std::cout << "MS-DMT output: ";
    for (int b : msdmt_out) std::cout << b;
    std::cout << std::endl;
    
    std::cout << "Our output:    ";
    for (uint8_t b : our_out) std::cout << (int)b;
    std::cout << std::endl;
    
    // Compare
    int diff = 0;
    for (size_t i = 0; i < std::min(msdmt_out.size(), our_out.size()); i++) {
        if (msdmt_out[i] != our_out[i]) diff++;
    }
    
    std::cout << "\nDifferences: " << diff << std::endl;
    
    if (diff == 0) {
        std::cout << "✓ Viterbi encoder matches MS-DMT" << std::endl;
    } else {
        std::cout << "✗ Viterbi encoder DOES NOT match MS-DMT" << std::endl;
        
        // Show state progression
        std::cout << "\nState progression comparison:" << std::endl;
        msdmt_enc.reset();
        our_enc.reset();
        
        for (int i = 0; i < 8; i++) {
            int b1, b2;
            msdmt_enc.encode(test_bits[i], &b1, &b2);
            auto [g1, g2] = our_enc.encode_bit(test_bits[i]);
            
            std::cout << "Input " << test_bits[i] 
                      << ": MS-DMT=" << b1 << b2 
                      << " Ours=" << (int)g1 << (int)g2 << std::endl;
        }
        return 1;
    }
    
    // Test loopback
    std::cout << "\n=== Loopback Test ===" << std::endl;
    
    std::string message = "TEST";
    std::vector<uint8_t> input_bits;
    for (char c : message) {
        for (int b = 7; b >= 0; b--) {
            input_bits.push_back((c >> b) & 1);
        }
    }
    
    // Verify input
    std::cout << "First 8 input bits (T=0x54): ";
    for (int i = 0; i < 8; i++) std::cout << (int)input_bits[i];
    std::cout << std::endl;
    
    // MS-DMT encode for comparison
    MSDMTViterbiEncoder msdmt_enc2;
    std::vector<int> msdmt_encoded;
    for (uint8_t bit : input_bits) {
        int b1, b2;
        msdmt_enc2.encode(bit, &b1, &b2);
        msdmt_encoded.push_back(b1);
        msdmt_encoded.push_back(b2);
    }
    
    // Our encode
    our_enc.reset();
    std::vector<uint8_t> encoded;
    our_enc.encode(input_bits, encoded, true);
    
    // Compare first 16 encoded bits
    std::cout << "First 16 MS-DMT encoded: ";
    for (int i = 0; i < 16 && i < (int)msdmt_encoded.size(); i++) {
        std::cout << msdmt_encoded[i];
    }
    std::cout << std::endl;
    
    std::cout << "First 16 our encoded:    ";
    for (int i = 0; i < 16 && i < (int)encoded.size(); i++) {
        std::cout << (int)encoded[i];
    }
    std::cout << std::endl;
    
    // Print first few encoded bits
    std::cout << "First 20 encoded bits: ";
    for (int i = 0; i < 20 && i < (int)encoded.size(); i++) {
        std::cout << (int)encoded[i];
    }
    std::cout << std::endl;
    
    // Convert to soft bits
    // Convention: positive = likely 1, negative = likely 0
    std::vector<soft_bit_t> soft;
    for (uint8_t b : encoded) {
        soft.push_back(b ? 100 : -100);
    }
    
    // Decode
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    dec.decode_block(soft, decoded, true);
    
    // Print first few decoded bits
    std::cout << "First 20 input bits:   ";
    for (int i = 0; i < 20 && i < (int)input_bits.size(); i++) {
        std::cout << (int)input_bits[i];
    }
    std::cout << std::endl;
    
    std::cout << "First 20 decoded bits: ";
    for (int i = 0; i < 20 && i < (int)decoded.size(); i++) {
        std::cout << (int)decoded[i];
    }
    std::cout << std::endl;
    
    // Compare
    int bit_errors = 0;
    for (size_t i = 0; i < std::min(input_bits.size(), decoded.size()); i++) {
        if (input_bits[i] != decoded[i]) bit_errors++;
    }
    
    std::cout << "Input: " << input_bits.size() << " bits" << std::endl;
    std::cout << "Encoded: " << encoded.size() << " bits" << std::endl;
    std::cout << "Decoded: " << decoded.size() << " bits" << std::endl;
    std::cout << "Bit errors: " << bit_errors << std::endl;
    
    if (bit_errors == 0) {
        std::cout << "✓ Viterbi loopback passed" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Viterbi loopback failed" << std::endl;
        return 1;
    }
}
