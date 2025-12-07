/**
 * Find probe pattern in raw symbols by looking for repeating sequences
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"

using namespace m110a;

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
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    const std::complex<float> con[8] = {
        {1,0}, {0.707f,0.707f}, {0,1}, {-0.707f,0.707f},
        {-1,0}, {-0.707f,-0.707f}, {0,-1}, {0.707f,-0.707f}
    };
    
    // Convert all symbols to tribits
    std::vector<int> tribits;
    for (const auto& sym : result.data_symbols) {
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        tribits.push_back(best);
    }
    
    // Look for the probe pattern by correlation
    // The probe is a repeating pattern of 16 or 20 symbols
    // Try to find where this pattern repeats
    
    std::cout << "Symbols: " << tribits.size() << std::endl;
    
    // Show first 200 tribits
    std::cout << "\nFirst 200 tribits:\n";
    for (int i = 0; i < 200 && i < tribits.size(); i++) {
        std::cout << tribits[i];
        if ((i+1) % 48 == 0) std::cout << " | ";  // M2400S frame boundary
        else if ((i+1) % 40 == 0) std::cout << " * "; // M1200S frame boundary
        else if ((i+1) % 10 == 0) std::cout << " ";
    }
    std::cout << std::endl;
    
    // Find repeating pattern by autocorrelation
    std::cout << "\nAutocorrelation for frame periods:" << std::endl;
    for (int period : {40, 48, 72, 80, 96}) {
        int match = 0;
        int total = 0;
        for (int i = 0; i < tribits.size() - period; i++) {
            if (tribits[i] == tribits[i + period]) match++;
            total++;
        }
        float corr = 100.0f * match / total;
        std::cout << "  Period " << period << ": " << corr << "% match" << std::endl;
    }
    
    return 0;
}
