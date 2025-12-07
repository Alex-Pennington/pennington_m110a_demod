/**
 * Complete TX trace matching exact reference modem behavior
 */
#include <iostream>
#include <vector>
#include <cstdint>
using namespace std;

const int mgd3[8] = {0,1,3,2,7,6,4,5};

// Reference encoder
class RefEncoder {
public:
    RefEncoder() : state(0) {}
    void reset() { state = 0; }
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

// Reference interleaver 
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

// Reference scrambler
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
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    const char* MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Step 1: Message to bits (LSB first)
    vector<int> msg_bits;
    for (const char* p = MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) msg_bits.push_back((c >> i) & 1);
    }
    
    cout << "Step 1: Message bits (LSB first)" << endl;
    cout << "  First 24: ";
    for (int i = 0; i < 24; i++) cout << msg_bits[i];
    cout << endl;
    
    // Step 2: Convolutional encode
    RefEncoder enc;
    vector<int> encoded;
    for (int bit : msg_bits) {
        auto [b1, b2] = enc.encode(bit);
        encoded.push_back(b1);
        encoded.push_back(b2);
    }
    // Flush encoder with K-1=6 zeros
    for (int i = 0; i < 6; i++) {
        auto [b1, b2] = enc.encode(0);
        encoded.push_back(b1);
        encoded.push_back(b2);
    }
    // Pad to block size
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    cout << "\nStep 2: Convolutional encoded" << endl;
    cout << "  First 24: ";
    for (int i = 0; i < 24; i++) cout << encoded[i];
    cout << endl;
    
    // Step 3: Interleave
    RefInterleaver lvr(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : encoded) lvr.load(bit);
    // After loading full block, lvr returns to (0,0) - perfect for fetch
    
    cout << "\nStep 3: After interleave (first 12 fetched bits):" << endl;
    cout << "  ";
    for (int i = 0; i < 12; i++) cout << lvr.fetch();
    cout << endl;
    
    // Step 4: Map to symbols with Gray and scrambler
    RefInterleaver lvr2(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : encoded) lvr2.load(bit);
    
    RefScrambler scr;
    vector<int> tx_symbols;
    
    for (int i = 0; i < BLOCK_BITS / 3; i++) {
        int tribit = (lvr2.fetch() << 2) | (lvr2.fetch() << 1) | lvr2.fetch();
        int gray = mgd3[tribit];
        int scr_val = scr.next();
        int sym = (gray + scr_val) % 8;
        tx_symbols.push_back(sym);
    }
    
    cout << "\nStep 4: TX symbols (first 32 data + 16 probe)" << endl;
    cout << "  First 32 data: ";
    for (int i = 0; i < 32; i++) cout << tx_symbols[i];
    cout << endl;
    
    // Generate probe symbols (scrambler values for data=0, gray=0)
    cout << "  Next 16 probe: ";
    for (int i = 0; i < 16; i++) cout << scr.next();
    cout << endl;
    
    return 0;
}
