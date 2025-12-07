/**
 * Brute force try all Gray code mappings
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    size_t num_samples = size / 2;
    vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    pos = ((pos % 8) + 8) % 8;
    return pos;
}

int try_decode(const vector<int>& positions, const int* gray_map) {
    vector<int> bits;
    for (int pos : positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave 40x36
    int rows = 40, cols = 36;
    vector<int> deinterleaved(1440);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = col * rows + row;
            int out_idx = row * cols + col;
            if (in_idx < (int)bits.size()) {
                deinterleaved[out_idx] = bits[in_idx];
            }
        }
    }
    
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < EXPECTED_LEN; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        if (byte == (uint8_t)EXPECTED[i/8]) matches++;
    }
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Extract and descramble 480 data symbols (no frame structure)
    vector<int> positions;
    RefScrambler scr;
    
    for (int i = 0; i < 480 && i < (int)result.data_symbols.size(); i++) {
        complex<float> sym = result.data_symbols[i];
        uint8_t scr_val = scr.next_tribit();
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        positions.push_back(decode_8psk_position(sym));
    }
    
    cout << "Testing all Gray code permutations..." << endl;
    
    // Try all permutations of 0-7 as Gray code
    int perm[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int best_matches = 0;
    int best_perm[8];
    
    int count = 0;
    do {
        int matches = try_decode(positions, perm);
        if (matches > best_matches) {
            best_matches = matches;
            copy(perm, perm + 8, best_perm);
        }
        count++;
        if (count % 1000 == 0) {
            cout << "Tried " << count << " permutations, best=" << best_matches << endl;
        }
    } while (next_permutation(perm, perm + 8));
    
    cout << "\nBest: " << best_matches << "/" << EXPECTED_LEN << " matches" << endl;
    cout << "Gray map: ";
    for (int i = 0; i < 8; i++) cout << best_perm[i] << " ";
    cout << endl;
    
    return 0;
}
