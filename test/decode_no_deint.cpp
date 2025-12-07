/**
 * Try decoding without deinterleaver - just raw soft bits
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

const int mgd3[8] = {0,1,3,2,7,6,4,5};
int inv_mgd3[8];

class RefDataScrambler {
public:
    RefDataScrambler() { reset(); }
    void reset() {
        sreg[0]=1; sreg[1]=0; sreg[2]=1; sreg[3]=1;
        sreg[4]=0; sreg[5]=1; sreg[6]=0; sreg[7]=1;
        sreg[8]=1; sreg[9]=1; sreg[10]=0; sreg[11]=1;
    }
    int next() {
        for (int j = 0; j < 8; j++) {
            int c = sreg[11];
            for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
            sreg[0] = c;
            sreg[6] ^= c; sreg[4] ^= c; sreg[1] ^= c;
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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Extract raw soft bits (no deinterleaving)
    RefDataScrambler scr;
    vector<int8_t> soft;
    
    int idx = 0;
    while (soft.size() < 2880 && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < 32 && soft.size() < 2880 && idx < (int)result.data_symbols.size(); i++) {
            int pos = decode_8psk_position(result.data_symbols[idx++]);
            int scr_val = scr.next();
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            soft.push_back((tribit & 4) ? -127 : 127);
            soft.push_back((tribit & 2) ? -127 : 127);
            soft.push_back((tribit & 1) ? -127 : 127);
        }
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    cout << "Raw soft bits: " << soft.size() << endl;
    
    // Try Viterbi decode without deinterleaving
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    cout << "Decoded bits: " << decoded.size() << endl;
    
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "Without deinterleaver:" << endl;
    cout << "Output: " << output.substr(0, 80) << endl;
    cout << "Matches: " << matches << "/" << EXPECTED_LEN << endl;
    
    // Show first 100 raw soft bits
    cout << "\nFirst 100 raw soft bits (+ = 0, - = 1): ";
    for (int i = 0; i < 100; i++) {
        cout << (soft[i] > 0 ? "0" : "1");
    }
    cout << endl;
    
    return 0;
}
