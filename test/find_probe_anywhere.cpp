/**
 * Find where probe pattern appears in data_symbols
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
    
    // Convert data_symbols to positions
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Generate probe pattern 
    RefScrambler scr;
    vector<int> probe;
    for (int i = 0; i < 40; i++) {
        probe.push_back(scr.next_tribit());
    }
    
    cout << "Probe (40 symbols): ";
    for (int p : probe) cout << p;
    cout << endl;
    
    // Search for any 20-symbol subsequence match
    cout << "\n--- Searching for probe subsequence ---" << endl;
    
    for (size_t start = 0; start + 20 <= received.size(); start++) {
        // Try matching probe[0:20] at position start
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            if (received[start + i] == probe[i]) matches++;
        }
        
        if (matches >= 15) {
            cout << "Position " << start << ": " << matches << "/20 matches  ";
            cout << "Received: ";
            for (int i = 0; i < 20; i++) cout << received[start + i];
            cout << endl;
        }
    }
    
    // Also try matching probe[20:40]
    cout << "\n--- Searching for second probe block ---" << endl;
    for (size_t start = 0; start + 20 <= received.size(); start++) {
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            if (received[start + i] == probe[20 + i]) matches++;
        }
        
        if (matches >= 15) {
            cout << "Position " << start << ": " << matches << "/20 matches  ";
            cout << "Received: ";
            for (int i = 0; i < 20; i++) cout << received[start + i];
            cout << endl;
        }
    }
    
    return 0;
}
