/**
 * Test decoder with new reference files at 48kHz
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "m110a/msdmt_decoder.h"

using namespace m110a;

std::vector<float> read_pcm_48k(const std::string& filename) {
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
    std::cout << "=== Testing with 48kHz sample rate ===" << std::endl;
    
    std::string file = "/mnt/user-data/uploads/tx_2400S_20251206_100439_978.pcm";
    auto samples = read_pcm_48k(file);
    std::cout << "Loaded " << samples.size() << " samples (" 
              << samples.size()/48000.0f << " sec)" << std::endl;
    
    // Configure for 48kHz
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "Mode: " << result.mode_name << std::endl;
    std::cout << "Correlation: " << result.correlation << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    // Also try with carrier at 1750 Hz
    std::cout << "\n--- Trying carrier at 1750 Hz ---" << std::endl;
    cfg.carrier_freq = 1750.0f;
    MSDMTDecoder decoder2(cfg);
    result = decoder2.decode(samples);
    std::cout << "Mode: " << result.mode_name << std::endl;
    std::cout << "Correlation: " << result.correlation << std::endl;
    
    // Try 1700 Hz
    std::cout << "\n--- Trying carrier at 1700 Hz ---" << std::endl;
    cfg.carrier_freq = 1700.0f;
    MSDMTDecoder decoder3(cfg);
    result = decoder3.decode(samples);
    std::cout << "Mode: " << result.mode_name << std::endl;
    std::cout << "Correlation: " << result.correlation << std::endl;
    
    return 0;
}
