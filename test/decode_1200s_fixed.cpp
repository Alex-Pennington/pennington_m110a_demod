/**
 * Decode M1200S with fixed scrambler
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

// Fixed MS-DMT scrambler
int msdmt_scrambler(uint16_t& lfsr) {
    for (int j = 0; j < 8; j++) {
        int carry = (lfsr >> 11) & 1;
        uint16_t new_lfsr = ((lfsr << 1) | carry) & 0xFFF;
        if (carry) {
            new_lfsr ^= (1 << 6) | (1 << 4) | (1 << 1);
        }
        lfsr = new_lfsr;
    }
    return lfsr & 7;  // Low 3 bits
}

int main() {
    std::cout << "=== Decode M1200S with Fixed Scrambler ===" << std::endl;
    std::cout << "Expected: THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890" << std::endl << std::endl;
    
    std::string file = "/mnt/user-data/uploads/tx_1200S_20251206_100436_261.pcm";
    auto samples = read_pcm(file);
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "Mode: " << result.mode_name << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    // Fixed scrambler
    uint16_t lfsr = 0xBAD;
    std::vector<soft_bit_t> soft_bits;
    
    int data_per_frame = 20;
    int probe_per_frame = 20;
    
    int sym_idx = 0;
    while (sym_idx < result.data_symbols.size()) {
        // Process data symbols
        for (int d = 0; d < data_per_frame && sym_idx < result.data_symbols.size(); d++, sym_idx++) {
            complex_t sym = result.data_symbols[sym_idx];
            int scr = msdmt_scrambler(lfsr);
            complex_t desc = sym * std::conj(con[scr]);
            
            // QPSK soft bits
            float conf = std::abs(desc) * 40.0f;
            conf = std::min(conf, 127.0f);
            soft_bits.push_back(desc.real() > 0 ? conf : -conf);
            soft_bits.push_back(desc.imag() > 0 ? conf : -conf);
        }
        
        // Skip probe symbols
        for (int p = 0; p < probe_per_frame && sym_idx < result.data_symbols.size(); p++, sym_idx++) {
            msdmt_scrambler(lfsr);
        }
    }
    
    std::cout << "Soft bits: " << soft_bits.size() << std::endl;
    
    // Deinterleave
    MultiModeInterleaver interleaver(ModeId::M1200S);
    int block_size = interleaver.block_size();
    
    std::vector<soft_bit_t> deinterleaved;
    for (size_t i = 0; i + block_size <= soft_bits.size(); i += block_size) {
        std::vector<soft_bit_t> block(soft_bits.begin() + i, soft_bits.begin() + i + block_size);
        auto di = interleaver.deinterleave(block);
        deinterleaved.insert(deinterleaved.end(), di.begin(), di.end());
    }
    
    std::cout << "Deinterleaved: " << deinterleaved.size() << std::endl;
    
    // Viterbi decode
    ViterbiDecoder vit;
    std::vector<uint8_t> decoded;
    vit.decode_block(deinterleaved, decoded, true);
    
    // Pack to bytes
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    int bc = 0;
    for (uint8_t b : decoded) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) {
            bytes.push_back(cur);
            cur = 0;
            bc = 0;
        }
    }
    
    std::cout << "\n=== Decoded Output ===" << std::endl;
    std::cout << "Bytes: " << bytes.size() << std::endl;
    
    std::cout << "ASCII: \"";
    for (uint8_t b : bytes) {
        char c = b;
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
    std::cout << "\"" << std::endl;
    
    std::string expected = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    int matches = 0;
    for (size_t i = 0; i < std::min(bytes.size(), expected.size()); i++) {
        if (bytes[i] == (uint8_t)expected[i]) matches++;
    }
    std::cout << "Matches: " << matches << "/" << expected.size() << std::endl;
    
    return 0;
}
