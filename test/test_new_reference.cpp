/**
 * Test decoder with new MS-DMT reference PCM files
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <string>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"
#include "modem/multimode_interleaver.h"

using namespace m110a;

// Read raw PCM file (16-bit signed, assumed 8kHz)
std::vector<float> read_pcm(const std::string& filename, int& sample_rate) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return {};
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read 16-bit samples
    size_t num_samples = size / 2;
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    
    // Convert to float
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    
    sample_rate = 8000;  // Assume 8kHz
    return samples;
}

int main() {
    std::cout << "=== New MS-DMT Reference File Decode Test ===" << std::endl;
    
    std::string base = "/mnt/user-data/uploads/";
    
    // Test files - use the latest for each mode
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"tx_2400S_20251206_100439_978.pcm", "M2400S"},
        {"tx_2400L_20251206_100441_817.pcm", "M2400L"},
        {"tx_1200S_20251206_100436_261.pcm", "M1200S"},
        {"tx_1200L_20251206_100438_128.pcm", "M1200L"},
        {"tx_600S_20251206_100432_066.pcm", "M600S"},
        {"tx_600L_20251206_100434_162.pcm", "M600L"},
        {"tx_300S_20251206_100428_384.pcm", "M300S"},
        {"tx_150S_20251206_100419_881.pcm", "M150S"},
        {"tx_75S_20251206_100415_270.pcm", "M75S"},
    };
    
    // Decoder config for 8kHz
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 8000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    int passed = 0, total = 0;
    
    for (const auto& [file, expected] : test_files) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "File: " << file << std::endl;
        std::cout << "Expected: " << expected << std::endl;
        std::cout << "========================================" << std::endl;
        
        int sr;
        auto samples = read_pcm(base + file, sr);
        if (samples.empty()) {
            std::cout << "  SKIP - file not found" << std::endl;
            continue;
        }
        
        std::cout << "Samples: " << samples.size() << " (" 
                  << (samples.size() / 8000.0f) << " sec)" << std::endl;
        
        total++;
        
        // Decode
        auto result = decoder.decode(samples);
        
        std::cout << "Mode detected: " << result.mode_name 
                  << " (corr=" << result.correlation << ")" << std::endl;
        std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
        
        if (result.mode_name == expected) {
            std::cout << "Mode: MATCH ✓" << std::endl;
            passed++;
        } else {
            std::cout << "Mode: MISMATCH ✗" << std::endl;
        }
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << "/" << total << " modes detected correctly" << std::endl;
    
    return passed == total ? 0 : 1;
}
