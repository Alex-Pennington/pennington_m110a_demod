/**
 * Test M75 with phase rotation
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
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

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            if (bits[i + b]) byte |= (1 << (7 - b));
        }
        bytes.push_back(byte);
    }
    return bytes;
}

bool try_decode(const std::vector<complex_t>& symbols, int offset, float phase_deg) {
    // Apply phase rotation
    float phase_rad = phase_deg * M_PI / 180.0f;
    complex_t rot(std::cos(phase_rad), std::sin(phase_rad));
    
    std::vector<complex_t> rotated;
    for (const auto& s : symbols) {
        rotated.push_back(s * rot);
    }
    
    // Duplicate for 4800 Hz
    std::vector<complex_t> symbols_4800;
    for (const auto& s : rotated) {
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    for (int w = 0; w < 45; w++) {
        int pos = offset * 2 + w * 64;  // offset is in 2400 Hz samples
        if (pos + 64 > (int)symbols_4800.size()) return false;
        auto res = decoder.decode(&symbols_4800[pos]);
        Walsh75Decoder::gray_decode(res.data, res.soft, soft_bits);
    }
    
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver deinterleaver(params);
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
    auto deint = deinterleaver.deinterleave(block);
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deint, decoded_bits, true);
    
    auto bytes = bits_to_bytes(decoded_bits);
    
    // Check for Hello
    return (bytes.size() >= 5 && 
            bytes[0] == 'H' && bytes[1] == 'e' && 
            bytes[2] == 'l' && bytes[3] == 'l' && bytes[4] == 'o');
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
    
    std::cout << "Symbols: " << result.data_symbols.size() << "\n";
    std::cout << "Testing phase rotations at various offsets...\n\n";
    
    // Try different phases and offsets
    for (int offset = 0; offset <= 1600; offset += 32) {
        for (float phase = 0; phase < 360; phase += 45) {
            if (try_decode(result.data_symbols, offset, phase)) {
                std::cout << "*** FOUND at offset=" << offset 
                          << ", phase=" << phase << "° ***\n";
                return 0;
            }
        }
    }
    
    // More fine-grained search
    std::cout << "Fine search...\n";
    for (int offset = 0; offset <= 100; offset += 1) {
        for (float phase = 0; phase < 360; phase += 15) {
            if (try_decode(result.data_symbols, offset, phase)) {
                std::cout << "*** FOUND at offset=" << offset 
                          << ", phase=" << phase << "° ***\n";
                return 0;
            }
        }
    }
    
    std::cout << "'Hello' not found with any phase/offset combination.\n";
    return 1;
}
