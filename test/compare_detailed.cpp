/**
 * Detailed comparison of expected vs received
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"

using namespace m110a;
using namespace std;

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
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    RefDataScrambler scr;
    
    cout << "=== Detailed Symbol Analysis ===" << endl;
    cout << endl;
    
    // Expected data (from manual_trace): 01433654777000534747706113266257
    string expected_data = "01433654777000534747706113266257";
    string expected_probe = "5570733373314237";
    
    cout << "Pos  Recv  ExpData  Scr  Recv-Scr  ExpData-Scr" << endl;
    cout << "---  ----  -------  ---  --------  -----------" << endl;
    
    for (int i = 0; i < 48 && i < (int)result.data_symbols.size(); i++) {
        int recv = decode_8psk_position(result.data_symbols[i]);
        int scr_val = scr.next();
        int recv_descr = (recv - scr_val + 8) % 8;
        
        int expected = 0;
        int exp_descr = 0;
        if (i < 32) {
            expected = expected_data[i] - '0';
            exp_descr = (expected - scr_val + 8) % 8;
        } else {
            expected = expected_probe[i-32] - '0';
            exp_descr = 0;  // Probe data is always 0
        }
        
        string marker = "";
        if (i == 32) marker = " <- probe start";
        
        printf("%3d    %d       %d      %d       %d            %d%s\n", 
               i, recv, expected, scr_val, recv_descr, exp_descr, marker.c_str());
    }
    
    // Calculate what "gray" values we're getting
    cout << "\n=== Descrambled Gray Analysis ===" << endl;
    scr.reset();
    
    cout << "First 32 descrambled (gray): ";
    for (int i = 0; i < 32 && i < (int)result.data_symbols.size(); i++) {
        int recv = decode_8psk_position(result.data_symbols[i]);
        int scr_val = scr.next();
        int gray = (recv - scr_val + 8) % 8;
        cout << gray;
    }
    cout << endl;
    
    cout << "Expected descrambled (gray): ";
    scr.reset();
    for (int i = 0; i < 32; i++) {
        int expected = expected_data[i] - '0';
        int scr_val = scr.next();
        int gray = (expected - scr_val + 8) % 8;
        cout << gray;
    }
    cout << endl;
    
    return 0;
}
