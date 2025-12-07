/**
 * Find correct data start by scanning for probe pattern
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
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Generate probe pattern (scrambled zeros) for first 100 symbols
    RefScrambler scr;
    vector<int> probe_pattern;
    for (int i = 0; i < 100; i++) {
        probe_pattern.push_back(scr.next_tribit());
    }
    
    cout << "\nProbe pattern (first 60): ";
    for (int i = 0; i < 60; i++) {
        cout << probe_pattern[i];
        if ((i+1) % 20 == 0) cout << " ";
    }
    cout << endl;
    
    // Search for probe pattern in data_symbols
    // Probes should appear at positions 20, 60, 100, ... (every 40 symbols, 20-symbol blocks)
    
    cout << "\n--- Searching for probe pattern offset ---" << endl;
    
    for (int data_offset = 0; data_offset < 40; data_offset++) {
        // Check probes at multiple frame positions
        int total_matches = 0;
        int total_checked = 0;
        
        for (int frame = 0; frame < 10; frame++) {
            int probe_start_in_data = data_offset + frame * 40 + 20;
            int probe_start_in_pattern = frame * 20;
            
            for (int i = 0; i < 20; i++) {
                int data_idx = probe_start_in_data + i;
                int pattern_idx = probe_start_in_pattern + i;
                
                if (data_idx < (int)result.data_symbols.size() && 
                    pattern_idx < (int)probe_pattern.size()) {
                    int received = decode_8psk_position(result.data_symbols[data_idx]);
                    if (received == probe_pattern[pattern_idx]) {
                        total_matches++;
                    }
                    total_checked++;
                }
            }
        }
        
        if (total_matches > 50) {  // Report good matches
            cout << "Offset " << data_offset << ": " << total_matches << "/" 
                 << total_checked << " probe matches" << endl;
        }
    }
    
    // Also try negative offsets (data starts before expected)
    cout << "\n--- Trying earlier starts (negative offsets) ---" << endl;
    for (int offset = 1; offset <= 40; offset++) {
        int total_matches = 0;
        
        for (int frame = 0; frame < 5; frame++) {
            int probe_start = frame * 40 + 20 - offset;
            int pattern_idx = frame * 20;
            
            for (int i = 0; i < 20; i++) {
                if (probe_start + i >= 0 && 
                    probe_start + i < (int)result.data_symbols.size() &&
                    pattern_idx + i < (int)probe_pattern.size()) {
                    int received = decode_8psk_position(result.data_symbols[probe_start + i]);
                    if (received == probe_pattern[pattern_idx + i]) {
                        total_matches++;
                    }
                }
            }
        }
        
        if (total_matches > 50) {
            cout << "Offset -" << offset << ": " << total_matches << " matches" << endl;
        }
    }
    
    return 0;
}
