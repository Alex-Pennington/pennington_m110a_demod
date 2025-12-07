/**
 * Walsh 75bps Test - Simulating Reference 4800 Hz
 * 
 * The reference code operates at 4800 Hz (2 samples per symbol) and uses
 * i*2 indexing in match_sequence. This test simulates that by duplicating
 * the 2400 Hz symbols from MSDMT.
 */

#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>

using namespace m110a;

// Walsh sequences (from reference t110a.cpp)
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

// 8PSK constellation (from reference t110a.cpp)
const complex_t con_symbol[8] = {
    {1.0f, 0.0f}, {0.707f, 0.707f}, {0.0f, 1.0f}, {-0.707f, 0.707f},
    {-1.0f, 0.0f}, {-0.707f, -0.707f}, {0.0f, -1.0f}, {0.707f, -0.707f}
};

// Scrambler (from reference t110a.cpp)
class Walsh75Scrambler {
public:
    Walsh75Scrambler() {
        int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
        
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                int carry = sreg[11];
                sreg[11] = sreg[10];
                sreg[10] = sreg[9];
                sreg[9] = sreg[8];
                sreg[8] = sreg[7];
                sreg[7] = sreg[6];
                sreg[6] = sreg[5] ^ carry;
                sreg[5] = sreg[4];
                sreg[4] = sreg[3] ^ carry;
                sreg[3] = sreg[2];
                sreg[2] = sreg[1];
                sreg[1] = sreg[0] ^ carry;
                sreg[0] = carry;
            }
            bits_[i] = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
            seq_[i] = con_symbol[bits_[i]];
        }
    }
    
    int get_bits(int idx) const { return bits_[idx % 160]; }
    complex_t get_sym(int idx) const { return seq_[idx % 160]; }
    
private:
    int bits_[160];
    complex_t seq_[160];
};

// Complex multiply helpers
inline float cmultRealConj(const complex_t& a, const complex_t& b) {
    return a.real() * b.real() + a.imag() * b.imag();
}
inline float cmultImagConj(const complex_t& a, const complex_t& b) {
    return a.imag() * b.real() - a.real() * b.imag();
}

// Scramble Walsh sequence (from reference)
void scramble_75bps_sequence(const int* walsh, complex_t* out, 
                              const Walsh75Scrambler& scr, int s_count) {
    for (int i = 0; i < 32; i++) {
        complex_t in_sym = con_symbol[walsh[i]];
        complex_t scr_sym = scr.get_sym(i + s_count);
        // Complex multiply (not conjugate)
        out[i] = complex_t(
            in_sym.real() * scr_sym.real() - in_sym.imag() * scr_sym.imag(),
            in_sym.real() * scr_sym.imag() + in_sym.imag() * scr_sym.real()
        );
    }
}

// Match sequence with i*2 spacing (exact reference implementation)
float match_sequence(const complex_t* in, const complex_t* seq, int length) {
    complex_t temp(0, 0);
    for (int i = 0; i < length; i++) {
        temp += complex_t(
            cmultRealConj(in[i * 2], seq[i]),
            cmultImagConj(in[i * 2], seq[i])
        );
    }
    return temp.real() * temp.real() + temp.imag() * temp.imag();
}

// Decode Walsh symbol
int decode_walsh(const complex_t* in, bool is_mes, 
                 const Walsh75Scrambler& scr, int scr_count, float& out_mag) {
    float best = -1;
    int best_data = 0;
    
    for (int d = 0; d < 4; d++) {
        complex_t expected[32];
        scramble_75bps_sequence(is_mes ? mes[d] : mns[d], expected, scr, scr_count);
        float mag = match_sequence(in, expected, 32);
        if (mag > best) {
            best = mag;
            best_data = d;
        }
    }
    
    out_mag = best;
    return best_data;
}

// Read PCM file
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

