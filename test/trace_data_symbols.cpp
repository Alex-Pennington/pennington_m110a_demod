/**
 * Trace exact data symbol computation
 */
#include <iostream>
#include <vector>
#include <cstdint>
using namespace std;

const int mgd3[8] = {0,1,3,2,7,6,4,5};
int inv_mgd3[8];

class RefEncoder {
public:
    RefEncoder() : state(0) {}
    pair<int,int> encode(int in) {
        state = state >> 1;
        if (in) state |= 0x40;
        int count1 = __builtin_popcount(state & 0x5B);
        int count2 = __builtin_popcount(state & 0x79);
        return {count1 & 1, count2 & 1};
    }
private:
    int state;
};

class RefInterleaver {
public:
    RefInterleaver(int row_nr, int col_nr, int row_inc, int col_inc) 
        : row_nr(row_nr), col_nr(col_nr), row_inc(row_inc), col_inc(col_inc) {
        array.resize(row_nr * col_nr, 0);
        row = col = col_last = 0;
    }
    void load(int bit) {
        array[row * col_nr + col] = bit;
        row = (row + row_inc) % row_nr;
        if (row == 0) col = (col + 1) % col_nr;
    }
    int fetch() {
        int bit = array[row * col_nr + col];
        row = (row + 1) % row_nr;
        col = (col + col_inc) % col_nr;
        if (row == 0) { col = (col_last + 1) % col_nr; col_last = col; }
        return bit;
    }
private:
    int row_nr, col_nr, row_inc, col_inc;
    int row, col, col_last;
    vector<int> array;
};

class RefScrambler {
public:
    RefScrambler() { reset(); }
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

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    const char* MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Generate encoded and interleaved bits
    vector<int> msg_bits;
    for (const char* p = MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) msg_bits.push_back((c >> i) & 1);
    }
    
    RefEncoder enc;
    vector<int> encoded;
    for (int bit : msg_bits) {
        auto [b1, b2] = enc.encode(bit);
        encoded.push_back(b1);
        encoded.push_back(b2);
    }
    for (int i = 0; i < 6; i++) {
        auto [b1, b2] = enc.encode(0);
        encoded.push_back(b1);
        encoded.push_back(b2);
    }
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    RefInterleaver lvr(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : encoded) lvr.load(bit);
    
    RefScrambler scr;
    
    cout << "First 32 data symbols (detailed):" << endl;
    cout << "Pos  Tribit  Gray  Scr  TxSym" << endl;
    
    for (int i = 0; i < 32; i++) {
        int tribit = (lvr.fetch() << 2) | (lvr.fetch() << 1) | lvr.fetch();
        int gray = mgd3[tribit];
        int scr_val = scr.next();
        int tx_sym = (gray + scr_val) % 8;
        
        printf("%3d    %d       %d     %d     %d\n", i, tribit, gray, scr_val, tx_sym);
    }
    
    return 0;
}
