/**
 * Try decoding from many different start positions
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

int try_decode(const vector<complex<float>>& data_symbols, int start, bool reset_scr) {
    if (start + 960 > (int)data_symbols.size()) return 0;
    
    vector<int> positions;
    RefScrambler scr;
    int idx = start;
    
    while (positions.size() < 480) {
        if (reset_scr) scr = RefScrambler();  // Reset every frame
        
        for (int i = 0; i < 20 && positions.size() < 480; i++) {
            complex<float> sym = data_symbols[idx + i];
            uint8_t scr_val = scr.next_tribit();
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            positions.push_back(decode_8psk_position(sym));
        }
        
        // Advance scrambler through probe
        if (!reset_scr) {
            for (int i = 0; i < 20; i++) scr.next_tribit();
        }
        
        idx += 40;
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
    
    // Deinterleave
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
    
    // Viterbi
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes and count matches
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
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    cout << "Trying all start positions..." << endl;
    
    int best_matches = 0;
    int best_start = 0;
    bool best_reset = false;
    
    for (int start = 0; start < min(500, (int)result.data_symbols.size() - 960); start++) {
        int matches = try_decode(result.data_symbols, start, false);
        if (matches > best_matches) {
            best_matches = matches;
            best_start = start;
            best_reset = false;
        }
        
        matches = try_decode(result.data_symbols, start, true);
        if (matches > best_matches) {
            best_matches = matches;
            best_start = start;
            best_reset = true;
        }
    }
    
    cout << "\nBest: start=" << best_start << " reset=" << best_reset 
         << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    
    if (best_matches > 5) {
        // Show decoded output
        cout << "\nDecoding with best parameters..." << endl;
        
        vector<int> positions;
        RefScrambler scr;
        int idx = best_start;
        
        while (positions.size() < 480) {
            if (best_reset) scr = RefScrambler();
            
            for (int i = 0; i < 20 && positions.size() < 480; i++) {
                complex<float> sym = result.data_symbols[idx + i];
                uint8_t scr_val = scr.next_tribit();
                float scr_phase = -scr_val * (M_PI / 4.0f);
                sym *= polar(1.0f, scr_phase);
                positions.push_back(decode_8psk_position(sym));
            }
            
            if (!best_reset) {
                for (int i = 0; i < 20; i++) scr.next_tribit();
            }
            
            idx += 40;
        }
        
        const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        vector<int> bits;
        for (int pos : positions) {
            int tribit = gray_map[pos];
            bits.push_back((tribit >> 2) & 1);
            bits.push_back((tribit >> 1) & 1);
            bits.push_back(tribit & 1);
        }
        
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
        
        string output;
        for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) {
                byte = (byte << 1) | (decoded[i + j] & 1);
            }
            if (byte >= 32 && byte < 127) output += (char)byte;
            else output += '.';
        }
        
        cout << "Output: " << output.substr(0, 60) << endl;
    }
    
    return 0;
}
