/**
 * Verify exactly what's at position 1440
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
    
    // Show exactly what's around position 1440
    cout << "=== Symbols around position 1440 ===" << endl;
    for (int i = 1420; i < 1480 && i < (int)result.data_symbols.size(); i++) {
        if ((i - 1420) % 20 == 0) {
            cout << "\nPos " << i << "-" << (i+19) << ": ";
        }
        cout << decode_8psk_position(result.data_symbols[i]);
    }
    cout << endl;
    
    // Generate scrambler outputs
    RefScrambler scr;
    cout << "\n=== Scrambler outputs ===" << endl;
    cout << "Scr 0-19:  ";
    for (int i = 0; i < 20; i++) cout << (int)scr.next_tribit();
    cout << endl;
    cout << "Scr 20-39: ";
    for (int i = 0; i < 20; i++) cout << (int)scr.next_tribit();
    cout << endl;
    
    // At position 1440, we have 02433645767055435437 (from earlier search)
    // Scrambler 0-19 is 02433645767055435437
    // These match exactly!
    
    // So position 1440 = first probe of a frame (symbols 20-39 of frame)
    // Therefore:
    // - Frame starts at 1420
    // - Data symbols at 1420-1439
    // - Probe symbols at 1440-1459
    
    // Let's check if 1420-1439 contains data that, when descrambled, makes sense
    cout << "\n=== Checking 1420-1439 as data ===" << endl;
    cout << "Raw:        ";
    for (int i = 1420; i < 1440; i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
    }
    cout << endl;
    
    // Descramble (scrambler starts at 0 for this frame)
    scr = RefScrambler();
    cout << "Descrambled: ";
    for (int i = 0; i < 20; i++) {
        complex<float> sym = result.data_symbols[1420 + i];
        uint8_t scr_val = scr.next_tribit();
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        cout << decode_8psk_position(sym);
    }
    cout << endl;
    
    // Verify probe descrambles to 0
    cout << "\nProbe descrambled: ";
    for (int i = 0; i < 20; i++) {
        complex<float> sym = result.data_symbols[1440 + i];
        uint8_t scr_val = scr.next_tribit();  // Continue from data
        float scr_phase = -scr_val * (M_PI / 4.0f);
        sym *= polar(1.0f, scr_phase);
        cout << decode_8psk_position(sym);
    }
    cout << " (should be all 0)" << endl;
    
    return 0;
}
