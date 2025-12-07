/**
 * Check symbol extraction from PCM
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
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Print first 48 symbols' angles and magnitudes
    cout << "\nFirst 48 symbols (data + probe):" << endl;
    cout << "Idx  Real     Imag     Mag      Angle    Pos" << endl;
    
    for (int i = 0; i < 48 && i < (int)result.data_symbols.size(); i++) {
        complex<float> sym = result.data_symbols[i];
        float mag = abs(sym);
        float angle = atan2(sym.imag(), sym.real()) * 180.0f / M_PI;
        int pos = static_cast<int>(round(atan2(sym.imag(), sym.real()) * 4.0f / M_PI));
        pos = ((pos % 8) + 8) % 8;
        
        printf("%3d  %7.4f  %7.4f  %6.4f  %7.2f°  %d\n", 
               i, sym.real(), sym.imag(), mag, angle, pos);
    }
    
    // Check constellation quality
    cout << "\nConstellation quality check:" << endl;
    float avg_mag = 0;
    float min_mag = 1e9, max_mag = 0;
    for (size_t i = 0; i < result.data_symbols.size(); i++) {
        float mag = abs(result.data_symbols[i]);
        avg_mag += mag;
        if (mag < min_mag) min_mag = mag;
        if (mag > max_mag) max_mag = mag;
    }
    avg_mag /= result.data_symbols.size();
    cout << "Average magnitude: " << avg_mag << endl;
    cout << "Min magnitude: " << min_mag << endl;
    cout << "Max magnitude: " << max_mag << endl;
    
    return 0;
}
