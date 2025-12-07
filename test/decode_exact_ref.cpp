/**
 * Decode using exact reference implementation
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

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

// Exact reference data scrambler
class RefDataScrambler {
public:
    RefDataScrambler() { reset(); }
    
    void reset() {
        sreg[0]=1; sreg[1]=0; sreg[2]=1; sreg[3]=1;
        sreg[4]=0; sreg[5]=1; sreg[6]=0; sreg[7]=1;
        sreg[8]=1; sreg[9]=1; sreg[10]=0; sreg[11]=1;
        offset = 0;
    }
    
    int next() {
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10]; sreg[10] = sreg[9]; sreg[9] = sreg[8];
            sreg[8] = sreg[7]; sreg[7] = sreg[6]; sreg[6] = sreg[5] ^ carry;
            sreg[5] = sreg[4]; sreg[4] = sreg[3] ^ carry; sreg[3] = sreg[2];
            sreg[2] = sreg[1]; sreg[1] = sreg[0] ^ carry; sreg[0] = carry;
        }
        int tribit = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
        offset = (offset + 1) % 160;
        return tribit;
    }
    
private:
    int sreg[12];
    int offset;
};

// Exact reference deinterleaver  
class RefDeinterleaver {
public:
    RefDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        reset();
    }
    
    void reset() {
        row_ = col_ = col_last_ = 0;
        fill(array_.begin(), array_.end(), 0.0f);
    }
    
    // Load: mirrors TX fetch pattern
    void load(float bit) {
        array_[row_ * cols_ + col_] = bit;
        row_ = (row_ + 1) % rows_;
        col_ = (col_ + col_inc_) % cols_;
        
        if (row_ == 0) {
            col_ = (col_last_ + 1) % cols_;
            col_last_ = col_;
        }
    }
    
    // Fetch: mirrors TX load pattern  
    float fetch() {
        float bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        
        if (fetch_row_ == 0) {
            fetch_col_ = (fetch_col_ + 1) % cols_;
        }
        
        return bit;
    }
    
    void start_fetch() {
        fetch_row_ = fetch_col_ = 0;
    }
    
private:
    int rows_, cols_, row_inc_, col_inc_;
    int row_, col_, col_last_;
    int fetch_row_, fetch_col_;
    vector<float> array_;
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

int try_decode(const vector<complex<float>>& data_symbols, int start, bool show) {
    // M2400S parameters
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int UNKNOWN_LEN = 32, KNOWN_LEN = 16;
    const int BLOCK_BITS = ROWS * COLS;  // 2880 bits
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;  // 960 data symbols
    
    // mgd3 inverse (gray position -> tribit)
    const int inv_mgd3[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = start;
    int symbols_processed = 0;
    
    // Process symbols in 32 data + 16 probe pattern
    while (symbols_processed < BLOCK_SYMBOLS && idx < (int)data_symbols.size()) {
        // 32 unknown (data) symbols
        for (int i = 0; i < UNKNOWN_LEN && symbols_processed < BLOCK_SYMBOLS && idx < (int)data_symbols.size(); i++) {
            complex<float> sym = data_symbols[idx++];
            int scr_val = scr.next();
            
            // Get 8PSK position
            int position = decode_8psk_position(sym);
            
            // Subtract scrambler
            int gray = (position - scr_val + 8) % 8;
            
            // Gray decode to tribit
            int tribit = inv_mgd3[gray];
            
            // Extract 3 bits and load into deinterleaver
            float bit2 = (tribit & 4) ? -1.0f : 1.0f;
            float bit1 = (tribit & 2) ? -1.0f : 1.0f;
            float bit0 = (tribit & 1) ? -1.0f : 1.0f;
            
            deint.load(bit2);
            deint.load(bit1);
            deint.load(bit0);
            
            symbols_processed++;
        }
        
        // 16 known (probe) symbols - skip but advance scrambler
        for (int i = 0; i < KNOWN_LEN && idx < (int)data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    if (symbols_processed < BLOCK_SYMBOLS) {
        if (show) cout << "Only got " << symbols_processed << " symbols" << endl;
        return 0;
    }
    
    // Fetch from deinterleaver and decode
    deint.start_fetch();
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) {
        float val = deint.fetch();
        soft.push_back(val > 0 ? 127 : -127);
    }
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Count matches
    int matches = 0;
    string output;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < EXPECTED_LEN && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    if (show) {
        cout << "Output: " << output.substr(0, 70) << endl;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    cout << "\nSearching with exact reference implementation..." << endl;
    
    int best_matches = 0;
    int best_start = 0;
    
    for (int start = 0; start < 200; start++) {
        int matches = try_decode(result.data_symbols, start, false);
        if (matches > best_matches) {
            best_matches = matches;
            best_start = start;
        }
    }
    
    cout << "\nBest: start=" << best_start << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    if (best_matches > 0) {
        try_decode(result.data_symbols, best_start, true);
    }
    
    return 0;
}
