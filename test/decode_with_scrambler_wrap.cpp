/**
 * Decode with scrambler wrapping at 160
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <cstdint>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"

using namespace std;
using namespace m110a;

const int mgd3[8] = {0,1,3,2,7,6,4,5};
int inv_mgd3[8];

class MyEncoder {
public:
    MyEncoder() : state(0) {}
    pair<int,int> encode(int in) {
        state = state >> 1;
        if (in) state |= 0x40;
        return {__builtin_popcount(state & 0x5B) & 1, __builtin_popcount(state & 0x79) & 1};
    }
private:
    int state;
};

class MyInterleaver {
public:
    MyInterleaver(int row_nr, int col_nr, int row_inc, int col_inc) 
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

class MyDeinterleaver {
public:
    MyDeinterleaver(int row_nr, int col_nr, int row_inc, int col_inc) 
        : row_nr(row_nr), col_nr(col_nr), row_inc(row_inc), col_inc(col_inc) {
        array.resize(row_nr * col_nr, 0.0f);
        load_row = load_col = load_col_last = fetch_row = fetch_col = 0;
    }
    void load(float bit) {
        array[load_row * col_nr + load_col] = bit;
        load_row = (load_row + 1) % row_nr;
        load_col = (load_col + col_inc) % col_nr;
        if (load_row == 0) { load_col = (load_col_last + 1) % col_nr; load_col_last = load_col; }
    }
    float fetch() {
        float bit = array[fetch_row * col_nr + fetch_col];
        fetch_row = (fetch_row + row_inc) % row_nr;
        if (fetch_row == 0) fetch_col = (fetch_col + 1) % col_nr;
        return bit;
    }
private:
    int row_nr, col_nr, row_inc, col_inc;
    int load_row, load_col, load_col_last, fetch_row, fetch_col;
    vector<float> array;
};

// Generate the 160-symbol scrambler sequence (pre-computed like TX does)
vector<int> generate_scrambler_sequence() {
    int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
    vector<int> seq;
    
    for (int i = 0; i < 160; i++) {
        for (int j = 0; j < 8; j++) {
            int c = sreg[11];
            for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
            sreg[0] = c;
            sreg[6] ^= c; sreg[4] ^= c; sreg[1] ^= c;
        }
        seq.push_back((sreg[2] << 2) + (sreg[1] << 1) + sreg[0]);
    }
    
    return seq;
}

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

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    const char* MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Pre-compute 160-symbol scrambler sequence (like TX does)
    vector<int> scrambler_seq = generate_scrambler_sequence();
    
    cout << "=== Decoding with scrambler wrap at 160 ===" << endl;
    cout << "First 20 scrambler values: ";
    for (int i = 0; i < 20; i++) cout << scrambler_seq[i];
    cout << endl;
    
    // Generate expected TX symbols for comparison
    vector<int> msg_bits;
    for (const char* p = MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) msg_bits.push_back((c >> i) & 1);
    }
    
    MyEncoder enc;
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
    
    MyInterleaver lvr(ROWS, COLS, ROW_INC, COL_INC);
    for (int bit : encoded) lvr.load(bit);
    
    // Generate expected symbols using wrapped scrambler
    vector<int> expected;
    int scr_offset = 0;
    for (int frame = 0; frame < 30; frame++) {
        for (int i = 0; i < 32; i++) {
            int tribit = (lvr.fetch() << 2) | (lvr.fetch() << 1) | lvr.fetch();
            int gray = mgd3[tribit];
            expected.push_back((gray + scrambler_seq[scr_offset % 160]) % 8);
            scr_offset++;
        }
        for (int i = 0; i < 16; i++) {
            expected.push_back(scrambler_seq[scr_offset % 160]);
            scr_offset++;
        }
    }
    
    // Get received symbols
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Compare symbols
    int total_match = 0;
    for (size_t i = 0; i < min(expected.size(), received.size()); i++) {
        if (expected[i] == received[i]) total_match++;
    }
    cout << "Total symbol matches: " << total_match << "/" << min(expected.size(), received.size()) << endl;
    
    // Decode received symbols using wrapped scrambler
    MyDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0, data_count = 0;
    scr_offset = 0;
    
    while (data_count < BLOCK_BITS / 3 && idx < (int)received.size()) {
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && idx < (int)received.size(); i++) {
            int pos = received[idx++];
            int scr_val = scrambler_seq[scr_offset % 160];
            scr_offset++;
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        for (int i = 0; i < 16 && idx < (int)received.size(); i++) {
            idx++;
            scr_offset++;
        }
    }
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    // Compare to expected encoded bits
    int bit_match = 0;
    for (int i = 0; i < BLOCK_BITS; i++) {
        int rx_bit = soft[i] > 0 ? 0 : 1;
        if (rx_bit == encoded[i]) bit_match++;
    }
    cout << "Encoded bit matches: " << bit_match << "/" << BLOCK_BITS << endl;
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes (LSB first)
    string output;
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < 54; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) if (decoded[i + j]) byte |= (1 << j);
        output += (byte >= 32 && byte < 127) ? (char)byte : '.';
        if (byte == (uint8_t)MSG[i/8]) matches++;
    }
    
    cout << "\n=== RESULT ===" << endl;
    cout << "Expected: " << MSG << endl;
    cout << "Decoded:  " << output << endl;
    cout << "Matches:  " << matches << "/54" << endl;
    
    return 0;
}
