/**
 * Test probe symbol theory:
 * probe[i] = (scrambler_output[i] + psymbol[D2][i%8]) mod 8
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"
#include "modem/scrambler.h"

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
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << " (D2=" << result.d2 << ")" << endl;
    
    // Theory: probe[i] = (LFSR_output + psymbol[D2][i%8]) mod 8
    // So: LFSR_output = (probe[i] - psymbol[D2][i%8] + 8) mod 8
    
    cout << "\npsymbol[" << result.d2 << "] = ";
    for (int i = 0; i < 8; i++) cout << (int)msdmt::psymbol[result.d2][i] << " ";
    cout << endl;
    
    // Extract received probe symbols for frame 0
    cout << "\nFrame 0 probe symbols (20-39):" << endl;
    cout << "Received:   ";
    for (int i = 20; i < 40; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        cout << pos << " ";
    }
    cout << endl;
    
    // Expected LFSR output at positions 20-39 (after processing 20 data symbols)
    RefScrambler scr;
    cout << "LFSR (0-19): ";
    for (int i = 0; i < 20; i++) {
        cout << (int)scr.next_tribit() << " ";
    }
    cout << endl;
    
    cout << "LFSR (20-39): ";
    vector<uint8_t> lfsr_probe;
    for (int i = 20; i < 40; i++) {
        uint8_t v = scr.next_tribit();
        lfsr_probe.push_back(v);
        cout << (int)v << " ";
    }
    cout << endl;
    
    // Expected probe = LFSR + psymbol[D2]
    cout << "Expected:   ";
    for (int i = 0; i < 20; i++) {
        int exp = (lfsr_probe[i] + msdmt::psymbol[result.d2][i % 8]) % 8;
        cout << exp << " ";
    }
    cout << endl;
    
    // Count matches
    int matches = 0;
    for (int i = 0; i < 20; i++) {
        auto sym = result.data_symbols[20 + i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int rcv = (int)round(phase / 45.0) % 8;
        int exp = (lfsr_probe[i] + msdmt::psymbol[result.d2][i % 8]) % 8;
        if (rcv == exp) matches++;
    }
    cout << "Matches: " << matches << "/20" << endl;
    
    // Alternative theory: probe is just psymbol[D2] + pscramble (like preamble)
    cout << "\n=== Alternative: pscramble theory ===" << endl;
    // pscramble offset after preamble (1440 symbols) + 20 data = 1460
    int pscr_offset = (1440 + 20) % 32;
    cout << "pscramble offset: " << pscr_offset << endl;
    cout << "Expected:   ";
    for (int i = 0; i < 20; i++) {
        int exp = (msdmt::psymbol[result.d2][i % 8] + msdmt::pscramble[(pscr_offset + i) % 32]) % 8;
        cout << exp << " ";
    }
    cout << endl;
    
    matches = 0;
    for (int i = 0; i < 20; i++) {
        auto sym = result.data_symbols[20 + i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int rcv = (int)round(phase / 45.0) % 8;
        int exp = (msdmt::psymbol[result.d2][i % 8] + msdmt::pscramble[(pscr_offset + i) % 32]) % 8;
        if (rcv == exp) matches++;
    }
    cout << "Matches: " << matches << "/20" << endl;
    
    // Let's try scanning different pscramble offsets
    cout << "\n=== Scanning pscramble offsets ===" << endl;
    for (int off = 0; off < 32; off++) {
        int m = 0;
        for (int i = 0; i < 20; i++) {
            auto sym = result.data_symbols[20 + i];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv = (int)round(phase / 45.0) % 8;
            int exp = (msdmt::psymbol[result.d2][i % 8] + msdmt::pscramble[(off + i) % 32]) % 8;
            if (rcv == exp) m++;
        }
        if (m >= 15) {
            cout << "Offset " << off << ": " << m << "/20 matches" << endl;
        }
    }
    
    return 0;
}
