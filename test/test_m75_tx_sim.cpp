/**
 * Simulate M75 TX to see expected Walsh patterns
 */

#include "modem/viterbi.h"
#include "modem/multimode_interleaver.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace m110a;

// M75NS interleaver parameters
InterleaverParams get_m75ns_params() {
    InterleaverParams p;
    p.rows = 10;
    p.cols = 9;
    p.row_inc = 7;
    p.col_inc = 2;
    p.block_count_mod = 45;
    return p;
}

// Gray code (mgd2): bits -> walsh index
int gray_encode(int bits) {
    static const int mgd2[] = {0, 1, 3, 2};
    return mgd2[bits & 3];
}

int main() {
    std::cout << "=== M75 TX Simulation ===\n\n";
    
    // Input: "Hello" = 5 bytes = 40 bits
    std::vector<uint8_t> input = {'H', 'e', 'l', 'l', 'o'};
    
    std::cout << "Input: ";
    for (uint8_t c : input) std::cout << c;
    std::cout << " (";
    for (uint8_t c : input) std::cout << std::hex << (int)c << " ";
    std::cout << std::dec << ")\n\n";
    
    // Convert to bits (MSB first)
    std::vector<uint8_t> data_bits;
    for (uint8_t c : input) {
        for (int b = 7; b >= 0; b--) {
            data_bits.push_back((c >> b) & 1);
        }
    }
    
    std::cout << "Data bits: " << data_bits.size() << " bits\n";
    std::cout << "First 40 bits: ";
    for (int i = 0; i < 40; i++) std::cout << (int)data_bits[i];
    std::cout << "\n\n";
    
    // FEC encode (K=7, rate 1/2)
    ConvEncoder encoder;
    std::vector<uint8_t> coded_bits;
    encoder.encode(data_bits, coded_bits, true);
    
    std::cout << "FEC coded: " << coded_bits.size() << " bits\n";
    std::cout << "First 80 coded bits: ";
    for (size_t i = 0; i < 80 && i < coded_bits.size(); i++) std::cout << (int)coded_bits[i];
    std::cout << "\n\n";
    
    // We need 90 bits for one interleaver block, but we have 40+6=46 data bits -> 92 coded
    // Actually we need multiple blocks
    std::cout << "Coded bits: " << coded_bits.size() << " (need multiple of 90 for interleaver)\n";
    
    // Pad to 90 bits for first block
    while (coded_bits.size() < 90) coded_bits.push_back(0);
    coded_bits.resize(90);
    
    // Interleave
    auto params = get_m75ns_params();
    MultiModeInterleaver interleaver(params);
    
    std::vector<soft_bit_t> coded_soft(coded_bits.begin(), coded_bits.end());
    auto interleaved = interleaver.interleave(coded_soft);
    
    std::cout << "Interleaved: " << interleaved.size() << " bits\n";
    std::cout << "First 40 interleaved: ";
    for (int i = 0; i < 40; i++) std::cout << (int)interleaved[i];
    std::cout << "\n\n";
    
    // Convert to dibits and then Walsh indices
    std::cout << "Walsh indices for first interleaver block (45 symbols):\n";
    for (size_t i = 0; i < interleaved.size(); i += 2) {
        int dibit = (interleaved[i] << 1) | interleaved[i+1];
        int walsh = gray_encode(dibit);
        std::cout << walsh;
        if ((i/2 + 1) % 15 == 0) std::cout << " ";
    }
    std::cout << "\n\n";
    
    // Now let's verify by decoding
    std::cout << "=== Verify by decoding ===\n";
    
    // De-Gray and convert back
    std::vector<uint8_t> verify_bits;
    for (size_t i = 0; i < interleaved.size(); i += 2) {
        int dibit = (interleaved[i] << 1) | interleaved[i+1];
        int walsh = gray_encode(dibit);
        
        // Reverse Gray decode
        int decoded_bits;
        switch(walsh) {
            case 0: decoded_bits = 0; break;  // 00
            case 1: decoded_bits = 1; break;  // 01
            case 2: decoded_bits = 3; break;  // 11
            case 3: decoded_bits = 2; break;  // 10
        }
        verify_bits.push_back((decoded_bits >> 1) & 1);
        verify_bits.push_back(decoded_bits & 1);
    }
    
    // Should match interleaved
    bool match = true;
    for (size_t i = 0; i < interleaved.size(); i++) {
        if (verify_bits[i] != interleaved[i]) {
            std::cout << "Mismatch at " << i << "\n";
            match = false;
        }
    }
    if (match) std::cout << "Verify: Gray encode/decode matches!\n\n";
    
    // Deinterleave
    std::vector<soft_bit_t> verify_soft(verify_bits.begin(), verify_bits.end());
    auto deinterleaved = interleaver.deinterleave(verify_soft);
    
    std::cout << "Deinterleaved first 40: ";
    for (int i = 0; i < 40; i++) std::cout << (int)deinterleaved[i];
    std::cout << "\n";
    
    std::cout << "Original coded first 40: ";
    for (int i = 0; i < 40; i++) std::cout << (int)coded_bits[i];
    std::cout << "\n\n";
    
    // Check if deinterleaved matches original coded
    match = true;
    for (size_t i = 0; i < coded_bits.size(); i++) {
        if (deinterleaved[i] != coded_bits[i]) {
            std::cout << "Deinterleave mismatch at " << i << "\n";
            match = false;
            break;
        }
    }
    if (match) std::cout << "Deinterleave: matches original coded bits!\n";
    
    return 0;
}
