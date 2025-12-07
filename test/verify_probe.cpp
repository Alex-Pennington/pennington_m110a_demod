/**
 * Verify probe pattern matches expected
 */
#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <fstream>
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

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")" << endl;
    
    // For M2400S, D2=4
    // Probe pattern: (psymbol[D2][i%8] + pscramble[offset]) mod 8
    // The pscramble cycles every 32 symbols
    
    // After preamble (1440 symbols), data frames start
    // Each frame: 20 data + 20 probe
    // Probe uses pscramble starting at different offsets
    
    cout << "\n=== Probe Pattern Verification ===" << endl;
    cout << "psymbol[4] = ";
    for (int i = 0; i < 8; i++) {
        cout << (int)msdmt::psymbol[4][i] << " ";
    }
    cout << endl;
    
    cout << "pscramble = ";
    for (int i = 0; i < 32; i++) {
        cout << (int)msdmt::pscramble[i] << " ";
    }
    cout << endl;
    
    // Generate expected probe for frame 0
    // After preamble of 1440 symbols = 3 frames × 480
    // In data section: frame 0 probe starts at symbol 20 within frame
    // pscramble offset = (preamble_symbols + frame * 40 + 20) mod 32
    
    cout << "\nFrame 0 probe symbols (position 20-39):" << endl;
    cout << "Received: ";
    for (int i = 20; i < 40; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        cout << pos << " ";
    }
    cout << endl;
    
    // The scramble offset for data section probes
    // Preamble is 1440 symbols = 3 × 480
    // Data frame 0: symbols 1440-1479 (relative 0-39)
    // Probe starts at relative symbol 20
    // pscramble offset = (1440 + 20) mod 32 = 20
    
    int scr_offset = (1440 + 20) % 32;
    cout << "Expected (offset=" << scr_offset << "): ";
    for (int i = 0; i < 20; i++) {
        uint8_t exp = (msdmt::psymbol[4][i % 8] + msdmt::pscramble[(scr_offset + i) % 32]) % 8;
        cout << (int)exp << " ";
    }
    cout << endl;
    
    // Count matches
    int matches = 0;
    for (int i = 0; i < 20; i++) {
        auto sym = result.data_symbols[20 + i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int rcv_pos = (int)round(phase / 45.0) % 8;
        uint8_t exp_pos = (msdmt::psymbol[4][i % 8] + msdmt::pscramble[(scr_offset + i) % 32]) % 8;
        if (rcv_pos == exp_pos) matches++;
    }
    cout << "Matches: " << matches << "/20" << endl;
    
    // Try different scramble offsets to find the correct one
    cout << "\n=== Scanning for correct scramble offset ===" << endl;
    for (int off = 0; off < 32; off++) {
        int m = 0;
        for (int i = 0; i < 20; i++) {
            auto sym = result.data_symbols[20 + i];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv_pos = (int)round(phase / 45.0) % 8;
            uint8_t exp_pos = (msdmt::psymbol[4][i % 8] + msdmt::pscramble[(off + i) % 32]) % 8;
            if (rcv_pos == exp_pos) m++;
        }
        if (m >= 15) {
            cout << "Offset " << off << ": " << m << "/20 matches" << endl;
        }
    }
    
    return 0;
}
