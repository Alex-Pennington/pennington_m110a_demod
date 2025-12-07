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

std::string try_decode(const std::vector<complex_t>& symbols_4800, 
                        int offset, int scrambler_start, bool invert) {
    Walsh75Decoder decoder(45);
    decoder.set_scrambler_count(scrambler_start);
    
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
    if (invert) { for (auto& s : soft_bits) s = -s; }
    
    InterleaverParams params;
    params.rows = 10; params.cols = 9; params.row_inc = 7; params.col_inc = 2;
    MultiModeInterleaver deinterleaver(params);
    
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.begin() + 90);
    auto deint = deinterleaver.deinterleave(block);
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    std::vector<soft_bit_t> soft_for_viterbi(deint.begin(), deint.end());
    viterbi.decode_block(soft_for_viterbi, decoded_bits, true);
    
    std::string result;
    for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            if (decoded_bits[i + b]) byte |= (1 << (7 - b));
        }
        result += (char)byte;
    }
    return result;
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
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    
    std::cout << "=== Fine-tuning search ===\n\n";
    
    // Try all offsets and scrambler combos
    for (int offset = 3830; offset <= 3850; offset += 2) {
        for (int scr = 40; scr <= 50; scr++) {
            for (int inv = 0; inv <= 1; inv++) {
                std::string out = try_decode(symbols_4800, offset, scr, inv);
                if (out.size() >= 5 && out[0] == 'H' && out[1] == 'e') {
                    std::cout << "offset=" << offset << " scr=" << scr 
                              << " inv=" << inv << ": ";
                    for (int i = 0; i < 5; i++) {
                        printf("%02x ", (uint8_t)out[i]);
                    }
                    std::cout << "\"" << out.substr(0, 8) << "\"\n";
                }
            }
        }
    }
    
    // Also try offset 1572
    std::cout << "\nAt offset 1572:\n";
    for (int scr = 90; scr <= 100; scr++) {
        for (int inv = 0; inv <= 1; inv++) {
            std::string out = try_decode(symbols_4800, 1572, scr, inv);
            if (out.size() >= 5 && out[0] == 'H') {
                std::cout << "scr=" << scr << " inv=" << inv << ": ";
                for (int i = 0; i < 5; i++) {
                    printf("%02x ", (uint8_t)out[i]);
                }
                std::cout << "\"" << out.substr(0, 8) << "\"\n";
            }
        }
    }
    
    return 0;
}
