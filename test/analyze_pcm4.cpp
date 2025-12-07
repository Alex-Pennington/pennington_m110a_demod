/**
 * Look at preamble section with 9600 Hz sample rate (common for HF)
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>

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
    
    // Test different sample rates
    std::vector<int> test_rates = {8000, 9600, 44100, 48000};
    
    for (int sr : test_rates) {
        std::cout << "\n=== Sample rate: " << sr << " Hz ===" << std::endl;
        std::cout << "Duration: " << samples.size() / (float)sr << " sec" << std::endl;
        
        // DFT around 1800 Hz with finer resolution
        int N = std::min(4096, (int)samples.size());
        
        float max_power = 0;
        int max_freq = 0;
        
        for (int freq = 1700; freq <= 1900; freq += 5) {
            float sum_re = 0, sum_im = 0;
            float omega = 2.0f * M_PI * freq / sr;
            
            for (int n = 0; n < N; n++) {
                sum_re += samples[n] * std::cos(omega * n);
                sum_im += samples[n] * std::sin(omega * n);
            }
            
            float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
            if (power > max_power) {
                max_power = power;
                max_freq = freq;
            }
        }
        
        std::cout << "Peak near 1800 Hz: " << max_freq << " (power=" << max_power << ")" << std::endl;
        
        // Also check if carrier is at different freq
        max_power = 0;
        for (int freq = 500; freq <= sr/2; freq += 50) {
            float sum_re = 0, sum_im = 0;
            float omega = 2.0f * M_PI * freq / sr;
            for (int n = 0; n < N; n++) {
                sum_re += samples[n] * std::cos(omega * n);
                sum_im += samples[n] * std::sin(omega * n);
            }
            float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
            if (power > max_power) {
                max_power = power;
                max_freq = freq;
            }
        }
        std::cout << "Overall peak: " << max_freq << " Hz (power=" << max_power << ")" << std::endl;
    }
    
    return 0;
}
