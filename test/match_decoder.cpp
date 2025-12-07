/**
 * Match MSDMTDecoder exactly to understand the data extraction
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

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
    
    // Use MSDMTDecoder to get symbols
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Accuracy: " << result.accuracy << "%" << endl;
    cout << "Preamble start: " << result.start_sample << endl;
    cout << "Phase offset: " << (result.phase_offset * 180/M_PI) << "°" << endl;
    cout << "Preamble symbols: " << result.preamble_symbols.size() << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Verify preamble extraction
    cout << "\n--- Preamble verification ---" << endl;
    vector<int> expected;
    for (int i = 0; i < 288; i++) {
        uint8_t d_val = msdmt::p_c_seq[i / 32];
        uint8_t base = msdmt::psymbol[d_val][i % 8];
        uint8_t scr = msdmt::pscramble[i % 32];
        expected.push_back((base + scr) % 8);
    }
    
    int matches = 0;
    for (int i = 0; i < 288 && i < (int)result.preamble_symbols.size(); i++) {
        int rcv = decode_8psk_position(result.preamble_symbols[i]);
        if (rcv == expected[i]) matches++;
    }
    cout << "First 288 preamble: " << matches << "/288 matches" << endl;
    
    // Now try different interpretations of data symbol mapping
    cout << "\n--- Data symbol analysis ---" << endl;
    
    // The data_symbols contain:
    // - 20 unknown (data) symbols
    // - 20 known (probe) symbols
    // - repeated for each frame
    
    // Check if "data" positions are actually data or probes
    cout << "First 40 data_symbols (positions): ";
    for (int i = 0; i < 40 && i < (int)result.data_symbols.size(); i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
    }
    cout << endl;
    
    // The probe should match the scrambler output (scrambled zero)
    RefScrambler scr;
    cout << "Scrambler first 40:              ";
    for (int i = 0; i < 40; i++) {
        cout << (int)scr.next_tribit();
    }
    cout << endl;
    
    // What if we need to advance scrambler by 1440 (preamble length)?
    scr = RefScrambler();
    for (int i = 0; i < 1440; i++) scr.next_tribit();
    cout << "Scrambler after 1440:            ";
    for (int i = 0; i < 40; i++) {
        cout << (int)scr.next_tribit();
    }
    cout << endl;
    
    return 0;
}
