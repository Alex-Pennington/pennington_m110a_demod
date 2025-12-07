/**
 * Full decode with all fixes:
 * 1. Correct inverse Gray code
 * 2. Inverted Viterbi soft bit polarity
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
const int EXPECTED_LEN = 54;

// Correct inverse Gray code: position → tribit
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

int main(int argc, char** argv) {
    string filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    }
    
    cout << "=== FULL DECODE (FIXED) ===" << endl;
    cout << "File: " << filename << endl;
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")" << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Descramble
    RefScrambler scr;
    vector<int> data_positions;
    int sym_idx = 0;
    
    while (sym_idx + 40 <= (int)result.data_symbols.size()) {
        // 20 data symbols
        for (int i = 0; i < 20; i++) {
            auto sym = result.data_symbols[sym_idx + i];
            float phase = atan2(sym.imag(), sym.real());
            if (phase < 0) phase += 2 * M_PI;
            int raw_pos = (int)round(phase * 4 / M_PI) % 8;
            
            uint8_t scr_val = scr.next_tribit();
            int desc_pos = (raw_pos - scr_val + 8) % 8;
            data_positions.push_back(desc_pos);
        }
        // 20 probe symbols
        for (int i = 0; i < 20; i++) scr.next_tribit();
        sym_idx += 40;
    }
    cout << "Descrambled " << data_positions.size() << " data symbols" << endl;
    
    // Inverse Gray code to tribits
    vector<int> tribits;
    for (int pos : data_positions) {
        tribits.push_back(inv_gray[pos]);
    }
    
    // Tribits to bits
    vector<uint8_t> bits;
    for (int t : tribits) {
        bits.push_back((t >> 2) & 1);
        bits.push_back((t >> 1) & 1);
        bits.push_back(t & 1);
    }
    cout << "Bits: " << bits.size() << endl;
    
    // Deinterleave (40x36)
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    vector<uint8_t> deinterleaved;
    
    for (size_t blk = 0; blk + block_size <= bits.size(); blk += block_size) {
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                int read_idx = col * rows + row;
                deinterleaved.push_back(bits[blk + read_idx]);
            }
        }
    }
    cout << "Deinterleaved: " << deinterleaved.size() << endl;
    
    // Viterbi decode with INVERTED polarity
    vector<int8_t> soft_bits;
    for (uint8_t b : deinterleaved) {
        soft_bits.push_back(b ? -127 : 127);  // INVERTED
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft_bits, decoded, true);
    cout << "Viterbi decoded: " << decoded.size() << " bits" << endl;
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        bytes.push_back(byte);
    }
    
    cout << "\n=== DECODED ===" << endl;
    cout << "ASCII: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    cout << "\nExpected: " << EXPECTED << endl;
    
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)EXPECTED_LEN); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    cout << "Match: " << matches << "/" << EXPECTED_LEN << " characters";
    if (matches == EXPECTED_LEN) cout << " ✓ PERFECT!";
    cout << endl;
    
    return 0;
}
