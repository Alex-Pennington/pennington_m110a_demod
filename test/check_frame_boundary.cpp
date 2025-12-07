/**
 * Check if frame boundary is at position 32
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
    
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Generate scrambler
    RefDataScrambler scr;
    int scrambler[200];
    for (int i = 0; i < 200; i++) scrambler[i] = scr.next();
    
    // Check frame boundaries
    cout << "\n=== Checking frame boundaries ===" << endl;
    cout << "Frame structure: 32 data + 16 probe" << endl;
    
    for (int frame = 0; frame < 5; frame++) {
        int data_start = frame * 48;
        int probe_start = frame * 48 + 32;
        int probe_scr_start = 32 + frame * 48;  // Scrambler position for probe
        
        cout << "\nFrame " << frame << ":" << endl;
        cout << "  Data at pos " << data_start << "-" << (data_start+31) << endl;
        cout << "  Probe at pos " << probe_start << "-" << (probe_start+15) << endl;
        
        // Check probe matches scrambler
        int matches = 0;
        cout << "  Probe: ";
        for (int i = 0; i < 16; i++) {
            int pos = decode_8psk_position(result.data_symbols[probe_start + i]);
            int scr_idx = (probe_scr_start + i) % 160;
            cout << pos;
            if (pos == scrambler[scr_idx]) matches++;
        }
        cout << " (scr[" << (probe_scr_start % 160) << "]: ";
        for (int i = 0; i < 16; i++) cout << scrambler[(probe_scr_start + i) % 160];
        cout << ") = " << matches << "/16 matches" << endl;
    }
    
    return 0;
}
