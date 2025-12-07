/**
 * Generate expected transmission and correlate with received
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

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

// Data scrambler 
class DataScrambler {
public:
    DataScrambler() { reset(); }
    void reset() {
        sreg[0]=1; sreg[1]=0; sreg[2]=1; sreg[3]=1;
        sreg[4]=0; sreg[5]=1; sreg[6]=0; sreg[7]=1;
        sreg[8]=1; sreg[9]=1; sreg[10]=0; sreg[11]=1;
    }
    uint8_t next_tribit() {
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

// Generate expected scrambled transmission
vector<int> generate_expected() {
    // Message to bits
    vector<uint8_t> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) msg_bits.push_back((c >> i) & 1);
    }
    
    // FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    
    // Pad to 1440 bits
    while (encoded.size() < 1440) encoded.push_back(0);
    
    // Interleave 40×36
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < 40; row++) {
        for (int col = 0; col < 36; col++) {
            int in_idx = row * 36 + col;
            int out_idx = col * 40 + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    // To tribits and Gray encode to positions
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    
    // Scramble
    DataScrambler scr;
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
    
    // Convert received to positions
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Generate expected
    auto expected = generate_expected();
    
    cout << "Expected scrambled (first 80): ";
    for (int i = 0; i < 80 && i < (int)expected.size(); i++) {
        cout << expected[i];
        if ((i+1) % 40 == 0) cout << " ";
    }
    cout << endl;
    
    cout << "\nReceived (first 80):          ";
    for (int i = 0; i < 80 && i < (int)received.size(); i++) {
        cout << received[i];
        if ((i+1) % 40 == 0) cout << " ";
    }
    cout << endl;
    
    // Search for correlation
    cout << "\n=== Searching for expected pattern in received ===" << endl;
    
    int search_len = min(40, (int)expected.size());
    
    for (size_t pos = 0; pos + search_len <= received.size(); pos++) {
        int matches = 0;
        for (int i = 0; i < search_len; i++) {
            if (received[pos + i] == expected[i]) matches++;
        }
        if (matches >= 30) {
            cout << "Position " << pos << ": " << matches << "/" << search_len << " matches" << endl;
        }
    }
    
    // Also search with phase rotation
    cout << "\n=== With phase rotation ===" << endl;
    for (int phase = 0; phase < 8; phase++) {
        int best_matches = 0;
        int best_pos = 0;
        
        for (size_t pos = 0; pos + search_len <= received.size(); pos++) {
            int matches = 0;
            for (int i = 0; i < search_len; i++) {
                int rotated = (received[pos + i] + phase) % 8;
                if (rotated == expected[i]) matches++;
            }
            if (matches > best_matches) {
                best_matches = matches;
                best_pos = pos;
            }
        }
        if (best_matches >= 25) {
            cout << "Phase " << phase << " (+" << (phase * 45) << "°): pos " 
                 << best_pos << " = " << best_matches << "/" << search_len << endl;
        }
    }
    
    return 0;
}
