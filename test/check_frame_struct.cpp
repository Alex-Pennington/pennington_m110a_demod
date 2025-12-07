/**
 * Check frame structure - data vs probe symbols
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
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
    cout << "Samples: " << samples.size() << " (" << samples.size()/48000.0 << " sec)" << endl;
    
    // Duration analysis
    double total_sec = samples.size() / 48000.0;
    double preamble_sec = 1440 / 2400.0;  // Short interleave: 3 frames × 480 symbols
    double data_sec = total_sec - preamble_sec;
    double data_symbols = data_sec * 2400;
    
    cout << "\nExpected structure:" << endl;
    cout << "  Total duration: " << total_sec << " sec" << endl;
    cout << "  Preamble: " << preamble_sec << " sec (" << 1440 << " symbols)" << endl;
    cout << "  Data: " << data_sec << " sec (" << data_symbols << " symbols)" << endl;
    cout << "  Data frames: " << data_symbols / 40.0 << " (40 symbols/frame)" << endl;
    
    // Decode
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "\nDecoder output:" << endl;
    cout << "  Mode: " << result.mode_name << endl;
    cout << "  Data symbols extracted: " << result.data_symbols.size() << endl;
    cout << "  Expected frames: " << result.data_symbols.size() / 40 << endl;
    cout << "  Data symbols per frame: 20" << endl;
    cout << "  Total data symbols: " << (result.data_symbols.size() / 40) * 20 << endl;
    
    // Analyze probe symbol pattern
    // Probe symbols should have known pattern - correlate to identify
    cout << "\n=== Probe Pattern Analysis ===" << endl;
    
    // Expected probe is scrambled version of D2 pattern
    // For M2400S, D2=4
    // Probe = (psymbol[4] + pscramble) mod 8
    
    // pscramble cycles every 32 symbols
    cout << "First 80 symbol phases (2 frames):" << endl;
    for (int i = 0; i < min(80, (int)result.data_symbols.size()); i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        
        // Mark frame boundaries
        if (i % 40 == 0) cout << "\n--- Frame " << i/40 << " (data) ---\n";
        if (i % 40 == 20) cout << "--- (probe) ---\n";
        
        printf("[%2d] pos=%d  ", i % 40, pos);
        if ((i % 40 + 1) % 10 == 0) cout << endl;
    }
    cout << endl;
    
    return 0;
}
