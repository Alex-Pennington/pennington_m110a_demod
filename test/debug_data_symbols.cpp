/**
 * Debug data symbol differences
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
    
    // Generate expected TX with LSB-first
    vector<int> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) msg_bits.push_back((c >> i) & 1);
    }
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    // Generate expected DATA symbols only (position within each frame)
    vector<int> expected_data;  // Just data symbols, no probes
    for (int i = 0; i < BLOCK_BITS / 3; i++) {
        int tribit = (interleaver.fetch() << 2) | (interleaver.fetch() << 1) | interleaver.fetch();
        expected_data.push_back(mgd3[tribit]);  // Gray-encoded value before scrambling
    }
    
    // Get received symbols
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Extract received data symbols (skip probes) and descramble
    RefDataScrambler scr;
    vector<int> received_gray;  // Descrambled to gray values
    
    int idx = 0;
    while (received_gray.size() < 960 && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < 32 && received_gray.size() < 960 && idx < (int)result.data_symbols.size(); i++) {
            int pos = decode_8psk_position(result.data_symbols[idx++]);
            int scr_val = scr.next();
            int gray = (pos - scr_val + 8) % 8;
            received_gray.push_back(gray);
        }
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    cout << "=== Data Symbol Analysis (Gray values after descrambling) ===" << endl;
    cout << "Expected data symbols: " << expected_data.size() << endl;
    cout << "Received data symbols: " << received_gray.size() << endl;
    
    int match = 0;
    for (size_t i = 0; i < min(expected_data.size(), received_gray.size()); i++) {
        if (expected_data[i] == received_gray[i]) match++;
    }
    cout << "Matches: " << match << "/" << min(expected_data.size(), received_gray.size()) << endl;
    
    cout << "\nFirst 100 expected (gray): ";
    for (int i = 0; i < 100; i++) cout << expected_data[i];
    cout << endl;
    
    cout << "First 100 received (gray): ";
    for (int i = 0; i < 100; i++) cout << received_gray[i];
    cout << endl;
    
    // Check differences position by position
    cout << "\nDifferences in first 100:" << endl;
    cout << "Pos  Exp  Rcv  Diff" << endl;
    for (int i = 0; i < 100; i++) {
        if (expected_data[i] != received_gray[i]) {
            int diff = (received_gray[i] - expected_data[i] + 8) % 8;
            printf("%3d   %d    %d    %d\n", i, expected_data[i], received_gray[i], diff);
        }
    }
    
    return 0;
}
