/**
 * Full encode/decode verification with LSB-first bit ordering
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int TEST_LEN = 54;

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

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    cout << "=== Full Chain Verification with LSB-first ===" << endl;
    
    // ========== TX SIDE ==========
    
    // Step 1: Message to bits - LSB first
    vector<int> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) {  // LSB first
            msg_bits.push_back((c >> i) & 1);
        }
    }
    cout << "TX Step 1: Message bits (LSB-first): " << msg_bits.size() << " bits" << endl;
    cout << "  First 24: ";
    for (int i = 0; i < 24; i++) cout << msg_bits[i];
    cout << endl;
    
    // Step 2: Convolutional encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    cout << "TX Step 2: Encoded bits: " << encoded.size() << endl;
    cout << "  First 24: ";
    for (int i = 0; i < 24; i++) cout << (int)encoded[i];
    cout << endl;
    
    // Step 3: Interleave
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    // Step 4: Generate TX symbols with scrambler
    RefDataScrambler tx_scr;
    vector<int> tx_symbols;
    int tx_data_count = 0;
    
    while (tx_data_count < BLOCK_BITS / 3) {
        for (int i = 0; i < 32 && tx_data_count < BLOCK_BITS / 3; i++) {
            int tribit = (interleaver.fetch() << 2) | (interleaver.fetch() << 1) | interleaver.fetch();
            int gray = mgd3[tribit];
            int scr_val = tx_scr.next();
            tx_symbols.push_back((gray + scr_val) % 8);
            tx_data_count++;
        }
        for (int i = 0; i < 16; i++) {
            tx_symbols.push_back(tx_scr.next());  // Probe symbols
        }
    }
    cout << "TX Step 4: TX symbols: " << tx_symbols.size() << endl;
    cout << "  First 48: ";
    for (int i = 0; i < 48; i++) cout << tx_symbols[i];
    cout << endl;
    
    // ========== RX SIDE ==========
    
    // Step 1: Descramble and demapper
    RefDataScrambler rx_scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int rx_idx = 0, rx_data_count = 0;
    while (rx_data_count < BLOCK_BITS / 3 && rx_idx < (int)tx_symbols.size()) {
        for (int i = 0; i < 32 && rx_data_count < BLOCK_BITS / 3 && rx_idx < (int)tx_symbols.size(); i++) {
            int pos = tx_symbols[rx_idx++];
            int scr_val = rx_scr.next();
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            rx_data_count++;
        }
        for (int i = 0; i < 16 && rx_idx < (int)tx_symbols.size(); i++) {
            rx_idx++;
            rx_scr.next();
        }
    }
    cout << "RX Step 1: Deinterleaver loaded: " << rx_data_count << " tribits" << endl;
    
    // Step 2: Fetch deinterleaved bits
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    cout << "RX Step 2: Soft bits: " << soft.size() << endl;
    cout << "  First 24: ";
    for (int i = 0; i < 24; i++) cout << (soft[i] > 0 ? "0" : "1");
    cout << endl;
    
    // Compare to TX encoded bits
    int enc_match = 0;
    for (int i = 0; i < BLOCK_BITS; i++) {
        int rx_bit = soft[i] > 0 ? 0 : 1;
        if (rx_bit == encoded[i]) enc_match++;
    }
    cout << "  Encoded bit matches: " << enc_match << "/" << BLOCK_BITS << endl;
    
    // Step 3: Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    cout << "RX Step 3: Decoded bits: " << decoded.size() << endl;
    
    // Step 4: Convert to bytes - LSB first
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < TEST_LEN; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            if (decoded[i + j]) byte |= (1 << j);  // LSB first
        }
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
        if (byte == (uint8_t)TEST_MSG[i/8]) matches++;
    }
    
    cout << "\n=== RESULT ===" << endl;
    cout << "Expected: " << TEST_MSG << endl;
    cout << "Decoded:  " << output << endl;
    cout << "Matches:  " << matches << "/" << TEST_LEN << endl;
    
    return 0;
}
