/**
 * Check preamble decoding
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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Phase offset: " << (result.phase_offset * 180 / M_PI) << " degrees" << endl;
    cout << "Start sample: " << result.start_sample << endl;
    cout << "Preamble accuracy: " << result.accuracy << "%" << endl;
    
    cout << "\nPreamble symbols: " << result.preamble_symbols.size() << endl;
    
    // Check preamble symbol positions
    cout << "\nFirst 50 preamble symbol positions:" << endl;
    for (int i = 0; i < 50 && i < (int)result.preamble_symbols.size(); i++) {
        cout << decode_8psk_position(result.preamble_symbols[i]);
    }
    cout << endl;
    
    // Show expected preamble (common pattern)
    cout << "\nExpected common pattern first 50:" << endl;
    for (int i = 0; i < 50 && i < 288; i++) {
        cout << msdmt::common_pattern[i];
    }
    cout << endl;
    
    // Count matches
    int matches = 0;
    for (int i = 0; i < 288 && i < (int)result.preamble_symbols.size(); i++) {
        int rcv = decode_8psk_position(result.preamble_symbols[i]);
        if (rcv == msdmt::common_pattern[i]) matches++;
    }
    cout << "\nPreamble common pattern matches: " << matches << "/288" << endl;
    
    return 0;
}
