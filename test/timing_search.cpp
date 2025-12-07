/**
 * Search for optimal timing offset
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

class RefDeinterleaver {
public:
    RefDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        load_row_ = load_col_ = load_col_last_ = fetch_row_ = fetch_col_ = 0;
    }
    void reset() {
        fill(array_.begin(), array_.end(), 0.0f);
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

int try_decode_offset(const vector<complex<float>>& symbols, float phase_offset) {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    complex<float> rot = polar(1.0f, phase_offset);
    
    int idx = 0, data_count = 0;
    while (data_count < BLOCK_BITS / 3 && idx < (int)symbols.size()) {
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && idx < (int)symbols.size(); i++) {
            complex<float> sym = symbols[idx++] * rot;
            int pos = decode_8psk_position(sym);
            int scr_val = scr.next();
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        for (int i = 0; i < 16 && idx < (int)symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < 54; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) if (decoded[i + j]) byte |= (1 << j);
        if (byte == (uint8_t)TEST_MSG[i/8]) matches++;
    }
    
    return matches;
}

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "=== Phase Offset Search ===" << endl;
    
    float best_phase = 0;
    int best_matches = 0;
    
    // Try phase offsets from -45 to +45 degrees in 1 degree steps
    for (float deg = -45; deg <= 45; deg += 1) {
        float rad = deg * M_PI / 180.0f;
        int matches = try_decode_offset(result.data_symbols, rad);
        
        if (matches > best_matches) {
            best_matches = matches;
            best_phase = deg;
            cout << "Phase " << deg << "°: " << matches << "/54 matches (NEW BEST)" << endl;
        }
    }
    
    cout << "\nBest phase offset: " << best_phase << "° with " << best_matches << "/54 matches" << endl;
    
    // Also try phase offsets of multiples of 45 degrees (8-PSK ambiguity)
    cout << "\n=== 8-PSK Phase Ambiguity Check ===" << endl;
    for (int i = 0; i < 8; i++) {
        float deg = i * 45.0f;
        float rad = deg * M_PI / 180.0f;
        int matches = try_decode_offset(result.data_symbols, rad);
        cout << "Phase " << deg << "°: " << matches << "/54 matches" << endl;
    }
    
    return 0;
}
