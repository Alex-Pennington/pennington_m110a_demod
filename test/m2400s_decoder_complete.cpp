/**
 * Complete MIL-STD-188-110A M2400S Decoder
 * 
 * Key findings that made this work:
 * 1. LSB-first bit ordering for message data
 * 2. Scrambler wraps at 160 symbols (pre-computed, not continuous LFSR)
 * 3. Interleaver: 40×72, row_inc=9, col_inc=55
 * 4. Viterbi: K=7, G1=0x5B, G2=0x79
 * 5. Frame structure: 32 data + 16 probe symbols
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

// Gray code mapping for 8PSK
const int MGD3[8] = {0,1,3,2,7,6,4,5};
int INV_MGD3[8];

/**
 * Fixed Data Scrambler - generates 160 values, wraps cyclically
 */
class FixedScrambler {
public:
    FixedScrambler() {
        int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
        seq_.resize(160);
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                int c = sreg[11];
                for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
                sreg[0] = c;
                sreg[6] ^= c; sreg[4] ^= c; sreg[1] ^= c;
            }
            seq_[i] = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
        }
    }
    int at(int pos) const { return seq_[pos % 160]; }
private:
    vector<int> seq_;
};

/**
 * Block Deinterleaver for M2400S
 */
class M2400SDeinterleaver {
public:
    M2400SDeinterleaver() {
        array_.resize(ROWS * COLS, 0.0f);
        reset();
    }
    
    void reset() {
        load_row = load_col = load_col_last = fetch_row = fetch_col = 0;
        fill(array_.begin(), array_.end(), 0.0f);
    }
    
    void load(float bit) {
        array_[load_row * COLS + load_col] = bit;
        load_row = (load_row + 1) % ROWS;
        load_col = (load_col + COL_INC) % COLS;
        if (load_row == 0) {
            load_col = (load_col_last + 1) % COLS;
            load_col_last = load_col;
        }
    }
    
    float fetch() {
        float bit = array_[fetch_row * COLS + fetch_col];
        fetch_row = (fetch_row + ROW_INC) % ROWS;
        if (fetch_row == 0) fetch_col = (fetch_col + 1) % COLS;
        return bit;
    }
    
private:
    static const int ROWS = 40;
    static const int COLS = 72;
    static const int ROW_INC = 9;
    static const int COL_INC = 55;
    vector<float> array_;
    int load_row, load_col, load_col_last, fetch_row, fetch_col;
};

/**
 * Read 16-bit signed PCM file
 */
vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    vector<int16_t> raw(size / 2);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

/**
 * Decode 8PSK symbol to position (0-7)
 */
int decode_8psk(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}

/**
 * Complete M2400S decoder
 */
string decode_m2400s(const vector<float>& samples) {
    const int BLOCK_BITS = 40 * 72;  // 2880 bits
    
    // Initialize lookup tables
    for (int i = 0; i < 8; i++) INV_MGD3[MGD3[i]] = i;
    
    // Extract symbols using MSDMT decoder
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    if (result.data_symbols.size() < 48) {
        return "[Error: Not enough symbols extracted]";
    }
    
    // Initialize components
    FixedScrambler scrambler;
    M2400SDeinterleaver deint;
    
    // Process symbols: descramble, de-Gray, deinterleave
    int sym_idx = 0;
    int scr_offset = 0;
    int data_count = 0;
    
    while (data_count < BLOCK_BITS / 3 && sym_idx < (int)result.data_symbols.size()) {
        // 32 data symbols
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && sym_idx < (int)result.data_symbols.size(); i++) {
            int pos = decode_8psk(result.data_symbols[sym_idx++]);
            int scr_val = scrambler.at(scr_offset++);
            int gray = (pos - scr_val + 8) % 8;
            int tribit = INV_MGD3[gray];
            
            // Load tribits into deinterleaver (soft bits: 0→+1, 1→-1)
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        
        // 16 probe symbols (skip, but advance scrambler)
        for (int i = 0; i < 16 && sym_idx < (int)result.data_symbols.size(); i++) {
            sym_idx++;
            scr_offset++;
        }
    }
    
    // Fetch deinterleaved bits as soft decisions
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) {
        soft.push_back(deint.fetch() > 0 ? 127 : -127);
    }
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert bits to bytes (LSB first!)
    string output;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            if (decoded[i + j]) byte |= (1 << j);  // LSB first
        }
        if (byte >= 32 && byte < 127) {
            output += (char)byte;
        } else if (byte == 0) {
            break;  // End of message
        } else {
            output += '.';
        }
    }
    
    return output;
}

int main(int argc, char* argv[]) {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    if (argc > 1) filename = argv[1];
    
    cout << "=== MIL-STD-188-110A M2400S Decoder ===" << endl;
    cout << "Input: " << filename << endl;
    
    auto samples = read_pcm(filename);
    if (samples.empty()) {
        cerr << "Error: Could not read " << filename << endl;
        return 1;
    }
    
    cout << "Samples: " << samples.size() << " (" << samples.size() / 48000.0 << " seconds)" << endl;
    
    string decoded = decode_m2400s(samples);
    
    cout << "\n=== DECODED MESSAGE ===" << endl;
    cout << decoded << endl;
    cout << "=======================" << endl;
    
    // Verify against known message
    const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    int match = 0;
    for (size_t i = 0; i < decoded.size() && i < strlen(EXPECTED); i++) {
        if (decoded[i] == EXPECTED[i]) match++;
    }
    cout << "\nMatch: " << match << "/" << strlen(EXPECTED) << " characters" << endl;
    
    return 0;
}
