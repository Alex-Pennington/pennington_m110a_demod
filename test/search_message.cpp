/**
 * Search for expected message pattern in received data
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

// Build expected scrambled data pattern
vector<int> build_expected_scrambled() {
    // Message to bits
    vector<uint8_t> msg_bits;
    for (const char* p = EXPECTED; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            msg_bits.push_back((c >> i) & 1);
        }
    }
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    
    // Pad
    while (encoded.size() < 1440) encoded.push_back(0);
    
    // Interleave
    int rows = 40, cols = 36;
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = row * cols + col;
            int out_idx = col * rows + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    // To tribits and positions
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    
    // Scramble (continuously)
    RefScrambler scr;
    for (auto& pos : positions) {
        pos = (pos + scr.next_tribit()) % 8;
    }
    
    return positions;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Build expected pattern
    auto expected = build_expected_scrambled();
    cout << "Expected scrambled data: " << expected.size() << " symbols" << endl;
    
    cout << "First 60: ";
    for (int i = 0; i < 60; i++) {
        cout << expected[i];
        if ((i+1) % 20 == 0) cout << " ";
    }
    cout << endl;
    
    // Convert received to positions
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Search for first 20 expected symbols in received
    cout << "\n=== Searching for expected pattern ===" << endl;
    
    string exp_first_20;
    for (int i = 0; i < 20; i++) exp_first_20 += '0' + expected[i];
    
    for (size_t pos = 0; pos + 20 <= received.size(); pos++) {
        int matches = 0;
        for (int i = 0; i < 20; i++) {
            if (received[pos + i] == expected[i]) matches++;
        }
        
        if (matches >= 15) {
            cout << "Position " << pos << ": " << matches << "/20 matches" << endl;
        }
    }
    
    // Maybe the data starts at a different scrambler position?
    // Try searching with scrambler offset
    cout << "\n=== Trying different scrambler offsets ===" << endl;
    
    for (int scr_offset = 0; scr_offset <= 480; scr_offset += 20) {
        RefScrambler scr;
        for (int i = 0; i < scr_offset; i++) scr.next_tribit();
        
        // Rebuild expected with this scrambler offset
        vector<int> exp_offset;
        for (int i = 0; i < 480 && i < (int)expected.size(); i++) {
            // Original unscrambled position
            const int pos_to_tribit[8] = {0, 1, 3, 2, 6, 7, 5, 4};
            const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
            
            // This is getting complex - let me try a simpler approach
            exp_offset.push_back((expected[i] - i * 0 + scr.next_tribit()) % 8);  // Wrong but placeholder
        }
        
        // Actually let me just check received vs scrambler directly
        int matches = 0;
        scr = RefScrambler();
        for (int i = 0; i < scr_offset; i++) scr.next_tribit();
        
        for (int i = 0; i < 40 && i < (int)received.size(); i++) {
            uint8_t scr_val = scr.next_tribit();
            // If received is pure scrambler, this would match
            if (received[i] == scr_val) matches++;
        }
        
        if (matches >= 30) {
            cout << "Scrambler offset " << scr_offset << ": " << matches << "/40 matches" << endl;
        }
    }
    
    return 0;
}
