/**
 * Try decoding 1200S file
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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int decode_qpsk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 2.0f / M_PI));
    return ((pos % 4) + 4) % 4;
}

int main() {
    string filename = "/home/claude/tx_1200S_20251206_202533_636.pcm";
    
    auto samples = read_pcm(filename);
    cout << "Samples: " << samples.size() << endl;
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Accuracy: " << result.accuracy << "%" << endl;
    cout << "Preamble start: " << result.start_sample << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // M1200S uses QPSK (2 bits/symbol), 20+20 frame structure
    // 40×36 = 1440 bits, 720 symbols
    
    const int UNKNOWN_LEN = 20;
    const int KNOWN_LEN = 20;
    const int BLOCK_BITS = 40 * 36;
    const int BLOCK_SYMBOLS = BLOCK_BITS / 2;  // QPSK = 2 bits/symbol
    
    cout << "\nNeed " << BLOCK_SYMBOLS << " data symbols" << endl;
    
    // Try simple block interleave first
    RefScrambler scr;
    vector<int> bits;
    
    int idx = 0;
    int symbols_processed = 0;
    
    while (symbols_processed < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < UNKNOWN_LEN && symbols_processed < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size(); i++) {
            complex<float> sym = result.data_symbols[idx++];
            uint8_t scr_val = scr.next_tribit();  // Still tribit for scrambler
            
            // QPSK: rotate by scrambler[1:0] * 90 degrees
            int qpsk_scr = scr_val & 3;  // Lower 2 bits
            float scr_phase = -qpsk_scr * (M_PI / 2.0f);
            sym *= polar(1.0f, scr_phase);
            
            int pos = decode_qpsk_position(sym);
            
            // QPSK Gray: 0→00, 1→01, 2→11, 3→10
            const int gray[4] = {0, 1, 3, 2};
            int dibit = gray[pos];
            
            bits.push_back((dibit >> 1) & 1);
            bits.push_back(dibit & 1);
            
            symbols_processed++;
        }
        
        for (int i = 0; i < KNOWN_LEN && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next_tribit();
        }
    }
    
    cout << "Got " << bits.size() << " bits from " << symbols_processed << " symbols" << endl;
    
    if (bits.size() >= BLOCK_BITS) {
        // Simple block deinterleave
        vector<int> deinterleaved(BLOCK_BITS);
        int rows = 40, cols = 36;
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                int in_idx = col * rows + row;
                int out_idx = row * cols + col;
                deinterleaved[out_idx] = bits[in_idx];
            }
        }
        
        vector<int8_t> soft;
        for (int b : deinterleaved) soft.push_back(b ? -127 : 127);
        
        ViterbiDecoder viterbi;
        vector<uint8_t> decoded;
        viterbi.decode_block(soft, decoded, true);
        
        string output;
        int matches = 0;
        for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
            if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
            output += (byte >= 32 && byte < 127) ? (char)byte : '.';
        }
        
        cout << "\nOutput: " << output.substr(0, 70) << endl;
        cout << "Matches: " << matches << "/" << EXPECTED_LEN << endl;
    }
    
    return 0;
}
