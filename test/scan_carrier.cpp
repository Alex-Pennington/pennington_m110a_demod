#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
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
    
    float sr = 48000.0f;
    int sps = 20;
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    std::cout << "Scanning carrier frequencies 1700-1900 Hz..." << std::endl;
    
    float best_overall_corr = 0;
    float best_fc = 0;
    int best_offset = 0;
    
    for (float fc = 1700; fc <= 1900; fc += 5) {
        // Mix to baseband at this frequency
        std::vector<complex_t> bb(samples.size());
        for (size_t i = 0; i < samples.size(); i++) {
            float t = i / sr;
            bb[i] = samples[i] * std::exp(complex_t(0, -2.0f * M_PI * fc * t));
        }
        
        // Simple filter
        std::vector<complex_t> filt(bb.size());
        for (size_t i = sps; i < bb.size(); i++) {
            complex_t sum(0, 0);
            for (int j = 0; j < sps; j++) sum += bb[i - j];
            filt[i] = sum / float(sps);
        }
        
        // Find best sync position
        float best_corr = 0;
        int best_pos = 0;
        
        for (int offset = 0; offset < 20000 && offset + 256*sps < filt.size(); offset += sps) {
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
        
        if (best_corr > 0.5) {
            std::cout << "  fc=" << fc << " Hz: corr=" << best_corr 
                      << " at " << best_pos << std::endl;
        }
        
        if (best_corr > best_overall_corr) {
            best_overall_corr = best_corr;
            best_fc = fc;
            best_offset = best_pos;
        }
    }
    
    std::cout << "\nBest: fc=" << best_fc << " Hz, corr=" << best_overall_corr 
              << " at sample " << best_offset << std::endl;
    
    return 0;
}
