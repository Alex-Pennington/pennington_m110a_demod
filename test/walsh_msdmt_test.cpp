/**
 * Walsh 75bps Test Using MSDMT Symbol Extraction
 * 
 * Uses MSDMT decoder to extract symbols at 2400 Hz, then applies Walsh
 * correlation. Since Walsh decode expects i*2 spacing at 4800 Hz,
 * we need to either:
 * A) Interpolate 2400 Hz symbols to 4800 Hz, or
 * B) Modify correlation to work on consecutive 2400 Hz symbols
 * 
 * This test explores both approaches.
 */

#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>

using namespace m110a;

// Use PI from m110a namespace (defined in constants.h)

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

// 8PSK constellation
const complex_t PSK8[8] = {
    {1.0f, 0.0f}, {0.7071f, 0.7071f}, {0.0f, 1.0f}, {-0.7071f, 0.7071f},
    {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}
};

// Scrambler
class Scrambler75 {
public:
    Scrambler75() {
        uint16_t r = 0;
        int init[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1};
        for (int i = 0; i < 12; i++) if (init[i]) r |= (1 << i);
        
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                r = ((r << 1) & 0xFFF) | ((((r >> 11) ^ (r >> 6) ^ (r >> 4) ^ (r >> 1)) & 1));
            }
            bits_[i] = (((r >> 0) & 1) << 2) | (((r >> 1) & 1) << 1) | ((r >> 2) & 1);
        }
    }
    int get(int idx) const { return bits_[idx % 160]; }
private:
    int bits_[160];
};

