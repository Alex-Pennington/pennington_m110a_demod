/**
 * Trace bits through encode and compare to received
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
const int inv_gray[8] = {0, 1, 3, 2, 7, 6, 4, 5};

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    vector<int16_t> raw(size / 2);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(size / 2);
    for (size_t i = 0; i < size / 2; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    // Generate expected symbols from known message
    cout << "=== GENERATING EXPECTED SYMBOLS ===" << endl;
    
    // Message to bits
    vector<uint8_t> msg_bits;
    for (size_t i = 0; i < strlen(EXPECTED); i++) {
        uint8_t byte = EXPECTED[i];
        for (int j = 7; j >= 0; j--) {
            msg_bits.push_back((byte >> j) & 1);
        }
    }
    cout << "Message bits: " << msg_bits.size() << endl;
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    cout << "Encoded bits: " << encoded.size() << endl;
    
    // Pad to interleave block
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    while (encoded.size() < (size_t)block_size) encoded.push_back(0);
    
    // Interleave
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
    cout << "Tribits: " << tribits.size() << endl;
    
    // Gray code and scramble
    RefScrambler scr_tx;
    vector<int> expected_symbols;
    for (int t : tribits) {
        int pos = gray_map[t];
        uint8_t scr = scr_tx.next_tribit();
        expected_symbols.push_back((pos + scr) % 8);
    }
    
    // Show first 40 expected symbols (first data frame)
    cout << "\nExpected first 40 symbols: ";
    for (int i = 0; i < 40; i++) {
        cout << expected_symbols[i];
    }
    cout << endl;
    
    // Now load actual received
    cout << "\n=== RECEIVED SYMBOLS ===" << endl;
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    // Extract first 40 received data symbols (positions 0-19 and 40-59, skipping probe)
    cout << "Received first 40 data symbols: ";
    for (int frame = 0; frame < 2; frame++) {
        for (int i = 0; i < 20; i++) {
            int idx = frame * 40 + i;
            auto sym = result.data_symbols[idx];
            float phase = atan2(sym.imag(), sym.real());
            if (phase < 0) phase += 2 * M_PI;
            int pos = (int)round(phase * 4 / M_PI) % 8;
            cout << pos;
        }
    }
    cout << endl;
    
    // Compare
    cout << "\nComparing first 20 data symbols:" << endl;
    int matches = 0;
    for (int i = 0; i < 20; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real());
        if (phase < 0) phase += 2 * M_PI;
        int rcv_pos = (int)round(phase * 4 / M_PI) % 8;
        int exp_pos = expected_symbols[i];
        bool match = (rcv_pos == exp_pos);
        if (match) matches++;
        printf("  [%2d] exp=%d rcv=%d %s\n", i, exp_pos, rcv_pos, match ? "✓" : "✗");
    }
    cout << "Matches: " << matches << "/20" << endl;
    
    return 0;
}
