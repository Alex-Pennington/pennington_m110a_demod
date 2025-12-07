/**
 * Verify interleaver matches reference
 */
#include <iostream>
#include <vector>
using namespace std;

// Reference interleaver using exact same method as reference modem
class RefInterleaver {
public:
    RefInterleaver(int row_nr, int col_nr, int row_inc, int col_inc) 
        : row_nr(row_nr), col_nr(col_nr), row_inc(row_inc), col_inc(col_inc) {
        array.resize(row_nr, vector<int>(col_nr, 0));
        reset();
    }
    
    void reset() {
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
    
private:
    int row_nr, col_nr, row_inc, col_inc;
    int row, col, col_last;
    vector<vector<int>> array;
};

// My interleaver (from test code)
class MyInterleaver {
public:
    MyInterleaver(int rows, int cols, int row_inc, int col_inc)
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
        if (fetch_row_ == 0) { 
            fetch_col_ = (fetch_col_last_ + 1) % cols_; 
            fetch_col_last_ = fetch_col_; 
        }
        return bit;
    }
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<int> array_;
    int load_row_, load_col_, fetch_row_, fetch_col_, fetch_col_last_;
};

int main() {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    // Test data
    vector<int> data(BLOCK_BITS);
    for (int i = 0; i < BLOCK_BITS; i++) data[i] = i % 2;
    
    // Reference interleaver
    RefInterleaver ref(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : data) ref.load(bit);
    ref.reset();  // Reset for fetch
    vector<int> ref_out;
    for (int i = 0; i < BLOCK_BITS; i++) ref_out.push_back(ref.fetch());
    
    // My interleaver
    MyInterleaver my(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : data) my.load(bit);
    vector<int> my_out;
    for (int i = 0; i < BLOCK_BITS; i++) my_out.push_back(my.fetch());
    
    // Compare
    int matches = 0;
    for (int i = 0; i < BLOCK_BITS; i++) {
        if (ref_out[i] == my_out[i]) matches++;
    }
    
    cout << "First 48 reference: ";
    for (int i = 0; i < 48; i++) cout << ref_out[i];
    cout << endl;
    
    cout << "First 48 my:        ";
    for (int i = 0; i < 48; i++) cout << my_out[i];
    cout << endl;
    
    cout << "\nMatches: " << matches << "/" << BLOCK_BITS << endl;
    
    return 0;
}