// Read PCM file
std::vector<float> read_pcm(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return {};
    
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    std::vector<int16_t> raw(size / 2);
    f.read(reinterpret_cast<char*>(raw.data()), size);
    
    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

// Generate expected scrambled Walsh pattern at position scr_offset
void generate_expected(const int* walsh, complex_t* out, 
                       const Scrambler75& scr, int scr_offset) {
    for (int i = 0; i < 32; i++) {
        // TX scrambles: (walsh_val + scrambler) % 8
        int scrambled_val = (walsh[i] + scr.get(scr_offset + i)) % 8;
        out[i] = PSK8[scrambled_val];
    }
}

// Correlate against expected pattern (direct, no i*2 spacing)
float correlate_direct(const complex_t* in, const complex_t* expected, int len) {
    complex_t sum(0, 0);
    for (int i = 0; i < len; i++) {
        // Conjugate multiply
        sum += in[i] * std::conj(expected[i]);
    }
    return std::norm(sum);
}

// Find best Walsh match
int decode_walsh_direct(const complex_t* symbols, bool is_mes,
                        const Scrambler75& scr, int scr_offset, float& out_mag) {
    float best = -1;
    int best_idx = 0;
    
    for (int d = 0; d < 4; d++) {
        complex_t expected[32];
        generate_expected(is_mes ? mes[d] : mns[d], expected, scr, scr_offset);
        float mag = correlate_direct(symbols, expected, 32);
        if (mag > best) {
            best = mag;
            best_idx = d;
        }
    }
    
    out_mag = best;
    return best_idx;
}

int main() {
    std::cout << "Walsh 75bps Test Using MSDMT Symbol Extraction\n";
    std::cout << "==============================================\n\n";
    
    // Read PCM file
    const char* filename = "/home/claude/tx_75S_20251206_202410_888.pcm";
    auto samples = read_pcm(filename);
    if (samples.empty()) {
        std::cout << "Cannot read " << filename << "\n";
        return 1;
    }
    std::cout << "Read " << samples.size() << " samples at 48kHz\n\n";
    
    // Use MSDMT decoder to extract symbols
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;  // Short interleave
    cfg.verbose = false;
    
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    std::cout << "MSDMT Results:\n";
    std::cout << "  Preamble found: " << (result.preamble_found ? "YES" : "NO") << "\n";
    std::cout << "  Correlation: " << result.correlation << "\n";
    std::cout << "  Accuracy: " << result.accuracy << "%\n";
    std::cout << "  Start sample: " << result.start_sample << "\n";
    std::cout << "  Phase offset: " << (result.phase_offset * 180 / PI) << "°\n";
    std::cout << "  Mode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")\n";
    std::cout << "  Data symbols: " << result.data_symbols.size() << " at 2400 Hz\n\n";
    
    if (result.data_symbols.size() < 100) {
        std::cout << "Not enough data symbols extracted\n";
        return 1;
    }
    
    // Examine first few data symbols
    std::cout << "First 64 data symbols (phases in degrees):\n";
    for (int i = 0; i < 64 && i < (int)result.data_symbols.size(); i++) {
        float phase = std::atan2(result.data_symbols[i].imag(), 
                                 result.data_symbols[i].real()) * 180 / PI;
        int psk_pos = ((int)std::round(phase / 45) + 8) % 8;
        std::cout << std::setw(2) << psk_pos << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << "\n";
    
    // Walsh correlation on data symbols
    Scrambler75 scr;
    
    // For M75S, we expect D1=0, D2=0 (based on mode_config.h)
    // Data starts after 1440 symbols preamble
    // Each Walsh symbol is 32 8PSK symbols at 2400 Hz
    
    std::cout << "Walsh Correlation Test (direct on 2400 Hz symbols):\n";
    
    int scr_offset = 0;
    std::vector<int> decoded;
    
    for (int walsh_idx = 0; walsh_idx < 20; walsh_idx++) {
        int sym_start = walsh_idx * 32;
        if (sym_start + 32 > (int)result.data_symbols.size()) break;
        
        bool is_mes = (walsh_idx % 45 == 0);  // MES every 45th block
        
        float mag;
        int data = decode_walsh_direct(&result.data_symbols[sym_start], is_mes,
                                       scr, scr_offset, mag);
        decoded.push_back(data);
        
        std::cout << "  Walsh " << std::setw(2) << walsh_idx << ": data=" << data
                  << " mag=" << std::fixed << std::setprecision(1) << mag
                  << (is_mes ? " (MES)" : "") << "\n";
        
        scr_offset += 32;
    }
    
    // Convert to bytes
    std::cout << "\nDecoded bytes: ";
    for (size_t i = 0; i + 3 < decoded.size(); i += 4) {
        uint8_t byte = 0;
        for (int j = 0; j < 4; j++) {
            byte = (byte << 2) | decoded[i + j];
        }
        if (byte >= 32 && byte < 127) {
            std::cout << (char)byte;
        } else {
            std::cout << "[" << std::hex << (int)byte << std::dec << "]";
        }
    }
    std::cout << "\n\n";
    
    // Try all 8 phase offsets
    std::cout << "Trying different phase offsets:\n";
    
    for (int phase_idx = 0; phase_idx < 8; phase_idx++) {
        float phase = phase_idx * PI / 4;
        complex_t rot(std::cos(phase), std::sin(phase));
        
        // Rotate all data symbols
        std::vector<complex_t> rotated(result.data_symbols.size());
        for (size_t i = 0; i < result.data_symbols.size(); i++) {
            rotated[i] = result.data_symbols[i] * rot;
        }
        
        // Decode first 10 Walsh symbols
        scr_offset = 0;
        float total_mag = 0;
        std::string decoded_str;
        
        for (int walsh_idx = 0; walsh_idx < 10; walsh_idx++) {
            int sym_start = walsh_idx * 32;
            if (sym_start + 32 > (int)rotated.size()) break;
            
            float mag;
            int data = decode_walsh_direct(&rotated[sym_start], false, scr, scr_offset, mag);
            total_mag += mag;
            decoded_str += ('0' + data);
            scr_offset += 32;
        }
        
        std::cout << "  Phase " << phase_idx << " (" << (phase_idx * 45) << "°): "
                  << "total_mag=" << std::fixed << std::setprecision(1) << total_mag
                  << " decoded=" << decoded_str << "\n";
    }
    
    return 0;
}
