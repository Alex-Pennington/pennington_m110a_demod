/**
 * Simple Viterbi Encoder/Decoder Test
 */

#include "modem/viterbi.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace m110a;

int main() {
    std::cout << "=== Simple Viterbi Test ===" << std::endl;
    
    // Test message
    std::string message = "Hi";
    std::vector<uint8_t> input_bits;
    for (char c : message) {
        for (int b = 7; b >= 0; b--) {
            input_bits.push_back((c >> b) & 1);
        }
    }
    
    std::cout << "Input bits (" << input_bits.size() << "): ";
    for (uint8_t b : input_bits) std::cout << (int)b;
    std::cout << std::endl;
    
    // Encode
    ConvEncoder encoder;
    std::vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    
    std::cout << "Encoded bits (" << encoded.size() << "): ";
    for (size_t i = 0; i < 40 && i < encoded.size(); i++) std::cout << (int)encoded[i];
    if (encoded.size() > 40) std::cout << "...";
    std::cout << std::endl;
    
    // Convert to soft bits - MS-DMT convention: +soft = 0, -soft = 1
    std::vector<soft_bit_t> soft_bits;
    for (uint8_t b : encoded) {
        soft_bits.push_back(b ? -100 : 100);
    }
    
    std::cout << "Soft bits (first 20): ";
    for (size_t i = 0; i < 20 && i < soft_bits.size(); i++) {
        std::cout << std::setw(5) << (int)soft_bits[i];
    }
    std::cout << std::endl;
    
    // Decode
    ViterbiDecoder decoder;
    std::vector<uint8_t> decoded;
    decoder.decode_block(soft_bits, decoded, true);
    
    std::cout << "Decoded bits (" << decoded.size() << "): ";
    for (uint8_t b : decoded) std::cout << (int)b;
    std::cout << std::endl;
    
    // Verify
    int errors = 0;
    for (size_t i = 0; i < input_bits.size() && i < decoded.size(); i++) {
        if (input_bits[i] != decoded[i]) errors++;
    }
    
    std::cout << "Bit errors: " << errors << " / " << input_bits.size() << std::endl;
    
    // Pack and display
    std::string decoded_str;
    uint8_t cur = 0; int bc = 0;
    for (uint8_t b : decoded) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) {
            if (cur >= 32 && cur < 127) decoded_str += (char)cur;
            else decoded_str += '.';
            cur = 0; bc = 0;
        }
    }
    std::cout << "Decoded: \"" << decoded_str << "\"" << std::endl;
    
    std::cout << (errors == 0 ? "\n✓ TEST PASSED" : "\n✗ TEST FAILED") << std::endl;
    
    return errors == 0 ? 0 : 1;
}
