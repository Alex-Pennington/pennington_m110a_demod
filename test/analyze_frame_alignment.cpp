/**
 * Analyze frame alignment by checking probe symbols
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
    
    // Generate full scrambler sequence
    RefDataScrambler scr;
    int scrambler_seq[2000];
    for (int i = 0; i < 2000; i++) scrambler_seq[i] = scr.next();
    
    cout << "=== Frame Alignment Analysis ===" << endl;
    cout << "Frame structure: 32 data + 16 probe, probe = scrambler sequence" << endl;
    cout << endl;
    
    // For each frame, check if probe symbols match scrambler
    // Frames: 0-31 data, 32-47 probe, 48-79 data, 80-95 probe, etc.
    
    cout << "Frame   Probe_Start  Probe_Matches  Best_Scr_Offset" << endl;
    
    for (int frame = 0; frame < 30 && frame * 48 + 47 < (int)result.data_symbols.size(); frame++) {
        int probe_start = frame * 48 + 32;
        
        // Find best scrambler offset for this probe
        int best_offset = 0;
        int best_matches = 0;
        
        for (int offset = 0; offset < 160; offset++) {
            int matches = 0;
            for (int i = 0; i < 16; i++) {
                int pos = decode_8psk_position(result.data_symbols[probe_start + i]);
                int expected = scrambler_seq[(offset + i) % 160];
                if (pos == expected) matches++;
            }
            if (matches > best_matches) {
                best_matches = matches;
                best_offset = offset;
            }
        }
        
        // Expected scrambler offset for this probe
        int expected_offset = (frame * 48 + 32) % 160;
        
        printf("  %2d     %4d          %2d/16            %3d (expected %3d) %s\n",
               frame, probe_start, best_matches, best_offset, expected_offset,
               best_offset == expected_offset ? "OK" : "MISMATCH");
    }
    
    // Now check if scrambler sequence is 160 symbols repeating
    cout << "\n=== Scrambler Period Verification ===" << endl;
    scr.reset();
    vector<int> seq;
    for (int i = 0; i < 320; i++) seq.push_back(scr.next());
    
    bool is_160 = true;
    for (int i = 0; i < 160; i++) {
        if (seq[i] != seq[i + 160]) is_160 = false;
    }
    cout << "Scrambler period is 160: " << (is_160 ? "YES" : "NO") << endl;
    
    return 0;
}
