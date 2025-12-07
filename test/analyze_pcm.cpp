/**
 * Analyze PCM file - check spectrum for carrier
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <algorithm>

// Simple DFT to find carrier
void find_carrier(const std::vector<float>& samples, int sample_rate) {
    // Use first 2048 samples
    int N = std::min(2048, (int)samples.size());
    
    // Check frequencies around expected 1800 Hz
    std::vector<std::pair<float, float>> results;
    
    for (int f = 1000; f <= 2500; f += 10) {
        float sum_re = 0, sum_im = 0;
        float omega = 2.0f * M_PI * f / sample_rate;
        
        for (int n = 0; n < N; n++) {
            sum_re += samples[n] * std::cos(omega * n);
            sum_im += samples[n] * std::sin(omega * n);
        }
        
        float power = std::sqrt(sum_re * sum_re + sum_im * sum_im) / N;
        results.push_back({f, power});
    }
    
    // Find peak
    auto max_it = std::max_element(results.begin(), results.end(),
        [](auto& a, auto& b) { return a.second < b.second; });
    
    std::cout << "Peak frequency: " << max_it->first << " Hz (power=" << max_it->second << ")" << std::endl;
    
    // Show top 5
    std::sort(results.begin(), results.end(), 
        [](auto& a, auto& b) { return a.second > b.second; });
    
    std::cout << "Top 5 frequencies:" << std::endl;
    for (int i = 0; i < 5 && i < results.size(); i++) {
        std::cout << "  " << results[i].first << " Hz: " << results[i].second << std::endl;
    }
}

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
    std::cout << "Max amplitude: " << *std::max_element(samples.begin(), samples.end()) << std::endl;
    std::cout << "Min amplitude: " << *std::min_element(samples.begin(), samples.end()) << std::endl;
    
    std::cout << "\n--- Assuming 8000 Hz sample rate ---" << std::endl;
    find_carrier(samples, 8000);
    
    std::cout << "\n--- Assuming 48000 Hz sample rate ---" << std::endl;
    find_carrier(samples, 48000);
    
    return 0;
}
