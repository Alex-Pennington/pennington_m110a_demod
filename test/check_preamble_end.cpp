/**
 * Check end of preamble to verify phase alignment
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
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
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Phase offset: " << (result.phase_offset * 180 / M_PI) << " degrees" << endl;
    cout << "Preamble symbols extracted: " << result.preamble_symbols.size() << endl;
    
    // Short interleave has 3 preamble frames of 480 symbols each = 1440 total
    // But we only extract 480 (first frame)
    
    cout << "\n--- Last 20 preamble symbols (frame 1, positions 460-479) ---" << endl;
    for (int i = 460; i < 480 && i < (int)result.preamble_symbols.size(); i++) {
        auto sym = result.preamble_symbols[i];
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        int pos = decode_8psk_position(sym);
        cout << "[" << i << "] phase=" << phase << " pos=" << pos << endl;
    }
    
    // The expected preamble pattern for the end of frame 1
    // Symbols 448-479 are the D2 pattern (32 symbols)
    cout << "\n--- Expected D2 pattern (symbols 448-479) ---" << endl;
    cout << "For M2400S, D2=4" << endl;
    
    // Generate expected D2 pattern
    cout << "Expected positions: ";
    for (int i = 448; i < 480; i++) {
        uint8_t base = msdmt::psymbol[4][i % 8];  // D2=4
        uint8_t scr = msdmt::pscramble[i % 32];
        uint8_t expected = (base + scr) % 8;
        cout << (int)expected;
    }
    cout << endl;
    
    // Compare
    cout << "\nActual positions: ";
    for (int i = 448; i < 480 && i < (int)result.preamble_symbols.size(); i++) {
        cout << decode_8psk_position(result.preamble_symbols[i]);
    }
    cout << endl;
    
    return 0;
}
