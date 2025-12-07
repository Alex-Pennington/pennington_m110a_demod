/**
 * Debug mode detection for all files
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
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
    std::cout << "=== Mode Detection Debug ===" << std::endl;
    
    std::string base = "/mnt/user-data/uploads/";
    std::vector<std::pair<std::string, std::string>> files = {
        {"tx_75S_20251206_100415_270.pcm", "M75S"},
        {"tx_150S_20251206_100419_881.pcm", "M150S"},
        {"tx_300S_20251206_100428_384.pcm", "M300S"},
        {"tx_600S_20251206_100432_066.pcm", "M600S"},
        {"tx_1200S_20251206_100436_261.pcm", "M1200S"},
        {"tx_2400S_20251206_100439_978.pcm", "M2400S"},
    };
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    // D1/D2 table for reference
    std::cout << "\nExpected D1/D2 values:" << std::endl;
    std::cout << "  M75S:  D1=?, D2=?" << std::endl;
    std::cout << "  M150S: D1=7, D2=4" << std::endl;
    std::cout << "  M300S: D1=6, D2=7" << std::endl;
    std::cout << "  M600S: D1=6, D2=6" << std::endl;
    std::cout << "  M1200S: D1=6, D2=5" << std::endl;
    std::cout << "  M2400S: D1=6, D2=4" << std::endl;
    std::cout << std::endl;
    
    for (const auto& [file, expected] : files) {
        auto samples = read_pcm(base + file);
        if (samples.empty()) continue;
        
        auto result = decoder.decode(samples);
        
        std::cout << file << ":" << std::endl;
        std::cout << "  Expected: " << expected << std::endl;
        std::cout << "  Detected: " << result.mode_name << std::endl;
        std::cout << "  D1=" << result.d1 << " (corr=" << result.d1_corr << ")" << std::endl;
        std::cout << "  D2=" << result.d2 << " (corr=" << result.d2_corr << ")" << std::endl;
        std::cout << std::endl;
    }
    
    return 0;
}
