/**
 * Compare expected vs received raw symbols
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

vector<int> compute_expected() {
    // Build expected transmitted symbols
    vector<uint8_t> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            msg_bits.push_back((c >> i) & 1);
        }
    }
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    
    while (encoded.size() < 1440) encoded.push_back(0);
    
    int rows = 40, cols = 36;
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = row * cols + col;
            int out_idx = col * rows + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    vector<int> data_positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        data_positions.push_back(tribit_to_pos[tribit]);
    }
    
    // Scramble data
    RefScrambler scr;
    for (auto& pos : data_positions) {
        pos = (pos + scr.next_tribit()) % 8;
    }
    
    // Build full frame sequence with probes
    vector<int> full_sequence;
    scr = RefScrambler();
    int data_idx = 0;
    
    while (data_idx < (int)data_positions.size()) {
        // 20 data symbols
        for (int i = 0; i < 20 && data_idx < (int)data_positions.size(); i++) {
            scr.next_tribit();  // Advance scrambler
            full_sequence.push_back(data_positions[data_idx++]);
        }
        
        // 20 probe symbols (scrambled 0)
        for (int i = 0; i < 20; i++) {
            full_sequence.push_back(scr.next_tribit());
        }
    }
    
    return full_sequence;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Get expected sequence
    auto expected = compute_expected();
    cout << "Expected symbols: " << expected.size() << endl;
    
    cout << "\n--- Symbol comparison (first 80) ---" << endl;
    cout << "Expected: ";
    for (int i = 0; i < 80 && i < (int)expected.size(); i++) {
        cout << expected[i];
        if ((i+1) % 20 == 0) cout << " ";
    }
    cout << endl;
    
    cout << "Received: ";
    for (int i = 0; i < 80 && i < (int)result.data_symbols.size(); i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
        if ((i+1) % 20 == 0) cout << " ";
    }
    cout << endl;
    
    // Try different phase rotations
    cout << "\n--- With phase rotations ---" << endl;
    for (int rot = 0; rot < 8; rot++) {
        complex<float> phase_rot = polar(1.0f, (float)(rot * M_PI / 4.0f));
        
        int matches = 0;
        for (int i = 0; i < 40 && i < min((int)expected.size(), (int)result.data_symbols.size()); i++) {
            int rcv = decode_8psk_position(result.data_symbols[i] * phase_rot);
            if (rcv == expected[i]) matches++;
        }
        
        cout << "Rot " << (rot*45) << "°: " << matches << "/40 matches in first frame" << endl;
    }
    
    return 0;
}
