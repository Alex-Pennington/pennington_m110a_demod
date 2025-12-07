/**
 * Find where expected pattern appears in received data
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

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

vector<int> generate_expected() {
    vector<uint8_t> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) msg_bits.push_back((c >> i) & 1);
    }
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    while (encoded.size() < 1440) encoded.push_back(0);
    
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < 40; row++) {
        for (int col = 0; col < 36; col++) {
            int in_idx = row * 36 + col;
            int out_idx = col * 40 + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    
    RefScrambler scr;
    vector<int> scrambled;
    for (int pos : positions) {
        int scr_val = scr.next_tribit();
        scrambled.push_back((pos + scr_val) % 8);
    }
    
    return scrambled;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    auto expected = generate_expected();
    
    cout << "Expected first 40: ";
    for (int i = 0; i < 40; i++) cout << expected[i];
    cout << endl;
    
    cout << "Received first 40: ";
    for (int i = 0; i < 40; i++) cout << received[i];
    cout << endl;
    
    // Search for first 20 expected symbols
    cout << "\n=== Searching for first 20 symbols ===" << endl;
    for (size_t pos = 0; pos + 20 <= received.size(); pos++) {
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            if (received[pos + i] == expected[i]) matches++;
        }
        if (matches >= 15) {
            cout << "Position " << pos << ": " << matches << "/20" << endl;
        }
    }
    
    // Search with 32+16 gap structure
    cout << "\n=== Searching with 32+16 frame structure ===" << endl;
    for (size_t start = 0; start < 200; start++) {
        int matches = 0;
        size_t exp_idx = 0;
        size_t rcv_idx = start;
        
        while (exp_idx < 64 && rcv_idx < received.size()) {
            for (int i = 0; i < 32 && exp_idx < 64 && rcv_idx < received.size(); i++) {
                if (received[rcv_idx++] == expected[exp_idx++]) matches++;
            }
            rcv_idx += 16;
        }
        
        if (matches >= 40) {
            cout << "Start " << start << ": " << matches << "/64" << endl;
        }
    }
    
    // Search contiguous
    cout << "\n=== Searching contiguous ===" << endl;
    for (size_t pos = 0; pos + 80 <= received.size(); pos++) {
        int matches = 0;
        for (int i = 0; i < 80; i++) {
            if (received[pos + i] == expected[i]) matches++;
        }
        if (matches >= 45) {
            cout << "Position " << pos << ": " << matches << "/80" << endl;
        }
    }
    
    return 0;
}
