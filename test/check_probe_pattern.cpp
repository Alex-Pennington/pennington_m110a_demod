/**
 * Check if probes use the same scrambler as data
 * MS-DMT may use a known probe pattern that's different
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
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // The probe symbols should follow a known pattern
    // In MIL-STD-188-110A, probe symbols are typically channel symbols (known values)
    // They may use the same D0 pattern as in preamble
    
    cout << "\n--- Received symbols at probe positions ---" << endl;
    cout << "Symbols 20-39 (first probe block):" << endl;
    for (int i = 20; i < 40 && i < (int)result.data_symbols.size(); i++) {
        auto sym = result.data_symbols[i];
        int pos = decode_8psk_position(sym);
        float phase = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        if (phase < 0) phase += 360;
        printf("[%2d] phase=%6.1f pos=%d\n", i, phase, pos);
    }
    
    // Check if probe matches D0 pattern (psymbol[0])
    cout << "\n--- Expected D0 pattern (like preamble probes) ---" << endl;
    for (int i = 0; i < 20; i++) {
        int idx = 20 + i;  // Probe position in frame
        uint8_t base = msdmt::psymbol[0][idx % 8];  // D0
        uint8_t scr = msdmt::pscramble[idx % 32];
        int expected = (base + scr) % 8;
        cout << expected;
    }
    cout << endl;
    
    cout << "Received: ";
    for (int i = 20; i < 40 && i < (int)result.data_symbols.size(); i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
    }
    cout << endl;
    
    // Try matching with different D values
    cout << "\n--- Try matching with D0-D7 patterns ---" << endl;
    for (int d = 0; d < 8; d++) {
        int matches = 0;
        for (int i = 0; i < 20 && (20 + i) < (int)result.data_symbols.size(); i++) {
            int idx = 20 + i;
            uint8_t base = msdmt::psymbol[d][idx % 8];
            uint8_t scr = msdmt::pscramble[idx % 32];
            int expected = (base + scr) % 8;
            int received = decode_8psk_position(result.data_symbols[20 + i]);
            if (received == expected) matches++;
        }
        cout << "D" << d << ": " << matches << "/20 matches" << endl;
    }
    
    return 0;
}
