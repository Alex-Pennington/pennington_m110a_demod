/**
 * Try decode with different phase offsets
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
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

int try_decode(const vector<complex<float>>& data_symbols, float phase_offset) {
    complex<float> phase_rot = polar(1.0f, phase_offset);
    
    // Descramble
    RefScrambler scr;
    vector<int> positions;
    
    int unknown_len = 20, known_len = 20;
    int sym_idx = 0;
    
    while (sym_idx + unknown_len + known_len <= (int)data_symbols.size()) {
        for (int i = 0; i < unknown_len; i++) {
            complex<float> sym = data_symbols[sym_idx + i] * phase_rot;
            uint8_t scr_val = scr.next_tribit();
            
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            
            positions.push_back(decode_8psk_position(sym));
        }
        
        for (int i = 0; i < known_len; i++) {
            scr.next_tribit();
        }
        
        sym_idx += unknown_len + known_len;
    }
    
    // Gray decode
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave (40x36)
    int rows = 40, cols = 36, block_size = 1440;
    vector<int> deinterleaved;
    for (size_t block_start = 0; block_start + block_size <= bits.size(); block_start += block_size) {
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                int in_idx = col * rows + row;
                if (block_start + in_idx < bits.size()) {
                    deinterleaved.push_back(bits[block_start + in_idx]);
                }
            }
        }
    }
    
    // Viterbi decode
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);  // Correct polarity
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        bytes.push_back(byte);
    }
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)EXPECTED_LEN); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    cout << "\n--- Trying different phase offsets ---" << endl;
    for (int rot = 0; rot < 8; rot++) {
        float phase = rot * M_PI / 4.0f;
        int matches = try_decode(result.data_symbols, phase);
        cout << "Phase " << (rot * 45) << "°: " << matches << "/" << EXPECTED_LEN << " matches" << endl;
    }
    
    return 0;
}
