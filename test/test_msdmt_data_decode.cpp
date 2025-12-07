/**
 * Test MS-DMT Data Decoding with Reference WAV Files
 * 
 * This test verifies:
 * 1. Preamble detection and mode identification
 * 2. Data symbol extraction
 * 3. Descrambling (complex conjugate method)
 * 4. Soft bit demapping
 */

#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"
#include "modem/scrambler.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>

using namespace m110a;

// WAV file reader
struct WavHeader {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
};

std::vector<float> read_wav(const std::string& filename, int& sample_rate) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open: " << filename << std::endl;
        return {};
    }
    
    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), 44);
    
    sample_rate = header.sample_rate;
    int num_samples = header.data_size / (header.bits_per_sample / 8);
    
    std::vector<float> samples(num_samples);
    
    if (header.bits_per_sample == 16) {
        std::vector<int16_t> raw(num_samples);
        file.read(reinterpret_cast<char*>(raw.data()), header.data_size);
        for (int i = 0; i < num_samples; i++) {
            samples[i] = raw[i] / 32768.0f;
        }
    }
    
    return samples;
}

// Get mode parameters
struct ModeParams {
    int unknown_len;  // Data symbols per pattern
    int known_len;    // Probe symbols per pattern
    int bits_per_symbol;
};

ModeParams get_mode_params(const std::string& mode) {
    if (mode.find("75") != std::string::npos) return {32, 0, 1};  // BPSK
    if (mode.find("150") != std::string::npos) return {20, 20, 1};  // BPSK
    if (mode.find("300") != std::string::npos) return {20, 20, 1};  // BPSK
    if (mode.find("600") != std::string::npos) return {20, 20, 2};  // QPSK
    if (mode.find("1200") != std::string::npos) return {20, 20, 2};  // QPSK
    if (mode.find("2400") != std::string::npos) return {32, 16, 3};  // 8PSK
    if (mode.find("4800") != std::string::npos) return {32, 16, 3};  // 8PSK (uncoded)
    return {20, 20, 3};  // Default
}

// Inverse Gray code for 8-PSK
int inv_gray_8psk(int pos) {
    static const int inv_gray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    return inv_gray[pos & 7];
}

// Manual descramble and demap
void descramble_symbols(const std::vector<complex_t>& symbols, 
                        int unknown_len, int known_len,
                        std::vector<int>& tribits,
                        std::vector<float>& soft_bits) {
    RefScrambler scr;
    
    int pattern_len = unknown_len + known_len;
    int sym_idx = 0;
    
    while (sym_idx + unknown_len <= static_cast<int>(symbols.size())) {
        // Process unknown (data) symbols
        for (int i = 0; i < unknown_len && sym_idx + i < static_cast<int>(symbols.size()); i++) {
            complex_t sym = symbols[sym_idx + i];
            uint8_t scr_val = scr.next_tribit();
            
            // Descramble: rotate by -scr_val * 45°
            float scr_phase = -scr_val * (PI / 4.0f);
            sym *= std::polar(1.0f, scr_phase);
            
            // Find nearest position
            float angle = std::atan2(sym.imag(), sym.real());
            int pos = static_cast<int>(std::round(angle * 4.0f / PI));
            pos = ((pos % 8) + 8) % 8;
            
            // Apply inverse Gray code
            int tribit = inv_gray_8psk(pos);
            tribits.push_back(tribit);
            
            // Generate soft decisions
            float mag = std::abs(sym);
            float conf = mag * 10.0f;
            soft_bits.push_back((tribit & 4) ? conf : -conf);
            soft_bits.push_back((tribit & 2) ? conf : -conf);
            soft_bits.push_back((tribit & 1) ? conf : -conf);
        }
        
        // Skip known (probe) symbols - still advance scrambler
        for (int i = 0; i < known_len; i++) {
            scr.next_tribit();
        }
        
        sym_idx += pattern_len;
        
        // Safety break for modes without probes
        if (known_len == 0) break;
    }
}

