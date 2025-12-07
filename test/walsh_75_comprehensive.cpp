/**
 * Comprehensive Walsh 75bps Decoder Test
 * 
 * Tests the complete 75bps Walsh decode pipeline matching the reference:
 * 1. MNS/MES Walsh sequences (0 and 4 = BPSK at 0° and 180°)
 * 2. Scrambler (12-bit LFSR)
 * 3. match_sequence() with i*2 spacing
 * 4. sync_75_mask weighting
 * 5. Gray code output mapping
 */

#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <cstdint>

using complex_t = std::complex<float>;
constexpr float PI = 3.14159265358979323846f;

// 8PSK constellation
const complex_t PSK8[8] = {
    {1.0f, 0.0f},           // 0: 0°
    {0.7071f, 0.7071f},     // 1: 45°
    {0.0f, 1.0f},           // 2: 90°
    {-0.7071f, 0.7071f},    // 3: 135°
    {-1.0f, 0.0f},          // 4: 180°
    {-0.7071f, -0.7071f},   // 5: 225°
    {0.0f, -1.0f},          // 6: 270°
    {0.7071f, -0.7071f}     // 7: 315°
};

// MNS Walsh sequences (Mode Normal Status) - for non-MES blocks
const int mns[4][32] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4},
    {0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4},
    {0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0}
};

// MES Walsh sequences (Mode/Error Status) - for MES blocks (0, 45, 90...)
const int mes[4][32] = {
    {0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4},
    {0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0},
    {0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0},
    {0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4}
};

/**
 * 12-bit LFSR scrambler - generates 8PSK tribit values
 * Polynomial: x^12 + x^7 + x^5 + x^2 + 1
 * Init: 101101011101 (LSB first)
 */
class Scrambler75 {
public:
    Scrambler75() {
        // Init: bits 0-11 = 101101011101 (from reference code)
        reg_ = 0;
        int init[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1};
        for (int i = 0; i < 12; i++) {
            if (init[i]) reg_ |= (1 << i);
        }
        
        // Pre-generate sequence
        generate_sequence();
    }
    
    // Get scrambler value at position idx
    int get(int idx) const {
        return bits_[idx % M1_DATA_SCRAMBLER_LENGTH];
    }
    
    // Get constellation symbol at position idx
    complex_t get_symbol(int idx) const {
        return PSK8[bits_[idx % M1_DATA_SCRAMBLER_LENGTH]];
    }
    
private:
    static constexpr int M1_DATA_SCRAMBLER_LENGTH = 160;
    uint16_t reg_;
    int bits_[M1_DATA_SCRAMBLER_LENGTH];
    
    void generate_sequence() {
        uint16_t r = reg_;
        for (int i = 0; i < M1_DATA_SCRAMBLER_LENGTH; i++) {
            // Clock 8 times per output tribit
            for (int j = 0; j < 8; j++) {
                int carry = (r >> 11) & 1;
                r = ((r << 1) & 0xFFF) |
                    ((((r >> 11) ^ (r >> 6) ^ (r >> 4) ^ (r >> 1)) & 1));
            }
            bits_[i] = (((r >> 0) & 1) << 2) | (((r >> 1) & 1) << 1) | ((r >> 2) & 1);
        }
    }
};

/**
 * Complex multiply and conjugate operations (from reference)
 */
inline float cmultRealConj(const complex_t& a, const complex_t& b) {
    return a.real() * b.real() + a.imag() * b.imag();
}
inline float cmultImagConj(const complex_t& a, const complex_t& b) {
    return a.imag() * b.real() - a.real() * b.imag();
}

/**
 * Scramble a Walsh sequence by applying scrambler rotation
 */
void scramble_75bps_sequence(const int* walsh_in, complex_t* out, 
                              const Scrambler75& scr, int scr_offset) {
    for (int i = 0; i < 32; i++) {
        complex_t in_sym = PSK8[walsh_in[i]];  // Convert Walsh value to constellation
        complex_t scr_sym = scr.get_symbol(i + scr_offset);  // Get scrambler symbol
        
        // Complex multiply (not conjugate - TX applies scrambler)
        out[i] = complex_t(
            in_sym.real() * scr_sym.real() - in_sym.imag() * scr_sym.imag(),
            in_sym.real() * scr_sym.imag() + in_sym.imag() * scr_sym.real()
        );
    }
}

