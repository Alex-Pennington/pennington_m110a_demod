/**
 * Direct Walsh correlation on 2400 Hz symbols
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

// Direct Walsh correlation on 2400 Hz symbols (no i*2 indexing)
float walsh_correlate_direct(const complex_t* sym, const complex_t* pattern, int len) {
    complex_t sum(0, 0);
    for (int i = 0; i < len; i++) {
        // Conjugate correlation
        sum += complex_t(
            sym[i].real() * pattern[i].real() + sym[i].imag() * pattern[i].imag(),
            sym[i].imag() * pattern[i].real() - sym[i].real() * pattern[i].imag()
        );
    }
    return std::norm(sum);
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
    
    std::cout << "Symbols: " << result.data_symbols.size() << "\n\n";
    
    // Generate MNS/MES patterns with scrambler
    static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
    static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    
    // Scrambler
    int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
    std::vector<int> scrambler(160);
    for (int i = 0; i < 160; i++) {
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10]; sreg[10] = sreg[9]; sreg[9] = sreg[8];
            sreg[8] = sreg[7]; sreg[7] = sreg[6]; sreg[6] = sreg[5] ^ carry;
            sreg[5] = sreg[4]; sreg[4] = sreg[3] ^ carry; sreg[3] = sreg[2];
            sreg[2] = sreg[1]; sreg[1] = sreg[0] ^ carry; sreg[0] = carry;
        }
        scrambler[i] = (sreg[2] << 2) | (sreg[1] << 1) | sreg[0];
    }
    
    // Test correlation at various offsets
    std::cout << "Direct correlation (32 symbols, no i*2):\n";
    std::cout << "Offset   MNS0   MNS1   MNS2   MNS3   Best\n";
    
    for (int offset = 0; offset <= 100; offset++) {
        if (offset + 32 > (int)result.data_symbols.size()) break;
        
        float mags[4];
        for (int p = 0; p < 4; p++) {
            // Generate scrambled pattern
            complex_t pattern[32];
            for (int i = 0; i < 32; i++) {
                int sym = (Walsh75Decoder::MNS[p][i] + scrambler[i]) % 8;
                pattern[i] = complex_t(psk8_i[sym], psk8_q[sym]);
            }
            mags[p] = walsh_correlate_direct(&result.data_symbols[offset], pattern, 32);
        }
        
        int best = 0;
        for (int p = 1; p < 4; p++) {
            if (mags[p] > mags[best]) best = p;
        }
        
        std::cout << std::setw(5) << offset;
        for (int p = 0; p < 4; p++) {
            std::cout << " " << std::setw(6) << std::fixed << std::setprecision(1) << mags[p];
        }
        std::cout << "  best=" << best << "\n";
    }
    
    // Try with different scrambler starting positions
    std::cout << "\n=== Searching different scrambler offsets ===\n";
    for (int scr_offset = 0; scr_offset < 160; scr_offset += 32) {
        float max_total = 0;
        int best_sym_offset = 0;
        
        for (int sym_offset = 0; sym_offset <= 200; sym_offset++) {
            if (sym_offset + 32 > (int)result.data_symbols.size()) break;
            
            float total = 0;
            for (int p = 0; p < 4; p++) {
                complex_t pattern[32];
                for (int i = 0; i < 32; i++) {
                    int sym = (Walsh75Decoder::MNS[p][i] + scrambler[(i + scr_offset) % 160]) % 8;
                    pattern[i] = complex_t(psk8_i[sym], psk8_q[sym]);
                }
                total += walsh_correlate_direct(&result.data_symbols[sym_offset], pattern, 32);
            }
            
            if (total > max_total) {
                max_total = total;
                best_sym_offset = sym_offset;
            }
        }
        
        std::cout << "Scrambler offset " << scr_offset << ": best at sym " << best_sym_offset 
                  << " total=" << std::fixed << std::setprecision(1) << max_total << "\n";
    }
    
    return 0;
}
