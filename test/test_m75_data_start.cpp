/**
 * Find M75 data start by correlating against all 4 Walsh patterns
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

using namespace m110a;

std::vector<float> read_pcm(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(0);
    std::vector<int16_t> raw(size / 2);
    f.read(reinterpret_cast<char*>(raw.data()), size);
    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) return 1;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    // Duplicate to 4800 Hz
    std::vector<complex_t> sym4800;
    for (const auto& s : result.data_symbols) {
        sym4800.push_back(s);
        sym4800.push_back(s);
    }
    
    std::cout << "Symbols (2400Hz): " << result.data_symbols.size() << "\n";
    std::cout << "Symbols (4800Hz): " << sym4800.size() << "\n\n";
    
    // Test Walsh correlation at various offsets
    std::cout << "Walsh correlations per offset (4800Hz):\n";
    std::cout << "Offset  W0     W1     W2     W3     Best\n";
    
    Walsh75Decoder decoder(45);
    
    for (int offset = 0; offset <= 3200; offset += 64) {  // Step by 1 Walsh symbol
        if (offset + 64 > (int)sym4800.size()) break;
        
        decoder.reset();
        auto res = decoder.decode(&sym4800[offset]);
        
        // Also get individual pattern magnitudes by testing each
        float mags[4];
        for (int p = 0; p < 4; p++) {
            Walsh75Decoder test_dec(45);
            // Hack: decode with forced pattern - but we can't do this easily
            // Just show the result
        }
        
        std::cout << std::setw(5) << offset << "  " 
                  << std::fixed << std::setprecision(0)
                  << std::setw(6) << res.magnitude 
                  << " best=" << res.data << "\n";
    }
    
    // Now show the first 20 Walsh symbols from offset 0
    std::cout << "\n=== First 20 Walsh symbols from offset 0 ===\n";
    decoder.reset();
    for (int w = 0; w < 20; w++) {
        int pos = w * 64;
        if (pos + 64 > (int)sym4800.size()) break;
        auto res = decoder.decode(&sym4800[pos]);
        std::cout << "Walsh " << std::setw(2) << w << ": data=" << res.data 
                  << " mag=" << std::setw(6) << std::fixed << std::setprecision(0) << res.magnitude << "\n";
    }
    
    return 0;
}
