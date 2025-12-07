/**
 * Loopback test with corrected Gray code
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
    cout << "=== LOOPBACK TEST (Fixed Gray Code) ===" << endl;
    
    // Gray code: tribit → position
    // 0→0, 1→1, 2→3, 3→2, 4→6, 5→7, 6→5, 7→4
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    // Inverse Gray code: position → tribit
    // 0→0, 1→1, 2→3, 3→2, 4→7, 5→6, 6→4, 7→5
    const int inv_gray[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    // Verify they're inverses
    cout << "Verifying Gray code inverse:" << endl;
    for (int t = 0; t < 8; t++) {
        int pos = gray_map[t];
        int back = inv_gray[pos];
        cout << "  tribit " << t << " → pos " << pos << " → tribit " << back;
        cout << (back == t ? " ✓" : " ✗") << endl;
    }
    
    // Convert message to bits
    vector<uint8_t> msg_bits;
    for (size_t i = 0; i < strlen(TEST_MSG); i++) {
        uint8_t byte = TEST_MSG[i];
        for (int j = 7; j >= 0; j--) {
            msg_bits.push_back((byte >> j) & 1);
        }
    }
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    cout << "\nEncoded bits: " << encoded.size() << endl;
    
    // Pad to interleave block
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    while (encoded.size() < (size_t)block_size) encoded.push_back(0);
    
    // Interleave (write rows, read columns)
    vector<uint8_t> interleaved(block_size);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int write_idx = row * cols + col;
            int read_idx = col * rows + row;
            interleaved[read_idx] = encoded[write_idx];
        }
    }
    
    // Bits to tribits
    vector<int> tribits;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        tribits.push_back((interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2]);
    }
    
    // Gray code and scramble
    RefScrambler scr_tx;
    vector<int> scrambled;
    for (int t : tribits) {
        int pos = gray_map[t];
        uint8_t scr = scr_tx.next_tribit();
        scrambled.push_back((pos + scr) % 8);
    }
    
    // === DECODE ===
    cout << "\n=== DECODE ===" << endl;
    
    // Descramble
    RefScrambler scr_rx;
    vector<int> descrambled;
    for (int sym : scrambled) {
        uint8_t scr = scr_rx.next_tribit();
        descrambled.push_back((sym - scr + 8) % 8);
    }
    
    // Inverse Gray code
    vector<int> rx_tribits;
    for (int pos : descrambled) {
        rx_tribits.push_back(inv_gray[pos]);
    }
    
    // Verify tribits match
    int tribit_matches = 0;
    for (size_t i = 0; i < tribits.size(); i++) {
        if (rx_tribits[i] == tribits[i]) tribit_matches++;
    }
    cout << "Tribit matches: " << tribit_matches << "/" << tribits.size() << endl;
    
    // Tribits to bits
    vector<uint8_t> rx_bits;
    for (int t : rx_tribits) {
        rx_bits.push_back((t >> 2) & 1);
        rx_bits.push_back((t >> 1) & 1);
        rx_bits.push_back(t & 1);
    }
    
    // Deinterleave
    vector<uint8_t> deinterleaved(block_size);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int read_idx = col * rows + row;
            int write_idx = row * cols + col;
            deinterleaved[write_idx] = rx_bits[read_idx];
        }
    }
    
    // Verify bits match
    int bit_matches = 0;
    for (size_t i = 0; i < encoded.size(); i++) {
        if (deinterleaved[i] == encoded[i]) bit_matches++;
    }
    cout << "Bit matches (before Viterbi): " << bit_matches << "/" << encoded.size() << endl;
    
    // Viterbi decode
    vector<int8_t> soft_bits;
    for (uint8_t b : deinterleaved) soft_bits.push_back(b ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft_bits, decoded, true);
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        bytes.push_back(byte);
    }
    
    cout << "\nDecoded: ";
    for (size_t i = 0; i < bytes.size() && i < 60; i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    int matches = 0;
    for (size_t i = 0; i < bytes.size() && i < strlen(TEST_MSG); i++) {
        if (bytes[i] == (uint8_t)TEST_MSG[i]) matches++;
    }
    cout << "Match: " << matches << "/" << strlen(TEST_MSG) << " characters" << endl;
    
    return 0;
}
