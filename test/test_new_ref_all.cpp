/**
 * Test all new reference files at 48kHz
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
    std::cout << "=== New Reference Files Test (48kHz) ===" << std::endl;
    
    std::string base = "/mnt/user-data/uploads/";
    std::vector<std::pair<std::string, std::string>> files = {
        {"tx_75S_20251206_100415_270.pcm", "M75S"},
        {"tx_75L_20251206_100417_915.pcm", "M75L"},
        {"tx_150S_20251206_100419_881.pcm", "M150S"},
        {"tx_150L_20251206_100423_918.pcm", "M150L"},
        {"tx_300S_20251206_100428_384.pcm", "M300S"},
        {"tx_300L_20251206_100430_409.pcm", "M300L"},
        {"tx_600S_20251206_100432_066.pcm", "M600S"},
        {"tx_600L_20251206_100434_162.pcm", "M600L"},
        {"tx_1200S_20251206_100436_261.pcm", "M1200S"},
        {"tx_1200L_20251206_100438_128.pcm", "M1200L"},
        {"tx_2400S_20251206_100439_978.pcm", "M2400S"},
        {"tx_2400L_20251206_100441_817.pcm", "M2400L"},
    };
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    int passed = 0;
    
    for (const auto& [file, expected] : files) {
        auto samples = read_pcm(base + file);
        if (samples.empty()) {
            std::cout << file << ": SKIP (not found)" << std::endl;
            continue;
        }
        
        auto result = decoder.decode(samples);
        
        bool match = (result.mode_name == expected);
        if (match) passed++;
        
        std::cout << file << ": " 
                  << result.mode_name << " (corr=" << result.correlation << ") "
                  << (match ? "✓" : "✗ expected " + expected)
                  << std::endl;
    }
    
    std::cout << "\nPassed: " << passed << "/" << files.size() << std::endl;
    
    return 0;
}
