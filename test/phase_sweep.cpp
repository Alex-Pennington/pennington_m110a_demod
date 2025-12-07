/**
 * Try different phase rotations on extracted symbols
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
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Preamble symbols: " << result.preamble_symbols.size() << endl;
    
    // Generate expected pattern for D2 (symbols 448-479)
    vector<int> expected_d2;
    for (int i = 448; i < 480; i++) {
        uint8_t base = msdmt::psymbol[4][i % 8];  // D2=4
        uint8_t scr = msdmt::pscramble[i % 32];
        expected_d2.push_back((base + scr) % 8);
    }
    
    cout << "\nExpected D2: ";
    for (int p : expected_d2) cout << p;
    cout << endl;
    
    // Try all 8 phase rotations
    cout << "\n--- Phase rotation sweep ---" << endl;
    for (int rot = 0; rot < 8; rot++) {
        float phase = rot * M_PI / 4.0f;
        complex<float> rotator = polar(1.0f, phase);
        
        int matches = 0;
        string actual;
        
        for (int i = 448; i < 480 && i < (int)result.preamble_symbols.size(); i++) {
            complex<float> sym = result.preamble_symbols[i] * rotator;
            int pos = decode_8psk_position(sym);
            actual += '0' + pos;
            if (pos == expected_d2[i - 448]) matches++;
        }
        
        cout << "Phase " << (rot * 45) << "°: " << actual << " matches=" << matches << "/32" << endl;
    }
    
    return 0;
}
