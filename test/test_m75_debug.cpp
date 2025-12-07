/**
 * Debug M75 Decode - dump Walsh patterns and search for "Hello"
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>

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

bool try_decode(const std::vector<complex_t>& symbols_4800, int offset, bool verbose) {
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    // Decode 45 Walsh symbols (one interleaver block)
    for (int w = 0; w < 45; w++) {
        int pos = offset + w * 64;
        if (pos + 64 > (int)symbols_4800.size()) return false;
        auto res = decoder.decode(&symbols_4800[pos]);
        Walsh75Decoder::gray_decode(res.data, res.soft, soft_bits);
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
    
    // Check for Hello
    std::string expected = "Hello";
    for (size_t i = 0; i + expected.size() <= bytes.size(); i++) {
        if (memcmp(&bytes[i], expected.data(), expected.size()) == 0) {
            if (verbose) {
                std::cout << "FOUND at offset " << offset << ": ";
                for (auto b : bytes) std::cout << (char)(b >= 32 && b < 127 ? b : '.');
                std::cout << "\n";
            }
            return true;
        }
    }
    
    if (verbose) {
        std::cout << "Offset " << offset << ": ";
        for (auto b : bytes) {
            if (b >= 32 && b < 127) std::cout << (char)b;
            else std::cout << ".";
        }
        std::cout << " (";
        for (auto b : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        std::cout << ")\n" << std::dec;
    }
    
    return false;
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
    
    std::cout << "Symbols: " << symbols_4800.size() << "\n";
    std::cout << "Testing offsets 0 to 3200 (step 2):\n\n";
    
    // Test every offset
    for (int offset = 0; offset < 3200; offset += 2) {
        if (try_decode(symbols_4800, offset, offset % 100 == 0)) {
            std::cout << "\n*** SUCCESS at offset " << offset << " ***\n";
            try_decode(symbols_4800, offset, true);
            return 0;
        }
    }
    
    std::cout << "\n'Hello' not found. Showing first 20 results:\n";
    for (int offset = 0; offset < 40; offset += 2) {
        try_decode(symbols_4800, offset, true);
    }
    
    return 1;
}
