/**
 * Trace through decode process step by step
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
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

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    cout << "=== PCM File Analysis ===" << endl;
    cout << "Total samples: " << samples.size() << endl;
    cout << "Duration at 48kHz: " << samples.size() / 48000.0f << " seconds" << endl;
    
    // At 2400 baud, SPS=20
    // Preamble: 3 frames x 480 symbols = 1440 symbols = 28800 samples
    // Data: remaining samples
    
    cout << "\n=== Expected Structure ===" << endl;
    cout << "Preamble samples: 28800 (1440 symbols at SPS=20)" << endl;
    cout << "Remaining for data: " << (samples.size() - 28800) << " samples" << endl;
    cout << "Data symbols: " << (samples.size() - 28800) / 20 << endl;
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "\n=== Decoder Results ===" << endl;
    cout << "Mode detected: " << result.mode_name << endl;
    cout << "Preamble start: sample " << result.start_sample << endl;
    cout << "Phase offset: " << result.phase_offset * 180 / M_PI << " degrees" << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Preamble symbols: " << result.preamble_symbols.size() << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Calculate where data should start
    int expected_data_start = result.start_sample + 1440 * 20;  // After 3 preamble frames
    int actual_data_start = result.start_sample + result.preamble_symbols.size() * 20;
    
    cout << "\n=== Data Start Analysis ===" << endl;
    cout << "Expected data start: sample " << expected_data_start << endl;
    cout << "Data start based on preamble symbols: sample " << actual_data_start << endl;
    
    // Check first data symbol timing
    cout << "\n=== First 10 data symbols ===" << endl;
    for (int i = 0; i < 10 && i < (int)result.data_symbols.size(); i++) {
        complex<float> sym = result.data_symbols[i];
        float mag = abs(sym);
        float angle = atan2(sym.imag(), sym.real()) * 180 / M_PI;
        int pos = decode_8psk_position(sym);
        printf("  %d: mag=%.3f angle=%6.1f pos=%d\n", i, mag, angle, pos);
    }
    
    return 0;
}
