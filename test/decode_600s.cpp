/**
 * Try decoding 600S mode
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

// For BPSK: position 0 = +1, position 4 = -1
int decode_bpsk_bit(complex<float> sym) {
    return (sym.real() > 0) ? 0 : 1;
}

int main() {
    string filename = "/home/claude/tx_600S_20251206_202518_709.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Symbols: " << result.data_symbols.size() << endl;
    
    // M600S: 20 unknown + 20 known, BPSK (1 bit/symbol)
    // Interleaver: 40x36 for short interleave
    const int ROWS = 40, COLS = 36;
    const int ROW_INC = 9, COL_INC = 19;
    const int UNKNOWN_LEN = 20, KNOWN_LEN = 20;
    const int BLOCK_BITS = ROWS * COLS;  // 1440 bits
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0, data_count = 0;
    
    while (data_count < BLOCK_BITS && idx < (int)result.data_symbols.size()) {
        // 20 unknown symbols
        for (int i = 0; i < UNKNOWN_LEN && data_count < BLOCK_BITS && idx < (int)result.data_symbols.size(); i++) {
            int bit = decode_bpsk_bit(result.data_symbols[idx++]);
            int scr_val = scr.next() & 1;  // For BPSK, use only LSB of scrambler
            int descr = bit ^ scr_val;
            deint.load(descr ? -1.0f : 1.0f);
            data_count++;
        }
        // 20 known (probe) - skip
        for (int i = 0; i < KNOWN_LEN && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    cout << "Loaded " << data_count << " bits into deinterleaver" << endl;
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "Output: " << output.substr(0, 80) << endl;
    cout << "Matches: " << matches << "/" << EXPECTED_LEN << endl;
    
    return 0;
}
