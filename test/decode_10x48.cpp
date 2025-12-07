/**
 * Decode with correct structure:
 * M2400S: 10 mini-frames × (32 unknown + 16 known) = 480 symbols per super-frame
 * 320 data symbols + 160 probe symbols = 480 per super-frame
 * 
 * Need 480 data symbols for 1440 bits (at 3 bits/symbol)
 * That's 480/320 = 1.5 super-frames worth of data symbols
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

// Data scrambler 
class DataScrambler {
public:
    DataScrambler() { reset(); }
    
    void reset() {
        sreg[0]  = 1;  sreg[1]  = 0;  sreg[2]  = 1;  sreg[3]  = 1;
        sreg[4]  = 0;  sreg[5]  = 1;  sreg[6]  = 0;  sreg[7]  = 1;
        sreg[8]  = 1;  sreg[9]  = 1;  sreg[10] = 0;  sreg[11] = 1;
        count = 0;
    }
    
    void advance_to(int n) {
        reset();
        for (int i = 0; i < n; i++) next_tribit();
    }
    
    uint8_t next_tribit() {
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10]; sreg[10] = sreg[9]; sreg[9] = sreg[8];
            sreg[8] = sreg[7]; sreg[7] = sreg[6]; sreg[6] = sreg[5] ^ carry;
            sreg[5] = sreg[4]; sreg[4] = sreg[3] ^ carry; sreg[3] = sreg[2];
            sreg[2] = sreg[1]; sreg[1] = sreg[0] ^ carry; sreg[0] = carry;
        }
        count = (count + 1) % 160;
        return (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
    }
    
private:
    int sreg[12];
    int count;
};

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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}

int try_decode(const vector<complex<float>>& data_symbols, int start, int scr_offset, bool show_detail) {
    const int UNKNOWN_LEN = 32;
    const int KNOWN_LEN = 16;
    const int SYMBOLS_NEEDED = 480;  // Data symbols for one interleave block
    
    vector<int> positions;
    DataScrambler scr;
    scr.advance_to(scr_offset);
    
    int idx = start;
    
    // Extract symbols: 32 data then 16 probe, 10 times per super-frame
    // Continue until we have 480 data symbols
    while (positions.size() < SYMBOLS_NEEDED && idx < (int)data_symbols.size()) {
        // 32 unknown (data)
        for (int i = 0; i < UNKNOWN_LEN && positions.size() < SYMBOLS_NEEDED; i++) {
            if (idx >= (int)data_symbols.size()) break;
            complex<float> sym = data_symbols[idx++];
            uint8_t scr_val = scr.next_tribit();
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            positions.push_back(decode_8psk_position(sym));
        }
        
        // 16 known (probe) - skip and advance scrambler
        for (int i = 0; i < KNOWN_LEN; i++) {
            if (idx >= (int)data_symbols.size()) break;
            idx++;
            scr.next_tribit();
        }
    }
    
    if (positions.size() < SYMBOLS_NEEDED) {
        if (show_detail) cout << "Only got " << positions.size() << " symbols" << endl;
        return 0;
    }
    
    if (show_detail) {
        cout << "First 40 descrambled positions: ";
        for (int i = 0; i < 40; i++) cout << positions[i];
        cout << endl;
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
    
    // Deinterleave 40×36
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
    
    // Count matches
    int matches = 0;
    string output;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < 60; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    if (show_detail) {
        cout << "Output: " << output.substr(0, 60) << endl;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    cout << "\nSearching with 10×(32+16) structure..." << endl;
    
    int best_matches = 0, best_start = 0, best_scr = 0;
    
    for (int start = 0; start < 200; start++) {
        for (int scr_offset = 0; scr_offset < 160; scr_offset++) {
            int matches = try_decode(result.data_symbols, start, scr_offset, false);
            if (matches > best_matches) {
                best_matches = matches;
                best_start = start;
                best_scr = scr_offset;
            }
        }
    }
    
    cout << "\nBest: start=" << best_start << " scr=" << best_scr 
         << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    
    if (best_matches > 0) {
        cout << "\nDetails:" << endl;
        try_decode(result.data_symbols, best_start, best_scr, true);
    }
    
    return 0;
}