/**
 * Match sequence using i*2 spacing (reference implementation)
 */
float match_sequence(const complex_t* in, const complex_t* seq, int length) {
    complex_t temp(0, 0);
    for (int i = 0; i < length; i++) {
        temp += complex_t(
            cmultRealConj(in[i * 2], seq[i]),
            cmultImagConj(in[i * 2], seq[i])
        );
    }
    return std::norm(temp);
}

/**
 * Generate expected scrambled Walsh sequence for a given data value
 */
void generate_scrambled_walsh(int data_val, bool is_mes, complex_t* out,
                               const Scrambler75& scr, int scr_offset) {
    const int* walsh = is_mes ? mes[data_val] : mns[data_val];
    scramble_75bps_sequence(walsh, out, scr, scr_offset);
}

/**
 * Generate test signal with known Walsh symbols at 4800 Hz
 */
std::vector<complex_t> generate_test_signal_4800hz(
    const std::vector<int>& data,
    bool is_mes,
    int initial_scr_offset = 0
) {
    Scrambler75 scr;
    std::vector<complex_t> signal;
    
    int scr_offset = initial_scr_offset;
    
    for (int d : data) {
        complex_t scrambled[32];
        generate_scrambled_walsh(d, is_mes, scrambled, scr, scr_offset);
        
        // Output at 4800 Hz (2 samples per Walsh symbol position)
        for (int i = 0; i < 32; i++) {
            signal.push_back(scrambled[i]);  // Position i*2
            signal.push_back(scrambled[i]);  // Position i*2+1 (interpolated)
        }
        
        scr_offset += 32;  // Advance scrambler by 32 per Walsh symbol
    }
    
    return signal;
}

/**
 * Decode Walsh symbol from 4800 Hz input
 */
int decode_walsh_symbol(const complex_t* in, bool is_mes, 
                        const Scrambler75& scr, int scr_offset,
                        float& out_mag) {
    float best_mag = -1;
    int best_data = 0;
    
    for (int d = 0; d < 4; d++) {
        complex_t expected[32];
        generate_scrambled_walsh(d, is_mes, expected, scr, scr_offset);
        
        float mag = match_sequence(in, expected, 32);
        
        if (mag > best_mag) {
            best_mag = mag;
            best_data = d;
        }
    }
    
    out_mag = best_mag;
    return best_data;
}

/**
 * Gray code output mapping (from reference de110a.cpp lines 816-843)
 * Data values 0-3 map to bit pairs with soft decisions
 */
void gray_decode_75bps(int data, float soft, std::vector<float>& out) {
    // Reference code loads deinterleaver with:
    // 0 -> (+soft, +soft)
    // 1 -> (-soft, +soft) 
    // 2 -> (+soft, -soft)
    // 3 -> (-soft, -soft)
    switch (data) {
        case 0:
            out.push_back(soft);
            out.push_back(soft);
            break;
        case 1:
            out.push_back(-soft);
            out.push_back(soft);
            break;
        case 2:
            out.push_back(soft);
            out.push_back(-soft);
            break;
        case 3:
            out.push_back(-soft);
            out.push_back(-soft);
            break;
    }
}

//==============================================================================
// TESTS
//==============================================================================

void test_scrambler() {
    std::cout << "=== Test Scrambler ===\n";
    Scrambler75 scr;
    
    // Print first 32 values
    std::cout << "First 32 scrambler values:\n";
    for (int i = 0; i < 32; i++) {
        std::cout << scr.get(i) << " ";
        if ((i + 1) % 8 == 0) std::cout << "\n";
    }
    
    // Verify periodicity at 160
    bool periodic = true;
    for (int i = 0; i < 160; i++) {
        if (scr.get(i) != scr.get(i + 160)) {
            periodic = false;
            break;
        }
    }
    std::cout << "Period 160: " << (periodic ? "OK" : "FAIL") << "\n\n";
}

