/**
 * Verify encode chain matches loopback test
 */
#include <iostream>
#include <vector>
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int MSG_LEN = 54;

int main() {
    // Message to bits
    vector<uint8_t> msg_bits;
    for (int i = 0; i < MSG_LEN; i++) {
        uint8_t c = TEST_MSG[i];
        for (int j = 7; j >= 0; j--) msg_bits.push_back((c >> j) & 1);
    }
    cout << "Message bits: " << msg_bits.size() << endl;
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    cout << "Encoded bits: " << encoded.size() << endl;
    
    // Pad to 1440 bits
    while (encoded.size() < 1440) encoded.push_back(0);
    
    // Interleave 40×36
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < 40; row++) {
        for (int col = 0; col < 36; col++) {
            int in_idx = row * 36 + col;
            int out_idx = col * 40 + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    cout << "First 30 interleaved bits: ";
    for (int i = 0; i < 30; i++) cout << (int)interleaved[i];
    cout << endl;
    
    // To tribits
    vector<int> tribits;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        tribits.push_back(tribit);
    }
    
    cout << "First 20 tribits: ";
    for (int i = 0; i < 20; i++) cout << tribits[i];
    cout << endl;
    
    // Gray encode to position
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    vector<int> positions;
    for (int t : tribits) {
        positions.push_back(tribit_to_pos[t]);
    }
    
    cout << "First 20 positions: ";
    for (int i = 0; i < 20; i++) cout << positions[i];
    cout << endl;
    
    // Scramble with RefScrambler (same as loopback)
    RefScrambler scr;
    vector<int> scrambled;
    for (int pos : positions) {
        int scr_val = scr.next_tribit();
        scrambled.push_back((pos + scr_val) % 8);
    }
    
    cout << "First 40 scrambled (RefScrambler): ";
    for (int i = 0; i < 40; i++) cout << scrambled[i];
    cout << endl;
    
    // Now decode and verify
    // Descramble
    scr = RefScrambler();
    vector<int> descrambled;
    for (int s : scrambled) {
        int scr_val = scr.next_tribit();
        descrambled.push_back((s - scr_val + 8) % 8);
    }
    
    // Gray decode
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : descrambled) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave
    vector<int> deinterleaved(1440);
    for (int row = 0; row < 40; row++) {
        for (int col = 0; col < 36; col++) {
            int in_idx = col * 40 + row;
            int out_idx = row * 36 + col;
            deinterleaved[out_idx] = bits[in_idx];
        }
    }
    
    // Viterbi
    vector<int8_t> soft;
    for (int b : deinterleaved) soft.push_back(b ? -127 : 127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes
    string output;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "\nDecoded: " << output.substr(0, 60) << endl;
    cout << "Expected: " << TEST_MSG << endl;
    
    int matches = 0;
    for (int i = 0; i < MSG_LEN && i < (int)output.size(); i++) {
        if (output[i] == TEST_MSG[i]) matches++;
    }
    cout << "Matches: " << matches << "/" << MSG_LEN << endl;
    
    return 0;
}
