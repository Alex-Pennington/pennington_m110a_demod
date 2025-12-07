/**
 * MS-DMT Interleaver Verification Test
 * 
 * Implements the exact MS-DMT interleaver algorithm and compares to our implementation.
 */

#include "modem/multimode_interleaver.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

using namespace m110a;

// Maximum interleaver dimensions from MS-DMT
#define MAX_ROWS 40
#define MAX_COLS 576

// MS-DMT Interleaver structure
struct MSDMTInterleaver {
    int array[MAX_ROWS][MAX_COLS];
    int row, col;
    int zrow, zcol;
    int col_last;
    int row_nr, col_nr;
    int row_inc, col_inc;
    int nbits;
};

// MS-DMT Deinterleaver structure  
struct MSDMTDeInterleaver {
    float array[MAX_ROWS][MAX_COLS];
    int row, col;
    int zrow, zcol;
    int col_last;
    int row_nr, col_nr;
    int row_inc, col_inc;
    int nbits;
};

// Reset interleaver
void reset_interleave(MSDMTInterleaver* lvr) {
    lvr->row = 0;
    lvr->col = 0;
    lvr->zrow = 0;
    lvr->zcol = 0;
    lvr->col_last = 0;
    lvr->nbits = 0;
    memset(lvr->array, 0, sizeof(lvr->array));
}

// Load bit into interleaver (TX write)
void load_interleaver(int bit, MSDMTInterleaver* lvr) {
    if (lvr->row_inc != 0) {
        lvr->array[lvr->row][lvr->col] = bit;
        lvr->row = (lvr->row + lvr->row_inc) % lvr->row_nr;
        if (lvr->row == 0) {
            lvr->col = (lvr->col + 1) % lvr->col_nr;
        }
    } else {
        lvr->array[lvr->row][lvr->col] = bit;
        lvr->row = (lvr->row + 1) % lvr->row_nr;
        if (lvr->row == 0) {
            lvr->col = (lvr->col + 1) % lvr->col_nr;
        }
    }
    lvr->nbits++;
}

// Fetch bit from interleaver (TX read)
int fetch_interleaver(MSDMTInterleaver* lvr) {
    int bit;
    
    if (lvr->row_inc != 0) {
        bit = lvr->array[lvr->row][lvr->col];
        lvr->row = (lvr->row + 1) % lvr->row_nr;
        lvr->col = (lvr->col + lvr->col_inc) % lvr->col_nr;
        if (lvr->row == 0) {
            lvr->col = (lvr->col_last + 1) % lvr->col_nr;
            lvr->col_last = lvr->col;
        }
    } else {
        bit = lvr->array[lvr->zrow][lvr->zcol];
        lvr->zrow = (lvr->zrow + 1) % lvr->row_nr;
        if (lvr->zrow == 0) {
            lvr->zcol = (lvr->zcol + 1) % lvr->col_nr;
        }
    }
    lvr->nbits--;
    return bit;
}

// Reset deinterleaver
void reset_deinterleave(MSDMTDeInterleaver* lvr) {
    lvr->row = 0;
    lvr->col = 0;
    lvr->zrow = 0;
    lvr->zcol = 0;
    lvr->col_last = 0;
    lvr->nbits = 0;
    memset(lvr->array, 0, sizeof(lvr->array));
}

// Load soft bit into deinterleaver (RX write - uses fetch pattern)
void load_deinterleaver(float bit, MSDMTDeInterleaver* lvr) {
    if (lvr->row_inc != 0) {
        lvr->array[lvr->row][lvr->col] = bit;
        lvr->row = (lvr->row + 1) % lvr->row_nr;
        lvr->col = (lvr->col + lvr->col_inc) % lvr->col_nr;
        
        if (lvr->row == 0) {
            lvr->col = (lvr->col_last + 1) % lvr->col_nr;
            lvr->col_last = lvr->col;
        }
    } else {
        lvr->array[lvr->row][lvr->col] = bit;
        lvr->row = (lvr->row + 1) % lvr->row_nr;
        if (lvr->row == 0) {
            lvr->col = (lvr->col + 1) % lvr->col_nr;
        }
    }
    lvr->nbits++;
}

// Fetch soft bit from deinterleaver (RX read - uses load pattern)
float fetch_deinterleaver(MSDMTDeInterleaver* lvr) {
    float bit;
    
    if (lvr->row_inc != 0) {
        bit = lvr->array[lvr->row][lvr->col];
        lvr->row = (lvr->row + lvr->row_inc) % lvr->row_nr;
        
        if (lvr->row == 0) {
            lvr->col = (lvr->col + 1) % lvr->col_nr;
        }
    } else {
        bit = lvr->array[lvr->zrow][lvr->zcol];
        lvr->zrow = (lvr->zrow + 1) % lvr->row_nr;
        if (lvr->zrow == 0) {
            lvr->zcol = (lvr->zcol + 1) % lvr->col_nr;
        }
    }
    lvr->nbits--;
    return bit;
}

