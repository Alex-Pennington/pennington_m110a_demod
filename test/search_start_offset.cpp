/**
 * Search for correct start offset
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

class MyScrambler {
public:
    MyScrambler() { reset(); }
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
    
    // Generate expected TX symbols
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
    
    MyScrambler scr;
    vector<int> expected;
    for (int frame = 0; frame < 30; frame++) {
        for (int i = 0; i < 32; i++) {
            int tribit = (lvr.fetch() << 2) | (lvr.fetch() << 1) | lvr.fetch();
            expected.push_back((mgd3[tribit] + scr.next()) % 8);
        }
        for (int i = 0; i < 16; i++) expected.push_back(scr.next());
    }
    
    // Get received symbols
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    vector<int> received;
    for (auto& sym : result.data_symbols) received.push_back(decode_8psk_position(sym));
    
    cout << "Expected symbols: " << expected.size() << endl;
    cout << "Received symbols: " << received.size() << endl;
    
    // Try different start offsets
    cout << "\nSearching for best start offset..." << endl;
    int best_offset = 0, best_matches = 0;
    
    for (int offset = 0; offset < 100; offset++) {
        int matches = 0;
        for (size_t i = 0; i < expected.size() && i + offset < received.size(); i++) {
            if (expected[i] == received[i + offset]) matches++;
        }
        if (matches > best_matches) {
            best_matches = matches;
            best_offset = offset;
            cout << "Offset " << offset << ": " << matches << "/" << expected.size() << " matches (NEW BEST)" << endl;
        }
    }
    
    cout << "\nBest offset: " << best_offset << " with " << best_matches << "/" << expected.size() << " matches" << endl;
    
    // Show per-frame match rates at best offset
    cout << "\nPer-frame match rates at offset " << best_offset << ":" << endl;
    for (int frame = 0; frame < 30 && frame * 48 < (int)expected.size(); frame++) {
        int frame_matches = 0;
        int start = frame * 48;
        for (int i = 0; i < 48 && start + i < (int)expected.size() && start + i + best_offset < (int)received.size(); i++) {
            if (expected[start + i] == received[start + i + best_offset]) frame_matches++;
        }
        printf("Frame %2d: %2d/48 (%.0f%%)\n", frame, frame_matches, frame_matches * 100.0 / 48);
    }
    
    return 0;
}
