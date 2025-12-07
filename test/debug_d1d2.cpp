/**
 * Detailed D1/D2 correlation analysis
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
    std::cout << "File: " << file << std::endl;
    std::cout << "Expected: M2400S (D1=6, D2=4)" << std::endl << std::endl;
    
    auto samples = read_pcm(file);
    float sr = 48000.0f;
    float fc = 1800.0f;
    int sps = 20;  // 48000/2400
    
    // Mix down and lowpass
    std::vector<complex_t> bb(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float t = i / sr;
        bb[i] = samples[i] * std::exp(complex_t(0, -2.0f * M_PI * fc * t));
    }
    
    // Find preamble start (simplified - look for energy)
    // The actual decoder uses correlation, but let's just use a fixed offset
    int start = 0;  // Assume preamble starts at beginning
    
    // Find sync - correlate with known sync pattern
    float best_corr = 0;
    int best_start = 0;
    for (int offset = 0; offset < 2000; offset += sps) {
        complex_t corr(0, 0);
        float pow = 0;
        for (int i = 0; i < 256; i++) {
            int idx = offset + i * sps;
            if (idx >= bb.size()) break;
            uint8_t pattern = m110a::msdmt::pscramble[i % 32];
            complex_t ref(m110a::msdmt::psk8_i[pattern], m110a::msdmt::psk8_q[pattern]);
            corr += bb[idx] * std::conj(ref);
            pow += std::norm(bb[idx]);
        }
        float c = std::abs(corr) / std::sqrt(pow * 256 + 0.0001f);
        if (c > best_corr) {
            best_corr = c;
            best_start = offset;
        }
    }
    
    std::cout << "Sync correlation: " << best_corr << " at sample " << best_start << std::endl;
    
    // Get phase from sync
    complex_t phase_sum(0, 0);
    for (int i = 0; i < 256; i++) {
        int idx = best_start + i * sps;
        if (idx >= bb.size()) break;
        uint8_t pattern = m110a::msdmt::pscramble[i % 32];
        complex_t ref(m110a::msdmt::psk8_i[pattern], m110a::msdmt::psk8_q[pattern]);
        phase_sum += bb[idx] * std::conj(ref);
    }
    float phase = std::arg(phase_sum);
    complex_t rot(std::cos(-phase), std::sin(-phase));
    
    std::cout << "Phase offset: " << (phase * 180.0f / M_PI) << " degrees" << std::endl << std::endl;
    
    // D1 analysis (symbol 288)
    int d1_start = best_start + 288 * sps;
    std::cout << "=== D1 Correlation (at symbol 288) ===" << std::endl;
    for (int d = 0; d < 8; d++) {
        complex_t corr(0, 0);
        float pow = 0;
        for (int i = 0; i < 32; i++) {
            uint8_t pattern = (m110a::msdmt::psymbol[d][i % 8] + m110a::msdmt::pscramble[(288 + i) % 32]) % 8;
            int idx = d1_start + i * sps;
            if (idx >= bb.size()) break;
            complex_t sym = bb[idx] * rot;
            complex_t ref(m110a::msdmt::psk8_i[pattern], m110a::msdmt::psk8_q[pattern]);
            corr += sym * std::conj(ref);
            pow += std::norm(bb[idx]);
        }
        float c = std::abs(corr) / std::sqrt(pow * 32 + 0.0001f);
        std::cout << "  D=" << d << ": " << std::fixed << std::setprecision(4) << c;
        if (d == 6) std::cout << " <-- expected";
        std::cout << std::endl;
    }
    
    // D2 analysis (symbol 320)
    int d2_start = best_start + 320 * sps;
    std::cout << std::endl << "=== D2 Correlation (at symbol 320) ===" << std::endl;
    for (int d = 0; d < 8; d++) {
        complex_t corr(0, 0);
        float pow = 0;
        for (int i = 0; i < 32; i++) {
            uint8_t pattern = (m110a::msdmt::psymbol[d][i % 8] + m110a::msdmt::pscramble[(320 + i) % 32]) % 8;
            int idx = d2_start + i * sps;
            if (idx >= bb.size()) break;
            complex_t sym = bb[idx] * rot;
            complex_t ref(m110a::msdmt::psk8_i[pattern], m110a::msdmt::psk8_q[pattern]);
            corr += sym * std::conj(ref);
            pow += std::norm(bb[idx]);
        }
        float c = std::abs(corr) / std::sqrt(pow * 32 + 0.0001f);
        std::cout << "  D=" << d << ": " << std::fixed << std::setprecision(4) << c;
        if (d == 4) std::cout << " <-- expected for M2400S";
        if (d == 5) std::cout << " <-- M1200S";
        std::cout << std::endl;
    }
    
    return 0;
}
