/**
 * Complete Walsh 75bps Decoder
 * 
 * Full decode chain:
 * 1. MSDMT symbol extraction (2400 Hz)
 * 2. Walsh correlation (4800 Hz simulation)
 * 3. Gray decode to soft bits
 * 4. Deinterleave (10×9 matrix)
 * 5. Viterbi decode
 * 6. Output bytes
 */

#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>

using namespace m110a;

// Walsh sequences
const int mns[4][32] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4},
    {0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4},
    {0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0}
};
const int mes[4][32] = {
    {0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4},
    {0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0},
    {0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0},
    {0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4}
};

const complex_t con_symbol[8] = {
    {1.0f, 0.0f}, {0.707f, 0.707f}, {0.0f, 1.0f}, {-0.707f, 0.707f},
    {-1.0f, 0.0f}, {-0.707f, -0.707f}, {0.0f, -1.0f}, {0.707f, -0.707f}
};

// Scrambler
class Walsh75Scrambler {
public:
    Walsh75Scrambler() {
        int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                int carry = sreg[11];
                for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
                sreg[6] ^= carry; sreg[4] ^= carry; sreg[1] ^= carry;
                sreg[0] = carry;
            }
            bits_[i] = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
            seq_[i] = con_symbol[bits_[i]];
        }
    }
    complex_t get_sym(int idx) const { return seq_[idx % 160]; }
private:
    int bits_[160];
    complex_t seq_[160];
};

// Scramble Walsh sequence
void scramble_walsh(const int* walsh, complex_t* out, const Walsh75Scrambler& scr, int offset) {
    for (int i = 0; i < 32; i++) {
        complex_t in_sym = con_symbol[walsh[i]];
        complex_t scr_sym = scr.get_sym(i + offset);
        out[i] = complex_t(
            in_sym.real() * scr_sym.real() - in_sym.imag() * scr_sym.imag(),
            in_sym.real() * scr_sym.imag() + in_sym.imag() * scr_sym.real()
        );
    }
}

// Match sequence with i*2 spacing
float match_sequence(const complex_t* in, const complex_t* seq, int length) {
    complex_t temp(0, 0);
    for (int i = 0; i < length; i++) {
        temp += complex_t(
            in[i*2].real() * seq[i].real() + in[i*2].imag() * seq[i].imag(),
            in[i*2].imag() * seq[i].real() - in[i*2].real() * seq[i].imag()
        );
    }
    return std::norm(temp);
}

// Decode Walsh symbol with soft output
int decode_walsh_soft(const complex_t* in, bool is_mes, 
                      const Walsh75Scrambler& scr, int offset, float& soft) {
    float mags[4];
    float total = 0;
    
    for (int d = 0; d < 4; d++) {
        complex_t expected[32];
        scramble_walsh(is_mes ? mes[d] : mns[d], expected, scr, offset);
        mags[d] = match_sequence(in, expected, 32);
        total += mags[d];
    }
    
    // Find best
    int best = 0;
    for (int d = 1; d < 4; d++) {
        if (mags[d] > mags[best]) best = d;
    }
    
    // Soft decision based on relative magnitude
    soft = std::sqrt(mags[best] / (total + 0.0001f));
    return best;
}

// Gray decode Walsh data to soft bits (from reference)
// 0 → (+, +), 1 → (+, -), 2 → (-, -), 3 → (-, +)
void gray_decode_soft(int data, float soft, std::vector<soft_bit_t>& out) {
    int s = static_cast<int>(soft * 127);
    s = std::max(-127, std::min(127, s));
    
    switch (data) {
        case 0: out.push_back(s);  out.push_back(s);  break;
        case 1: out.push_back(s);  out.push_back(-s); break;
        case 2: out.push_back(-s); out.push_back(-s); break;
        case 3: out.push_back(-s); out.push_back(s);  break;
    }
}

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

