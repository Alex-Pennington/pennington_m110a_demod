/**
 * Debug preamble detection - look at raw D1/D2 symbol positions
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
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
    float fc = 1800.0f;
    int sps = 20;
    
    // Mix to baseband
    std::vector<complex_t> bb(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float t = i / sr;
        bb[i] = samples[i] * std::exp(complex_t(0, -2.0f * M_PI * fc * t));
    }
    
    // Simple lowpass (moving average)
    std::vector<complex_t> filt(bb.size());
    int avg_len = sps;
    for (size_t i = avg_len; i < bb.size(); i++) {
        complex_t sum(0, 0);
        for (int j = 0; j < avg_len; j++) {
            sum += bb[i - j];
        }
        filt[i] = sum / float(avg_len);
    }
    
    // Find preamble by correlation with sync pattern
    float best_corr = 0;
    int best_start = 0;
    float best_phase = 0;
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    for (int offset = 0; offset < 5000; offset++) {
        complex_t corr(0, 0);
        float pow = 0;
        for (int i = 0; i < 256; i++) {
            int idx = offset + i * sps;
            if (idx >= filt.size()) break;
            uint8_t pattern = m110a::msdmt::pscramble[i % 32];
            corr += filt[idx] * std::conj(con[pattern]);
            pow += std::norm(filt[idx]);
        }
        float c = std::abs(corr) / std::sqrt(pow * 256 + 0.0001f);
        if (c > best_corr) {
            best_corr = c;
            best_start = offset;
            best_phase = std::arg(corr);
        }
    }
    
    std::cout << "Best sync at sample " << best_start << " (corr=" << best_corr << ")" << std::endl;
    std::cout << "Phase: " << (best_phase * 180 / M_PI) << " degrees" << std::endl;
    
    complex_t rot = std::exp(complex_t(0, -best_phase));
    
    // Extract and show symbols at D1 and D2 positions
    std::cout << "\n=== D1 symbols (at 288-319) ===" << std::endl;
    for (int i = 288; i < 320; i++) {
        int idx = best_start + i * sps;
        if (idx >= filt.size()) break;
        complex_t sym = filt[idx] * rot;
        
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
    }
    std::cout << std::endl;
    
    std::cout << "\n=== D2 symbols (at 320-351) ===" << std::endl;
    for (int i = 320; i < 352; i++) {
        int idx = best_start + i * sps;
        if (idx >= filt.size()) break;
        complex_t sym = filt[idx] * rot;
        
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
    }
    std::cout << std::endl;
    
    // Now show what D1=6 and D2=4 patterns SHOULD look like (for M2400S)
    std::cout << "\n=== Expected patterns ===" << std::endl;
    std::cout << "D1=6 at pos 288-319: ";
    for (int i = 0; i < 32; i++) {
        uint8_t pattern = (m110a::msdmt::psymbol[6][i % 8] + m110a::msdmt::pscramble[(288 + i) % 32]) % 8;
        std::cout << (int)pattern;
    }
    std::cout << std::endl;
    
    std::cout << "D2=4 at pos 320-351: ";
    for (int i = 0; i < 32; i++) {
        uint8_t pattern = (m110a::msdmt::psymbol[4][i % 8] + m110a::msdmt::pscramble[(320 + i) % 32]) % 8;
        std::cout << (int)pattern;
    }
    std::cout << std::endl;
    
    std::cout << "D2=5 at pos 320-351: ";
    for (int i = 0; i < 32; i++) {
        uint8_t pattern = (m110a::msdmt::psymbol[5][i % 8] + m110a::msdmt::pscramble[(320 + i) % 32]) % 8;
        std::cout << (int)pattern;
    }
    std::cout << std::endl;
    
    return 0;
}
