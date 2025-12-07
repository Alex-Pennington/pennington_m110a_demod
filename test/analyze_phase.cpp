/**
 * Analyze phase behavior of symbols
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

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    RefDataScrambler scr;
    
    // For probe symbols, we expect: received_angle = scrambler_angle
    // So: phase_error = received_angle - scrambler_angle should be ~0
    
    // Probe is at positions 32-47, 80-95, etc.
    cout << "Probe phase error analysis:" << endl;
    cout << "Frame  Idx   Scr  Recv  Err(deg)" << endl;
    
    for (int frame = 0; frame < 5; frame++) {
        int probe_start = frame * 48 + 32;
        
        // Advance scrambler to probe position
        RefDataScrambler frame_scr;
        for (int i = 0; i < probe_start; i++) frame_scr.next();
        
        float total_err = 0;
        for (int i = 0; i < 16 && probe_start + i < (int)result.data_symbols.size(); i++) {
            complex<float> sym = result.data_symbols[probe_start + i];
            int scr_val = frame_scr.next();
            
            float recv_angle = atan2(sym.imag(), sym.real());
            float scr_angle = scr_val * M_PI / 4.0f;
            
            float err = recv_angle - scr_angle;
            // Wrap to [-pi, pi]
            while (err > M_PI) err -= 2*M_PI;
            while (err < -M_PI) err += 2*M_PI;
            
            total_err += err;
            
            if (i < 4) {  // Show first 4 of each frame
                printf("  %d    %3d    %d    %5.1f°  %5.1f°\n", 
                       frame, probe_start+i, scr_val, recv_angle*180/M_PI, err*180/M_PI);
            }
        }
        printf("  Frame %d avg error: %.1f°\n\n", frame, (total_err/16)*180/M_PI);
    }
    
    // Check if there's a systematic phase offset
    cout << "\n=== Global phase analysis ===" << endl;
    
    // At probe positions, received = scrambler, so we can compute phase offset
    vector<float> phase_offsets;
    for (int frame = 0; frame < 30 && frame * 48 + 32 < (int)result.data_symbols.size(); frame++) {
        int probe_start = frame * 48 + 32;
        RefDataScrambler frame_scr;
        for (int i = 0; i < probe_start; i++) frame_scr.next();
        
        for (int i = 0; i < 16 && probe_start + i < (int)result.data_symbols.size(); i++) {
            complex<float> sym = result.data_symbols[probe_start + i];
            int scr_val = frame_scr.next();
            
            float recv_angle = atan2(sym.imag(), sym.real());
            float scr_angle = scr_val * M_PI / 4.0f;
            
            float err = recv_angle - scr_angle;
            while (err > M_PI) err -= 2*M_PI;
            while (err < -M_PI) err += 2*M_PI;
            
            phase_offsets.push_back(err);
        }
    }
    
    float sum = 0, sum_sq = 0;
    for (float e : phase_offsets) {
        sum += e;
        sum_sq += e*e;
    }
    float mean = sum / phase_offsets.size();
    float stddev = sqrt(sum_sq / phase_offsets.size() - mean*mean);
    
    cout << "Mean phase offset: " << (mean * 180 / M_PI) << "°" << endl;
    cout << "Stddev: " << (stddev * 180 / M_PI) << "°" << endl;
    
    return 0;
}
