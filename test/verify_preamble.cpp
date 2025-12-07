/**
 * Verify preamble symbols match expected pattern
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"

using namespace m110a;
using namespace std;

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

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Preamble symbols: " << result.preamble_symbols.size() << endl;
    
    // Generate expected preamble pattern (first 288 common symbols)
    vector<int> expected;
    for (int i = 0; i < 9; i++) {
        uint8_t d_val = msdmt::p_c_seq[i];
        for (int j = 0; j < 32; j++) {
            uint8_t base = msdmt::psymbol[d_val][j % 8];
            uint8_t scr = msdmt::pscramble[j];
            expected.push_back((base + scr) % 8);
        }
    }
    
    cout << "\n--- First 64 preamble symbols ---" << endl;
    cout << "Expected: ";
    for (int i = 0; i < 64; i++) cout << expected[i];
    cout << endl;
    
    cout << "Received: ";
    for (int i = 0; i < 64 && i < (int)result.preamble_symbols.size(); i++) {
        cout << decode_8psk_position(result.preamble_symbols[i]);
    }
    cout << endl;
    
    // Count matches
    int matches = 0;
    for (int i = 0; i < 64 && i < (int)result.preamble_symbols.size(); i++) {
        if (decode_8psk_position(result.preamble_symbols[i]) == expected[i]) {
            matches++;
        }
    }
    cout << "Matches: " << matches << "/64" << endl;
    
    // The preamble detection says it found a good correlation
    // Let me check the raw correlation at preamble start
    cout << "\n--- Preamble detection result ---" << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Accuracy: " << result.accuracy << "%" << endl;
    
    // Try applying detected phase offset
    cout << "\n--- With phase offset " << (result.phase_offset * 180/M_PI) << "° ---" << endl;
    cout << "Received: ";
    for (int i = 0; i < 64 && i < (int)result.preamble_symbols.size(); i++) {
        // Symbols should already have phase offset applied by decoder
        cout << decode_8psk_position(result.preamble_symbols[i]);
    }
    cout << endl;
    
    return 0;
}
