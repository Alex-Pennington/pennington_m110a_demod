/**
 * Verify the encode/decode chain without PCM
 */
#include <iostream>
#include <vector>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

// mgd3 table from reference
const int mgd3[8] = {0,1,3,2,7,6,4,5};  // tribit -> gray position
int inv_mgd3[8];  // gray position -> tribit

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

// Convolutional interleaver matching reference
class RefInterleaver {
public:
    RefInterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0);
        reset();
    }
    
    void reset() {
        load_row_ = load_col_ = 0;
        fetch_row_ = fetch_col_ = fetch_col_last_ = 0;
    }
    
    void load(int bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + row_inc_) % rows_;
        if (load_row_ == 0) {
            load_col_ = (load_col_ + 1) % cols_;
        }
    }
    
    int fetch() {
        int bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + 1) % rows_;
        fetch_col_ = (fetch_col_ + col_inc_) % cols_;
        if (fetch_row_ == 0) {
            fetch_col_ = (fetch_col_last_ + 1) % cols_;
            fetch_col_last_ = fetch_col_;
        }
        return bit;
    }
    
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<int> array_;
    int load_row_, load_col_;
    int fetch_row_, fetch_col_, fetch_col_last_;
};

class RefDeinterleaver {
public:
    RefDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        reset();
    }
    
    void reset() {
        load_row_ = load_col_ = load_col_last_ = 0;
        fetch_row_ = fetch_col_ = 0;
    }
    
    void load(float bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + 1) % rows_;
        load_col_ = (load_col_ + col_inc_) % cols_;
        if (load_row_ == 0) {
            load_col_ = (load_col_last_ + 1) % cols_;
            load_col_last_ = load_col_;
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
    vector<float> array_;
    int load_row_, load_col_, load_col_last_;
    int fetch_row_, fetch_col_;
};

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int UNKNOWN_LEN = 32, KNOWN_LEN = 16;
    const int BLOCK_BITS = ROWS * COLS;
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;
    
    // TX
    vector<int> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        for (int i = 7; i >= 0; i--) msg_bits.push_back((*p >> i) & 1);
    }
    
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (int i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    RefDataScrambler tx_scr;
    vector<int> tx_symbols;
    
    while (tx_symbols.size() < BLOCK_SYMBOLS) {
        for (int i = 0; i < UNKNOWN_LEN && tx_symbols.size() < BLOCK_SYMBOLS; i++) {
            int tribit = (interleaver.fetch() << 2) | (interleaver.fetch() << 1) | interleaver.fetch();
            int gray = mgd3[tribit];
            int position = (gray + tx_scr.next()) % 8;
            tx_symbols.push_back(position);
        }
        for (int i = 0; i < KNOWN_LEN; i++) {
            tx_symbols.push_back(tx_scr.next());
        }
    }
    
    cout << "TX: " << tx_symbols.size() << " symbols" << endl;
    
    // RX
    RefDataScrambler rx_scr;
    RefDeinterleaver deinterleaver(ROWS, COLS, ROW_INC, COL_INC);
    
    int rx_idx = 0, rx_processed = 0;
    while (rx_processed < BLOCK_SYMBOLS && rx_idx < (int)tx_symbols.size()) {
        for (int i = 0; i < UNKNOWN_LEN && rx_processed < BLOCK_SYMBOLS; i++) {
            int gray = (tx_symbols[rx_idx++] - rx_scr.next() + 8) % 8;
            int tribit = inv_mgd3[gray];
            deinterleaver.load((tribit & 4) ? -1.0f : 1.0f);
            deinterleaver.load((tribit & 2) ? -1.0f : 1.0f);
            deinterleaver.load((tribit & 1) ? -1.0f : 1.0f);
            rx_processed++;
        }
        for (int i = 0; i < KNOWN_LEN; i++) {
            rx_idx++;
            rx_scr.next();
        }
    }
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) {
        soft.push_back(deinterleaver.fetch() > 0 ? 127 : -127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < strlen(TEST_MSG) && byte == (uint8_t)TEST_MSG[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "RX: " << output.substr(0, 70) << endl;
    cout << "Expected: " << TEST_MSG << endl;
    cout << "Matches: " << matches << "/" << strlen(TEST_MSG) << endl;
    
    return 0;
}