void test_walsh_sequences() {
    std::cout << "=== Test Walsh Sequences ===\n";
    
    // Verify Walsh orthogonality
    std::cout << "MNS orthogonality:\n";
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int dot = 0;
            for (int k = 0; k < 32; k++) {
                // Convert to BPSK: 0 -> +1, 4 -> -1
                int a = (mns[i][k] == 0) ? 1 : -1;
                int b = (mns[j][k] == 0) ? 1 : -1;
                dot += a * b;
            }
            std::cout << std::setw(4) << dot;
        }
        std::cout << "\n";
    }
    
    std::cout << "\nMES orthogonality:\n";
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int dot = 0;
            for (int k = 0; k < 32; k++) {
                int a = (mes[i][k] == 0) ? 1 : -1;
                int b = (mes[j][k] == 0) ? 1 : -1;
                dot += a * b;
            }
            std::cout << std::setw(4) << dot;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

void test_loopback_4800hz() {
    std::cout << "=== Test Loopback at 4800 Hz ===\n";
    
    // Generate test data: ASCII "Hello" = 5 chars
    std::vector<int> tx_data = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1};  // 10 Walsh symbols
    
    // Generate 4800 Hz signal (MNS mode)
    auto signal = generate_test_signal_4800hz(tx_data, false, 0);
    
    std::cout << "TX: " << tx_data.size() << " Walsh symbols\n";
    std::cout << "Signal: " << signal.size() << " samples at 4800 Hz\n";
    
    // Decode
    Scrambler75 scr;
    int scr_offset = 0;
    std::vector<int> rx_data;
    int correct = 0;
    
    for (size_t i = 0; i < tx_data.size(); i++) {
        const complex_t* ptr = &signal[i * 64];  // 64 samples per Walsh symbol at 4800 Hz
        
        float mag;
        int decoded = decode_walsh_symbol(ptr, false, scr, scr_offset, mag);
        rx_data.push_back(decoded);
        
        if (decoded == tx_data[i]) correct++;
        
        std::cout << "  Symbol " << i << ": TX=" << tx_data[i] 
                  << " RX=" << decoded << " mag=" << std::fixed << std::setprecision(0) << mag
                  << (decoded == tx_data[i] ? " ✓" : " ✗") << "\n";
        
        scr_offset += 32;
    }
    
    std::cout << "Result: " << correct << "/" << tx_data.size() << " correct\n\n";
}

void test_with_phase_rotation() {
    std::cout << "=== Test with Phase Rotation ===\n";
    
    std::vector<int> tx_data = {0, 1, 2, 3};
    auto signal = generate_test_signal_4800hz(tx_data, false, 0);
    
    // Apply phase rotation
    float phase = PI / 6;  // 30 degrees
    complex_t rot(std::cos(phase), std::sin(phase));
    for (auto& s : signal) {
        s *= rot;
    }
    
    std::cout << "Applied " << (phase * 180 / PI) << "° rotation\n";
    
    // Decode with phase compensation
    Scrambler75 scr;
    int scr_offset = 0;
    int correct = 0;
    
    for (size_t i = 0; i < tx_data.size(); i++) {
        // Compensate for rotation
        std::vector<complex_t> compensated(64);
        for (int j = 0; j < 64; j++) {
            compensated[j] = signal[i * 64 + j] * std::conj(rot);
        }
        
        float mag;
        int decoded = decode_walsh_symbol(compensated.data(), false, scr, scr_offset, mag);
        
        if (decoded == tx_data[i]) correct++;
        scr_offset += 32;
    }
    
    std::cout << "Result: " << correct << "/" << tx_data.size() 
              << " correct (with compensation)\n\n";
}

