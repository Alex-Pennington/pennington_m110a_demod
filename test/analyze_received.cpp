/**
 * Analyze received symbol magnitudes and phases
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"

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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Analyze symbol statistics
    float sum_mag = 0;
    float min_mag = 1000, max_mag = 0;
    
    for (auto& sym : result.data_symbols) {
        float mag = sqrt(sym.real() * sym.real() + sym.imag() * sym.imag());
        sum_mag += mag;
        min_mag = min(min_mag, mag);
        max_mag = max(max_mag, mag);
    }
    
    cout << "Symbol statistics:" << endl;
    cout << "  Count: " << result.data_symbols.size() << endl;
    cout << "  Avg magnitude: " << sum_mag / result.data_symbols.size() << endl;
    cout << "  Min magnitude: " << min_mag << endl;
    cout << "  Max magnitude: " << max_mag << endl;
    
    // Check for phase rotation by looking at probe symbols
    // Probe at position 32-47 should match scrambler[32:47]
    cout << "\n=== Probe analysis (pos 32-47) ===" << endl;
    cout << "Received (phase/magnitude):" << endl;
    for (int i = 32; i < 48; i++) {
        complex<float> sym = result.data_symbols[i];
        float mag = sqrt(sym.real() * sym.real() + sym.imag() * sym.imag());
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        cout << "  " << i << ": mag=" << mag << " phase=" << phase << "°" << endl;
    }
    
    // Expected phases for scrambler values
    cout << "\nExpected phases for scrambler[32:47]:" << endl;
    int scr_seq[16] = {5,5,7,0,7,3,3,3,7,3,3,1,4,2,3,7}; // from earlier
    for (int i = 0; i < 16; i++) {
        float expected_phase = scr_seq[i] * 45.0f;
        if (expected_phase > 180) expected_phase -= 360;
        cout << "  scr=" << scr_seq[i] << " -> " << expected_phase << "°" << endl;
    }
    
    return 0;
}
