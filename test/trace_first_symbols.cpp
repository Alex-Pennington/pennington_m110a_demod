/**
 * Trace first few symbols after preamble in detail
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
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

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    pos = ((pos % 8) + 8) % 8;
    return pos;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    cout << "=== First Symbol Trace ===" << endl;
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Phase offset: " << (result.phase_offset * 180 / M_PI) << " degrees" << endl;
    
    // For M2400S, the expected first byte is 'T' = 0x54 = 01010100
    // After FEC (rate 1/2), first 8 bits become 16 bits
    // These 16 bits get interleaved across the 40x36 matrix
    
    cout << "\n--- Expected first byte 'T' = 0x54 ---" << endl;
    cout << "Binary: 01010100" << endl;
    
    // Show first 80 symbols (2 frames)
    cout << "\n--- First 80 data symbols ---" << endl;
    cout << "Format: [idx] raw_phase raw_pos scr_val desc_pos" << endl;
    
    RefScrambler scr;
    
    for (int i = 0; i < min(80, (int)result.data_symbols.size()); i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int raw_pos = decode_8psk_position(sym);
        
        uint8_t scr_val = scr.next_tribit();
        
        // Descramble
        float scr_phase = -scr_val * (M_PI / 4.0f);
        auto desc_sym = sym * polar(1.0f, scr_phase);
        int desc_pos = decode_8psk_position(desc_sym);
        
        if (i < 40) {  // First frame
            printf("[%2d] phase=%6.1f pos=%d scr=%d -> %d%s\n", 
                   i, phase, raw_pos, scr_val, desc_pos,
                   i < 20 ? " (data)" : " (probe)");
        }
    }
    
    // The scrambler should produce consistent output
    // Let's verify by checking what the scrambler outputs
    cout << "\n--- Scrambler first 40 outputs ---" << endl;
    RefScrambler scr2;
    for (int i = 0; i < 40; i++) {
        cout << (int)scr2.next_tribit();
        if (i == 19) cout << " | ";
    }
    cout << endl;
    
    return 0;
}
