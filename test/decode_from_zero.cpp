/**
 * Decode starting from position 0 with correct frame structure
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

// Exact reference deinterleaver
class RefDeinterleaver {
public:
    RefDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        reset();
    }
    
    void reset() {
        row_ = col_ = col_last_ = 0;
        fetch_row_ = fetch_col_ = 0;
        fill(array_.begin(), array_.end(), 0.0f);
    }
    
    void load(float bit) {
        array_[row_ * cols_ + col_] = bit;
        row_ = (row_ + 1) % rows_;
        col_ = (col_ + col_inc_) % cols_;
        if (row_ == 0) {
            col_ = (col_last_ + 1) % cols_;
            col_last_ = col_;
        }
    }
    
    float fetch() {
        float bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        if (fetch_row_ == 0) {
            fetch_col_ = (fetch_col_ + 1) % cols_;
        }
        return bit;
    }
    
private:
    int rows_, cols_, row_inc_, col_inc_;
    int row_, col_, col_last_;
    int fetch_row_, fetch_col_;
    vector<float> array_;
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
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // M2400S parameters
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int UNKNOWN_LEN = 32, KNOWN_LEN = 16;
    const int BLOCK_BITS = ROWS * COLS;  // 2880 bits
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;  // 960 data symbols
    
    // inv_mgd3: gray position -> tribit
    const int inv_mgd3[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0;  // Start from position 0
    int symbols_processed = 0;
    int scr_idx = 0;
    
    cout << "\nProcessing with 32+16 frame structure starting from position 0..." << endl;
    
    while (symbols_processed < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size()) {
        // 32 data symbols
        for (int i = 0; i < UNKNOWN_LEN && symbols_processed < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size(); i++) {
            int position = decode_8psk_position(result.data_symbols[idx++]);
            int scr_val = scr.next();
            scr_idx++;
            
            int gray = (position - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            
            float bit2 = (tribit & 4) ? -1.0f : 1.0f;
            float bit1 = (tribit & 2) ? -1.0f : 1.0f;
            float bit0 = (tribit & 1) ? -1.0f : 1.0f;
            
            deint.load(bit2);
            deint.load(bit1);
            deint.load(bit0);
            
            symbols_processed++;
        }
        
        // 16 probe symbols - skip but advance scrambler
        for (int i = 0; i < KNOWN_LEN && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
            scr_idx++;
        }
    }
    
    cout << "Processed " << symbols_processed << " data symbols" << endl;
    cout << "Scrambler advanced " << scr_idx << " positions" << endl;
    
    // Fetch from deinterleaver
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) {
        float val = deint.fetch();
        soft.push_back(val > 0 ? 127 : -127);
    }
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to string and count matches
    int matches = 0;
    string output;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "\nOutput: " << output.substr(0, 70) << endl;
    cout << "Matches: " << matches << "/" << EXPECTED_LEN << endl;
    
    return 0;
}
