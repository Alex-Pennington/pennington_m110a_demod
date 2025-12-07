/**
 * Check where data starts after preamble
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"

using namespace m110a;
using namespace std;

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
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

int main(int argc, char** argv) {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    if (argc > 1) filename = argv[1];
    
    cout << "=== Data Start Check ===" << endl;
    
    auto samples = read_pcm(filename);
    cout << "File: " << filename << endl;
    cout << "Total samples: " << samples.size() << endl;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "\nMode: " << result.mode_name << endl;
    cout << "Preamble start: sample " << result.start_sample << endl;
    
    // Preamble length for short interleave
    // 3 preamble frames × 480 symbols = 1440 symbols
    int preamble_symbols = 1440;
    int sps = 20;  // 48000 / 2400
    
    int expected_data_start = result.start_sample + preamble_symbols * sps;
    cout << "Preamble symbols: " << preamble_symbols << endl;
    cout << "Expected data start: sample " << expected_data_start << " (t=" 
         << expected_data_start/48000.0 << "s)" << endl;
    
    // The file duration is ~1.4 sec
    // Preamble should be about 1440/2400 = 0.6 sec
    // Data should start around 0.6 sec
    
    cout << "\nFile duration: " << samples.size()/48000.0 << " sec" << endl;
    cout << "Preamble duration: " << 1440/2400.0 << " sec" << endl;
    cout << "Expected data duration: " << (samples.size()/48000.0 - 1440/2400.0) << " sec" << endl;
    
    // Check where MSDMTDecoder thinks data starts
    cout << "\nData symbols extracted: " << result.data_symbols.size() << endl;
    
    // Expected data for 54 bytes:
    // 54 bytes = 432 bits = 216 dibits (for QPSK) or 144 tribits (for 8-PSK)
    // With rate 1/2 FEC: 864 bits 
    // With 40x36 interleave block: 1440 bits
    // 1440 bits / 3 = 480 8-PSK symbols
    // Plus need to account for frames: 40 symbols per frame (20 data + 20 probe)
    // So 480/20 = 24 frames, but with probes: 24 * 40 = 960 total symbols
    
    cout << "\nFor 54 bytes (432 bits):" << endl;
    cout << "  After FEC: 864 + 12 flush = 876 bits" << endl;
    cout << "  Interleave block: 1440 bits" << endl;
    cout << "  8-PSK symbols needed: 480" << endl;
    cout << "  Including probes (20+20): 960 symbols" << endl;
    
    return 0;
}
