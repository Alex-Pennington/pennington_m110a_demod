/**
 * Detailed trace of 2400S decode chain
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
#include "m110a/msdmt_decoder.h"
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
    
    auto samples = read_pcm(filename);
    cout << "Samples: " << samples.size() << endl;
    
    // Decode
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Preamble start: " << result.start_sample << endl;
    cout << "Phase offset: " << result.phase_offset * 180 / M_PI << " deg" << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Show first data symbol phases
    cout << "\n=== First 40 data symbol phases (raw) ===" << endl;
    for (int i = 0; i < min(40, (int)result.data_symbols.size()); i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        printf("[%2d] phase=%6.1f° pos=%d  ", i, phase, pos);
        if ((i+1) % 4 == 0) cout << endl;
    }
    
    // Generate expected scrambler output for first frame
    cout << "\n=== Expected scrambler output for first 40 symbols ===" << endl;
    RefScrambler scr;
    for (int i = 0; i < 40; i++) {
        uint8_t scr_val = scr.next_tribit();
        printf("%d", scr_val);
        if ((i+1) % 20 == 0) cout << " | ";
    }
    cout << endl;
    
    // Descramble first frame
    cout << "\n=== First frame descrambled ===" << endl;
    scr = RefScrambler();  // Reset
    
    for (int i = 0; i < min(20, (int)result.data_symbols.size()); i++) {
        auto sym = result.data_symbols[i];
        uint8_t scr_val = scr.next_tribit();
        
        // Descramble
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        
        // Gray decode
        const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        int tribit = gray_map[pos];
        
        printf("[%2d] scr=%d raw_pos=%d desc_phase=%6.1f° desc_pos=%d gray=%d (%d%d%d)\n",
               i, scr_val,
               (int)round(atan2(result.data_symbols[i].imag(), 
                                result.data_symbols[i].real()) * 4 / M_PI + 8) % 8,
               phase, pos, tribit, (tribit>>2)&1, (tribit>>1)&1, tribit&1);
    }
    
    // Skip probe symbols and advance scrambler
    for (int i = 0; i < 20; i++) {
        scr.next_tribit();
    }
    
    // Second frame
    cout << "\n=== Second frame descrambled ===" << endl;
    for (int i = 0; i < min(20, (int)result.data_symbols.size() - 40); i++) {
        auto sym = result.data_symbols[20 + i];  // After first 20 data
        uint8_t scr_val = scr.next_tribit();
        
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        
        const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        int tribit = gray_map[pos];
        
        printf("[%2d] scr=%d desc_pos=%d gray=%d (%d%d%d)\n",
               i+20, scr_val, pos, tribit, (tribit>>2)&1, (tribit>>1)&1, tribit&1);
    }
    
    return 0;
}
