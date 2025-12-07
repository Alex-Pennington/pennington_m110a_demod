/**
 * Decode with phase tracking using probes
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

const int mgd3[8] = {0,1,3,2,7,6,4,5};
int inv_mgd3[8];

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

class RefDeinterleaver {
public:
    RefDeinterleaver(int rows, int cols, int row_inc, int col_inc)
        : rows_(rows), cols_(cols), row_inc_(row_inc), col_inc_(col_inc) {
        array_.resize(rows * cols, 0.0f);
        load_row_ = load_col_ = load_col_last_ = fetch_row_ = fetch_col_ = 0;
    }
    void load(float bit) {
        array_[load_row_ * cols_ + load_col_] = bit;
        load_row_ = (load_row_ + 1) % rows_;
        load_col_ = (load_col_ + col_inc_) % cols_;
        if (load_row_ == 0) { load_col_ = (load_col_last_ + 1) % cols_; load_col_last_ = load_col_; }
    }
    float fetch() {
        float bit = array_[fetch_row_ * cols_ + fetch_col_];
        fetch_row_ = (fetch_row_ + row_inc_) % rows_;
        if (fetch_row_ == 0) fetch_col_ = (fetch_col_ + 1) % cols_;
        return bit;
    }
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<float> array_;
    int load_row_, load_col_, load_col_last_, fetch_row_, fetch_col_;
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

// Constellation points
const complex<float> constellation[8] = {
    {1.0f, 0.0f},
    {0.707f, 0.707f},
    {0.0f, 1.0f},
    {-0.707f, 0.707f},
    {-1.0f, 0.0f},
    {-0.707f, -0.707f},
    {0.0f, -1.0f},
    {0.707f, -0.707f}
};

// Find nearest constellation point
int nearest_point(complex<float> sym) {
    int best = 0;
    float best_dist = abs(sym - constellation[0]);
    for (int i = 1; i < 8; i++) {
        float dist = abs(sym - constellation[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Symbols: " << result.data_symbols.size() << endl;
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    const int BLOCK_SYMBOLS = BLOCK_BITS / 3;
    
    // Decode with phase tracking
    cout << "\n=== Decode with phase tracking ===" << endl;
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    float phase_offset = 0;  // Track cumulative phase error
    int idx = 0, data_count = 0;
    
    while (data_count < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size()) {
        // Estimate phase from upcoming probe (look ahead)
        if (idx + 32 + 8 < (int)result.data_symbols.size()) {
            // Look at probe symbols to estimate phase
            RefDataScrambler probe_scr;
            for (int i = 0; i < idx + 32; i++) probe_scr.next();
            
            float phase_sum = 0;
            int count = 0;
            for (int i = 0; i < 16 && idx + 32 + i < (int)result.data_symbols.size(); i++) {
                complex<float> sym = result.data_symbols[idx + 32 + i];
                int scr_val = probe_scr.next();
                
                // Expected constellation point for probe
                complex<float> expected = constellation[scr_val];
                
                // Compute phase difference
                float recv_angle = atan2(sym.imag(), sym.real());
                float exp_angle = atan2(expected.imag(), expected.real());
                float diff = recv_angle - exp_angle;
                
                // Wrap to [-pi, pi]
                while (diff > M_PI) diff -= 2*M_PI;
                while (diff < -M_PI) diff += 2*M_PI;
                
                phase_sum += diff;
                count++;
            }
            phase_offset = phase_sum / count;
        }
        
        // Apply phase correction and process 32 data symbols
        complex<float> rot = polar(1.0f, -phase_offset);
        
        for (int i = 0; i < 32 && data_count < BLOCK_SYMBOLS && idx < (int)result.data_symbols.size(); i++) {
            complex<float> sym = result.data_symbols[idx++] * rot;
            int scr_val = scr.next();
            
            // Descramble
            complex<float> scr_conj = conj(constellation[scr_val]);
            complex<float> descr = sym * scr_conj;
            
            // Find nearest constellation point (gives us the gray code)
            int gray = nearest_point(descr);
            int tribit = inv_mgd3[gray];
            
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        
        // Skip 16 probe symbols, advance scrambler
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    // Fetch and decode
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        if (i/8 < strlen(EXPECTED) && byte == (uint8_t)EXPECTED[i/8]) matches++;
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
    }
    
    cout << "Output: " << output.substr(0, 80) << endl;
    cout << "Matches: " << matches << "/" << strlen(EXPECTED) << endl;
    
    return 0;
}
