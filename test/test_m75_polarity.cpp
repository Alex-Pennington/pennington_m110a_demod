/**
 * Test M75 with soft bit polarity inversion
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
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

void test_with_inversion(const std::vector<complex_t>& symbols_4800, int offset, bool invert_soft) {
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    // Decode 45 Walsh symbols
    for (int w = 0; w < 45; w++) {
        int pos = offset + w * 64;
        if (pos + 64 > (int)symbols_4800.size()) return;
        auto res = decoder.decode(&symbols_4800[pos]);
        
        // Gray decode with optional inversion
        int s = static_cast<int>(res.soft * 127);
        s = std::max(-127, std::min(127, s));
        if (invert_soft) s = -s;
        
        switch (res.data) {
            case 0: soft_bits.push_back(s);  soft_bits.push_back(s);  break;
            case 1: soft_bits.push_back(s);  soft_bits.push_back(-s); break;
            case 2: soft_bits.push_back(-s); soft_bits.push_back(-s); break;
            case 3: soft_bits.push_back(-s); soft_bits.push_back(s);  break;
        }
    }
    
    // Deinterleave
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver deinterleaver(params);
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
    auto deint = deinterleaver.deinterleave(block);
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deint, decoded_bits, true);
    
    auto bytes = bits_to_bytes(decoded_bits);
    
    std::cout << (invert_soft ? "Inverted: " : "Normal:   ");
    for (auto b : bytes) {
        if (b >= 32 && b < 127) std::cout << (char)b;
        else std::cout << ".";
    }
    std::cout << " (";
    for (auto b : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    std::cout << ")\n" << std::dec;
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
    
    std::vector<complex_t> symbols_4800;
    for (const auto& s : result.data_symbols) {
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    
    std::cout << "Testing polarity inversion at various offsets:\n\n";
    
    for (int offset = 0; offset <= 2000; offset += 200) {
        std::cout << "Offset " << offset << ":\n";
        test_with_inversion(symbols_4800, offset, false);
        test_with_inversion(symbols_4800, offset, true);
        std::cout << "\n";
    }
    
    return 0;
}