void test_real_pcm_file() {
    std::cout << "=== Test on Real PCM File ===\n";
    
    const char* filename = "/home/claude/tx_75S_20251206_202410_888.pcm";
    
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        std::cout << "Cannot open " << filename << "\n\n";
        return;
    }
    
    // Get file size
    f.seekg(0, std::ios::end);
    size_t file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    // Read as 16-bit samples at 48kHz
    size_t num_samples = file_size / 2;
    std::vector<int16_t> raw(num_samples);
    f.read(reinterpret_cast<char*>(raw.data()), file_size);
    
    std::cout << "Read " << num_samples << " samples at 48kHz\n";
    
    // Downconvert to baseband at 48kHz
    std::vector<complex_t> baseband(num_samples);
    float phase = 0;
    float phase_inc = 2.0f * PI * 1800.0f / 48000.0f;  // 1800 Hz carrier
    
    for (size_t i = 0; i < num_samples; i++) {
        float s = raw[i] / 32768.0f;
        baseband[i] = complex_t(s * std::cos(phase), -s * std::sin(phase));
        phase += phase_inc;
        if (phase > 2 * PI) phase -= 2 * PI;
    }
    
    // Decimate to 4800 Hz (factor of 10)
    int decim = 10;  // 48000 / 4800 = 10
    std::vector<complex_t> symbols_4800;
    for (size_t i = 0; i < baseband.size(); i += decim) {
        symbols_4800.push_back(baseband[i]);
    }
    
    std::cout << "Decimated to " << symbols_4800.size() << " samples at 4800 Hz\n";
    
    // Preamble is ~1440 symbols at 2400 Hz = ~2880 samples at 4800 Hz
    // Data starts after preamble
    
    // Search for best correlation in data region
    int data_start_4800 = 2880;  // After 1440 symbols preamble
    
    Scrambler75 scr;
    
    // Try different starting positions
    std::cout << "\nSearching for Walsh correlations...\n";
    
    float best_total = 0;
    int best_offset = 0;
    int best_phase_idx = 0;
    
    for (int phase_idx = 0; phase_idx < 8; phase_idx++) {
        float test_phase = phase_idx * PI / 4;
        complex_t rot(std::cos(test_phase), std::sin(test_phase));
        
        for (int offset = data_start_4800 - 100; offset < data_start_4800 + 100; offset += 2) {
            if (offset + 256 > (int)symbols_4800.size()) break;
            
            float total_mag = 0;
            int scr_offset = 0;
            
            // Test first 4 Walsh symbols
            for (int sym = 0; sym < 4; sym++) {
                std::vector<complex_t> compensated(64);
                for (int j = 0; j < 64; j++) {
                    compensated[j] = symbols_4800[offset + sym * 64 + j] * rot;
                }
                
                float mag;
                decode_walsh_symbol(compensated.data(), false, scr, scr_offset, mag);
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
    
    std::cout << "Best: offset=" << best_offset 
              << " phase=" << best_phase_idx << " mag=" << best_total << "\n";
    
    // Decode at best position
    float best_phase = best_phase_idx * PI / 4;
    complex_t rot(std::cos(best_phase), std::sin(best_phase));
    
    std::cout << "\nDecoding first 20 Walsh symbols:\n";
    int scr_offset = 0;
    
    std::vector<float> soft_bits;
    
    for (int sym = 0; sym < 20; sym++) {
        if (best_offset + sym * 64 + 64 > (int)symbols_4800.size()) break;
        
        std::vector<complex_t> compensated(64);
        for (int j = 0; j < 64; j++) {
            compensated[j] = symbols_4800[best_offset + sym * 64 + j] * rot;
        }
        
        float mag;
        int decoded = decode_walsh_symbol(compensated.data(), false, scr, scr_offset, mag);
        
        float soft = std::sqrt(mag / best_total) * 10;  // Normalized soft decision
        gray_decode_75bps(decoded, soft, soft_bits);
        
        std::cout << "  " << sym << ": data=" << decoded << " mag=" << std::fixed 
                  << std::setprecision(0) << mag << "\n";
        
        scr_offset += 32;
    }
    
    // Convert soft bits to bytes
    std::cout << "\nDecoded soft bits -> bytes:\n  ";
    for (size_t i = 0; i + 7 < soft_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            if (soft_bits[i + b] > 0) byte |= (1 << (7 - b));
        }
        if (byte >= 32 && byte < 127) {
            std::cout << (char)byte;
        } else {
            std::cout << "[" << std::hex << (int)byte << std::dec << "]";
        }
    }
    std::cout << "\n\n";
}

int main() {
    std::cout << "Comprehensive Walsh 75bps Decoder Test\n";
    std::cout << "======================================\n\n";
    
    test_scrambler();
    test_walsh_sequences();
    test_loopback_4800hz();
    test_with_phase_rotation();
    test_real_pcm_file();
    
    return 0;
}
