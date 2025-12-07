/**
 * Walsh 75bps Test with Proper Signal Processing
 * 
 * This test implements the reference signal processing chain:
 * 1. 48kHz PCM input
 * 2. Downconvert to baseband
 * 3. Resample to 9600 Hz (ref modem rate)
 * 4. Apply matched filter + decimate to 4800 Hz
 * 5. Walsh correlation with i*2 spacing
 */

#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cstdint>

using complex_t = std::complex<float>;
constexpr float PI = 3.14159265358979323846f;

// 8PSK constellation
const complex_t PSK8[8] = {
    {1.0f, 0.0f},
    {0.7071f, 0.7071f},
    {0.0f, 1.0f},
    {-0.7071f, 0.7071f},
    {-1.0f, 0.0f},
    {-0.7071f, -0.7071f},
    {0.0f, -1.0f},
    {0.7071f, -0.7071f}
};

// Reference RX filter (19 taps at 9600 Hz)
const float rx_coffs[19] = {
    0.001572f, 0.004287f, 0.004740f, -0.006294f, -0.028729f,
    -0.034880f, 0.015939f, 0.131216f, 0.257323f, 0.312500f,
    0.257323f, 0.131216f, 0.015939f, -0.034880f, -0.028729f,
    -0.006294f, 0.004740f, 0.004287f, 0.001572f
};

// MNS Walsh sequences
const int mns[4][32] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4},
    {0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4},
    {0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0}
};

// MES Walsh sequences
const int mes[4][32] = {
    {0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4},
    {0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0},
    {0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0},
    {0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4}
};

/**
 * 12-bit LFSR scrambler
 */
class Scrambler75 {
public:
    Scrambler75() { generate_sequence(); }
    
    int get(int idx) const { return bits_[idx % 160]; }
    complex_t get_symbol(int idx) const { return PSK8[bits_[idx % 160]]; }
    
private:
    int bits_[160];
    
    void generate_sequence() {
        uint16_t r = 0;
        int init[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1};
        for (int i = 0; i < 12; i++) if (init[i]) r |= (1 << i);
        
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                int carry = (r >> 11) & 1;
                r = ((r << 1) & 0xFFF) | ((((r >> 11) ^ (r >> 6) ^ (r >> 4) ^ (r >> 1)) & 1));
            }
            bits_[i] = (((r >> 0) & 1) << 2) | (((r >> 1) & 1) << 1) | ((r >> 2) & 1);
        }
    }
};

/**
 * Apply matched filter to complex samples
 */
complex_t apply_filter(const complex_t* in, const float* taps, int ntaps) {
    complex_t sum(0, 0);
    for (int i = 0; i < ntaps; i++) {
        sum += in[i] * taps[i];
    }
    return sum;
}

/**
 * Resample from src_rate to dst_rate using linear interpolation
 */
std::vector<complex_t> resample(const std::vector<complex_t>& in, 
                                 float src_rate, float dst_rate) {
    float ratio = src_rate / dst_rate;
    size_t out_len = static_cast<size_t>(in.size() / ratio);
    std::vector<complex_t> out(out_len);
    
    for (size_t i = 0; i < out_len; i++) {
        float pos = i * ratio;
        size_t idx = static_cast<size_t>(pos);
        float frac = pos - idx;
        
        if (idx + 1 < in.size()) {
            out[i] = in[idx] * (1.0f - frac) + in[idx + 1] * frac;
        } else {
            out[i] = in[idx];
        }
    }
    return out;
}

/**
 * Scramble Walsh sequence
 */
void scramble_walsh(const int* walsh, complex_t* out, 
                    const Scrambler75& scr, int scr_offset) {
    for (int i = 0; i < 32; i++) {
        complex_t in_sym = PSK8[walsh[i]];
        complex_t scr_sym = scr.get_symbol(i + scr_offset);
        out[i] = complex_t(
            in_sym.real() * scr_sym.real() - in_sym.imag() * scr_sym.imag(),
            in_sym.real() * scr_sym.imag() + in_sym.imag() * scr_sym.real()
        );
    }
}

/**
 * Match sequence with i*2 spacing (reference implementation)
 */
float match_sequence(const complex_t* in, const complex_t* seq, int length) {
    complex_t temp(0, 0);
    for (int i = 0; i < length; i++) {
        // Conjugate multiply: conj(seq) * in
        temp += complex_t(
            in[i * 2].real() * seq[i].real() + in[i * 2].imag() * seq[i].imag(),
            in[i * 2].imag() * seq[i].real() - in[i * 2].real() * seq[i].imag()
        );
    }
    return std::norm(temp);
}

/**
 * Decode Walsh symbol
 */
int decode_walsh(const complex_t* in, bool is_mes, 
                 const Scrambler75& scr, int scr_offset, float& out_mag) {
    float best_mag = -1;
    int best_data = 0;
    
    for (int d = 0; d < 4; d++) {
        complex_t expected[32];
        const int* walsh = is_mes ? mes[d] : mns[d];
        scramble_walsh(walsh, expected, scr, scr_offset);
        
        float mag = match_sequence(in, expected, 32);
        if (mag > best_mag) {
            best_mag = mag;
            best_data = d;
        }
    }
    
    out_mag = best_mag;
    return best_data;
}

