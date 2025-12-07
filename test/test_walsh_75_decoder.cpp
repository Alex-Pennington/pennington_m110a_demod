/**
 * Test Walsh75Decoder
 * 
 * Tests the Walsh 75bps decoder against:
 * 1. Loopback (generate signal, decode)
 * 2. Real PCM file from transmitter
 */

#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>

using namespace m110a;

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

//=============================================================================
// Test 1: Verify Walsh sequence orthogonality
//=============================================================================
void test_walsh_orthogonality() {
    std::cout << "=== Test Walsh Orthogonality ===\n";
    
    std::cout << "MNS orthogonality (should be 32 on diagonal, 0 elsewhere):\n";
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int dot = 0;
            for (int k = 0; k < 32; k++) {
                int a = (Walsh75Decoder::MNS[i][k] == 0) ? 1 : -1;
                int b = (Walsh75Decoder::MNS[j][k] == 0) ? 1 : -1;
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
                int a = (Walsh75Decoder::MES[i][k] == 0) ? 1 : -1;
                int b = (Walsh75Decoder::MES[j][k] == 0) ? 1 : -1;
                dot += a * b;
            }
            std::cout << std::setw(4) << dot;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

//=============================================================================
// Test 2: Loopback test - generate signal and decode
//=============================================================================
void test_loopback() {
    std::cout << "=== Test Loopback ===\n";
    
    // Generate test data: 0, 1, 2, 3, 0, 1, 2, 3
    std::vector<int> tx_data = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1};
    
    // Generate TX signal at 4800 Hz (64 samples per Walsh symbol)
    std::vector<complex_t> tx_signal;
    
    Walsh75Decoder tx_encoder(45);  // Use for scrambler sequence
    
    for (int data : tx_data) {
        // Generate scrambled Walsh pattern
        for (int i = 0; i < 32; i++) {
            int walsh_val = Walsh75Decoder::MNS[data][i];
            int scr_val = (i + tx_encoder.scrambler_count()) % 160;
            
            // Get scrambler from encoder state
            // For simplicity, regenerate scrambler
            static int scrambler[160];
            static bool init = false;
            if (!init) {
                int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
                for (int k = 0; k < 160; k++) {
                    for (int j = 0; j < 8; j++) {
                        int carry = sreg[11];
                        for (int m = 11; m > 0; m--) sreg[m] = sreg[m-1];
                        sreg[6] ^= carry; sreg[4] ^= carry; sreg[1] ^= carry;
                        sreg[0] = carry;
                    }
                    scrambler[k] = (sreg[2] << 2) | (sreg[1] << 1) | sreg[0];
                }
                init = true;
            }
            
            int scr_bits = scrambler[(i + tx_encoder.scrambler_count()) % 160];
            int out_sym = (walsh_val + scr_bits) % 8;
            
            float phase = out_sym * PI / 4;
            complex_t sym(std::cos(phase), std::sin(phase));
            
            // Duplicate for 4800 Hz (i*2 indexing)
            tx_signal.push_back(sym);
            tx_signal.push_back(sym);
        }
        tx_encoder.set_scrambler_count((tx_encoder.scrambler_count() + 32) % 160);
    }
    
    std::cout << "Generated " << tx_signal.size() << " samples for " 
              << tx_data.size() << " Walsh symbols\n";
    
    // Decode
    Walsh75Decoder decoder(45);
    int correct = 0;
    
    for (size_t i = 0; i < tx_data.size(); i++) {
        auto result = decoder.decode(&tx_signal[i * 64], false);  // All MNS
        
        bool match = (result.data == tx_data[i]);
        if (match) correct++;
        
        std::cout << "  " << i << ": TX=" << tx_data[i] 
                  << " RX=" << result.data
                  << " mag=" << std::fixed << std::setprecision(1) << result.magnitude
                  << " soft=" << std::setprecision(2) << result.soft
                  << (match ? " ✓" : " ✗") << "\n";
    }
    
    std::cout << "Result: " << correct << "/" << tx_data.size() << " correct\n\n";
}

//=============================================================================
// Test 3: Decode real PCM file
//=============================================================================
void test_real_file() {
    std::cout << "=== Test Real PCM File ===\n";
    
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) {
        std::cout << "Cannot read PCM file\n\n";
        return;
    }
    
    std::cout << "Read " << samples.size() << " samples at 48kHz\n";
    
    // Use MSDMT to extract symbols
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    std::cout << "MSDMT: " << result.data_symbols.size() << " symbols\n";
    std::cout << "Mode: D1=" << result.d1 << " D2=" << result.d2;
    if (result.d1 == 7 && result.d2 == 5) std::cout << " (M75NS)";
    std::cout << "\n\n";
    
    if (result.data_symbols.size() < 100) {
        std::cout << "Not enough symbols\n\n";
        return;
    }
    
    // Duplicate 2400 Hz symbols to 4800 Hz
    std::vector<complex_t> symbols_4800;
    for (const auto& s : result.data_symbols) {
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    
    // Find best starting offset by searching for strong correlations
    Walsh75Decoder search_decoder(45);
    
    float best_total = 0;
    int best_offset = 0;
    
    for (int offset = 0; offset < 2000; offset += 2) {
        if (offset + 640 > (int)symbols_4800.size()) break;
        
        search_decoder.reset();
        float total = 0;
        
        for (int w = 0; w < 10; w++) {
            auto r = search_decoder.decode(&symbols_4800[offset + w * 64], false);
            total += r.magnitude;
        }
        
        if (total > best_total) {
            best_total = total;
            best_offset = offset;
        }
    }
    
    std::cout << "Best offset: " << best_offset << " (total=" << best_total << ")\n\n";
    
    // Decode at best offset
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    std::cout << "First 45 Walsh symbols (1 interleaver block):\n";
    
    for (int w = 0; w < 45; w++) {
        int pos = best_offset + w * 64;
        if (pos + 64 > (int)symbols_4800.size()) break;
        
        bool is_mes = (w == 0);  // First is MES
        auto result = decoder.decode(&symbols_4800[pos], is_mes);
        
        Walsh75Decoder::gray_decode(result.data, result.soft, soft_bits);
        
        if (w < 20 || w >= 40) {
            std::cout << "  " << std::setw(2) << w << ": " << result.data
                      << " mag=" << std::fixed << std::setprecision(1) << result.magnitude
                      << " soft=" << std::setprecision(2) << result.soft
                      << (is_mes ? " (MES)" : "") << "\n";
        } else if (w == 20) {
            std::cout << "  ...\n";
        }
    }
    
    std::cout << "\nSoft bits: " << soft_bits.size() << " bits\n";
    
    // Convert to hard bits for display
    std::cout << "Decoded bits (hard): ";
    for (size_t i = 0; i < std::min(soft_bits.size(), (size_t)40); i++) {
        std::cout << (soft_bits[i] > 0 ? "1" : "0");
        if ((i + 1) % 8 == 0) std::cout << " ";
    }
    std::cout << "...\n";
    
    // Try to decode as bytes (without deinterleaver/Viterbi for now)
    std::cout << "Raw bytes (no deint/Viterbi): ";
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
    
    std::cout << "Expected: Hello (48 65 6C 6C 6F hex)\n";
    std::cout << "Note: Full decode requires deinterleaver + Viterbi\n\n";
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "Walsh 75bps Decoder Test\n";
    std::cout << "========================\n\n";
    
    test_walsh_orthogonality();
    test_loopback();
    test_real_file();
    
    return 0;
}
