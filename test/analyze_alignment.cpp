/**
 * Analyze alignment between received symbols and scrambler
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
    
    // Convert to positions
    vector<int> positions;
    for (auto& sym : result.data_symbols) {
        positions.push_back(decode_8psk_position(sym));
    }
    
    // Generate scrambler sequence
    RefDataScrambler scr;
    int scrambler[160];
    for (int i = 0; i < 160; i++) scrambler[i] = scr.next();
    
    cout << "\nScrambler first 40: ";
    for (int i = 0; i < 40; i++) cout << scrambler[i];
    cout << endl;
    
    // M2400S: 32 unknown + 16 known
    // Probe symbols are at positions: 32, 80, 128, ... (every 48 symbols)
    // The probe is transmitted as scrambler value (data input = 0)
    
    cout << "\n=== Checking probe positions ===" << endl;
    cout << "Expected probe pattern at start of frame: data=0 means transmitted = scrambler" << endl;
    
    // Check if probe at position 32 (after first 32 data symbols)
    for (int probe_start = 28; probe_start < 40; probe_start++) {
        int matches = 0;
        for (int i = 0; i < 16 && probe_start + i < (int)positions.size(); i++) {
            int expected_scr_idx = (32 + i) % 160;  // scrambler position for probe
            if (positions[probe_start + i] == scrambler[expected_scr_idx]) matches++;
        }
        if (matches >= 12) {
            cout << "Probe at " << probe_start << ": " << matches << "/16 matches (scr_idx start=" << 32 << ")" << endl;
        }
    }
    
    // Actually, the scrambler runs continuously, so probe uses current scrambler position
    // Let me search for where the probes might be
    cout << "\n=== Searching for probe patterns ===" << endl;
    
    for (int start = 0; start < min(200, (int)positions.size()); start++) {
        // Check if this position starts a run matching scrambler[0:15]
        int matches = 0;
        for (int i = 0; i < 16 && start + i < (int)positions.size(); i++) {
            if (positions[start + i] == scrambler[i]) matches++;
        }
        if (matches >= 14) {
            cout << "Position " << start << " matches scr[0:15]: " << matches << "/16" << endl;
        }
    }
    
    // The key insight: position 1440 matches scrambler[0:39] exactly
    // This means at position 1440, the received symbol = scrambler value
    // Therefore data input was 0 (probe symbol) AND scrambler was at position 0
    
    // If scrambler is continuous from start, position 1440 would use scrambler[1440 % 160] = scrambler[0]
    cout << "\n1440 % 160 = " << (1440 % 160) << endl;
    cout << "This confirms scrambler alignment at position 1440!" << endl;
    
    // So if scrambler is at position 0 when we're at position 1440,
    // then at position 0, scrambler should be at 160 - (1440 % 160) = 0
    // Wait, that's also 0! So scrambler DOES start at position 0!
    
    // Let me check what's at position 0-32 (should be first 32 data symbols)
    cout << "\nFirst 32 symbols (should be data):" << endl;
    for (int i = 0; i < 32 && i < (int)positions.size(); i++) {
        cout << positions[i];
    }
    cout << endl;
    
    // If these are scrambled data, descrambling should give meaningful data
    cout << "\nDescrambled first 32:" << endl;
    for (int i = 0; i < 32 && i < (int)positions.size(); i++) {
        int descr = (positions[i] - scrambler[i] + 8) % 8;
        cout << descr;
    }
    cout << endl;
    
    return 0;
}
