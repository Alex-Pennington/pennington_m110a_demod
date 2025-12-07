/**
 * Decode with LSB-first bit ordering
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

class RefInterleaver {
public:
    RefInterleaver(int rows, int cols, int row_inc, int col_inc)
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
        if (fetch_row_ == 0) { fetch_col_ = (fetch_col_last_ + 1) % cols_; fetch_col_last_ = fetch_col_; }
        return bit;
    }
private:
    int rows_, cols_, row_inc_, col_inc_;
    vector<int> array_;
    int load_row_, load_col_, fetch_row_, fetch_col_, fetch_col_last_;
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
    
    // Generate expected TX symbols using LSB-first bit order
    cout << "=== Generating expected TX with LSB-first ===" << endl;
    
    // Message to bits - LSB FIRST!
    vector<int> msg_bits;
    for (const char* p = EXPECTED; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) {  // LSB first
            msg_bits.push_back((c >> i) & 1);
        }
    }
    
    cout << "First char 'T' = 0x54 = 01010100" << endl;
    cout << "LSB first bits: ";
    for (int i = 0; i < 8; i++) cout << msg_bits[i];
    cout << " (should be 00101010)" << endl;
    
    // Convolutional encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(vector<uint8_t>(msg_bits.begin(), msg_bits.end()), encoded, true);
    while (encoded.size() < BLOCK_BITS) encoded.push_back(0);
    
    // Interleave
    RefInterleaver interleaver(ROWS, COLS, ROW_INC, COL_INC);
    for (size_t i = 0; i < BLOCK_BITS; i++) interleaver.load(encoded[i]);
    
    // Generate TX symbols
    RefDataScrambler tx_scr;
    vector<int> expected_symbols;
    int tx_data_count = 0;
    
    while (tx_data_count < BLOCK_BITS / 3) {
        for (int i = 0; i < 32 && tx_data_count < BLOCK_BITS / 3; i++) {
            int tribit = (interleaver.fetch() << 2) | (interleaver.fetch() << 1) | interleaver.fetch();
            int gray = mgd3[tribit];
            int scr_val = tx_scr.next();
            expected_symbols.push_back((gray + scr_val) % 8);
            tx_data_count++;
        }
        for (int i = 0; i < 16; i++) {
            expected_symbols.push_back(tx_scr.next());
        }
    }
    
    cout << "Expected first 48: ";
    for (int i = 0; i < 48; i++) cout << expected_symbols[i];
    cout << endl;
    
    // Get received symbols
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "\nReceived first 48: ";
    for (int i = 0; i < 48 && i < (int)result.data_symbols.size(); i++) {
        cout << decode_8psk_position(result.data_symbols[i]);
    }
    cout << endl;
    
    // Compare
    int matches = 0;
    for (int i = 0; i < 48 && i < (int)result.data_symbols.size(); i++) {
        if (decode_8psk_position(result.data_symbols[i]) == expected_symbols[i]) matches++;
    }
    cout << "First 48 symbol matches: " << matches << "/48" << endl;
    
    // Now decode with LSB-first output
    cout << "\n=== Decoding received signal ===" << endl;
    
    RefDataScrambler rx_scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0, data_count = 0;
    while (data_count < BLOCK_BITS / 3 && idx < (int)result.data_symbols.size()) {
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && idx < (int)result.data_symbols.size(); i++) {
            int pos = decode_8psk_position(result.data_symbols[idx++]);
            int scr_val = rx_scr.next();
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        for (int i = 0; i < 16 && idx < (int)result.data_symbols.size(); i++) {
            idx++;
            rx_scr.next();
        }
    }
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes - LSB first!
    string output_lsb, output_msb;
    int matches_lsb = 0, matches_msb = 0;
    
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        // LSB first
        uint8_t byte_lsb = 0;
        for (int j = 0; j < 8; j++) {
            if (decoded[i + j]) byte_lsb |= (1 << j);
        }
        output_lsb += (byte_lsb >= 32 && byte_lsb < 127) ? (char)byte_lsb : '.';
        if (i/8 < EXPECTED_LEN && byte_lsb == (uint8_t)EXPECTED[i/8]) matches_lsb++;
        
        // MSB first (original)
        uint8_t byte_msb = 0;
        for (int j = 0; j < 8; j++) byte_msb = (byte_msb << 1) | (decoded[i + j] & 1);
        output_msb += (byte_msb >= 32 && byte_msb < 127) ? (char)byte_msb : '.';
        if (i/8 < EXPECTED_LEN && byte_msb == (uint8_t)EXPECTED[i/8]) matches_msb++;
    }
    
    cout << "\nLSB-first output: " << output_lsb.substr(0, 70) << endl;
    cout << "LSB-first matches: " << matches_lsb << "/" << EXPECTED_LEN << endl;
    
    cout << "\nMSB-first output: " << output_msb.substr(0, 70) << endl;
    cout << "MSB-first matches: " << matches_msb << "/" << EXPECTED_LEN << endl;
    
    return 0;
}
