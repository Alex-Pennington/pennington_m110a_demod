/**
 * Manual trace through TX process
 */
#include <iostream>
#include <vector>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const int mgd3[8] = {0,1,3,2,7,6,4,5};

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

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

int main() {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    // Step 1: Message to bits
    vector<int> msg_bits;
    for (const char* p = TEST_MSG; *p; p++)
        for (int i = 7; i >= 0; i--) msg_bits.push_back((*p >> i) & 1);
    
    cout << "=== Step 1: Message bits ===" << endl;
    cout << "First 8 chars = 'THE QUIC'" << endl;
    cout << "T = 0x54 = 01010100" << endl;
    cout << "H = 0x48 = 01001000" << endl;
    cout << "E = 0x45 = 01000101" << endl;
    cout << "Message bits first 24: ";
    for (int i = 0; i < 24; i++) cout << msg_bits[i];
    cout << endl;
    
    // Step 2: Convolutional encoding
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    cout << "\n=== Step 2: Convolutional encoding ===" << endl;
    cout << "Encoded bits first 48: ";
    for (int i = 0; i < 48; i++) cout << (int)encoded[i];
    cout << endl;
    
    // Step 3: Interleaving
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    cout << "\n=== Step 3: Interleaving ===" << endl;
    cout << "After interleave, first 12 bits (4 tribits): ";
    for (int i = 0; i < 12; i++) {
        int bit = interleaver.fetch();
        cout << bit;
    }
    // Reset and refetch
    RefInterleaver int2(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) int2.load(encoded[i]);
    
    cout << endl;
    cout << "First 4 tribits: ";
    for (int i = 0; i < 4; i++) {
        int tribit = (int2.fetch() << 2) | (int2.fetch() << 1) | int2.fetch();
        cout << tribit << " ";
    }
    cout << endl;
    
    // Step 4: Gray encoding
    RefInterleaver int3(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) int3.load(encoded[i]);
    
    cout << "\n=== Step 4: Gray encoding (mgd3) ===" << endl;
    cout << "mgd3 table: tribit -> gray position" << endl;
    for (int i = 0; i < 8; i++) cout << "  " << i << " -> " << mgd3[i] << endl;
    
    cout << "First 4 symbols (after Gray): ";
    for (int i = 0; i < 4; i++) {
        int tribit = (int3.fetch() << 2) | (int3.fetch() << 1) | int3.fetch();
        int gray = mgd3[tribit];
        cout << gray << " (tribit=" << tribit << ") ";
    }
    cout << endl;
    
    // Step 5: Scrambling
    RefInterleaver int4(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) int4.load(encoded[i]);
    RefDataScrambler scr;
    
    cout << "\n=== Step 5: Scrambling ===" << endl;
    cout << "First 4 scrambler values: ";
    for (int i = 0; i < 4; i++) cout << scr.next() << " ";
    cout << endl;
    
    scr.reset();
    cout << "First 10 transmitted symbols (gray + scr): ";
    for (int i = 0; i < 10; i++) {
        int tribit = (int4.fetch() << 2) | (int4.fetch() << 1) | int4.fetch();
        int gray = mgd3[tribit];
        int scr_val = scr.next();
        int tx_pos = (gray + scr_val) % 8;
        cout << tx_pos;
    }
    cout << endl;
    
    // Full first 32 + 16 frame
    RefInterleaver int5(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) int5.load(encoded[i]);
    scr.reset();
    
    cout << "\n=== Full first frame (32 data + 16 probe) ===" << endl;
    cout << "Data symbols (0-31): ";
    for (int i = 0; i < 32; i++) {
        int tribit = (int5.fetch() << 2) | (int5.fetch() << 1) | int5.fetch();
        int gray = mgd3[tribit];
        int scr_val = scr.next();
        cout << (gray + scr_val) % 8;
    }
    cout << endl;
    
    cout << "Probe symbols (32-47, data=0): ";
    for (int i = 0; i < 16; i++) {
        int scr_val = scr.next();
        cout << scr_val;  // Probe = 0 + scrambler
    }
    cout << endl;
    
    return 0;
}
