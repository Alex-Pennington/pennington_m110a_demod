/**
 * Compare symbols between loopback and real signal
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
    // Generate loopback reference
    std::cout << "=== Loopback Reference ===\n";
    
    // Simple test: generate MNS[0] (all zeros = all +1)
    static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
    static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    
    // First 10 symbols of MNS[0] scrambled with scrambler starting at 0
    int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
    std::vector<int> scrambler(160);
    for (int i = 0; i < 160; i++) {
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10]; sreg[10] = sreg[9]; sreg[9] = sreg[8];
            sreg[8] = sreg[7]; sreg[7] = sreg[6]; sreg[6] = sreg[5] ^ carry;
            sreg[5] = sreg[4]; sreg[4] = sreg[3] ^ carry; sreg[3] = sreg[2];
            sreg[2] = sreg[1]; sreg[1] = sreg[0] ^ carry; sreg[0] = carry;
        }
        scrambler[i] = (sreg[2] << 2) | (sreg[1] << 1) | sreg[0];
    }
    
    std::cout << "First 10 scrambler tribits: ";
    for (int i = 0; i < 10; i++) std::cout << scrambler[i] << " ";
    std::cout << "\n";
    
    std::cout << "Expected symbols for MNS[0] (Walsh 0=0=all +1):\n";
    for (int i = 0; i < 10; i++) {
        int sym = (0 + scrambler[i]) % 8;  // MNS[0][i] = 0
        float I = psk8_i[sym];
        float Q = psk8_q[sym];
        float phase = std::atan2(Q, I) * 180 / M_PI;
        std::cout << "  " << i << ": sym=" << sym 
                  << " I=" << std::fixed << std::setprecision(3) << I 
                  << " Q=" << Q << " phase=" << phase << "°\n";
    }
    
    // Load real signal
    std::cout << "\n=== Real Signal ===\n";
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) return 1;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    // Show first 10 symbols at a few offsets
    for (int offset : {0, 786, 1572}) {
        std::cout << "\nOffset " << offset << ":\n";
        for (int i = 0; i < 10; i++) {
            if (offset + i >= (int)result.data_symbols.size()) break;
            auto s = result.data_symbols[offset + i];
            float mag = std::abs(s);
            float phase = std::atan2(s.imag(), s.real()) * 180 / M_PI;
            // Quantize to nearest 8PSK point
            int nearest = ((int)std::round(phase / 45) + 8) % 8;
            std::cout << "  " << i << ": I=" << std::fixed << std::setprecision(3) << s.real()
                      << " Q=" << s.imag() << " mag=" << mag << " phase=" << phase 
                      << "° nearest=" << nearest << "\n";
        }
    }
    
    // Try to find a region where symbols match the expected pattern
    std::cout << "\n=== Pattern Search ===\n";
    std::cout << "Looking for MNS[0] pattern (scrambled sym 6,5,1,2,0,7,1,1,6,4...)\n";
    
    // Expected pattern for MNS[0] with scrambler at 0
    std::vector<int> expected;
    for (int i = 0; i < 32; i++) {
        expected.push_back((0 + scrambler[i]) % 8);
    }
    
    for (int offset = 0; offset < 2000; offset++) {
        int matches = 0;
        for (int i = 0; i < 10; i++) {
            if (offset + i >= (int)result.data_symbols.size()) break;
            auto s = result.data_symbols[offset + i];
            float phase = std::atan2(s.imag(), s.real()) * 180 / M_PI;
            int nearest = ((int)std::round(phase / 45) + 8) % 8;
            if (nearest == expected[i]) matches++;
        }
        if (matches >= 8) {
            std::cout << "High match at offset " << offset << ": " << matches << "/10\n";
        }
    }
    
    return 0;
}