int main() {
    std::cout << "=== MS-DMT Data Decode Test ===" << std::endl;
    std::cout << std::endl;
    
    // Test on 2400bps_Short (simplest 8-PSK mode with known structure)
    std::string base = "/mnt/user-data/uploads/MIL-STD-188-110A_";
    std::string test_file = "2400bps_Short";
    
    int sr;
    auto samples = read_wav(base + test_file + ".wav", sr);
    
    if (samples.empty()) {
        std::cerr << "Failed to load " << test_file << std::endl;
        return 1;
    }
    
    std::cout << "Loaded " << test_file << ": " << samples.size() << " samples @ " << sr << " Hz" << std::endl;
    
    // Decode
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.verbose = false;
    
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    std::cout << "\n=== Preamble Detection ===" << std::endl;
    std::cout << "Found: " << (result.preamble_found ? "YES" : "NO") << std::endl;
    std::cout << "Correlation: " << std::fixed << std::setprecision(3) << result.correlation << std::endl;
    std::cout << "Start sample: " << result.start_sample << std::endl;
    std::cout << "Phase offset: " << (result.phase_offset * 180.0f / PI) << " degrees" << std::endl;
    std::cout << "Mode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")" << std::endl;
    
    std::cout << "\n=== Symbol Extraction ===" << std::endl;
    std::cout << "Preamble symbols: " << result.preamble_symbols.size() << std::endl;
    std::cout << "Data symbols: " << result.data_symbols.size() << std::endl;
    
    // Get mode parameters
    auto params = get_mode_params(result.mode_name);
    std::cout << "Mode params: " << params.unknown_len << " unknown, " 
              << params.known_len << " known, " << params.bits_per_symbol << " bps" << std::endl;
    
    // Descramble data symbols
    std::vector<int> tribits;
    std::vector<float> soft_bits;
    descramble_symbols(result.data_symbols, params.unknown_len, params.known_len, 
                       tribits, soft_bits);
    
    std::cout << "\n=== Descrambled Data ===" << std::endl;
    std::cout << "Tribits extracted: " << tribits.size() << std::endl;
    std::cout << "Soft bits: " << soft_bits.size() << std::endl;
    
    // Print first 32 tribits
    std::cout << "\nFirst 32 tribits (descrambled): " << std::endl;
    for (int i = 0; i < 32 && i < static_cast<int>(tribits.size()); i++) {
        std::cout << tribits[i] << " ";
    }
    std::cout << std::endl;
    
    // Histogram of tribits
    std::cout << "\nTribit histogram:" << std::endl;
    int hist[8] = {0};
    for (int t : tribits) hist[t & 7]++;
    for (int i = 0; i < 8; i++) {
        std::cout << "  " << i << ": " << hist[i] << " (" 
                  << std::setprecision(1) << (100.0f * hist[i] / tribits.size()) << "%)" << std::endl;
    }
    
    // Print first 48 soft bits (16 tribits worth)
    std::cout << "\nFirst 48 soft bits:" << std::endl;
    for (int i = 0; i < 48 && i < static_cast<int>(soft_bits.size()); i++) {
        std::cout << std::setw(6) << std::setprecision(2) << soft_bits[i] << " ";
        if ((i + 1) % 12 == 0) std::cout << std::endl;
    }
    std::cout << std::endl;
    
    // Test on other modes
    std::cout << "\n=== Testing All Modes ===" << std::endl;
    
    std::vector<std::string> test_files = {
        "150bps_Short",
        "300bps_Short", 
        "600bps_Short",
        "1200bps_Short",
        "2400bps_Short",
        "4800bps_Short"
    };
    
    for (const auto& fname : test_files) {
        samples = read_wav(base + fname + ".wav", sr);
        if (samples.empty()) continue;
        
        auto res = decoder.decode(samples);
        auto p = get_mode_params(res.mode_name);
        
        std::vector<int> tb;
        std::vector<float> sb;
        descramble_symbols(res.data_symbols, p.unknown_len, p.known_len, tb, sb);
        
        // Calculate tribit entropy (should be ~3 bits for uniform distribution)
        int h[8] = {0};
        for (int t : tb) h[t & 7]++;
        float entropy = 0;
        for (int i = 0; i < 8; i++) {
            if (h[i] > 0) {
                float p = static_cast<float>(h[i]) / tb.size();
                entropy -= p * std::log2(p);
            }
        }
        
        std::cout << std::setw(14) << fname 
                  << "  Mode: " << std::setw(8) << res.mode_name
                  << "  Data syms: " << std::setw(5) << res.data_symbols.size()
                  << "  Tribits: " << std::setw(5) << tb.size()
                  << "  Entropy: " << std::setprecision(2) << entropy << " bits"
                  << std::endl;
    }
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    
    return 0;
}