int main() {
    std::cout << "Walsh 75bps Test - Simulating Reference 4800 Hz\n";
    std::cout << "===============================================\n\n";
    
    // Read file
    const char* filename = "/home/claude/tx_75S_20251206_202410_888.pcm";
    auto samples = read_pcm(filename);
    if (samples.empty()) {
        std::cout << "Cannot read " << filename << "\n";
        return 1;
    }
    std::cout << "Read " << samples.size() << " samples at 48kHz\n\n";
    
    // Use MSDMT to extract symbols at 2400 Hz
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    std::cout << "MSDMT: " << result.data_symbols.size() << " symbols at 2400 Hz\n";
    std::cout << "Mode detected: D1=" << result.d1 << " D2=" << result.d2;
    if (result.d1 == 7 && result.d2 == 5) std::cout << " (M75NS confirmed!)";
    std::cout << "\n\n";
    
    // Duplicate symbols to simulate 4800 Hz (for i*2 correlation)
    std::vector<complex_t> symbols_4800;
    for (const auto& sym : result.data_symbols) {
        symbols_4800.push_back(sym);  // Position i*2
        symbols_4800.push_back(sym);  // Position i*2+1
    }
    std::cout << "Duplicated to " << symbols_4800.size() << " samples at 4800 Hz\n\n";
    
    Walsh75Scrambler scr;
    
    // Test different starting offsets across wider range
    std::cout << "Testing Walsh correlations at different offsets:\n";
    
    float best_total = 0;
    int best_start = 0;
    int best_phase = 0;
    
    for (int phase_idx = 0; phase_idx < 8; phase_idx++) {
        float phase = phase_idx * PI / 4;
        complex_t rot(std::cos(phase), std::sin(phase));
        
        // Search wider range - symbols_4800 has 43k samples
        for (int start = 0; start < 2000; start += 2) {
            if (start + 640 > (int)symbols_4800.size()) break;
            
            // Rotate for phase
            std::vector<complex_t> rotated(640);
            for (int i = 0; i < 640; i++) {
                rotated[i] = symbols_4800[start + i] * rot;
            }
            
            float total = 0;
            int scr_count = 0;
            
            // Test first 10 Walsh symbols - try all MNS first
            for (int w = 0; w < 10; w++) {
                float mag;
                // First try all as MNS to check correlation
                decode_walsh(&rotated[w * 64], false, scr, scr_count, mag);
                total += mag;
                scr_count += 32;
            }
            
            if (total > best_total) {
                best_total = total;
                best_start = start;
                best_phase = phase_idx;
            }
        }
    }
    
    std::cout << "Best: start=" << best_start << " phase=" << best_phase 
              << " total=" << best_total << "\n\n";
    
    // Decode at best position
    float best_phase_rad = best_phase * PI / 4;
    complex_t rot(std::cos(best_phase_rad), std::sin(best_phase_rad));
    
    // Rotate all symbols
    std::vector<complex_t> rotated_4800(symbols_4800.size());
    for (size_t i = 0; i < symbols_4800.size(); i++) {
        rotated_4800[i] = symbols_4800[i] * rot;
    }
    
    std::cout << "Decoding Walsh symbols from offset " << best_start << ":\n";
    
    int scr_count = 0;
    std::vector<int> decoded_data;
    
    // Decode all as MNS first (most blocks are MNS)
    for (int w = 0; w < 54; w++) {  
        int pos = best_start + w * 64;
        if (pos + 64 > (int)rotated_4800.size()) break;
        
        float mag;
        int data = decode_walsh(&rotated_4800[pos], false, scr, scr_count, mag);
        decoded_data.push_back(data);
        
        if (w < 30) {
            std::cout << "  " << std::setw(2) << w << ": data=" << data
                      << " mag=" << std::fixed << std::setprecision(1) << mag << "\n";
        }
        
        scr_count = (scr_count + 32) % 160;
    }
    
    // Convert to bytes
    // Gray decode: 0→00, 1→01, 2→11, 3→10
    int gray_inv[4] = {0, 1, 3, 2};
    
    std::cout << "\nDecoded bytes (Gray decoded): ";
    for (size_t i = 0; i + 3 < decoded_data.size(); i += 4) {
        uint8_t byte = 0;
        for (int j = 0; j < 4; j++) {
            int bits = gray_inv[decoded_data[i + j]];
            byte = (byte << 2) | bits;
        }
        if (byte >= 32 && byte < 127) {
            std::cout << (char)byte;
        } else {
            std::cout << "[" << std::hex << (int)byte << std::dec << "]";
        }
    }
    std::cout << "\n";
    
    // Also show raw (no Gray)
    std::cout << "Decoded bytes (raw): ";
    for (size_t i = 0; i + 3 < decoded_data.size(); i += 4) {
        uint8_t byte = 0;
        for (int j = 0; j < 4; j++) {
            byte = (byte << 2) | decoded_data[i + j];
        }
        if (byte >= 32 && byte < 127) {
            std::cout << (char)byte;
        } else {
            std::cout << "[" << std::hex << (int)byte << std::dec << "]";
        }
    }
    std::cout << "\n\n";
    
    // Expected output: "Hello" = 0x48 0x65 0x6C 0x6C 0x6F
    std::cout << "Expected 'Hello' = 48 65 6C 6C 6F (hex)\n";
    
    return 0;
}
