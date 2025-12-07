/**
 * Check the DATA scrambler from reference code
 * This is DIFFERENT from the preamble scrambler!
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"

using namespace m110a;
using namespace std;

// Reference data scrambler (from t110a.cpp)
class DataScrambler {
public:
    DataScrambler() {
        // Initial state from reference: 1011 0101 1101
        sreg[0]  = 1;
        sreg[1]  = 0;
        sreg[2]  = 1;
        sreg[3]  = 1;
        sreg[4]  = 0;
        sreg[5]  = 1;
        sreg[6]  = 0;
        sreg[7]  = 1;
        sreg[8]  = 1;
        sreg[9]  = 1;
        sreg[10] = 0;
        sreg[11] = 1;
    }
    
    uint8_t next_tribit() {
        // Clock 8 times (once per bit output)
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10];
            sreg[10] = sreg[9];
            sreg[9]  = sreg[8];
            sreg[8]  = sreg[7];
            sreg[7]  = sreg[6];
            sreg[6]  = sreg[5] ^ carry;
            sreg[5]  = sreg[4];
            sreg[4]  = sreg[3] ^ carry;
            sreg[3]  = sreg[2];
            sreg[2]  = sreg[1];
            sreg[1]  = sreg[0] ^ carry;
            sreg[0]  = carry;
        }
        return (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
    }
    
private:
    int sreg[12];
};

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
    
    cout << "=== DATA scrambler vs received symbols ===" << endl;
    
    // Generate data scrambler output (160 symbols)
    DataScrambler dscr;
    cout << "\nData scrambler first 80:" << endl;
    for (int i = 0; i < 80; i++) {
        cout << (int)dscr.next_tribit();
        if ((i+1) % 40 == 0) cout << endl;
    }
    
    // Reset and compare with received
    cout << "\n\nReceived at position 0:" << endl;
    for (int i = 0; i < 80 && i < (int)result.data_symbols.size(); i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
        if ((i+1) % 40 == 0) cout << endl;
    }
    
    // Check if data scrambler matches anywhere
    cout << "\n\n=== Searching for data scrambler pattern ===" << endl;
    
    dscr = DataScrambler();
    vector<int> ds_pattern;
    for (int i = 0; i < 80; i++) {
        ds_pattern.push_back(dscr.next_tribit());
    }
    
    cout << "Data scrambler first 40: ";
    for (int i = 0; i < 40; i++) cout << ds_pattern[i];
    cout << endl;
    
    // Search for this pattern
    for (size_t pos = 0; pos + 40 <= result.data_symbols.size(); pos++) {
        int matches = 0;
        for (int i = 0; i < 40; i++) {
            if (decode_8psk_position(result.data_symbols[pos + i]) == ds_pattern[i]) {
                matches++;
            }
        }
        if (matches >= 30) {
            cout << "Position " << pos << ": " << matches << "/40 matches" << endl;
        }
    }
    
    return 0;
}
