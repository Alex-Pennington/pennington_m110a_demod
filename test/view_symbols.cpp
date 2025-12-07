/**
 * View the raw symbols from the decoded signal
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
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
    
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    std::cout << "First 48 data symbols (1 frame for 2400bps):" << std::endl;
    
    // 8PSK constellation reference
    const std::complex<float> con[8] = {
        {1,0}, {0.707f,0.707f}, {0,1}, {-0.707f,0.707f},
        {-1,0}, {-0.707f,-0.707f}, {0,-1}, {0.707f,-0.707f}
    };
    
    // For each symbol, show magnitude and detected tribit
    for (int i = 0; i < 48 && i < result.data_symbols.size(); i++) {
        auto sym = result.data_symbols[i];
        float mag = std::abs(sym);
        float phase = std::arg(sym) * 180.0f / M_PI;
        
        // Find closest constellation point
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) {
                best_dist = dist;
                best = t;
            }
        }
        
        std::cout << std::setw(3) << i << ": "
                  << "(" << std::fixed << std::setprecision(3) << std::setw(7) << sym.real() 
                  << ", " << std::setw(7) << sym.imag() << ") "
                  << "mag=" << std::setw(5) << mag << " "
                  << "phase=" << std::setw(7) << phase << "° "
                  << "tribit=" << best
                  << std::endl;
    }
    
    // Also look at probe symbols at positions 32-47
    std::cout << "\nFrame structure check (symbols 0-47):" << std::endl;
    std::cout << "Data (0-31): ";
    for (int i = 0; i < 32 && i < result.data_symbols.size(); i++) {
        auto sym = result.data_symbols[i];
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
    }
    std::cout << std::endl;
    
    std::cout << "Probe? (32-47): ";
    for (int i = 32; i < 48 && i < result.data_symbols.size(); i++) {
        auto sym = result.data_symbols[i];
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
    }
    std::cout << std::endl;
    
    return 0;
}
