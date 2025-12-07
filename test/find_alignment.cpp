/**
 * Try to find symbol alignment by correlating expected vs received
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

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
    
    // Extract received positions
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Generate expected probe pattern only (scrambled zeros)
    RefScrambler scr;
    vector<int> probe_pattern;
    for (int i = 0; i < 200; i++) {
        probe_pattern.push_back(scr.next_tribit());
    }
    
    // Try to find probe pattern in received (probes at positions 20-39, 60-79, etc)
    cout << "Looking for probe pattern..." << endl;
    cout << "Probe pattern (first 40): ";
    for (int i = 0; i < 40; i++) cout << probe_pattern[i];
    cout << endl;
    
    // Search for best match
    int best_offset = -1;
    int best_matches = 0;
    
    for (int offset = 0; offset < 100; offset++) {
        int matches = 0;
        // Check probes at expected positions
        for (int frame = 0; frame < 5; frame++) {
            int probe_start_recv = offset + frame * 40 + 20;
            int probe_start_exp = frame * 20;
            
            for (int i = 0; i < 20; i++) {
                if (probe_start_recv + i < (int)received.size() &&
                    probe_start_exp + i < (int)probe_pattern.size()) {
                    if (received[probe_start_recv + i] == probe_pattern[probe_start_exp + i]) {
                        matches++;
                    }
                }
            }
        }
        
        if (matches > best_matches) {
            best_matches = matches;
            best_offset = offset;
        }
    }
    
    cout << "Best offset: " << best_offset << " with " << best_matches << "/100 probe matches" << endl;
    
    // Show alignment at best offset
    cout << "\n--- At offset " << best_offset << " ---" << endl;
    for (int frame = 0; frame < 2; frame++) {
        int data_start = best_offset + frame * 40;
        int probe_start = data_start + 20;
        
        cout << "Frame " << frame << " data (pos " << data_start << "-" << (data_start+19) << "): ";
        for (int i = 0; i < 20 && data_start + i < (int)received.size(); i++) {
            cout << received[data_start + i];
        }
        cout << endl;
        
        cout << "Frame " << frame << " probe (pos " << probe_start << "-" << (probe_start+19) << "): ";
        for (int i = 0; i < 20 && probe_start + i < (int)received.size(); i++) {
            cout << received[probe_start + i];
        }
        cout << endl;
        
        cout << "Expected probe: ";
        for (int i = 0; i < 20 && frame * 20 + i < (int)probe_pattern.size(); i++) {
            cout << probe_pattern[frame * 20 + i];
        }
        cout << endl << endl;
    }
    
    return 0;
}