int main() {
    std::cout << "Complete Walsh 75bps Decoder\n";
    std::cout << "===========================\n\n";
    
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) { std::cout << "Cannot read file\n"; return 1; }
    
    // MSDMT extraction
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    std::cout << "Symbols: " << result.data_symbols.size() << " at 2400 Hz\n";
    std::cout << "Mode: D1=" << result.d1 << " D2=" << result.d2 << "\n\n";
    
    // Duplicate to 4800 Hz
    std::vector<complex_t> sym_4800;
    for (const auto& s : result.data_symbols) {
        sym_4800.push_back(s);
        sym_4800.push_back(s);
    }
    
    Walsh75Scrambler scr;
    
    // Search for best offset
    float best_total = 0;
    int best_start = 0;
    
    for (int start = 0; start < 2000; start += 2) {
        if (start + 640 > (int)sym_4800.size()) break;
        
        float total = 0;
        int scr_off = 0;
        for (int w = 0; w < 10; w++) {
            complex_t expected[32];
            scramble_walsh(mns[0], expected, scr, scr_off);  // Just use pattern 0 for search
            total += match_sequence(&sym_4800[start + w*64], expected, 32);
            scr_off += 32;
        }
        if (total > best_total) {
            best_total = total;
            best_start = start;
        }
    }
    
    std::cout << "Best offset: " << best_start << " (total=" << best_total << ")\n\n";
    
    // Decode Walsh symbols
    std::cout << "Walsh decode (first 45 = 1 interleaver block):\n";
    
    int scr_offset = 0;
    std::vector<soft_bit_t> soft_bits;
    std::vector<int> walsh_data;
    
    // M75NS: 45 Walsh symbols per interleaver block
    // Interleaver: 10×9 = 90 bits = 45 Walsh symbols × 2 bits
    const int WALSH_PER_BLOCK = 45;
    
    for (int w = 0; w < WALSH_PER_BLOCK; w++) {
        int pos = best_start + w * 64;
        if (pos + 64 > (int)sym_4800.size()) break;
        
        // MES at block 0, then every 45 blocks
        bool is_mes = (w == 0);
        
        float soft;
        int data = decode_walsh_soft(&sym_4800[pos], is_mes, scr, scr_offset, soft);
        walsh_data.push_back(data);
        gray_decode_soft(data, soft, soft_bits);
        
        if (w < 15 || w >= 40) {
            std::cout << "  " << std::setw(2) << w << ": " << data 
                      << " (soft=" << std::fixed << std::setprecision(2) << soft << ")\n";
        } else if (w == 15) {
            std::cout << "  ...\n";
        }
        
        scr_offset = (scr_offset + 32) % 160;
    }
    
    std::cout << "\nSoft bits: " << soft_bits.size() << " bits\n";
    
    // Deinterleave
    // M75NS interleaver: rows=10, cols=9, row_inc=7, col_inc=2
    InterleaverParams il_params{10, 9, 7, 2, 45};
    MultiModeInterleaver deint(il_params);
    
    if (soft_bits.size() >= 90) {
        std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.begin() + 90);
        auto deinterleaved = deint.deinterleave(block);
        
        std::cout << "Deinterleaved: " << deinterleaved.size() << " bits\n";
        
        // Viterbi decode
        ViterbiDecoder viterbi;
        auto decoded_bits = viterbi.decode(deinterleaved);
        
        std::cout << "Viterbi output: " << decoded_bits.size() << " bits\n";
        
        // Pack to bytes
        std::cout << "\nDecoded bytes: ";
        for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; b++) {
                if (decoded_bits[i + b]) byte |= (1 << (7 - b));
            }
            if (byte >= 32 && byte < 127) {
                std::cout << (char)byte;
            } else {
                std::cout << "[" << std::hex << (int)byte << std::dec << "]";
            }
        }
        std::cout << "\n";
    }
    
    // Also try without deinterleaver (raw Walsh → Viterbi)
    std::cout << "\nAlternate: Walsh → Viterbi (no deinterleave):\n";
    {
        std::vector<soft_bit_t> raw_soft;
        for (int d : walsh_data) {
            // Simple hard decision
            raw_soft.push_back((d & 2) ? 64 : -64);
            raw_soft.push_back((d & 1) ? 64 : -64);
        }
        
        Viterbi viterbi;
        auto decoded = viterbi.decode(raw_soft);
        
        std::cout << "  Output: ";
        for (size_t i = 0; i + 7 < decoded.size(); i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8; b++) {
                if (decoded[i + b]) byte |= (1 << (7 - b));
            }
            if (byte >= 32 && byte < 127) {
                std::cout << (char)byte;
            } else {
                std::cout << "[" << std::hex << (int)byte << std::dec << "]";
            }
        }
        std::cout << "\n";
    }
    
    std::cout << "\nExpected: Hello (48 65 6C 6C 6F)\n";
    
    return 0;
}
