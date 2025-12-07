/**
 * Full decode of 2400S reference file with known plaintext
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
    std::cout << "=== Decode 2400S Reference File ===" << std::endl;
    std::cout << "Expected: THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890" << std::endl;
    std::cout << std::endl;
    
    std::string file = "/mnt/user-data/uploads/tx_2400S_20251206_100439_978.pcm";
    auto samples = read_pcm(file);
    std::cout << "Samples: " << samples.size() << " (" << samples.size()/48000.0f << " sec)" << std::endl;
    
    // Decoder
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    std::cout << "Preamble correlation: " << result.correlation << std::endl;
    std::cout << "Mode detected: " << result.mode_name << std::endl;
    std::cout << "D1=" << result.d1 << " D2=" << result.d2 << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    // For M2400S: 8-PSK, no repetition, 40x72 interleave
    // Let's try decoding with correct mode params regardless of detection
    
    // Descramble and get soft bits
    uint16_t lfsr = 0xBAD;
    std::vector<soft_bit_t> soft_bits;
    
    const complex_t con[8] = {
        {1,0}, {0.707107f,0.707107f}, {0,1}, {-0.707107f,0.707107f},
        {-1,0}, {-0.707107f,-0.707107f}, {0,-1}, {0.707107f,-0.707107f}
    };
    
    // M2400S: 32 data + 16 probe per frame
    int data_per_frame = 32;
    int probe_per_frame = 16;
    int frame_len = data_per_frame + probe_per_frame;
    
    int sym_idx = 0;
    while (sym_idx < result.data_symbols.size()) {
        // Process data symbols
        for (int d = 0; d < data_per_frame && sym_idx < result.data_symbols.size(); d++, sym_idx++) {
            complex_t sym = result.data_symbols[sym_idx];
            
            // Generate scrambler
            int scr = 0;
            for (int i = 0; i < 8; i++) {
                int fb = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
                lfsr = ((lfsr << 1) | fb) & 0xFFF;
                if (i == 7) scr = (lfsr >> 9) & 7;
            }
            
            // Descramble
            complex_t desc = sym * std::conj(con[scr]);
            
            // Find best tribit
            int best_tri = 0;
            float best_corr = -1e9f;
            for (int t = 0; t < 8; t++) {
                float corr = desc.real() * con[t].real() + desc.imag() * con[t].imag();
                if (corr > best_corr) {
                    best_corr = corr;
                    best_tri = t;
                }
            }
            
            // 8-PSK: 3 soft bits per symbol
            float conf = std::abs(desc) * 40.0f;
            conf = std::min(conf, 127.0f);
            
            // MS-DMT convention: +soft=0, -soft=1
            soft_bits.push_back((best_tri & 4) ? -conf : conf);
            soft_bits.push_back((best_tri & 2) ? -conf : conf);
            soft_bits.push_back((best_tri & 1) ? -conf : conf);
        }
        
        // Skip probe symbols (but still advance scrambler)
        for (int p = 0; p < probe_per_frame && sym_idx < result.data_symbols.size(); p++, sym_idx++) {
            for (int i = 0; i < 8; i++) {
                int fb = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
                lfsr = ((lfsr << 1) | fb) & 0xFFF;
            }
        }
    }
    
    std::cout << "Soft bits: " << soft_bits.size() << std::endl;
    
    // Deinterleave (40x72 for M2400S)
    MultiModeInterleaver interleaver(ModeId::M2400S);
    int block_size = interleaver.block_size();
    std::cout << "Interleaver block: " << block_size << std::endl;
    
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
    
    std::cout << "\n=== Decoded Output ===" << std::endl;
    std::cout << "Bytes: " << bytes.size() << std::endl;
    
    std::cout << "Hex (first 64): ";
    for (size_t i = 0; i < 64 && i < bytes.size(); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i] << " ";
    }
    std::cout << std::dec << std::endl;
    
    std::cout << "ASCII: \"";
    for (size_t i = 0; i < bytes.size(); i++) {
        char c = bytes[i];
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
    std::cout << "\"" << std::endl;
    
    // Check for expected text
    std::string expected = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::string got(bytes.begin(), bytes.begin() + std::min(bytes.size(), expected.size()));
    
    int matches = 0;
    for (size_t i = 0; i < std::min(got.size(), expected.size()); i++) {
        if (got[i] == expected[i]) matches++;
    }
    std::cout << "\nCharacter matches: " << matches << "/" << expected.size() << std::endl;
    
    return 0;
}