// Test a mode
bool test_mode(const char* name, int rows, int cols, int row_inc, int col_inc) {
    std::cout << "\n=== Testing " << name << " ===" << std::endl;
    std::cout << "Rows=" << rows << " Cols=" << cols 
              << " RowInc=" << row_inc << " ColInc=" << col_inc << std::endl;
    
    int block_size = rows * cols;
    
    // Generate test data
    std::vector<int> input_bits(block_size);
    for (int i = 0; i < block_size; i++) {
        input_bits[i] = i % 2;  // Alternating 0/1
    }
    
    // MS-DMT interleave
    MSDMTInterleaver ilv;
    ilv.row_nr = rows;
    ilv.col_nr = cols;
    ilv.row_inc = row_inc;
    ilv.col_inc = col_inc;
    reset_interleave(&ilv);
    
    // Load all bits
    for (int bit : input_bits) {
        load_interleaver(bit, &ilv);
    }
    
    // Fetch all bits (interleaved order)
    ilv.row = 0;
    ilv.col = 0;
    ilv.col_last = 0;
    ilv.nbits = block_size;
    
    std::vector<int> msdmt_interleaved(block_size);
    for (int i = 0; i < block_size; i++) {
        msdmt_interleaved[i] = fetch_interleaver(&ilv);
    }
    
    // MS-DMT deinterleave
    MSDMTDeInterleaver dilv;
    dilv.row_nr = rows;
    dilv.col_nr = cols;
    dilv.row_inc = row_inc;
    dilv.col_inc = col_inc;
    reset_deinterleave(&dilv);
    
    // Load interleaved bits
    for (int bit : msdmt_interleaved) {
        load_deinterleaver((float)bit, &dilv);
    }
    
    // Fetch deinterleaved bits
    dilv.row = 0;
    dilv.col = 0;
    dilv.nbits = block_size;
    
    std::vector<int> msdmt_deinterleaved(block_size);
    for (int i = 0; i < block_size; i++) {
        msdmt_deinterleaved[i] = (int)fetch_deinterleaver(&dilv);
    }
    
    // Verify MS-DMT loopback
    int msdmt_errors = 0;
    for (int i = 0; i < block_size; i++) {
        if (input_bits[i] != msdmt_deinterleaved[i]) msdmt_errors++;
    }
    std::cout << "MS-DMT loopback errors: " << msdmt_errors << std::endl;
    
    // Now test our implementation
    InterleaverParams params;
    params.rows = rows;
    params.cols = cols;
    params.row_inc = row_inc;
    params.col_inc = col_inc;
    
    MultiModeInterleaver our_ilv(params);
    
    std::vector<soft_bit_t> our_input(input_bits.begin(), input_bits.end());
    auto our_interleaved = our_ilv.interleave(our_input);
    auto our_deinterleaved = our_ilv.deinterleave(our_interleaved);
    
    // Compare interleaved output
    int interleave_diff = 0;
    for (int i = 0; i < block_size; i++) {
        if (msdmt_interleaved[i] != (int)our_interleaved[i]) interleave_diff++;
    }
    std::cout << "Interleave differences: " << interleave_diff << std::endl;
    
    // Compare deinterleaved output  
    int deinterleave_diff = 0;
    for (int i = 0; i < block_size; i++) {
        if (input_bits[i] != (int)our_deinterleaved[i]) deinterleave_diff++;
    }
    std::cout << "Our loopback errors: " << deinterleave_diff << std::endl;
    
    // Show first few values if there are differences
    if (interleave_diff > 0) {
        std::cout << "First 20 interleaved (MS-DMT vs ours):" << std::endl;
        for (int i = 0; i < 20 && i < block_size; i++) {
            std::cout << msdmt_interleaved[i] << "/" << (int)our_interleaved[i] << " ";
        }
        std::cout << std::endl;
    }
    
    bool pass = (msdmt_errors == 0) && (interleave_diff == 0) && (deinterleave_diff == 0);
    std::cout << (pass ? "✓ PASS" : "✗ FAIL") << std::endl;
    
    return pass;
}

int main() {
    std::cout << "=== MS-DMT Interleaver Verification ===" << std::endl;
    
    bool all_pass = true;
    
    // Test all data modes
    all_pass &= test_mode("M600S",  40, 18, 9, 1);
    all_pass &= test_mode("M1200S", 40, 36, 9, 19);
    all_pass &= test_mode("M2400S", 40, 72, 9, 55);
    all_pass &= test_mode("M600L",  40, 36, 9, 17);
    all_pass &= test_mode("M1200L", 40, 72, 9, 53);
    all_pass &= test_mode("M2400L", 40, 144, 9, 107);
    all_pass &= test_mode("M150S",  40, 36, 9, 17);
    all_pass &= test_mode("M300S",  40, 36, 9, 17);
    
    std::cout << "\n=== OVERALL: " << (all_pass ? "ALL PASSED" : "SOME FAILED") << " ===" << std::endl;
    
    return all_pass ? 0 : 1;
}
