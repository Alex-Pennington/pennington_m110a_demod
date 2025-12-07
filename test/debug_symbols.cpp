/**
 * Debug: Look at raw complex symbols
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

const int mgd3[8] = {0,1,3,2,7,6,4,5};

// con_symbol from reference
const complex<float> con_symbol[8] = {
    {1.000f, 0.000f},
    {0.707f, 0.707f},
    {0.000f, 1.000f},
    {-0.707f, 0.707f},
    {-1.000f, 0.000f},
    {-0.707f, -0.707f},
    {0.000f, -1.000f},
    {0.707f, -0.707f}
};

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

class RefInterleaver {
public:
    RefInterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0);
        load_row_ = load_col_ = fetch_row_ = fetch_col_ = fetch_col_last_ = 0;
    }
    void load(int bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + row_inc_) % rows_;
        if (load_row_ == 0) load_col_ = (load_col_ + 1) % cols_;
    }
    int fetch() {
        int bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + 1) % rows_;
        fetch_col_ = (fetch_col_ + col_inc_) % cols_;
        if (fetch_row_ == 0) { fetch_col_ = (fetch_col_last_ + 1) % cols_; fetch_col_last_ = fetch_col_; }
        return bit;
    }
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<int> array_;
    int load_row_, load_col_, fetch_row_, fetch_col_, fetch_col_last_;
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

int decode_position_correlation(complex<float> sym) {
    // Find closest constellation point by correlation
    float max_corr = -1000;
    int best = 0;
    for (int i = 0; i < 8; i++) {
        float corr = sym.real() * con_symbol[i].real() + sym.imag() * con_symbol[i].imag();
        if (corr > max_corr) {
            max_corr = corr;
            best = i;
        }
    }
    return best;
}

int main() {
    const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    // Generate expected first 32 data symbols
    vector<int> msg_bits;
    for (const char* p = TEST_MSG; *p; p++)
        for (int i = 7; i >= 0; i--) msg_bits.push_back((*p >> i) & 1);
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    RefDataScrambler sim_scr;
    vector<complex<float>> expected_symbols;
    for (int i = 0; i < 32; i++) {
        int tribit = (interleaver.fetch() << 2) | (interleaver.fetch() << 1) | interleaver.fetch();
        int gray = mgd3[tribit];
        int scr_val = sim_scr.next();
        int position = (gray + scr_val) % 8;
        expected_symbols.push_back(con_symbol[position]);
    }
    
    // Load received
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "=== First 10 data symbols comparison ===" << endl;
    cout << "Expected (from simulation) vs Received" << endl;
    
    for (int i = 0; i < 10; i++) {
        complex<float> exp = expected_symbols[i];
        complex<float> rcv = result.data_symbols[i];
        
        int exp_pos = decode_position_correlation(exp);
        int rcv_pos = decode_position_correlation(rcv);
        
        cout << "  " << i << ": exp(" << exp.real() << ", " << exp.imag() << ") pos=" << exp_pos
             << " | rcv(" << rcv.real() << ", " << rcv.imag() << ") pos=" << rcv_pos;
        if (exp_pos == rcv_pos) cout << " MATCH";
        cout << endl;
    }
    
    // Now descramble both and compare
    RefDataScrambler scr1, scr2;
    
    cout << "\n=== Descrambled positions ===" << endl;
    for (int i = 0; i < 10; i++) {
        int scr_val1 = scr1.next();
        int scr_val2 = scr2.next();
        
        int exp_pos = decode_position_correlation(expected_symbols[i]);
        int rcv_pos = decode_position_correlation(result.data_symbols[i]);
        
        int exp_gray = (exp_pos - scr_val1 + 8) % 8;
        int rcv_gray = (rcv_pos - scr_val2 + 8) % 8;
        
        cout << "  " << i << ": scr=" << scr_val1 << " exp_gray=" << exp_gray << " rcv_gray=" << rcv_gray;
        if (exp_gray == rcv_gray) cout << " MATCH";
        cout << endl;
    }
    
    return 0;
}
