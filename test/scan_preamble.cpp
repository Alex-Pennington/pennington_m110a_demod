#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <algorithm>
#include "m110a/msdmt_preamble.h"

using complex_t = std::complex<float>;

std::vector<float> read_pcm(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    size_t num_samples = size / 2;
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

int main() {
    std::string file = "/mnt/user-data/uploads/tx_2400S_20251206_100439_978.pcm";
    auto samples = read_pcm(file);
    
    std::cout << "Samples: " << samples.size() << std::endl;
    
    float sr = 48000.0f;
    float fc = 1800.0f;
    int sps = 20;
    
    // Mix to baseband
    std::vector<complex_t> bb(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float t = i / sr;
        bb[i] = samples[i] * std::exp(complex_t(0, -2.0f * M_PI * fc * t));
    }
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    // Apply matched filter (simple average approximation)
    std::vector<complex_t> filt(bb.size());
    for (size_t i = sps; i < bb.size(); i++) {
        complex_t sum(0, 0);
        for (int j = 0; j < sps; j++) sum += bb[i - j];
        filt[i] = sum / float(sps);
    }
    
    std::cout << "\nScanning for sync..." << std::endl;
    
    float best_corr = 0;
    int best_pos = 0;
    
    for (int offset = 0; offset < 20000 && offset + 256*sps < filt.size(); offset++) {
        complex_t corr(0, 0);
        float pow = 0;
        for (int i = 0; i < 256; i++) {
            int idx = offset + i * sps;
            uint8_t pattern = m110a::msdmt::pscramble[i % 32];
            corr += filt[idx] * std::conj(con[pattern]);
            pow += std::norm(filt[idx]);
        }
        float c = std::abs(corr) / std::sqrt(pow * 256 + 0.0001f);
        if (c > best_corr) {
            best_corr = c;
            best_pos = offset;
        }
    }
    
    std::cout << "Best sync: " << best_corr << " at sample " << best_pos 
              << " (" << best_pos/sr*1000 << " ms)" << std::endl;
    
    // Now look at what the actual MSDMTDecoder finds
    std::cout << "\nComparing with MSDMTDecoder..." << std::endl;
    
    return 0;
}
