/**
 * Trace data extraction timing in detail
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

// Downconvert and RRC filter
vector<complex<float>> process_rf(const vector<float>& samples) {
    // Simple downconvert (no RRC for now)
    float fc = 1800.0f, fs = 48000.0f;
    float phase_inc = 2 * M_PI * fc / fs;
    
    vector<complex<float>> bb(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float phase = i * phase_inc;
        bb[i] = complex<float>(samples[i] * cos(phase), -samples[i] * sin(phase));
    }
    
    return bb;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    auto bb = process_rf(samples);
    
    int sps = 20;  // samples per symbol
    
    // Known preamble start (from detection) 
    int preamble_start = 257;
    
    // Generate expected preamble pattern for all 1440 symbols (3 frames)
    vector<int> expected_preamble;
    for (int frame = 0; frame < 3; frame++) {
        for (int i = 0; i < 480; i++) {
            int d_idx = i / 32;
            int d_val;
            if (d_idx < 9) {
                d_val = msdmt::p_c_seq[d_idx];
            } else if (d_idx == 9) {
                d_val = 6;  // D1 for M2400S
            } else if (d_idx == 10) {
                d_val = 4;  // D2 for M2400S
            } else {
                d_val = 0;
            }
            
            uint8_t base = msdmt::psymbol[d_val][i % 8];
            uint8_t scr = msdmt::pscramble[i % 32];
            expected_preamble.push_back((base + scr) % 8);
        }
    }
    
    cout << "Total expected preamble: " << expected_preamble.size() << " symbols" << endl;
    
    // Verify preamble frame by frame
    cout << "\n--- Preamble verification (frame by frame) ---" << endl;
    for (int frame = 0; frame < 3; frame++) {
        int frame_start = preamble_start + frame * 480 * sps;
        int matches = 0;
        
        for (int i = 0; i < 64; i++) {
            int idx = frame_start + i * sps;
            if (idx < (int)bb.size()) {
                int received = decode_8psk_position(bb[idx]);
                int expected = expected_preamble[frame * 480 + i];
                if (received == expected) matches++;
            }
        }
        
        cout << "Frame " << frame << " (sample " << frame_start << "): "
             << matches << "/64 first symbols match" << endl;
    }
    
    // Check where data starts
    int data_start = preamble_start + 1440 * sps;
    cout << "\n--- Data starts at sample " << data_start << " ---" << endl;
    cout << "File has " << samples.size() << " samples" << endl;
    cout << "Data region: " << (samples.size() - data_start) / sps << " symbols" << endl;
    
    // Show first 40 data symbols
    cout << "\n--- First 40 data symbols ---" << endl;
    cout << "Received: ";
    for (int i = 0; i < 40; i++) {
        int idx = data_start + i * sps;
        if (idx < (int)bb.size()) {
            cout << decode_8psk_position(bb[idx]);
        }
    }
    cout << endl;
    
    return 0;
}
