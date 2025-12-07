/**
 * Check what parameters the decoder is using
 */
#include <iostream>
#include <fstream>
#include <vector>
#include "m110a/msdmt_decoder.h"

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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    cout << "File: " << samples.size() << " samples" << endl;
    cout << "At 48kHz: " << samples.size() / 48000.0 << " seconds" << endl;
    
    MSDMTDecoderConfig cfg;
    cout << "\nDecoder config:" << endl;
    cout << "  Sample rate: " << cfg.sample_rate << endl;
    cout << "  Carrier freq: " << cfg.carrier_freq << endl;
    cout << "  Baud rate: " << cfg.baud_rate << endl;
    cout << "  SPS: " << cfg.sample_rate / cfg.baud_rate << endl;
    
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "\nDetected mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Preamble accuracy: " << result.accuracy << "%" << endl;
    cout << "Start sample: " << result.start_sample << endl;
    cout << "Data symbols extracted: " << result.data_symbols.size() << endl;
    
    // Calculate expected data
    int sps = 20;
    int preamble_samples = 288 * sps;  // Preamble is 288 symbols
    int remaining_samples = samples.size() - result.start_sample - preamble_samples;
    int expected_data_symbols = remaining_samples / sps;
    
    cout << "\nExpected data symbols: " << expected_data_symbols << endl;
    
    // For M2400S: frame = 32 data + 16 probe = 48 symbols
    // One interleave block = 960 data symbols = 30 frames
    // Total symbols for one block = 30 * 48 = 1440
    cout << "\nM2400S parameters:" << endl;
    cout << "  Frame: 32 data + 16 probe = 48 symbols" << endl;
    cout << "  Block: 960 data symbols = 30 frames = 1440 total symbols" << endl;
    
    return 0;
}
