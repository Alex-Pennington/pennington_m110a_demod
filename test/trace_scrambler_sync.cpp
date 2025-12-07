/**
 * Find when data scrambler synchronizes
 * 
 * The preamble uses pscramble (fixed pattern), but data uses LFSR scrambler.
 * Need to find where LFSR starts relative to preamble.
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"
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
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // The expected probe pattern for D2=4 is:
    // probe[i] = (psymbol[4][i%8] + pscramble[offset]) mod 8
    // But the LFSR scrambler is used for data descrambling
    
    // Let's try descrambling with LFSR and see if probe matches
    cout << "\n=== Testing LFSR scrambler on probe symbols ===" << endl;
    
    // First 20 data symbols descrambled
    RefScrambler scr;
    cout << "First 20 data symbols with LFSR descramble:" << endl;
    for (int i = 0; i < 20; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int raw_pos = (int)round(phase / 45.0) % 8;
        
        uint8_t scr_val = scr.next_tribit();
        int desc_pos = (raw_pos - scr_val + 8) % 8;  // Descramble by subtracting
        
        cout << "raw=" << raw_pos << " scr=" << (int)scr_val << " desc=" << desc_pos << "  ";
        if ((i+1) % 5 == 0) cout << endl;
    }
    
    // Now probe symbols (20-39) with continuing LFSR
    cout << "\nProbe symbols (20-39) with LFSR:" << endl;
    cout << "Received: ";
    for (int i = 20; i < 40; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = (int)round(phase / 45.0) % 8;
        cout << pos << " ";
    }
    cout << endl;
    
    cout << "LFSR descrambled: ";
    for (int i = 20; i < 40; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int raw_pos = (int)round(phase / 45.0) % 8;
        
        uint8_t scr_val = scr.next_tribit();
        int desc_pos = (raw_pos - scr_val + 8) % 8;
        cout << desc_pos << " ";
    }
    cout << endl;
    
    // Expected probe pattern (psymbol[4])
    cout << "Expected probe: ";
    for (int i = 0; i < 20; i++) {
        cout << (int)msdmt::psymbol[4][i % 8] << " ";
    }
    cout << endl;
    
    // Count matches
    scr = RefScrambler();
    // Skip first 20 data symbols
    for (int i = 0; i < 20; i++) scr.next_tribit();
    
    int matches = 0;
    for (int i = 20; i < 40; i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int raw_pos = (int)round(phase / 45.0) % 8;
        
        uint8_t scr_val = scr.next_tribit();
        int desc_pos = (raw_pos - scr_val + 8) % 8;
        
        int expected = msdmt::psymbol[4][i % 8];
        if (desc_pos == expected) matches++;
    }
    cout << "Matches: " << matches << "/20" << endl;
    
    // Try different starting offsets for LFSR
    cout << "\n=== Scanning LFSR start offsets ===" << endl;
    for (int offset = 0; offset <= 40; offset++) {
        RefScrambler scr;
        // Skip 'offset' symbols
        for (int i = 0; i < offset; i++) scr.next_tribit();
        
        // Then check probe at positions 20-39
        int m = 0;
        for (int i = 20; i < 40; i++) {
            auto sym = result.data_symbols[i];
            float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
            if (phase < 0) phase += 360;
            int raw_pos = (int)round(phase / 45.0) % 8;
            
            uint8_t scr_val = scr.next_tribit();
            int desc_pos = (raw_pos - scr_val + 8) % 8;
            
            int expected = msdmt::psymbol[4][(i - 20) % 8];
            if (desc_pos == expected) m++;
        }
        if (m >= 10) {
            cout << "Offset " << offset << ": " << m << "/20 matches" << endl;
        }
    }
    
    return 0;
}
