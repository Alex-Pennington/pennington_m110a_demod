/**
 * Full decode of 300S reference file
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include "m110a/msdmt_decoder.h"
#include "modem/viterbi.h"
#include "modem/multimode_interleaver.h"

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
    std::string file = "/mnt/user-data/uploads/tx_300S_20251206_100428_384.pcm";
    
    auto samples = read_pcm(file);
    std::cout << "File: " << file << std::endl;
    std::cout << "Samples: " << samples.size() << " (" << samples.size()/48000.0f << " sec)" << std::endl;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "\nMode: " << result.mode_name << std::endl;
    std::cout << "Correlation: " << result.correlation << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    if (result.data_symbols.empty()) {
        std::cout << "No data to decode" << std::endl;
        return 1;
    }
    
    // M300S: QPSK with 2x repetition
    // Descramble and extract soft bits
    uint16_t lfsr = 0xBAD;
    std::vector<soft_bit_t> soft_bits;
    
    // Constellation for descrambling
    const std::complex<float> con[8] = {
        {1,0}, {0.707f,0.707f}, {0,1}, {-0.707f,0.707f},
        {-1,0}, {-0.707f,-0.707f}, {0,-1}, {0.707f,-0.707f}
    };
    
    for (const auto& sym : result.data_symbols) {
        // Generate scrambler tribit
        int scr = 0;
        for (int i = 0; i < 8; i++) {
            int fb = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
            lfsr = ((lfsr << 1) | fb) & 0xFFF;
            if (i == 7) scr = (lfsr >> 9) & 7;
        }
        
        // Descramble
        auto desc = sym * std::conj(con[scr]);
        
        // For QPSK (M300S), use I and Q as soft bits
        float conf = std::abs(desc) * 50.0f;
        conf = std::min(conf, 127.0f);
        
        // MS-DMT convention: +soft = 0, -soft = 1  
        soft_bits.push_back(desc.real() > 0 ? conf : -conf);
        soft_bits.push_back(desc.imag() > 0 ? conf : -conf);
    }
    
    std::cout << "Soft bits: " << soft_bits.size() << std::endl;
    
    // 2x repetition combining for M300S
    std::vector<soft_bit_t> combined;
    for (size_t i = 0; i + 1 < soft_bits.size(); i += 2) {
        combined.push_back((soft_bits[i] + soft_bits[i+1]) / 2);
    }
    std::cout << "After repetition combining: " << combined.size() << std::endl;
    
    // Deinterleave (M300S: 40x36)
    MultiModeInterleaver interleaver(ModeId::M300S);
    int block_size = interleaver.block_size();
    std::cout << "Block size: " << block_size << std::endl;
    
    std::vector<soft_bit_t> deinterleaved;
    for (size_t i = 0; i + block_size <= combined.size(); i += block_size) {
        std::vector<soft_bit_t> block(combined.begin() + i, combined.begin() + i + block_size);
        auto di = interleaver.deinterleave(block);
        deinterleaved.insert(deinterleaved.end(), di.begin(), di.end());
    }
    std::cout << "Deinterleaved: " << deinterleaved.size() << std::endl;
    
    // Viterbi decode
    ViterbiDecoder vit;
    std::vector<uint8_t> decoded;
    vit.decode_block(deinterleaved, decoded, true);
    std::cout << "Decoded bits: " << decoded.size() << std::endl;
    
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
    
    std::cout << "\nDecoded bytes: " << bytes.size() << std::endl;
    std::cout << "First 64 bytes (hex): ";
    for (size_t i = 0; i < 64 && i < bytes.size(); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i] << " ";
    }
    std::cout << std::dec << std::endl;
    
    std::cout << "ASCII: ";
    for (size_t i = 0; i < 64 && i < bytes.size(); i++) {
        char c = bytes[i];
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
    std::cout << std::endl;
    
    return 0;
}
