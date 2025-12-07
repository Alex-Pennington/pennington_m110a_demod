/**
 * Analyze at 48kHz sample rate
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
    
    std::cout << "Assuming 48kHz sample rate:" << std::endl;
    std::cout << "Duration: " << samples.size() / 48000.0f << " seconds" << std::endl;
    
    // Check around 1800 Hz more finely
    int N = 8192;
    std::cout << "\nSpectrum 1500-2100 Hz (at 48kHz):" << std::endl;
    
    float max_power = 0;
    int max_freq = 0;
    
    for (int f = 1500; f <= 2100; f += 10) {
        float sum_re = 0, sum_im = 0;
        float omega = 2.0f * M_PI * f / 48000.0f;
        
        for (int n = 0; n < N && n < samples.size(); n++) {
            sum_re += samples[n] * std::cos(omega * n);
            sum_im += samples[n] * std::sin(omega * n);
        }
        
        float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
        if (power > max_power) {
            max_power = power;
            max_freq = f;
        }
        
        int bar_len = (int)(power * 500);
        if (bar_len > 50) bar_len = 50;
        std::cout << f << " Hz: ";
        for (int i = 0; i < bar_len; i++) std::cout << "#";
        if (power > 0.01) std::cout << " " << power;
        std::cout << std::endl;
    }
    
    std::cout << "\nPeak in range: " << max_freq << " Hz" << std::endl;
    
    // Also check wider range
    std::cout << "\nFull spectrum peaks (0-10kHz at 48kHz):" << std::endl;
    std::vector<std::pair<float, int>> peaks;
    for (int f = 0; f <= 10000; f += 100) {
        float sum_re = 0, sum_im = 0;
        float omega = 2.0f * M_PI * f / 48000.0f;
        for (int n = 0; n < N && n < samples.size(); n++) {
            sum_re += samples[n] * std::cos(omega * n);
            sum_im += samples[n] * std::sin(omega * n);
        }
        float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
        peaks.push_back({power, f});
    }
    std::sort(peaks.begin(), peaks.end(), std::greater<>());
    for (int i = 0; i < 10; i++) {
        std::cout << "  " << peaks[i].second << " Hz: " << peaks[i].first << std::endl;
    }
    
    return 0;
}
