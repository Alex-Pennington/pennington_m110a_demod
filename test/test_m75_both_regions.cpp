#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>

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

std::string try_decode(const std::vector<complex_t>& symbols_4800, int offset, int scr) {
    Walsh75Decoder decoder(45);
    decoder.set_scrambler_count(scr);
    
    std::vector<int8_t> soft_bits;
    int block_count = 0;
    
    for (int w = 0; w < 45; w++) {
        int pos = offset + w * 64;
        if (pos + 64 > (int)symbols_4800.size()) break;
        block_count++;
        bool is_mes = (block_count == 45);
        if (is_mes) block_count = 0;
        auto r = decoder.decode(&symbols_4800[pos], is_mes);
        Walsh75Decoder::gray_decode(r.data, r.soft, soft_bits);
    }
    
    if (soft_bits.size() < 90) return "";
    
    InterleaverParams params;
    params.rows = 10; params.cols = 9; params.row_inc = 7; params.col_inc = 2;
    MultiModeInterleaver deinterleaver(params);
    
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.begin() + 90);
    auto deint = deinterleaver.deinterleave(block);
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> bits;
    std::vector<soft_bit_t> soft(deint.begin(), deint.end());
    viterbi.decode_block(soft, bits, true);
    
    std::string out;
    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) if (bits[i+b]) byte |= (1 << (7-b));
        out += (char)byte;
    }
    return out;
}

int main() {
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f; cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f; cfg.preamble_symbols = 1440;
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    std::vector<complex_t> symbols_4800;
    for (const auto& s : result.data_symbols) {
        symbols_4800.push_back(s); symbols_4800.push_back(s);
    }
    
    std::cout << "=== Testing Both Regions ===\n\n";
    
    // Region 1: offset 1572
    std::cout << "Region 1 (offset 1572):\n";
    for (int scr = 90; scr < 100; scr++) {
        std::string out = try_decode(symbols_4800, 1572, scr);
        printf("  scr=%3d: ", scr);
        for (int i = 0; i < 5 && i < (int)out.size(); i++) printf("%02x ", (uint8_t)out[i]);
        printf(" \"%.8s\"\n", out.c_str());
    }
    
    // Region 2: offset 3838
    std::cout << "\nRegion 2 (offset 3838):\n";
    for (int scr = 40; scr < 50; scr++) {
        std::string out = try_decode(symbols_4800, 3838, scr);
        printf("  scr=%3d: ", scr);
        for (int i = 0; i < 5 && i < (int)out.size(); i++) printf("%02x ", (uint8_t)out[i]);
        printf(" \"%.8s\"\n", out.c_str());
    }
    
    return 0;
}
