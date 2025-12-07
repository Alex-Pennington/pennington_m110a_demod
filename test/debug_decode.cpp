/**
 * Debug the decode chain step by step
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"
#include "modem/multimode_interleaver.h"

using namespace m110a;
using complex_t = std::complex<float>;

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
    std::string file = "/mnt/user-data/uploads/tx_1200S_20251206_100436_261.pcm";
    auto samples = read_pcm(file);
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    // Show first few tribits (before descrambling)
    std::cout << "\nFirst 40 symbols (raw tribits before descrambling):" << std::endl;
    for (int i = 0; i < 40 && i < result.data_symbols.size(); i++) {
        complex_t sym = result.data_symbols[i];
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(sym - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
    }
    std::cout << std::endl;
    
    // Descramble first frame
    std::cout << "\nFirst 40 symbols (after descrambling):" << std::endl;
    uint16_t lfsr = 0xBAD;
    
    for (int i = 0; i < 40 && i < result.data_symbols.size(); i++) {
        // Generate scrambler
        int scr = 0;
        for (int j = 0; j < 8; j++) {
            int fb = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
            lfsr = ((lfsr << 1) | fb) & 0xFFF;
            if (j == 7) scr = (lfsr >> 9) & 7;
        }
        
        complex_t sym = result.data_symbols[i];
        complex_t desc = sym * std::conj(con[scr]);
        
        int best = 0;
        float best_dist = 1e9f;
        for (int t = 0; t < 8; t++) {
            float dist = std::abs(desc - con[t]);
            if (dist < best_dist) { best_dist = dist; best = t; }
        }
        std::cout << best;
        if ((i+1) % 20 == 0) std::cout << " | ";
    }
    std::cout << std::endl;
    
    // For M1200S, we should see probe symbols at positions 20-39
    // Probe symbols should decode to known pattern
    std::cout << "\nExpected probe pattern (positions 20-39):" << std::endl;
    // The probe is a known sequence - let's see what it should be
    
    // Show first 8 bytes as bits to check alignment
    std::cout << "\nConverting first 20 descrambled symbols to dibits:" << std::endl;
    lfsr = 0xBAD;
    std::vector<int> dibits;
    
    for (int i = 0; i < 20 && i < result.data_symbols.size(); i++) {
        int scr = 0;
        for (int j = 0; j < 8; j++) {
            int fb = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
            lfsr = ((lfsr << 1) | fb) & 0xFFF;
            if (j == 7) scr = (lfsr >> 9) & 7;
        }
        
        complex_t sym = result.data_symbols[i];
        complex_t desc = sym * std::conj(con[scr]);
        
        // QPSK dibit from I/Q
        int bit0 = desc.real() > 0 ? 0 : 1;  // I bit
        int bit1 = desc.imag() > 0 ? 0 : 1;  // Q bit
        dibits.push_back(bit0);
        dibits.push_back(bit1);
    }
    
    std::cout << "Dibits: ";
    for (int b : dibits) std::cout << b;
    std::cout << std::endl;
    
    // Pack into bytes
    std::cout << "First 5 bytes: ";
    for (int b = 0; b < 5; b++) {
        int val = 0;
        for (int i = 0; i < 8; i++) {
            val = (val << 1) | dibits[b*8 + i];
        }
        std::cout << std::hex << std::setw(2) << std::setfill('0') << val << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Expected first 5 bytes: "THE Q" = 0x54 0x48 0x45 0x20 0x51
    std::cout << "Expected: 54 48 45 20 51 (THE Q)" << std::endl;
    
    return 0;
}
