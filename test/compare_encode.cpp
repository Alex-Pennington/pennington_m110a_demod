/**
 * Show what the encoded 'T' looks like
 */
#include <iostream>
#include <vector>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

int main() {
    // 'T' = 0x54 = 01010100
    vector<uint8_t> T_bits = {0,1,0,1,0,1,0,0};
    
    cout << "=== Encoding 'T' (0x54) ===" << endl;
    cout << "Input bits: ";
    for (auto b : T_bits) cout << (int)b;
    cout << endl;
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(T_bits, encoded, false);  // No flush
    
    cout << "Encoded (16 bits): ";
    for (auto b : encoded) cout << (int)b;
    cout << endl;
    
    cout << "As pairs: ";
    for (size_t i = 0; i < encoded.size(); i += 2) {
        cout << (int)encoded[i] << (int)encoded[i+1] << " ";
    }
    cout << endl;
    
    // Now map to tribits and positions
    cout << "\n--- To 8-PSK symbols ---" << endl;
    
    // Gray code: tribit → position
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    // 16 bits = 5 tribits (with 1 bit left over)
    cout << "Tribits (5 full + 1 partial): ";
    for (size_t i = 0; i + 3 <= encoded.size(); i += 3) {
        int tribit = (encoded[i] << 2) | (encoded[i+1] << 1) | encoded[i+2];
        int pos = tribit_to_pos[tribit];
        cout << tribit << "→" << pos << " ";
    }
    cout << endl;
    
    // After interleaving, these bits would be scattered across the 40x36 matrix
    // Row 0 would get bits 0, 36, 72, 108, ...
    // So the first few symbols would come from different parts of the message!
    
    cout << "\n--- Interleave effect ---" << endl;
    cout << "40x36 interleave means row 0 gets bits: 0, 36, 72, 108, ..." << endl;
    cout << "So first 3-bit tribit comes from bits 0, 36, 72!" << endl;
    cout << "These bits are from DIFFERENT bytes of the message!" << endl;
    
    return 0;
}
