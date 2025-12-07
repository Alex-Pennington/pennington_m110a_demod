/**
 * Analyze Decoded Reference File Data
 * 
 * Look for patterns in decoded data to understand what the
 * reference files contain.
 */

#include "m110a/msdmt_decoder.h"
#include "m110a/mode_config.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include "modem/scrambler.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <map>

using namespace m110a;

// WAV reader (simplified)
std::vector<float> read_wav(const std::string& filename, int& sr) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return {};
    char hdr[44]; file.read(hdr, 44);
    sr = *reinterpret_cast<uint32_t*>(hdr + 24);
    uint32_t data_size = *reinterpret_cast<uint32_t*>(hdr + 40);
    int n = data_size / 2;
    std::vector<float> samples(n);
    std::vector<int16_t> raw(n);
    file.read(reinterpret_cast<char*>(raw.data()), data_size);
    for (int i = 0; i < n; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

ModeId get_mode_id(const std::string& name) {
    if (name == "M600S") return ModeId::M600S;
    if (name == "M1200S") return ModeId::M1200S;
    if (name == "M2400S") return ModeId::M2400S;
    if (name == "M4800S") return ModeId::M4800S;
    return ModeId::M2400S;
}

int get_repetition(const std::string& mode) {
    if (mode.find("150") != std::string::npos) return 8;
    if (mode.find("300") != std::string::npos) return 4;
    if (mode.find("600") != std::string::npos) return 2;
    return 1;
}

int inv_gray_8psk(int pos) {
    static const int inv_gray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    return inv_gray[pos & 7];
}

void descramble_to_soft_bits(const std::vector<complex_t>& symbols,
                              int unknown_len, int known_len,
                              int bits_per_sym,
                              std::vector<soft_bit_t>& soft_bits) {
    RefScrambler scr;
    int pattern_len = unknown_len + known_len;
    int sym_idx = 0;
    
    while (sym_idx + unknown_len <= (int)symbols.size()) {
        for (int i = 0; i < unknown_len && sym_idx + i < (int)symbols.size(); i++) {
            complex_t sym = symbols[sym_idx + i];
            uint8_t scr_val = scr.next_tribit();
            float scr_phase = -scr_val * (PI / 4.0f);
            sym *= std::polar(1.0f, scr_phase);
            
            float angle = std::atan2(sym.imag(), sym.real());
            float mag = std::abs(sym);
            float conf = std::min(mag * 30.0f, 127.0f);
            
            if (bits_per_sym == 3) {
                int pos = ((int)std::round(angle * 4.0f / PI) % 8 + 8) % 8;
                int tribit = inv_gray_8psk(pos);
                soft_bits.push_back((tribit & 4) ? conf : -conf);
                soft_bits.push_back((tribit & 2) ? conf : -conf);
                soft_bits.push_back((tribit & 1) ? conf : -conf);
            } else if (bits_per_sym == 2) {
                soft_bits.push_back(sym.real() * conf);
                soft_bits.push_back(sym.imag() * conf);
            } else {
                soft_bits.push_back(sym.real() * conf);
            }
        }
        for (int i = 0; i < known_len; i++) scr.next_tribit();
        sym_idx += pattern_len;
        if (known_len == 0) break;
    }
}

std::vector<soft_bit_t> combine_repetitions(const std::vector<soft_bit_t>& input, int rep) {
    if (rep <= 1) return input;
    int n = input.size() / rep;
    std::vector<soft_bit_t> out(n);
    for (int i = 0; i < n; i++) {
        float sum = 0;
        for (int r = 0; r < rep; r++) {
            int idx = i * rep + r;
            if (idx < (int)input.size()) sum += input[idx];
        }
        out[i] = std::max(-127.0f, std::min(127.0f, sum / sqrtf((float)rep)));
    }
    return out;
}

int main() {
    std::cout << "=== Reference File Data Analysis ===" << std::endl;
    
    std::string base = "/mnt/user-data/uploads/MIL-STD-188-110A_";
    std::string file = "2400bps_Short";
    
    int sr;
    auto samples = read_wav(base + file + ".wav", sr);
    if (samples.empty()) { std::cerr << "Failed to load file" << std::endl; return 1; }
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    std::cout << "\nMode: " << result.mode_name << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    // Get params
    int unknown_len = 32, known_len = 16, bits_per_sym = 3, rep = 1;
    
    // Descramble
    std::vector<soft_bit_t> soft_bits;
    descramble_to_soft_bits(result.data_symbols, unknown_len, known_len, bits_per_sym, soft_bits);
    auto combined = combine_repetitions(soft_bits, rep);
    
    // Deinterleave
    ModeId mode_id = ModeId::M2400S;
    MultiModeInterleaver deinterleaver(mode_id);
    std::vector<soft_bit_t> deinterleaved;
    int bs = deinterleaver.block_size();
    
    for (size_t i = 0; i + bs <= combined.size(); i += bs) {
        std::vector<soft_bit_t> block(combined.begin() + i, combined.begin() + i + bs);
        auto di = deinterleaver.deinterleave(block);
        deinterleaved.insert(deinterleaved.end(), di.begin(), di.end());
    }
    
    // Viterbi
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deinterleaved, decoded_bits, true);
    
    // Pack bytes
    std::vector<uint8_t> bytes;
    uint8_t cur = 0; int bc = 0;
    for (uint8_t b : decoded_bits) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) { bytes.push_back(cur); cur = 0; bc = 0; }
    }
    
    std::cout << "\n=== Decoded Data Analysis ===" << std::endl;
    std::cout << "Total bytes: " << bytes.size() << std::endl;
    
    // Byte value histogram
    std::map<int, int> hist;
    for (uint8_t b : bytes) hist[b]++;
    
    std::cout << "\nMost common bytes:" << std::endl;
    std::vector<std::pair<int, int>> sorted_hist(hist.begin(), hist.end());
    std::sort(sorted_hist.begin(), sorted_hist.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    
    for (int i = 0; i < 10 && i < (int)sorted_hist.size(); i++) {
        int val = sorted_hist[i].first;
        int cnt = sorted_hist[i].second;
        std::cout << "  0x" << std::hex << std::setw(2) << std::setfill('0') << val
                  << " (" << std::dec << cnt << " times) ";
        if (val >= 32 && val < 127) std::cout << "'" << (char)val << "'";
        std::cout << std::endl;
    }
    
    // Check for patterns
    std::cout << "\nPattern search:" << std::endl;
    
    // Look for repeating sequences
    bool found_pattern = false;
    for (int period = 1; period <= 16; period++) {
        int matches = 0;
        for (size_t i = period; i < bytes.size(); i++) {
            if (bytes[i] == bytes[i - period]) matches++;
        }
        float ratio = (float)matches / (bytes.size() - period);
        if (ratio > 0.3) {
            std::cout << "  Period " << period << ": " << (ratio * 100) << "% matches" << std::endl;
            found_pattern = true;
        }
    }
    if (!found_pattern) std::cout << "  No significant repeating patterns found" << std::endl;
    
    // Look for null padding
    int consecutive_nulls = 0, max_nulls = 0;
    for (uint8_t b : bytes) {
        if (b == 0) {
            consecutive_nulls++;
            max_nulls = std::max(max_nulls, consecutive_nulls);
        } else {
            consecutive_nulls = 0;
        }
    }
    std::cout << "\nMax consecutive nulls: " << max_nulls << std::endl;
    
    // Bit statistics
    int ones = 0;
    for (uint8_t b : decoded_bits) if (b) ones++;
    float one_ratio = (float)ones / decoded_bits.size();
    std::cout << "Bit statistics: " << ones << " ones / " << decoded_bits.size()
              << " total (" << (one_ratio * 100) << "%)" << std::endl;
    
    // Print all bytes as hex
    std::cout << "\n=== All Decoded Bytes (hex) ===" << std::endl;
    for (size_t i = 0; i < bytes.size(); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
        if ((i + 1) % 32 == 0) std::cout << std::endl;
        else std::cout << " ";
    }
    std::cout << std::dec << std::endl;
    
    return 0;
}
