/**
 * Decode with LFSR offset adjustment
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

void try_decode(vector<complex<float>>& syms, int start_offset) {
    cout << "\n=== Trying offset " << start_offset << " ===" << endl;
    
    RefScrambler scr;
    
    // Pre-advance scrambler by offset
    for (int i = 0; i < start_offset; i++) scr.next_tribit();
    
    // Descramble data symbols (skip probe)
    vector<int> data_positions;
    int sym_idx = 0;
    
    while (sym_idx + 40 <= (int)syms.size()) {
        // Process 20 data symbols
        for (int i = 0; i < 20; i++) {
            auto sym = syms[sym_idx + i];
            float phase = atan2(sym.imag(), sym.real());
            if (phase < 0) phase += 2 * M_PI;
            int raw_pos = (int)round(phase * 4 / M_PI) % 8;
            
            uint8_t scr_val = scr.next_tribit();
            int desc_pos = (raw_pos - scr_val + 8) % 8;
            data_positions.push_back(desc_pos);
        }
        
        // Skip 20 probe symbols (but advance scrambler)
        for (int i = 0; i < 20; i++) scr.next_tribit();
        
        sym_idx += 40;
    }
    
    cout << "Descrambled " << data_positions.size() << " data symbols" << endl;
    
    // Gray decode to bits
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : data_positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave (40x36)
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    vector<int> deinterleaved;
    
    for (size_t blk = 0; blk + block_size <= bits.size(); blk += block_size) {
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                int in_idx = col * rows + row;
                deinterleaved.push_back(bits[blk + in_idx]);
            }
        }
    }
    
    // Viterbi decode
    vector<int8_t> soft_bits;
    for (int b : deinterleaved) soft_bits.push_back(b ? 127 : -127);
    
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
    
    // Show result
    cout << "Decoded " << bytes.size() << " bytes" << endl;
    cout << "ASCII: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)54); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    cout << "Match: " << matches << "/54 characters" << endl;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    // Try different offsets
    for (int off = 0; off <= 20; off += 2) {
        try_decode(result.data_symbols, off);
    }
    
    return 0;
}
