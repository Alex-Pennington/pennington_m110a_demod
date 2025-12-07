#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <fstream>
#include <iomanip>

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
    for (size_t i = 0; i < raw.size(); i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

int main() {
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    std::cout << "Read " << samples.size() << " samples\n";
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    std::cout << "MSDMT: " << result.data_symbols.size() << " symbols\n";
    
    // Duplicate to 4800 Hz
    std::vector<complex_t> symbols_4800;
    for (const auto& s : result.data_symbols) {
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    
    // Fine scan around best region
    std::cout << "\nFine offset scan:\n";
    std::cout << "Offset  Mag     Walsh pattern (first 15)\n";
    std::cout << "------  ------  -------------------------\n";
    
    for (int offset = 3820; offset <= 3860; offset += 1) {
        Walsh75Decoder dec(45);
        float total = 0;
        std::string pattern;
        
        for (int w = 0; w < 15; w++) {
            int pos = offset + w * 64;
            if (pos + 64 > (int)symbols_4800.size()) break;
            auto r = dec.decode(&symbols_4800[pos], false);
            total += r.magnitude;
            pattern += std::to_string(r.data);
        }
        
        std::cout << std::setw(6) << offset << "  " 
                  << std::setw(6) << std::fixed << std::setprecision(0) << total << "  "
                  << pattern << "\n";
    }
    
    // Also scan earlier region around 1572
    std::cout << "\nEarlier region scan:\n";
    for (int offset = 1560; offset <= 1600; offset += 1) {
        Walsh75Decoder dec(45);
        float total = 0;
        std::string pattern;
        
        for (int w = 0; w < 15; w++) {
            int pos = offset + w * 64;
            if (pos + 64 > (int)symbols_4800.size()) break;
            auto r = dec.decode(&symbols_4800[pos], false);
            total += r.magnitude;
            pattern += std::to_string(r.data);
        }
        
        std::cout << std::setw(6) << offset << "  " 
                  << std::setw(6) << std::fixed << std::setprecision(0) << total << "  "
                  << pattern << "\n";
    }
    
    return 0;
}
