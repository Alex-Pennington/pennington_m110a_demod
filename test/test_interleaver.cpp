/**
 * Test interleaver alone
 */
#include <iostream>
#include <vector>

using namespace std;

class RefInterleaver {
public:
    RefInterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0);
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
        array_.resize(rows * cols, 0);
        load_row_ = load_col_ = load_col_last_ = 0;
        fetch_row_ = fetch_col_ = 0;
    }
    
    void load(int bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + 1) % rows_;
        load_col_ = (load_col_ + col_inc_) % cols_;
        if (load_row_ == 0) {
            load_col_ = (load_col_last_ + 1) % cols_;
            load_col_last_ = load_col_;
        }
    }
    
    int fetch() {
        int bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        if (fetch_row_ == 0) {
            fetch_col_ = (fetch_col_ + 1) % cols_;
        }
        return bit;
    }
    
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<int> array_;
    int load_row_, load_col_, load_col_last_;
    int fetch_row_, fetch_col_;
};

int main() {
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;  // 2880
    
    // Create test pattern
    vector<int> input;
    for (int i = 0; i < BLOCK_BITS; i++) {
        input.push_back(i % 2);  // Alternating 0,1
    }
    
    // Interleave
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (int i = 0; i < BLOCK_BITS; i++) {
        interleaver.load(input[i]);
    }
    
    vector<int> interleaved;
    for (int i = 0; i < BLOCK_BITS; i++) {
        interleaved.push_back(interleaver.fetch());
    }
    
    // Deinterleave  
    RefDeinterleaver deinterleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (int i = 0; i < BLOCK_BITS; i++) {
        deinterleaver.load(interleaved[i]);
    }
    
    vector<int> output;
    for (int i = 0; i < BLOCK_BITS; i++) {
        output.push_back(deinterleaver.fetch());
    }
    
    // Check
    int errors = 0;
    for (int i = 0; i < BLOCK_BITS; i++) {
        if (output[i] != input[i]) errors++;
    }
    
    cout << "Interleaver test: " << errors << " errors out of " << BLOCK_BITS << endl;
    
    if (errors > 0) {
        cout << "First 20 input:       ";
        for (int i = 0; i < 20; i++) cout << input[i];
        cout << endl;
        cout << "First 20 interleaved: ";
        for (int i = 0; i < 20; i++) cout << interleaved[i];
        cout << endl;
        cout << "First 20 output:      ";
        for (int i = 0; i < 20; i++) cout << output[i];
        cout << endl;
    }
    
    return 0;
}
