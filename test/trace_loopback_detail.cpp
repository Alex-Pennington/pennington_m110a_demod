/**
 * Detailed trace of loopback encoding
 */
#include <iostream>
#include <vector>
#include <cstring>
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

int main() {
    cout << "=== Detailed Loopback Trace ===" << endl;
    
    // Convert to bits
    vector<uint8_t> input_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            input_bits.push_back((c >> i) & 1);
        }
    }
    cout << "Input: " << strlen(TEST_MSG) << " bytes = " << input_bits.size() << " bits" << endl;
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    cout << "After FEC: " << encoded.size() << " bits" << endl;
    
    // Pad to interleave block
    int rows = 40, cols = 36;
    while (encoded.size() < (size_t)(rows * cols)) {
        encoded.push_back(0);
    }
    cout << "Padded to: " << encoded.size() << " bits" << endl;
    
    // Interleave
    vector<uint8_t> interleaved(rows * cols);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = row * cols + col;
            int out_idx = col * rows + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    cout << "\n--- First 60 interleaved bits ---" << endl;
    for (int i = 0; i < 60; i++) {
        cout << (int)interleaved[i];
        if ((i+1) % 3 == 0) cout << " ";
    }
    cout << endl;
    
    // Map to tribits and positions
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    
    cout << "\n--- First 20 positions (before scrambling) ---" << endl;
    for (int i = 0; i < 20; i++) {
        cout << positions[i];
    }
    cout << endl;
    
    // Scramble
    RefScrambler scr;
    vector<int> scrambled;
    for (int pos : positions) {
        uint8_t scr_val = scr.next_tribit();
        scrambled.push_back((pos + scr_val) % 8);
    }
    
    cout << "\n--- First 20 scrambled positions ---" << endl;
    for (int i = 0; i < 20; i++) {
        cout << scrambled[i];
    }
    cout << endl;
    
    // Descramble (verify)
    scr = RefScrambler();
    cout << "\n--- First 20 descrambled positions ---" << endl;
    for (int i = 0; i < 20; i++) {
        uint8_t scr_val = scr.next_tribit();
        int desc = (scrambled[i] - scr_val + 8) % 8;
        cout << desc;
    }
    cout << endl;
    
    return 0;
}
