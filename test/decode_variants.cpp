/**
 * Try different decode variants
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

void try_decode(vector<complex<float>>& syms, int gray_variant, bool reverse_interleave, bool flip_bits) {
    cout << "\n=== Gray=" << gray_variant << " RevInt=" << reverse_interleave << " FlipBits=" << flip_bits << " ===" << endl;
    
    // Different Gray code mappings to try
    int gray_maps[4][8] = {
        {0, 1, 3, 2, 6, 7, 5, 4},  // Standard
        {0, 1, 3, 2, 7, 6, 4, 5},  // Alt 1
        {0, 1, 2, 3, 7, 6, 5, 4},  // Linear + flip
        {0, 4, 6, 2, 3, 7, 5, 1},  // Different
    };
    
    RefScrambler scr;
    
    // Descramble data symbols
    vector<int> data_positions;
    int sym_idx = 0;
    
    while (sym_idx + 40 <= (int)syms.size()) {
        for (int i = 0; i < 20; i++) {
            auto sym = syms[sym_idx + i];
            float phase = atan2(sym.imag(), sym.real());
            if (phase < 0) phase += 2 * M_PI;
            int raw_pos = (int)round(phase * 4 / M_PI) % 8;
            
            uint8_t scr_val = scr.next_tribit();
            int desc_pos = (raw_pos - scr_val + 8) % 8;
            data_positions.push_back(desc_pos);
        }
        for (int i = 0; i < 20; i++) scr.next_tribit();
        sym_idx += 40;
    }
    
    // Gray decode to bits
    vector<int> bits;
    for (int pos : data_positions) {
        int tribit = gray_maps[gray_variant][pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave (40x36)
    int rows = 40, cols = 36;
    int block_size = rows * cols;
    vector<int> deinterleaved;
    
    if (reverse_interleave) {
        // Row-major read
        for (size_t blk = 0; blk + block_size <= bits.size(); blk += block_size) {
            for (int row = 0; row < rows; row++) {
                for (int col = 0; col < cols; col++) {
                    int in_idx = row * cols + col;
                    deinterleaved.push_back(bits[blk + in_idx]);
                }
            }
        }
    } else {
        // Column-major read
        for (size_t blk = 0; blk + block_size <= bits.size(); blk += block_size) {
            for (int row = 0; row < rows; row++) {
                for (int col = 0; col < cols; col++) {
                    int in_idx = col * rows + row;
                    deinterleaved.push_back(bits[blk + in_idx]);
                }
            }
        }
    }
    
    // Flip bits if requested
    if (flip_bits) {
        for (auto& b : deinterleaved) b = 1 - b;
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
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)54); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    
    if (matches > 2) {
        cout << "Match: " << matches << "/54 - ";
        for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
            char c = bytes[i];
            if (c >= 32 && c < 127) cout << c;
            else cout << '.';
        }
        cout << endl;
    } else {
        cout << "Match: " << matches << "/54" << endl;
    }
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    // Try all combinations
    for (int gray = 0; gray < 4; gray++) {
        for (int rev = 0; rev <= 1; rev++) {
            for (int flip = 0; flip <= 1; flip++) {
                try_decode(result.data_symbols, gray, rev, flip);
            }
        }
    }
    
    return 0;
}
