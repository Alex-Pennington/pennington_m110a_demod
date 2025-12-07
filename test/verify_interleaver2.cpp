/**
 * Verify interleaver with proper state handling
 */
#include <iostream>
#include <vector>
#include <cstdint>
using namespace std;

// Reference interleaver matching exact reference behavior
class RefInterleaver {
public:
    RefInterleaver(int row_nr, int col_nr, int row_inc, int col_inc) 
        : row_nr(row_nr), col_nr(col_nr), row_inc(row_inc), col_inc(col_inc) {
        array.resize(row_nr, vector<int>(col_nr, 0));
        row = col = col_last = 0;
    }
    
    void load(int bit) {
        array[row][col] = bit;
        row = (row + row_inc) % row_nr;
        if (row == 0) {
            col = (col + 1) % col_nr;
        }
    }
    
    int fetch() {
        int bit = array[row][col];
        row = (row + 1) % row_nr;
        col = (col + col_inc) % col_nr;
        if (row == 0) {
            col = (col_last + 1) % col_nr;
            col_last = col;
        }
        return bit;
    }
    
    // For debugging
    int get_row() const { return row; }
    int get_col() const { return col; }
    
private:
    int row_nr, col_nr, row_inc, col_inc;
    int row, col, col_last;
    vector<vector<int>> array;
};

int main() {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    // Test with actual message data
    const char* MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    vector<int> msg_bits;
    for (const char* p = MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) msg_bits.push_back((c >> i) & 1);
    }
    
    // Pad to full block
    while (msg_bits.size() < BLOCK_BITS) msg_bits.push_back(0);
    
    RefInterleaver lvr(ROWS, COLS, ROW_INC, COL_INC);
    
    // Load all data
    for (int bit : msg_bits) lvr.load(bit);
    
    cout << "After loading " << BLOCK_BITS << " bits:" << endl;
    cout << "  row=" << lvr.get_row() << " col=" << lvr.get_col() << endl;
    
    // Fetch first 12 bits (4 tribits)
    cout << "First 12 fetched (continuing from load position): ";
    for (int i = 0; i < 12; i++) cout << lvr.fetch();
    cout << endl;
    
    // Now test with reset to 0 after load
    RefInterleaver lvr2(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : msg_bits) lvr2.load(bit);
    
    // Create new interleaver with same data, start fetch from 0
    RefInterleaver lvr3(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : msg_bits) lvr3.load(bit);
    // Manually reset row/col by creating new and copying array... 
    // Actually, let me just manually do the same starting position
    
    // The reference code shows that TX interleaver is shared for load and fetch
    // But after loading the full block, row should be back at 0 due to modular arithmetic
    
    cout << "\nNote: After loading " << BLOCK_BITS << " bits with row_inc=" << ROW_INC << ":" << endl;
    cout << "  Final row = " << (BLOCK_BITS * ROW_INC) % ROWS << endl;
    
    return 0;
}
