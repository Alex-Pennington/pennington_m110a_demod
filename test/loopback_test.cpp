/**
 * Loopback test - encode then decode to verify chain
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

int main() {
    cout << "=== Loopback Test ===" << endl;
    cout << "Test message: " << TEST_MSG << " (" << strlen(TEST_MSG) << " bytes)" << endl;
    
    // Convert to bits
    vector<uint8_t> input_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            input_bits.push_back((c >> i) & 1);
        }
    }
    cout << "Input bits: " << input_bits.size() << endl;
    
    // Step 1: FEC encode (rate 1/2, K=7)
    cout << "\n--- FEC Encode ---" << endl;
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    cout << "Encoded bits: " << encoded.size() << endl;
    
    // Step 2: Interleave (40x36 for M2400S)
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    cout << "\n--- Interleave (" << rows << "x" << cols << ") ---" << endl;
    
    // Pad to block size
    while (encoded.size() < (size_t)block_size) {
        encoded.push_back(0);
    }
    
    // Interleave: write row-by-row, read column-by-column
    vector<uint8_t> interleaved(block_size);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = row * cols + col;
            int out_idx = col * rows + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    cout << "Interleaved bits: " << interleaved.size() << endl;
    
    // Step 3: Map to tribits and symbols
    cout << "\n--- Map to 8-PSK symbols ---" << endl;
    
    // Gray code: tribit → position
    // 0→0, 1→1, 2→3, 3→2, 4→7, 5→6, 6→4, 7→5
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    cout << "Symbol positions: " << positions.size() << endl;
    
    // Step 4: Scramble
    cout << "\n--- Scramble ---" << endl;
    RefScrambler scr;
    vector<int> scrambled;
    for (int pos : positions) {
        uint8_t scr_val = scr.next_tribit();
        int scr_pos = (pos + scr_val) % 8;
        scrambled.push_back(scr_pos);
    }
    cout << "Scrambled positions: " << scrambled.size() << endl;
    cout << "First 20: ";
    for (int i = 0; i < min(20, (int)scrambled.size()); i++) {
        cout << scrambled[i];
    }
    cout << endl;
    
    // === Now decode ===
    cout << "\n=== DECODE ===" << endl;
    
    // Step 5: Descramble
    cout << "\n--- Descramble ---" << endl;
    scr = RefScrambler();
    vector<int> descrambled;
    for (int pos : scrambled) {
        uint8_t scr_val = scr.next_tribit();
        int desc_pos = (pos - scr_val + 8) % 8;
        descrambled.push_back(desc_pos);
    }
    cout << "First 20: ";
    for (int i = 0; i < min(20, (int)descrambled.size()); i++) {
        cout << descrambled[i];
    }
    cout << endl;
    
    // Verify descramble matches original positions
    bool match = true;
    for (size_t i = 0; i < positions.size(); i++) {
        if (positions[i] != descrambled[i]) {
            cout << "Mismatch at " << i << ": " << positions[i] << " vs " << descrambled[i] << endl;
            match = false;
            break;
        }
    }
    cout << "Descramble " << (match ? "OK ✓" : "FAILED ✗") << endl;
    
    // Step 6: Gray decode (position → tribit)
    cout << "\n--- Gray decode ---" << endl;
    const int pos_to_tribit[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    vector<uint8_t> decoded_bits;
    for (int pos : descrambled) {
        int tribit = pos_to_tribit[pos];
        decoded_bits.push_back((tribit >> 2) & 1);
        decoded_bits.push_back((tribit >> 1) & 1);
        decoded_bits.push_back(tribit & 1);
    }
    cout << "Bits: " << decoded_bits.size() << endl;
    
    // Step 7: Deinterleave
    cout << "\n--- Deinterleave ---" << endl;
    vector<uint8_t> deinterleaved(block_size);
    for (int col = 0; col < cols; col++) {
        for (int row = 0; row < rows; row++) {
            int in_idx = col * rows + row;
            int out_idx = row * cols + col;
            if (in_idx < (int)decoded_bits.size()) {
                deinterleaved[out_idx] = decoded_bits[in_idx];
            }
        }
    }
    cout << "Deinterleaved bits: " << deinterleaved.size() << endl;
    
    // Verify deinterleave matches encoded
    match = true;
    for (size_t i = 0; i < min(encoded.size(), deinterleaved.size()); i++) {
        if (encoded[i] != deinterleaved[i]) {
            cout << "Mismatch at " << i << endl;
            match = false;
            break;
        }
    }
    cout << "Deinterleave " << (match ? "OK ✓" : "FAILED ✗") << endl;
    
    // Step 8: Viterbi decode
    cout << "\n--- Viterbi decode ---" << endl;
    vector<int8_t> soft;
    for (uint8_t b : deinterleaved) {
        // MS-DMT convention: bit 0 -> +127, bit 1 -> -127
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> viterbi_out;
    viterbi.decode_block(soft, viterbi_out, true);
    cout << "Output bits: " << viterbi_out.size() << endl;
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= viterbi_out.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (viterbi_out[i + j] & 1);
        }
        bytes.push_back(byte);
    }
    
    cout << "\n=== RESULT ===" << endl;
    cout << "Output: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), strlen(TEST_MSG)); i++) {
        if (bytes[i] == (uint8_t)TEST_MSG[i]) matches++;
    }
    cout << "Match: " << matches << "/" << strlen(TEST_MSG) << endl;
    
    return 0;
}
