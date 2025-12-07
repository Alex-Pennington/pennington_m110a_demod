/**
 * Check for frequency offset causing phase drift
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

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Checking phase drift across preamble..." << endl;
    
    // Check phase at different points in preamble
    for (int section = 0; section < 3; section++) {
        int start = section * 160;  // 3 sections of ~160 symbols each
        int end = min(start + 32, (int)result.preamble_symbols.size());
        
        float avg_phase = 0;
        for (int i = start; i < end; i++) {
            avg_phase += atan2(result.preamble_symbols[i].imag(), 
                              result.preamble_symbols[i].real());
        }
        avg_phase /= (end - start);
        
        cout << "Section " << section << " (symbols " << start << "-" << end 
             << "): avg phase = " << (avg_phase * 180 / M_PI) << "°" << endl;
    }
    
    // Check phase of probe symbols in data section
    cout << "\nChecking phase of data symbols..." << endl;
    for (int frame = 0; frame < 3; frame++) {
        int start = frame * 40 + 20;  // Probe symbols
        int end = min(start + 20, (int)result.data_symbols.size());
        
        if (end <= start) break;
        
        float avg_phase = 0;
        for (int i = start; i < end; i++) {
            avg_phase += atan2(result.data_symbols[i].imag(), 
                              result.data_symbols[i].real());
        }
        avg_phase /= (end - start);
        
        cout << "Data frame " << frame << " probes: avg phase = " 
             << (avg_phase * 180 / M_PI) << "°" << endl;
    }
    
    return 0;
}
