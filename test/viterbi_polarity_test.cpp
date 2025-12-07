/**
 * Test Viterbi soft bit polarity
 */
#include <iostream>
#include <vector>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

int main() {
    cout << "=== Viterbi Polarity Test ===" << endl;
    
    vector<uint8_t> input = {0,1,0,0,0,0,0,1, 0,1,0,0,0,0,1,0}; // "AB"
    
    // Encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(input, encoded, true);
    cout << "Encoded: " << encoded.size() << " bits" << endl;
    
    // Test both polarities
    for (int polarity = 0; polarity < 2; polarity++) {
        cout << "\n--- Polarity " << polarity << " ---" << endl;
        
        vector<int8_t> soft;
        for (uint8_t b : encoded) {
            if (polarity == 0) {
                // bit=1 -> +127, bit=0 -> -127 (my original assumption)
                soft.push_back(b ? 127 : -127);
            } else {
                // bit=0 -> +127, bit=1 -> -127 (MS-DMT convention?)
                soft.push_back(b ? -127 : 127);
            }
        }
        
        ViterbiDecoder decoder;
        vector<uint8_t> decoded;
        decoder.decode_block(soft, decoded, true);
        
        cout << "Input:   ";
        for (size_t i = 0; i < min(input.size(), (size_t)16); i++) cout << (int)input[i];
        cout << endl;
        
        cout << "Decoded: ";
        for (size_t i = 0; i < min(decoded.size(), (size_t)16); i++) cout << (int)decoded[i];
        cout << endl;
        
        bool match = true;
        for (size_t i = 0; i < min(input.size(), decoded.size()); i++) {
            if (input[i] != decoded[i]) {
                match = false;
                break;
            }
        }
        cout << "Match: " << (match ? "YES" : "NO") << endl;
    }
    
    return 0;
}
