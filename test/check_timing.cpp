/**
 * Check symbol timing by examining symbol magnitude and angle quality
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

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "=== Symbol Timing Analysis ===" << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    // Analyze angle distribution relative to expected 8PSK positions
    // Expected angles: 0, 45, 90, 135, 180, 225, 270, 315 degrees
    
    vector<float> angle_errors;
    float sum_mag = 0, sum_error = 0;
    
    for (size_t i = 0; i < result.data_symbols.size(); i++) {
        complex<float> sym = result.data_symbols[i];
        float mag = abs(sym);
        float angle = atan2(sym.imag(), sym.real()) * 180.0f / M_PI;
        
        // Find nearest 8PSK angle
        float expected = round(angle / 45.0f) * 45.0f;
        float error = angle - expected;
        if (error > 22.5f) error -= 45.0f;
        if (error < -22.5f) error += 45.0f;
        
        angle_errors.push_back(error);
        sum_mag += mag;
        sum_error += abs(error);
    }
    
    cout << "\nAverage magnitude: " << sum_mag / result.data_symbols.size() << endl;
    cout << "Average angle error: " << sum_error / result.data_symbols.size() << " degrees" << endl;
    
    // Histogram of angle errors
    int hist[21] = {0};  // -10 to +10 degrees in 1-degree bins
    for (float e : angle_errors) {
        int bin = (int)(e + 10.5f);
        if (bin >= 0 && bin < 21) hist[bin]++;
    }
    
    cout << "\nAngle error histogram:" << endl;
    for (int i = 0; i < 21; i++) {
        printf("%+3d°: %5d ", i - 10, hist[i]);
        for (int j = 0; j < hist[i] / 10; j++) cout << '*';
        cout << endl;
    }
    
    // Check symbols with large errors
    cout << "\nSymbols with angle error > 15 degrees:" << endl;
    cout << "Idx   Mag    Angle   Error" << endl;
    int large_errors = 0;
    for (size_t i = 0; i < result.data_symbols.size() && large_errors < 20; i++) {
        if (abs(angle_errors[i]) > 15.0f) {
            complex<float> sym = result.data_symbols[i];
            printf("%4zu  %5.3f  %6.1f°  %+5.1f°\n", 
                   i, abs(sym), atan2(sym.imag(), sym.real()) * 180.0f / M_PI, angle_errors[i]);
            large_errors++;
        }
    }
    
    return 0;
}
