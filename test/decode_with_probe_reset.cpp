/**
 * Try decoding with scrambler reset every frame
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
const int EXPECTED_LEN = 54;

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
    
    // Position 1420 = start of data, 1440 = start of probe
    // Scrambler resets every 40 symbols
    
    int data_start = 1420;
    
    cout << "\n=== Decode with scrambler reset every frame ===" << endl;
    
    vector<int> positions;
    int idx = data_start;
    
    while (idx + 40 <= (int)result.data_symbols.size() && positions.size() < 480) {
        RefScrambler scr;  // Reset every frame
        
        // 20 data symbols
        for (int i = 0; i < 20 && positions.size() < 480; i++) {
            complex<float> sym = result.data_symbols[idx + i];
            uint8_t scr_val = scr.next_tribit();
            
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            
            positions.push_back(decode_8psk_position(sym));
        }
        
        // Skip 20 probe symbols
        idx += 40;
    }
    
    cout << "Extracted " << positions.size() << " data symbols" << endl;
    
    // Gray decode
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave
    int rows = 40, cols = 36, block_size = 1440;
    vector<int> deinterleaved;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = col * rows + row;
            if (in_idx < (int)bits.size()) {
                deinterleaved.push_back(bits[in_idx]);
            }
        }
    }
    
    // Viterbi
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        bytes.push_back(byte);
    }
    
    // Show result
    cout << "\nDecoded " << bytes.size() << " bytes:" << endl;
    cout << "ASCII: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)EXPECTED_LEN); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    cout << "Match: " << matches << "/" << EXPECTED_LEN << endl;
    
    return 0;
}