int main() {
    std::cout << "Walsh 75bps Test with Proper Signal Processing\n";
    std::cout << "==============================================\n\n";
    
    const char* filename = "/home/claude/tx_75S_20251206_202410_888.pcm";
    
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        std::cout << "Cannot open " << filename << "\n";
        return 1;
    }
    
    f.seekg(0, std::ios::end);
    size_t file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    size_t num_samples = file_size / 2;
    std::vector<int16_t> raw(num_samples);
    f.read(reinterpret_cast<char*>(raw.data()), file_size);
    
    std::cout << "1. Read " << num_samples << " samples at 48kHz\n";
    
    // Step 1: Downconvert to baseband at 48kHz
    std::vector<complex_t> bb_48k(num_samples);
    float phase = 0;
    float phase_inc = 2.0f * PI * 1800.0f / 48000.0f;
    
    for (size_t i = 0; i < num_samples; i++) {
        float s = raw[i] / 32768.0f;
        bb_48k[i] = complex_t(s * std::cos(phase), -s * std::sin(phase));
        phase += phase_inc;
        if (phase > 2 * PI) phase -= 2 * PI;
    }
    
    // Step 2: Resample 48kHz → 9600 Hz
    auto bb_9600 = resample(bb_48k, 48000.0f, 9600.0f);
    std::cout << "2. Resampled to " << bb_9600.size() << " samples at 9600 Hz\n";
    
    // Step 3: Apply matched filter + 2x decimation → 4800 Hz
    std::vector<complex_t> bb_4800;
    int half_filter = 19 / 2;
    
    for (size_t i = half_filter; i + half_filter < bb_9600.size(); i += 2) {
        bb_4800.push_back(apply_filter(&bb_9600[i - half_filter], rx_coffs, 19));
    }
    std::cout << "3. Filtered and decimated to " << bb_4800.size() << " samples at 4800 Hz\n";
    
    // Preamble: 1440 symbols at 2400 Hz = 2880 samples at 4800 Hz
    int preamble_end_4800 = 2880;
    
    std::cout << "\n4. Searching for Walsh correlations after preamble...\n";
    
    Scrambler75 scr;
    
    float best_total = 0;
    int best_offset = 0;
    int best_phase_idx = 0;
    
    // Search across timing offsets and phase
    for (int phase_idx = 0; phase_idx < 8; phase_idx++) {
        float test_phase = phase_idx * PI / 4;
        complex_t rot(std::cos(test_phase), std::sin(test_phase));
        
        for (int offset = preamble_end_4800 - 200; offset < preamble_end_4800 + 200; offset++) {
            if (offset < 0 || offset + 320 > (int)bb_4800.size()) continue;
            
            // Rotate for phase test
            std::vector<complex_t> rotated(320);
            for (int i = 0; i < 320; i++) {
                rotated[i] = bb_4800[offset + i] * rot;
            }
            
            float total_mag = 0;
            int scr_offset = 0;
            
            // Test 5 Walsh symbols
            for (int sym = 0; sym < 5; sym++) {
                float mag;
                decode_walsh(&rotated[sym * 64], false, scr, scr_offset, mag);
                total_mag += mag;
                scr_offset += 32;
            }
            
            if (total_mag > best_total) {
                best_total = total_mag;
                best_offset = offset;
                best_phase_idx = phase_idx;
            }
        }
    }
    
    std::cout << "   Best: offset=" << best_offset 
              << " (delta=" << (best_offset - preamble_end_4800) << ")"
              << " phase=" << best_phase_idx << " total_mag=" << best_total << "\n";
    
    // Decode at best position
    float best_phase = best_phase_idx * PI / 4;
    complex_t rot(std::cos(best_phase), std::sin(best_phase));
    
    std::cout << "\n5. Decoding Walsh symbols:\n";
    
    int scr_offset = 0;
    std::vector<int> decoded_data;
    
    for (int sym = 0; sym < 27; sym++) {  // ~54 bits / 2 bits per Walsh = 27 symbols
        int idx = best_offset + sym * 64;
        if (idx + 64 > (int)bb_4800.size()) break;
        
        std::vector<complex_t> rotated(64);
        for (int i = 0; i < 64; i++) {
            rotated[i] = bb_4800[idx + i] * rot;
        }
        
        // MES every 45 blocks (block 0 is MES), but for now test MNS
        bool is_mes = (sym % 45 == 0);
        
        float mag;
        int data = decode_walsh(rotated.data(), is_mes, scr, scr_offset, mag);
        decoded_data.push_back(data);
        
        std::cout << "   " << std::setw(2) << sym << ": data=" << data 
                  << " mag=" << std::fixed << std::setprecision(1) << mag
                  << (is_mes ? " (MES)" : "") << "\n";
        
        scr_offset += 32;
    }
    
    // Convert to bytes (2 bits per Walsh symbol)
    std::cout << "\n6. Decoded bytes:\n   ";
    
    for (size_t i = 0; i + 3 < decoded_data.size(); i += 4) {
        // Gray decode: 0→00, 1→01, 2→10, 3→11
        uint8_t byte = 0;
        for (int j = 0; j < 4; j++) {
            int bits;
            switch (decoded_data[i + j]) {
                case 0: bits = 0b00; break;
                case 1: bits = 0b01; break;
                case 2: bits = 0b10; break;
                case 3: bits = 0b11; break;
            }
            byte = (byte << 2) | bits;
        }
        
        if (byte >= 32 && byte < 127) {
            std::cout << (char)byte;
        } else {
            std::cout << "[" << std::hex << (int)byte << std::dec << "]";
        }
    }
    std::cout << "\n\n";
    
    // Also try without Gray decode
    std::cout << "   Raw (no Gray): ";
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
    std::cout << "\n";
    
    return 0;
}
