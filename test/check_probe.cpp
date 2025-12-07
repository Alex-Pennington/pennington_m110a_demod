/**
 * Check if probe symbols match expected pattern
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"

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
    
    // Expected probe pattern from MS-DMT (16 symbols, repeated)
    // From msdmt_preamble.h - look at the channel probe pattern
    std::cout << "Looking for probe pattern in data symbols..." << std::endl;
    
    // M2400S frame: 32 data + 16 probe
    // Check several frames
    for (int frame = 0; frame < 5; frame++) {
        int probe_start = frame * 48 + 32;  // After 32 data symbols
        
        std::cout << "\nFrame " << frame << " probes at [" << probe_start << "-" << probe_start+15 << "]: ";
        
        for (int i = 0; i < 16 && (probe_start + i) < result.data_symbols.size(); i++) {
            auto sym = result.data_symbols[probe_start + i];
            int best = 0;
            float best_dist = 1e9f;
            for (int t = 0; t < 8; t++) {
                float dist = std::abs(sym - con[t]);
                if (dist < best_dist) { best_dist = dist; best = t; }
            }
            std::cout << best;
        }
    }
    std::cout << std::endl;
    
    // Also try M1200S frame: 20 data + 20 probe  
    std::cout << "\nIf M1200S (20 data + 20 probe):" << std::endl;
    for (int frame = 0; frame < 5; frame++) {
        int probe_start = frame * 40 + 20;
        
        std::cout << "Frame " << frame << " probes at [" << probe_start << "-" << probe_start+19 << "]: ";
        
        for (int i = 0; i < 20 && (probe_start + i) < result.data_symbols.size(); i++) {
            auto sym = result.data_symbols[probe_start + i];
            int best = 0;
            float best_dist = 1e9f;
            for (int t = 0; t < 8; t++) {
                float dist = std::abs(sym - con[t]);
                if (dist < best_dist) { best_dist = dist; best = t; }
            }
            std::cout << best;
        }
        std::cout << std::endl;
    }
    
    return 0;
}
