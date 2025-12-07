/**
 * Full spectrum analysis of PCM file
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <algorithm>

int main() {
    std::string file = "/mnt/user-data/uploads/tx_2400S_20251206_100439_978.pcm";
    
    std::ifstream f(file, std::ios::binary);
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    size_t num_samples = size / 2;
    std::vector<int16_t> raw(num_samples);
    f.read(reinterpret_cast<char*>(raw.data()), size);
    
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    
    std::cout << "File: " << file << std::endl;
    std::cout << "Samples: " << samples.size() << std::endl;
    
    // Check sample statistics
    float sum = 0, sum_sq = 0;
    for (float s : samples) {
        sum += s;
        sum_sq += s * s;
    }
    float mean = sum / samples.size();
    float rms = std::sqrt(sum_sq / samples.size());
    std::cout << "Mean (DC): " << mean << std::endl;
    std::cout << "RMS: " << rms << std::endl;
    
    // Check zero crossings to estimate frequency
    int zero_crossings = 0;
    for (size_t i = 1; i < samples.size(); i++) {
        if ((samples[i-1] < 0 && samples[i] >= 0) ||
            (samples[i-1] >= 0 && samples[i] < 0)) {
            zero_crossings++;
        }
    }
    
    // Assuming 8kHz sample rate
    float duration = samples.size() / 8000.0f;
    float approx_freq = zero_crossings / 2.0f / duration;
    std::cout << "Zero crossings: " << zero_crossings << std::endl;
    std::cout << "Approx frequency (8kHz): " << approx_freq << " Hz" << std::endl;
    
    // Full spectrum 0-4000 Hz (assuming 8kHz)
    std::cout << "\nSpectrum (0-4000 Hz at 8kHz sample rate):" << std::endl;
    int N = 4096;
    if (N > samples.size()) N = samples.size();
    
    float max_power = 0;
    int max_freq = 0;
    
    for (int f = 0; f <= 4000; f += 50) {
        float sum_re = 0, sum_im = 0;
        float omega = 2.0f * M_PI * f / 8000.0f;
        
        for (int n = 0; n < N; n++) {
            sum_re += samples[n] * std::cos(omega * n);
            sum_im += samples[n] * std::sin(omega * n);
        }
        
        float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
        if (power > max_power) {
            max_power = power;
            max_freq = f;
        }
        
        // Print bar graph
        int bar_len = (int)(power * 500);
        if (bar_len > 50) bar_len = 50;
        std::cout << f << " Hz: ";
        for (int i = 0; i < bar_len; i++) std::cout << "#";
        if (power > 0.02) std::cout << " " << power;
        std::cout << std::endl;
    }
    
    std::cout << "\nPeak: " << max_freq << " Hz (power=" << max_power << ")" << std::endl;
    
    return 0;
}
