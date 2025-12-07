/**
 * Search for optimal sample timing offset
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const float MY_PI = 3.14159265358979323846f;
const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
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
    void reset() {
        fill(array_.begin(), array_.end(), 0.0f);
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
    int pos = static_cast<int>(round(angle * 4.0f / MY_PI));
    return ((pos % 8) + 8) % 8;
}

// Generate SRRC filter taps
vector<float> generate_srrc(float alpha, int span, float sps) {
    int length = span * (int)sps + 1;
    vector<float> taps(length);
    float sum = 0;
    int center = length / 2;
    
    for (int i = 0; i < length; i++) {
        float t = (i - center) / sps;
        float val;
        
        if (abs(t) < 1e-6f) {
            val = 1.0f + alpha * (4.0f / MY_PI - 1.0f);
        } else if (abs(abs(t) - 1.0f / (4.0f * alpha)) < 1e-6f) {
            val = alpha / sqrt(2.0f) * ((1 + 2.0f / MY_PI) * sin(MY_PI / (4 * alpha)) +
                                         (1 - 2.0f / MY_PI) * cos(MY_PI / (4 * alpha)));
        } else {
            float num = sin(MY_PI * t * (1 - alpha)) + 4 * alpha * t * cos(MY_PI * t * (1 + alpha));
            float den = MY_PI * t * (1 - (4 * alpha * t) * (4 * alpha * t));
            val = num / den;
        }
        taps[i] = val;
        sum += val;
    }
    
    for (auto& t : taps) t /= sum;
    return taps;
}

int try_decode_timing(const vector<float>& samples, int timing_offset) {
    const float sample_rate = 48000.0f;
    const float baud_rate = 2400.0f;
    const float carrier_freq = 1800.0f;
    const int sps = (int)(sample_rate / baud_rate);
    
    // Downconvert
    vector<complex<float>> bb(samples.size());
    float phase = 0;
    float phase_inc = 2.0f * MY_PI * carrier_freq / sample_rate;
    for (size_t i = 0; i < samples.size(); i++) {
        bb[i] = complex<float>(samples[i] * cos(phase), -samples[i] * sin(phase));
        phase += phase_inc;
        if (phase > 2*MY_PI) phase -= 2*MY_PI;
    }
    
    // Matched filter
    auto taps = generate_srrc(0.35f, 6, sps);
    int half = taps.size() / 2;
    vector<complex<float>> filtered(bb.size());
    for (size_t i = 0; i < bb.size(); i++) {
        complex<float> sum(0, 0);
        for (size_t j = 0; j < taps.size(); j++) {
            int idx = (int)i - half + j;
            if (idx >= 0 && idx < (int)bb.size()) sum += bb[idx] * taps[j];
        }
        filtered[i] = sum;
    }
    
    // Find preamble start (rough search)
    int preamble_start = 257;  // Known from previous runs
    
    // Extract data symbols with timing offset
    int data_start = preamble_start + 1440 * sps + timing_offset;
    
    vector<complex<float>> symbols;
    for (int idx = data_start; idx < (int)filtered.size(); idx += sps) {
        symbols.push_back(filtered[idx]);
    }
    
    // Decode
    const int ROWS = 40, COLS = 72;
    const int ROW_INC = 9, COL_INC = 55;
    const int BLOCK_BITS = ROWS * COLS;
    
    RefDataScrambler scr;
    RefDeinterleaver deint(ROWS, COLS, ROW_INC, COL_INC);
    
    int idx = 0, data_count = 0;
    while (data_count < BLOCK_BITS / 3 && idx < (int)symbols.size()) {
        for (int i = 0; i < 32 && data_count < BLOCK_BITS / 3 && idx < (int)symbols.size(); i++) {
            int pos = decode_8psk_position(symbols[idx++]);
            int scr_val = scr.next();
            int gray = (pos - scr_val + 8) % 8;
            int tribit = inv_mgd3[gray];
            deint.load((tribit & 4) ? -1.0f : 1.0f);
            deint.load((tribit & 2) ? -1.0f : 1.0f);
            deint.load((tribit & 1) ? -1.0f : 1.0f);
            data_count++;
        }
        for (int i = 0; i < 16 && idx < (int)symbols.size(); i++) {
            idx++;
            scr.next();
        }
    }
    
    vector<int8_t> soft;
    for (int i = 0; i < BLOCK_BITS; i++) soft.push_back(deint.fetch() > 0 ? 127 : -127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    int matches = 0;
    for (size_t i = 0; i + 8 <= decoded.size() && i/8 < 54; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) if (decoded[i + j]) byte |= (1 << j);
        if (byte == (uint8_t)TEST_MSG[i/8]) matches++;
    }
    
    return matches;
}

int main() {
    for (int i = 0; i < 8; i++) inv_mgd3[mgd3[i]] = i;
    
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    cout << "=== Sample Timing Offset Search ===" << endl;
    cout << "SPS = 20, searching timing offsets from -10 to +10 samples" << endl;
    
    int best_offset = 0;
    int best_matches = 0;
    
    for (int offset = -10; offset <= 10; offset++) {
        int matches = try_decode_timing(samples, offset);
        
        if (matches > best_matches) {
            best_matches = matches;
            best_offset = offset;
        }
        
        cout << "Offset " << offset << ": " << matches << "/54 matches";
        if (matches == best_matches && matches > 0) cout << " (BEST)";
        cout << endl;
    }
    
    cout << "\nBest timing offset: " << best_offset << " samples with " << best_matches << "/54 matches" << endl;
    
    return 0;
}
