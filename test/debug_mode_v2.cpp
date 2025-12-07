/**
 * Debug mode detection using MSDMTDecoder internals
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
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
    // Test multiple files
    std::vector<std::pair<std::string, std::string>> files = {
        {"/mnt/user-data/uploads/tx_1200S_20251206_100436_261.pcm", "M1200S (D1=6,D2=5)"},
        {"/mnt/user-data/uploads/tx_2400S_20251206_100439_978.pcm", "M2400S (D1=6,D2=4)"},
    };
    
    for (const auto& [file, expected] : files) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "File: " << file << std::endl;
        std::cout << "Expected: " << expected << std::endl;
        std::cout << "========================================" << std::endl;
        
        auto samples = read_pcm(file);
        
        MSDMTDecoderConfig cfg;
        cfg.sample_rate = 48000.0f;
        cfg.carrier_freq = 1800.0f;
        cfg.baud_rate = 2400.0f;
        MSDMTDecoder decoder(cfg);
        
        auto result = decoder.decode(samples);
        
        std::cout << "Mode detected: " << result.mode_name << std::endl;
        std::cout << "D1=" << result.d1 << " (corr=" << result.d1_corr << ")" << std::endl;
        std::cout << "D2=" << result.d2 << " (corr=" << result.d2_corr << ")" << std::endl;
        std::cout << "Preamble start: " << result.start_sample << std::endl;
        std::cout << "Phase offset: " << (result.phase_offset * 180 / M_PI) << " degrees" << std::endl;
        
        // Show D1/D2 correlations for all values
        std::cout << "\nAll D1 correlations:" << std::endl;
        // Note: we can't access internal correlations directly, so let's just show the detected value
    }
    
    return 0;
}
