/**
 * Check if data symbols decode differently than probe symbols
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

const complex<float> con_symbol[8] = {
    {1.000f, 0.000f}, {0.707f, 0.707f}, {0.000f, 1.000f}, {-0.707f, 0.707f},
    {-1.000f, 0.000f}, {-0.707f, -0.707f}, {0.000f, -1.000f}, {0.707f, -0.707f}
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

int decode_position(complex<float> sym) {
    float max_corr = -1000;
    int best = 0;
    for (int i = 0; i < 8; i++) {
        float corr = sym.real() * con_symbol[i].real() + sym.imag() * con_symbol[i].imag();
        if (corr > max_corr) { max_corr = corr; best = i; }
    }
    return best;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    RefDataScrambler scr;
    int scrambler[200];
    for (int i = 0; i < 200; i++) scrambler[i] = scr.next();
    
    cout << "Looking at frames 0-2 with frame structure 32 data + 16 probe" << endl;
    
    for (int frame = 0; frame < 3; frame++) {
        cout << "\n=== Frame " << frame << " ===" << endl;
        
        int data_start = frame * 48;
        int probe_start = frame * 48 + 32;
        
        // Data symbols
        cout << "Data (pos " << data_start << "-" << (data_start+31) << "):" << endl;
        cout << "  Positions: ";
        for (int i = 0; i < 32; i++) {
            int pos = decode_position(result.data_symbols[data_start + i]);
            cout << pos;
        }
        cout << endl;
        
        cout << "  Scrambler: ";
        for (int i = 0; i < 32; i++) {
            int scr_idx = (data_start + i) % 160;
            cout << scrambler[scr_idx];
        }
        cout << endl;
        
        cout << "  Descrambled: ";
        for (int i = 0; i < 32; i++) {
            int pos = decode_position(result.data_symbols[data_start + i]);
            int scr_idx = (data_start + i) % 160;
            int gray = (pos - scrambler[scr_idx] + 8) % 8;
            cout << gray;
        }
        cout << endl;
        
        // Probe symbols  
        cout << "Probe (pos " << probe_start << "-" << (probe_start+15) << "):" << endl;
        cout << "  Positions: ";
        for (int i = 0; i < 16; i++) {
            int pos = decode_position(result.data_symbols[probe_start + i]);
            cout << pos;
        }
        cout << endl;
        
        cout << "  Scrambler: ";
        int probe_matches = 0;
        for (int i = 0; i < 16; i++) {
            int scr_idx = (probe_start + i) % 160;
            cout << scrambler[scr_idx];
            if (decode_position(result.data_symbols[probe_start + i]) == scrambler[scr_idx]) probe_matches++;
        }
        cout << " (" << probe_matches << "/16 matches)" << endl;
    }
    
    return 0;
}
