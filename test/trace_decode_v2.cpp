/**
 * Detailed trace of decode chain for debugging
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
#include "modem/scrambler.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

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

int main(int argc, char** argv) {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    if (argc > 1) filename = argv[1];
    
    cout << "=== Detailed Decode Trace ===" << endl;
    cout << "File: " << filename << endl << endl;
    
    auto samples = read_pcm(filename);
    cout << "Total samples: " << samples.size() << " (" << samples.size()/48000.0 << " sec)" << endl;
    
    // Decode and get symbols
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")" << endl;
    cout << "Preamble start: sample " << result.start_sample << endl;
    cout << "Preamble symbols: " << result.preamble_symbols.size() << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Calculate expected data start
    int preamble_symbols = 1440;  // 3 frames x 480 for short interleave
    int sps = 20;  // samples per symbol at 48kHz/2400
    int expected_data_start = result.start_sample + preamble_symbols * sps;
    cout << "Expected data start: sample " << expected_data_start 
         << " (" << expected_data_start/48000.0 << " sec)" << endl;
    
    // Show first few raw data symbols
    cout << "\n--- First 10 RAW data symbols ---" << endl;
    for (int i = 0; i < min(10, (int)result.data_symbols.size()); i++) {
        auto sym = result.data_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = decode_8psk_position(sym);
        printf("[%2d] mag=%.3f phase=%6.1f° pos=%d\n", 
               i, abs(sym), phase, pos);
    }
    
    // Descramble and show first symbols
    cout << "\n--- First 20 DESCRAMBLED symbols ---" << endl;
    RefScrambler scr;
    
    for (int i = 0; i < min(20, (int)result.data_symbols.size()); i++) {
        complex<float> sym = result.data_symbols[i];
        uint8_t scr_val = scr.next_tribit();
        
        // Original position
        int orig_pos = decode_8psk_position(sym);
        
        // Descramble: rotate by -scr_val * 45°
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        
        int desc_pos = decode_8psk_position(sym);
        
        printf("[%2d] orig=%d scr=%d descr=%d\n", i, orig_pos, scr_val, desc_pos);
    }
    
    // Check expected first byte 'T' = 0x54 = 01010100
    cout << "\n--- Expected first byte 'T' = 0x54 = 01010100 ---" << endl;
    cout << "After rate-1/2 FEC expansion: 4 pairs of coded bits" << endl;
    cout << "With interleaving, these will be scattered" << endl;
    
    // Gray code: position → tribit
    // MS-DMT uses: 0→0, 1→1, 2→3, 3→2, 4→6, 5→7, 6→5, 7→4
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    cout << "\n--- Descrambled symbols → tribits (Gray decoded) ---" << endl;
    scr = RefScrambler();  // Reset
    for (int i = 0; i < min(40, (int)result.data_symbols.size()); i++) {
        complex<float> sym = result.data_symbols[i];
        uint8_t scr_val = scr.next_tribit();
        
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        
        int pos = decode_8psk_position(sym);
        int tribit = gray_map[pos];
        
        printf("%d", tribit);
        if ((i+1) % 20 == 0) printf("  (frame boundary)\n");
    }
    
    return 0;
}
