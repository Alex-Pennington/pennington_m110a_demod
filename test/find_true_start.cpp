/**
 * Find true preamble start by correlating with known pattern
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
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
    
    // Simple downconvert (no RRC for speed)
    int sps = 20;
    float fc = 1800.0f, fs = 48000.0f;
    float phase_inc = 2 * M_PI * fc / fs;
    
    // Generate expected first 64 preamble symbols pattern
    vector<int> expected;
    for (int i = 0; i < 64; i++) {
        uint8_t base = msdmt::psymbol[msdmt::p_c_seq[i/32]][i % 8];
        uint8_t scr = msdmt::pscramble[i % 32];
        expected.push_back((base + scr) % 8);
    }
    
    cout << "Expected first 64: ";
    for (int i = 0; i < 64; i++) cout << expected[i];
    cout << endl;
    
    // Search for pattern
    int best_match = 0;
    int best_start = 0;
    int best_phase_offset = 0;
    
    for (int start = 0; start < 1000; start++) {
        for (int phase_off = 0; phase_off < 8; phase_off++) {
            int matches = 0;
            
            for (int i = 0; i < 64; i++) {
                int idx = start + i * sps;
                if (idx >= (int)samples.size()) break;
                
                float phase = idx * phase_inc;
                complex<float> bb(samples[idx] * cos(phase), -samples[idx] * sin(phase));
                
                // Apply phase offset
                float rot = phase_off * M_PI / 4.0f;
                bb *= polar(1.0f, rot);
                
                int pos = decode_8psk_position(bb);
                if (pos == expected[i]) matches++;
            }
            
            if (matches > best_match) {
                best_match = matches;
                best_start = start;
                best_phase_offset = phase_off;
            }
        }
    }
    
    cout << "\nBest match: " << best_match << "/64 at sample " << best_start 
         << " phase_offset=" << best_phase_offset << endl;
    
    // Show actual symbols at best position
    cout << "Actual at best: ";
    for (int i = 0; i < 64; i++) {
        int idx = best_start + i * sps;
        if (idx >= (int)samples.size()) break;
        
        float phase = idx * phase_inc;
        complex<float> bb(samples[idx] * cos(phase), -samples[idx] * sin(phase));
        bb *= polar(1.0f, (float)(best_phase_offset * M_PI / 4.0f));
        
        cout << decode_8psk_position(bb);
    }
    cout << endl;
    
    return 0;
}
