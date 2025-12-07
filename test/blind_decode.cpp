/**
 * Blind decode - just decode whatever is in the file
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

const int inv_mgd3[8] = {0, 1, 3, 2, 6, 7, 5, 4};

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

const complex<float> con_symbol[8] = {
    {1.000f, 0.000f}, {0.707f, 0.707f}, {0.000f, 1.000f}, {-0.707f, 0.707f},
    {-1.000f, 0.000f}, {-0.707f, -0.707f}, {0.000f, -1.000f}, {0.707f, -0.707f}
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

int decode_position(complex<float> sym) {
    float max_corr = -1000;
    int best = 0;
    for (int i = 0; i < 8; i++) {
        float corr = sym.real() * con_symbol[i].real() + sym.imag() * con_symbol[i].imag();
        if (corr > max_corr) { max_corr = corr; best = i; }
    }
    return best;
}

int main() {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Try different soft bit polarities
    for (int polarity = 0; polarity < 2; polarity++) {
        float soft_one = (polarity == 0) ? -1.0f : 1.0f;
        float soft_zero = (polarity == 0) ? 1.0f : -1.0f;
        
        RefDataScrambler scr;
        RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
        
        int idx = 0, data_count = 0;
        while (data_count < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size()) {
            for (int i = 0; i < 32 && data_count < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size(); i++) {
                int position = decode_position(result.data_symbols[idx++]);
                int scr_val = scr.next();
                int gray = (position - scr_val + 8) % 8;
                int tribit = inv_mgd3[gray];
                
                deint.load((tribit & 4) ? soft_one : soft_zero);
                deint.load((tribit & 2) ? soft_one : soft_zero);
                deint.load((tribit & 1) ? soft_one : soft_zero);
                data_count++;
            }
            for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
                idx++;
                scr.next();
            }
        }
        
        vector<int8_t> soft;
        for (int i = 0; i < BLOCK_BITS; i++)
            soft.push_back(deint.fetch() > 0 ? 127 : -127);
        
        ViterbiDecoder viterbi;
        vector<uint8_t> decoded;
        viterbi.decode_block(soft, decoded, true);
        
        string output;
        for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
            output += (byte >= 32 && byte < 127) ? (char)byte : '.';
        }
        
        cout << "\nPolarity " << polarity << ": " << output.substr(0, 80) << endl;
    }
    
    // Also try without interleaving at all
    cout << "\n=== Without interleaving ===" << endl;
    RefDataScrambler scr2;
    vector<int8_t> direct_soft;
    
    int idx = 0;
    while (direct_soft.size() < BLOCK_BITS && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < 32 && direct_soft.size() < BLOCK_BITS && idx < (int)result.data_symbols.size(); i++) {
            int position = decode_position(result.data_symbols[idx++]);
            int scr_val = scr2.next();
            int gray = (position - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            
            direct_soft.push_back((tribit & 4) ? -127 : 127);
            direct_soft.push_back((tribit & 2) ? -127 : 127);
            direct_soft.push_back((tribit & 1) ? -127 : 127);
        }
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr2.next();
        }
    }
    
    ViterbiDecoder viterbi2;
    vector<uint8_t> decoded2;
    viterbi2.decode_block(direct_soft, decoded2, true);
    
    string output2;
    for (size_t i = 0; i + 8 <= decoded2.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded2[i + j] & 1);
        output2 += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    cout << "No interleave: " << output2.substr(0, 80) << endl;
    
    return 0;
}
