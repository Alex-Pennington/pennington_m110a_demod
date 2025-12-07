/**
 * Decode new reference PCM files with known plaintext
 * Test message: "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890" (54 bytes)
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

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

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

void test_file(const std::string& filename, const std::string& expected_mode) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "File: " << filename << std::endl;
    std::cout << "Expected mode: " << expected_mode << std::endl;
    std::cout << "========================================" << std::endl;
    
    auto samples = read_pcm(filename);
    if (samples.empty()) {
        std::cout << "ERROR: Cannot read file" << std::endl;
        return;
    }
    
    std::cout << "Samples: " << samples.size() << " (" 
              << std::fixed << std::setprecision(3) 
              << samples.size()/48000.0 << " sec)" << std::endl;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "\nMode detected: " << result.mode_name << std::endl;
    std::cout << "D1=" << result.d1 << " (corr=" << std::fixed << std::setprecision(3) 
              << result.d1_corr << ")" << std::endl;
    std::cout << "D2=" << result.d2 << " (corr=" << result.d2_corr << ")" << std::endl;
    std::cout << "Preamble start: sample " << result.start_sample 
              << " (t=" << result.start_sample/48000.0 << "s)" << std::endl;
    std::cout << "Phase offset: " << (result.phase_offset * 180 / M_PI) << " degrees" << std::endl;
    
    // Check if mode matches expected
    bool mode_ok = (result.mode_name.find(expected_mode.substr(0, 4)) != std::string::npos);
    std::cout << "\nMode match: " << (mode_ok ? "YES ✓" : "NO ✗") << std::endl;
    
    // Try to get decoded data if available
    if (result.data.size() > 0) {
        std::cout << "\nDecoded " << result.data.size() << " bytes:" << std::endl;
        std::cout << "  Hex: ";
        for (size_t i = 0; i < std::min(result.data.size(), (size_t)20); i++) {
            printf("%02x ", result.data[i]);
        }
        if (result.data.size() > 20) std::cout << "...";
        std::cout << std::endl;
        
        std::cout << "  Ascii: ";
        for (size_t i = 0; i < std::min(result.data.size(), (size_t)54); i++) {
            char c = result.data[i];
            if (c >= 32 && c < 127) std::cout << c;
            else std::cout << '.';
        }
        std::cout << std::endl;
        
        // Compare to expected
        int matches = 0;
        for (size_t i = 0; i < std::min(result.data.size(), (size_t)EXPECTED_LEN); i++) {
            if (result.data[i] == (uint8_t)EXPECTED[i]) matches++;
        }
        std::cout << "  Match: " << matches << "/" << EXPECTED_LEN << " chars" << std::endl;
    }
}

int main(int argc, char** argv) {
    std::string base = "/home/claude/m110a_demod/ref_pcm/";
    
    if (argc > 1) {
        // Test specific file
        test_file(argv[1], "unknown");
        return 0;
    }
    
    // Test all reference files
    std::vector<std::pair<std::string, std::string>> files = {
        {"tx_75S_20251206_202410_888.pcm", "M75S"},
        {"tx_75L_20251206_202421_539.pcm", "M75L"},
        {"tx_150S_20251206_202440_580.pcm", "M150S"},
        {"tx_150L_20251206_202446_986.pcm", "M150L"},
        {"tx_300S_20251206_202501_840.pcm", "M300S"},
        {"tx_300L_20251206_202506_058.pcm", "M300L"},
        {"tx_600S_20251206_202518_709.pcm", "M600S"},
        {"tx_600L_20251206_202521_953.pcm", "M600L"},
        {"tx_1200S_20251206_202533_636.pcm", "M1200S"},
        {"tx_1200L_20251206_202536_295.pcm", "M1200L"},
        {"tx_2400S_20251206_202547_345.pcm", "M2400S"},
        {"tx_2400L_20251206_202549_783.pcm", "M2400L"},
    };
    
    int total = 0, correct = 0;
    
    for (const auto& [file, mode] : files) {
        test_file(base + file, mode);
        total++;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Tested: " << total << " files" << std::endl;
    
    return 0;
}
