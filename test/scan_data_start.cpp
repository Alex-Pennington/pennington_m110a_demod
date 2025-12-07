/**
 * Scan for actual data start by finding where LFSR matches probe
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
    vector<int16_t> raw(size / 2);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(size / 2);
    for (size_t i = 0; i < size / 2; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    // The decoder's data_symbols starts where it thinks data begins
    // Let's scan for the best LFSR alignment
    
    cout << "Total data_symbols: " << result.data_symbols.size() << endl;
    cout << "\nScanning for LFSR alignment..." << endl;
    
    // For each possible start offset, check how many probe symbols match LFSR
    cout << "Offset: matches (testing probe at positions 20-39)" << endl;
    
    for (int offset = -100; offset <= 100; offset++) {
        RefScrambler scr;
        
        // Advance scrambler by offset (can be negative by using more initial clocks)
        int actual_offset = offset;
        if (offset < 0) {
            actual_offset = 0;  // Can't go negative, start from 0
        }
        for (int i = 0; i < actual_offset; i++) scr.next_tribit();
        
        // Check probe alignment at position 20-39 (first probe)
        // Skip first 20 for data
        for (int i = 0; i < 20; i++) scr.next_tribit();
        
        // Count matches for probe
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            int sym_idx = offset + 20 + i;
            if (sym_idx < 0 || sym_idx >= (int)result.data_symbols.size()) continue;
            
            auto sym = result.data_symbols[sym_idx];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv = (int)round(phase / 45.0) % 8;
            uint8_t lfsr = scr.next_tribit();
            if (rcv == lfsr) matches++;
        }
        
        if (matches >= 12) {
            cout << "Offset " << offset << ": " << matches << "/20 matches" << endl;
        }
    }
    
    // Also try scanning for where probe = LFSR continuously
    cout << "\n=== Scanning for continuous LFSR match ===" << endl;
    for (int frame_start = 0; frame_start < min(200, (int)result.data_symbols.size() - 40); frame_start++) {
        RefScrambler scr;
        
        // Check if symbols at frame_start+20 to frame_start+39 match LFSR at positions 20-39
        // First advance LFSR for the "data" portion (0-19)
        for (int i = 0; i < 20; i++) scr.next_tribit();
        
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            int sym_idx = frame_start + 20 + i;
            if (sym_idx >= (int)result.data_symbols.size()) break;
            
            auto sym = result.data_symbols[sym_idx];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int rcv = (int)round(phase / 45.0) % 8;
            uint8_t lfsr = scr.next_tribit();
            if (rcv == lfsr) matches++;
        }
        
        if (matches >= 15) {
            cout << "Frame starting at " << frame_start << ": " << matches << "/20 probe matches" << endl;
        }
    }
    
    return 0;
}
