/**
 * Full M75 decode with correct scrambler
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

InterleaverParams get_m75ns_params() {
    InterleaverParams p;
    p.rows = 10; p.cols = 9; p.row_inc = 7; p.col_inc = 2; p.block_count_mod = 45;
    return p;
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
    
    std::cout << "=== Full M75 Decode ===\n";
    std::cout << "Symbols: " << symbols_4800.size() << "\n\n";
    
    // Try scrambler values around 45
    for (int scrambler_start = 40; scrambler_start <= 50; scrambler_start++) {
        int offset = 3838;
        
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
        
        auto params = get_m75ns_params();
        MultiModeInterleaver deinterleaver(params);
        
        std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.begin() + 90);
        auto deinterleaved = deinterleaver.deinterleave(block);
        
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        std::vector<soft_bit_t> soft_for_viterbi(deinterleaved.begin(), deinterleaved.end());
        viterbi.decode_block(soft_for_viterbi, decoded_bits, true);
        
        // Convert to bytes
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; b++) {
                if (decoded_bits[i + b]) byte |= (1 << (7 - b));
            }
            bytes.push_back(byte);
        }
        
        std::cout << "scr=" << scrambler_start << ": ";
        for (int i = 0; i < 5 && i < (int)bytes.size(); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i] << " ";
        }
        std::cout << std::dec << "  \"";
        for (int i = 0; i < 8 && i < (int)bytes.size(); i++) {
            if (bytes[i] >= 32 && bytes[i] < 127) std::cout << (char)bytes[i];
            else std::cout << ".";
        }
        std::cout << "\"\n";
    }
    
    return 0;
}
