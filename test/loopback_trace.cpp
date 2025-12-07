/**
 * Detailed loopback trace
 */
#include <iostream>
#include <vector>
#include <cstring>
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

int main() {
    cout << "=== Detailed Loopback Trace ===" << endl;
    
    // Simple test: just "AB" 
    const char* TEST_MSG = "AB";
    
    vector<uint8_t> input_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            input_bits.push_back((c >> i) & 1);
        }
    }
    cout << "Input: " << TEST_MSG << " = " << input_bits.size() << " bits" << endl;
    cout << "  ";
    for (auto b : input_bits) cout << (int)b;
    cout << endl;
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    cout << "\nEncoded: " << encoded.size() << " bits" << endl;
    cout << "  ";
    for (size_t i = 0; i < encoded.size(); i++) {
        cout << (int)encoded[i];
        if ((i+1) % 8 == 0) cout << " ";
    }
    cout << endl;
    
    // Viterbi decode directly (no interleave)
    cout << "\n--- Direct Viterbi decode ---" << endl;
    vector<int8_t> soft;
    for (uint8_t b : encoded) {
        soft.push_back(b ? 127 : -127);
    }
    
    ViterbiDecoder decoder;
    vector<uint8_t> decoded;
    decoder.decode_block(soft, decoded, true);
    
    cout << "Decoded: " << decoded.size() << " bits" << endl;
    cout << "  ";
    for (auto b : decoded) cout << (int)b;
    cout << endl;
    
    // Check
    bool match = true;
    for (size_t i = 0; i < min(input_bits.size(), decoded.size()); i++) {
        if (input_bits[i] != decoded[i]) {
            match = false;
            break;
        }
    }
    cout << "Direct decode: " << (match ? "PASS" : "FAIL") << endl;
    
    // Now test with small interleave (4x4)
    cout << "\n--- Small interleave test (4x4) ---" << endl;
    
    // Use 16 encoded bits
    vector<uint8_t> small_enc(encoded.begin(), encoded.begin() + 16);
    
    // Interleave 4x4: write rows, read cols
    vector<uint8_t> interleaved(16);
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int in_idx = row * 4 + col;
            int out_idx = col * 4 + row;
            interleaved[out_idx] = small_enc[in_idx];
        }
    }
    
    cout << "Original:     ";
    for (auto b : small_enc) cout << (int)b;
    cout << endl;
    
    cout << "Interleaved:  ";
    for (auto b : interleaved) cout << (int)b;
    cout << endl;
    
    // Deinterleave: read cols, write rows
    vector<uint8_t> deinterleaved(16);
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            int in_idx = col * 4 + row;
            int out_idx = row * 4 + col;
            deinterleaved[out_idx] = interleaved[in_idx];
        }
    }
    
    cout << "Deinterleaved: ";
    for (auto b : deinterleaved) cout << (int)b;
    cout << endl;
    
    match = true;
    for (int i = 0; i < 16; i++) {
        if (small_enc[i] != deinterleaved[i]) {
            match = false;
            cout << "Mismatch at " << i << endl;
            break;
        }
    }
    cout << "Interleave round-trip: " << (match ? "PASS" : "FAIL") << endl;
    
    return 0;
}
