/**
 * Decode with proper soft decisions based on symbol quality
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <cstdint>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"

using namespace std;
using namespace m110a;

const int mgd3[8] = {0,1,3,2,7,6,4,5};
int inv_mgd3[8];

// Soft decode: return soft bits for each tribit position
void soft_decode_8psk(complex<float> sym, int scr_val, float soft[3]) {
    // First, rotate by scrambler
    float angles[8];
    for (int i = 0; i < 8; i++) {
        angles[i] = (i - scr_val + 8) % 8 * M_PI / 4.0f;
    }
    
    // Calculate angle of received symbol
    float rx_angle = atan2(sym.imag(), sym.real());
    float rx_mag = abs(sym);
    
    // Find nearest constellation point
    float min_dist = 1e10f;
    int best_gray = 0;
    for (int g = 0; g < 8; g++) {
        float expected_angle = g * M_PI / 4.0f;  // Unscrambled position
        float scrambled_angle = ((g + scr_val) % 8) * M_PI / 4.0f;
        
        // Distance to this point
        float angle_diff = rx_angle - scrambled_angle;
        while (angle_diff > M_PI) angle_diff -= 2*M_PI;
        while (angle_diff < -M_PI) angle_diff += 2*M_PI;
        
        float dist = abs(angle_diff);
        if (dist < min_dist) {
            min_dist = dist;
            best_gray = g;
        }
    }
    
    // Convert gray to tribit
    int tribit = inv_mgd3[best_gray];
    
    // Calculate soft decision based on confidence
    // Scale by magnitude and inverse of angular error
    float confidence = rx_mag * (1.0f - min_dist / (M_PI / 4.0f));
    confidence = max(0.1f, min(1.0f, confidence * 2.0f));
    
    // Hard decision with confidence scaling
    soft[0] = (tribit & 4) ? -127.0f * confidence : 127.0f * confidence;
    soft[1] = (tribit & 2) ? -127.0f * confidence : 127.0f * confidence;
    soft[2] = (tribit & 1) ? -127.0f * confidence : 127.0f * confidence;
}

class MyScrambler {
public:
    MyScrambler() { reset(); }
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

class MyDeinterleaver {
public:
    MyDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        load_row_ = load_col_ = load_col_last_ = fetch_row_ = fetch_col_ = 0;
    }
    void load(float bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + 1) % rows_;
        load_col_ = (load_col_ + col_inc_) % cols_;
        if (load_row_ == 0) { load_col_ = (load_col_last_ + 1) % cols_; load_col_last_ = load_col_; }
    }
    float fetch() {
        float bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        if (fetch_row_ == 0) fetch_col_ = (fetch_col_ + 1) % cols_;
        return bit;
    }
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<float> array_;
    int load_row_, load_col_, load_col_last_, fetch_row_, fetch_col_;
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
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Soft decision decode
    MyScrambler scr;
    MyDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0, data_count = 0;
    while (data_count < BLOCK_BITS / 3 && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && idx < (int)result.data_symbols.size(); i++) {
            int scr_val = scr.next();
            float soft[3];
            soft_decode_8psk(result.data_symbols[idx++], scr_val, soft);
            deint.load(soft[0]);
            deint.load(soft[1]);
            deint.load(soft[2]);
            data_count++;
        }
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    vector<int8_t> soft_bits;
    for (int i = 0; i < BLOCK_BITS; i++) {
        float val = deint.fetch();
        soft_bits.push_back((int8_t)max(-127.0f, min(127.0f, val)));
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft_bits, decoded, true);
    
    // Convert to bytes - LSB first
    cout << "Decoded (soft decision, LSB-first):" << endl;
    string output;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) if (decoded[i + j]) byte |= (1 << j);
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    cout << output.substr(0, 80) << endl;
    
    // Compare with expected
    const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < 54; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) if (decoded[i + j]) byte |= (1 << j);
        if (byte == (uint8_t)EXPECTED[i/8]) matches++;
    }
    cout << "\nMatches: " << matches << "/54" << endl;
    
    return 0;
}
