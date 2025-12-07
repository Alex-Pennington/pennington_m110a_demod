/**
 * Decode with correct MIL-STD-188-110A convolutional interleaver
 * 
 * For M2400S:
 * - row_nr = 40, col_nr = 72
 * - row_inc = 9, col_inc = 55
 * - Load: row += 1, col += col_inc
 * - Fetch: row += row_inc, col += 1
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <cstring>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

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

class ConvDeinterleaver {
public:
    ConvDeinterleaver(int rows, int cols, int row_inc, int col_inc) 
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0);
        reset();
    }
    
    void reset() {
        load_row_ = 0;
        load_col_ = 0;
        load_col_last_ = 0;
        fetch_row_ = 0;
        fetch_col_ = 0;
        fill(array_.begin(), array_.end(), 0);
    }
    
    void load(int8_t bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + 1) % rows_;
        load_col_ = (load_col_ + col_inc_) % cols_;
        
        if (load_row_ == 0) {
            load_col_ = (load_col_last_ + 1) % cols_;
            load_col_last_ = load_col_;
        }
    }
    
    int8_t fetch() {
        int8_t bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        
        if (fetch_row_ == 0) {
            fetch_col_ = (fetch_col_ + 1) % cols_;
        }
        
        return bit;
    }
    
private:
    int rows_, cols_, row_inc_, col_inc_;
    int load_row_, load_col_, load_col_last_;
    int fetch_row_, fetch_col_;
    vector<int8_t> array_;
};

int try_decode(const vector<complex<float>>& data_symbols, int start) {
    const int UNKNOWN_LEN = 32;
    const int KNOWN_LEN = 16;
    
    // M2400S convolutional interleaver parameters
    const int ROWS = 40;
    const int COLS = 72;
    const int ROW_INC = 9;
    const int COL_INC = COLS - 17;  // 55
    const int BLOCK_BITS = ROWS * COLS;  // 2880 bits
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;  // 960 data symbols
    
    RefScrambler scr;
    ConvDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    int idx = start;
    int symbols_processed = 0;
    
    // Extract and deinterleave
    while (symbols_processed < BLOCK_SYMBOLS && idx < (int)data_symbols.size()) {
        // 32 unknown (data) symbols
        for (int i = 0; i < UNKNOWN_LEN && symbols_processed < BLOCK_SYMBOLS && idx < (int)data_symbols.size(); i++) {
            complex<float> sym = data_symbols[idx++];
            uint8_t scr_val = scr.next_tribit();
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            
            int pos = decode_8psk_position(sym);
            int tribit = gray_map[pos];
            
            // Load 3 soft bits into deinterleaver
            deint.load((tribit & 4) ? -127 : 127);
            deint.load((tribit & 2) ? -127 : 127);
            deint.load((tribit & 1) ? -127 : 127);
            
            symbols_processed++;
        }
        
        // Skip 16 known (probe) symbols
        for (int i = 0; i < KNOWN_LEN && idx < (int)data_symbols.size(); i++) {
            idx++;
            scr.next_tribit();
        }
    }
    
    if (symbols_processed < BLOCK_SYMBOLS) {
        return 0;
    }
    
    // Fetch from deinterleaver
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) {
        soft.push_back(deint.fetch());
    }
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < EXPECTED_LEN; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (byte == (uint8_t)EXPECTED[i/8]) matches++;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    cout << "\nTrying correct convolutional interleaver..." << endl;
    
    int best_matches = 0;
    int best_start = 0;
    
    for (int start = 0; start < 200; start++) {
        int matches = try_decode(result.data_symbols, start);
        if (matches > best_matches) {
            best_matches = matches;
            best_start = start;
        }
    }
    
    cout << "\nBest: start=" << best_start << " matches=" << best_matches << "/" << EXPECTED_LEN << endl;
    
    // Show output for best
    if (best_matches > 0) {
        const int ROWS = 40, COLS = 72, ROW_INC = 9, COL_INC = 55;
        const int BLOCK_BITS = ROWS * COLS;
        
        RefScrambler scr;
        ConvDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
        const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        
        int idx = best_start;
        int symbols_processed = 0;
        
        while (symbols_processed < 960 && idx < (int)result.data_symbols.size()) {
            for (int i = 0; i < 32 && symbols_processed < 960 && idx < (int)result.data_symbols.size(); i++) {
                complex<float> sym = result.data_symbols[idx++];
                uint8_t scr_val = scr.next_tribit();
                sym *= polar(1.0f, -scr_val * (float)(M_PI / 4.0f));
                int pos = decode_8psk_position(sym);
                int tribit = gray_map[pos];
                deint.load((tribit & 4) ? -127 : 127);
                deint.load((tribit & 2) ? -127 : 127);
                deint.load((tribit & 1) ? -127 : 127);
                symbols_processed++;
            }
            for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
                idx++; scr.next_tribit();
            }
        }
        
        vector<int8_t> soft;
        for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch());
        
        ViterbiDecoder viterbi;
        vector<uint8_t> decoded;
        viterbi.decode_block(soft, decoded, true);
        
        string output;
        for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
            output += (byte >= 32 && byte < 127) ? (char)byte : '.';
        }
        cout << "Output: " << output.substr(0, 70) << endl;
    }
    
    return 0;
}
