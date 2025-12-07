/**
 * Check if scrambler resets every frame
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
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
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Check probe patterns at different positions
    // If scrambler resets every frame, all probes should match Scr 0-19 and Scr 20-39
    
    RefScrambler scr;
    string probe_0_19, probe_20_39;
    for (int i = 0; i < 20; i++) probe_0_19 += ('0' + scr.next_tribit());
    for (int i = 0; i < 20; i++) probe_20_39 += ('0' + scr.next_tribit());
    
    cout << "Reference patterns:" << endl;
    cout << "Scr 0-19:  " << probe_0_19 << endl;
    cout << "Scr 20-39: " << probe_20_39 << endl;
    
    // Find all positions where probe_0_19 appears
    cout << "\n=== Looking for Scr 0-19 pattern ===" << endl;
    for (size_t pos = 0; pos + 20 <= result.data_symbols.size(); pos++) {
        string received;
        for (int i = 0; i < 20; i++) {
            received += '0' + decode_8psk_position(result.data_symbols[pos + i]);
        }
        
        if (received == probe_0_19) {
            cout << "EXACT at position " << pos << endl;
            
            // Check if this is a probe position (multiple of 40, offset by 20)
            // Actually, let's check what comes after
            if (pos + 40 <= result.data_symbols.size()) {
                string next_block;
                for (int i = 0; i < 20; i++) {
                    next_block += '0' + decode_8psk_position(result.data_symbols[pos + 20 + i]);
                }
                
                if (next_block == probe_20_39) {
                    cout << "  Next block is Scr 20-39 - this looks like a frame probe!" << endl;
                }
            }
        }
    }
    
    // Check spacing between perfect matches
    cout << "\n=== Checking if scrambler runs continuously or resets ===" << endl;
    
    // If scrambler resets every 40 symbols, probes at 1440, 1480, 1520 would all be 02433645...
    // If scrambler runs continuously, probe at 1480 would be Scr 40-59, at 1520 would be Scr 80-99
    
    scr = RefScrambler();
    vector<string> expected_probes;
    for (int frame = 0; frame < 10; frame++) {
        string probe;
        // Advance through data portion
        for (int i = 0; i < 20; i++) scr.next_tribit();
        // Capture probe portion
        for (int i = 0; i < 20; i++) probe += '0' + scr.next_tribit();
        expected_probes.push_back(probe);
    }
    
    cout << "If scrambler runs continuously:" << endl;
    for (int i = 0; i < 5; i++) {
        cout << "Frame " << i << " probe: " << expected_probes[i] << endl;
    }
    
    // Check actual probes starting at 1440
    cout << "\nActual probes at 40-symbol spacing from 1440:" << endl;
    for (int frame = 0; frame < 5; frame++) {
        int pos = 1440 + frame * 40;
        if (pos + 20 > (int)result.data_symbols.size()) break;
        
        string actual;
        for (int i = 0; i < 20; i++) {
            actual += '0' + decode_8psk_position(result.data_symbols[pos + i]);
        }
        cout << "Pos " << pos << ": " << actual << endl;
    }
    
    return 0;
}
