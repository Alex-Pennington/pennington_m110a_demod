/**
 * Test if probe = LFSR directly (no psymbol addition)
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
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
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Testing if probe = LFSR directly\n" << endl;
    
    // Theory: Both data and probe use LFSR
    // data[i] = (input_tribit + LFSR[i]) mod 8
    // probe[i] = LFSR[i] (no data added, or data=0)
    
    RefScrambler scr;
    
    cout << "Position  Received  LFSR  Match?" << endl;
    cout << "========  ========  ====  ======" << endl;
    
    int data_matches = 0;
    int probe_matches = 0;
    
    for (int frame = 0; frame < 3; frame++) {
        int base = frame * 40;
        
        cout << "\n--- Frame " << frame << " ---" << endl;
        cout << "DATA symbols (" << base << "-" << base+19 << "):" << endl;
        for (int i = 0; i < 20 && base + i < (int)result.data_symbols.size(); i++) {
            auto sym = result.data_symbols[base + i];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv = (int)round(phase / 45.0) % 8;
            uint8_t lfsr = scr.next_tribit();
            
            // For data: descrambled = (rcv - lfsr + 8) % 8
            int desc = (rcv - lfsr + 8) % 8;
            printf("  [%2d] rcv=%d lfsr=%d desc=%d\n", base + i, rcv, (int)lfsr, desc);
        }
        
        cout << "PROBE symbols (" << base+20 << "-" << base+39 << "):" << endl;
        for (int i = 0; i < 20 && base + 20 + i < (int)result.data_symbols.size(); i++) {
            auto sym = result.data_symbols[base + 20 + i];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv = (int)round(phase / 45.0) % 8;
            uint8_t lfsr = scr.next_tribit();
            
            bool match = (rcv == lfsr);
            if (match) probe_matches++;
            printf("  [%2d] rcv=%d lfsr=%d %s\n", base + 20 + i, rcv, (int)lfsr, match ? "✓" : "✗");
        }
    }
    
    cout << "\n=== Summary ===" << endl;
    cout << "Probe matches (= LFSR): " << probe_matches << "/60" << endl;
    
    return 0;
}
