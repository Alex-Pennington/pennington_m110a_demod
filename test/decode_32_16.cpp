/**
 * Decode with correct frame structure for M2400S:
 * 32 unknown (data) + 16 known (probe) = 48 symbols per mini-frame
 * Scrambler period = 160 symbols
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

// Data scrambler (same polynomial, different clock rate from preamble)
class DataScrambler {
public:
    DataScrambler() { reset(); }
    
    void reset() {
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
        count = 0;
    }
    
    uint8_t next_tribit() {
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
        count = (count + 1) % 160;
        return (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
    }
    
    int get_count() const { return count; }
    
private:
    int sreg[12];
    int count;
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

int try_decode(const vector<complex<float>>& data_symbols, int start, int scr_offset) {
    const int UNKNOWN_LEN = 32;
    const int KNOWN_LEN = 16;
    const int FRAME_LEN = UNKNOWN_LEN + KNOWN_LEN;
    const int BLOCK_SIZE = 1440;  // Bits per interleave block
    const int SYMBOLS_NEEDED = BLOCK_SIZE / 3;  // 480 symbols
    
    if (start + SYMBOLS_NEEDED * FRAME_LEN / UNKNOWN_LEN > (int)data_symbols.size()) {
        return 0;
    }
    
    vector<int> positions;
    DataScrambler scr;
    
    // Advance scrambler to offset
    for (int i = 0; i < scr_offset; i++) scr.next_tribit();
    
    int idx = start;
    
    // Extract frames: 32 data + 16 probe each
    while (positions.size() < SYMBOLS_NEEDED && idx + FRAME_LEN <= (int)data_symbols.size()) {
        // 32 unknown (data) symbols
        for (int i = 0; i < UNKNOWN_LEN && positions.size() < SYMBOLS_NEEDED; i++) {
            complex<float> sym = data_symbols[idx + i];
            uint8_t scr_val = scr.next_tribit();
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            positions.push_back(decode_8psk_position(sym));
        }
        
        // 16 known (probe) symbols - just advance scrambler
        for (int i = 0; i < KNOWN_LEN; i++) {
            scr.next_tribit();
        }
        
        idx += FRAME_LEN;
    }
    
    if (positions.size() < SYMBOLS_NEEDED) return 0;
    
    // Gray decode
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave 40x36
    int rows = 40, cols = 36;
    vector<int> deinterleaved(BLOCK_SIZE);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = col * rows + row;
            int out_idx = row * cols + col;
            if (in_idx < (int)bits.size()) {
                deinterleaved[out_idx] = bits[in_idx];
            }
        }
    }
    
    // Viterbi decode
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < EXPECTED_LEN; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        if (byte == (uint8_t)EXPECTED[i/8]) matches++;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    cout << "\nTrying 32+16 frame structure..." << endl;
    
    int best_matches = 0;
    int best_start = 0;
    int best_scr = 0;
    
    // Try different start positions and scrambler offsets
    for (int start = 0; start < 100; start++) {
        for (int scr_offset = 0; scr_offset < 160; scr_offset += 16) {
            int matches = try_decode(result.data_symbols, start, scr_offset);
            if (matches > best_matches) {
                best_matches = matches;
                best_start = start;
                best_scr = scr_offset;
            }
        }
    }
    
    cout << "\nBest: start=" << best_start << " scr_offset=" << best_scr 
         << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    
    // Fine tune around best
    if (best_matches > 2) {
        for (int start = max(0, best_start - 5); start <= best_start + 5; start++) {
            for (int scr_offset = max(0, best_scr - 20); scr_offset <= min(159, best_scr + 20); scr_offset++) {
                int matches = try_decode(result.data_symbols, start, scr_offset);
                if (matches > best_matches) {
                    best_matches = matches;
                    best_start = start;
                    best_scr = scr_offset;
                }
            }
        }
        cout << "After fine-tuning: start=" << best_start << " scr_offset=" << best_scr 
             << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    }
    
    return 0;
}
