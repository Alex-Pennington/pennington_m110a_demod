/**
 * Direct Viterbi test
 */
#include <iostream>
#include <vector>
#include <cstring>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

int main() {
    cout << "Start" << endl;
    cout.flush();
    
    // Simple test: encode "AB"
    vector<uint8_t> input = {0,1,0,0,0,0,0,1,  // 'A' = 0x41
                              0,1,0,0,0,0,1,0}; // 'B' = 0x42
    cout << "Input created" << endl;
    cout.flush();
    
    // Encode
    ConvEncoder encoder;
    cout << "Encoder created" << endl;
    cout.flush();
    
    vector<uint8_t> encoded;
    encoder.encode(input, encoded, true);
    cout << "Encoded: " << encoded.size() << " bits" << endl;
    cout.flush();
    
    // Decode
    vector<int8_t> soft;
    for (uint8_t b : encoded) {
        soft.push_back(b ? 127 : -127);
    }
    cout << "Soft bits created" << endl;
    cout.flush();
    
    ViterbiDecoder decoder;
    cout << "Decoder created" << endl;
    cout.flush();
    
    vector<uint8_t> decoded;
    decoder.decode_block(soft, decoded, true);
    cout << "Decoded: " << decoded.size() << " bits" << endl;
    
    return 0;
}
