/**
 * Complete M75 Decode Test
 * 
 * Full pipeline: PCM → MSDMT → Walsh → Deinterleave → Viterbi → Output
 */

#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
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

// Convert bits to bytes
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            if (bits[i + b]) byte |= (1 << (7 - b));
        }
        bytes.push_back(byte);
    }
    return bytes;
}

int main() {
    std::cout << "=== Complete M75 Decode Test ===\n\n";
    
    // Load PCM file
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) {
        std::cout << "Cannot read PCM file\n";
        return 1;
    }
    std::cout << "Loaded " << samples.size() << " samples at 48kHz\n";
    
    // Step 1: MSDMT symbol extraction
    std::cout << "\n--- Step 1: MSDMT Symbol Extraction ---\n";
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    std::cout << "Symbols extracted: " << result.data_symbols.size() << "\n";
    std::cout << "Mode: D1=" << result.d1 << " D2=" << result.d2;
    if (result.d1 == 7 && result.d2 == 5) std::cout << " (M75NS confirmed)";
    std::cout << "\n";
    
    if (result.data_symbols.size() < 1500) {
        std::cout << "Not enough symbols\n";
        return 1;
    }
    
    // Step 2: Duplicate 2400 Hz → 4800 Hz for Walsh correlation
    std::cout << "\n--- Step 2: Symbol Duplication (2400→4800 Hz) ---\n";
    std::vector<complex_t> symbols_4800;
    for (const auto& s : result.data_symbols) {
        symbols_4800.push_back(s);
        symbols_4800.push_back(s);
    }
    std::cout << "4800 Hz symbols: " << symbols_4800.size() << "\n";
    
    // Step 3: Find best offset with Walsh correlation search
    std::cout << "\n--- Step 3: Find Data Start ---\n";
    
    // Try multiple offsets and find one that produces "Hello"
    std::vector<int> candidate_offsets;
    
    // Search for high correlation regions
    for (int offset = 0; offset < 3200; offset += 2) {
        if (offset + 45 * 64 > (int)symbols_4800.size()) break;
        
        Walsh75Decoder search_decoder(45);
        float total = 0;
        
        for (int w = 0; w < 15; w++) {
            auto r = search_decoder.decode(&symbols_4800[offset + w * 64]);
            total += r.magnitude;
        }
        
        if (total > 40000) {  // High correlation threshold
            candidate_offsets.push_back(offset);
        }
    }
    
    std::cout << "Found " << candidate_offsets.size() << " high-correlation offsets\n";
    
    // Try each candidate offset
    bool found_hello = false;
    int best_offset = 0;
    
    for (int test_offset : candidate_offsets) {
        Walsh75Decoder decoder(45);
        std::vector<int8_t> soft_bits;
        
        // Decode 45 Walsh symbols
        for (int w = 0; w < 45; w++) {
            int pos = test_offset + w * 64;
            auto res = decoder.decode(&symbols_4800[pos]);
            Walsh75Decoder::gray_decode(res.data, res.soft, soft_bits);
        }
        
        // Deinterleave
        InterleaverParams params{10, 9, 7, 2, 45};
        MultiModeInterleaver deinterleaver(params);
        std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
        auto deint_block = deinterleaver.deinterleave(block);
        
        // Viterbi decode
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        viterbi.decode_block(deint_block, decoded_bits, true);
        
        // Convert to bytes
        auto bytes = bits_to_bytes(decoded_bits);
        
        // Check for "Hello"
        std::string expected = "Hello";
        for (size_t i = 0; i + expected.size() <= bytes.size(); i++) {
            if (memcmp(&bytes[i], expected.data(), expected.size()) == 0) {
                found_hello = true;
                best_offset = test_offset;
                std::cout << "*** Found 'Hello' at offset " << test_offset << " ***\n";
                break;
            }
        }
        
        if (found_hello) break;
    }
    
    if (!found_hello && !candidate_offsets.empty()) {
        best_offset = candidate_offsets[0];
        std::cout << "Using first candidate offset: " << best_offset << "\n";
    }
    
    // Step 4: Walsh decode to soft bits
    std::cout << "\n--- Step 4: Walsh Decode ---\n";
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    // M75NS: 45 Walsh symbols per interleaver block
    // Each Walsh symbol = 2 soft bits
    // So 45 Walsh × 2 = 90 soft bits per block
    
    int num_walsh = (symbols_4800.size() - best_offset) / 64;
    std::cout << "Max Walsh symbols available: " << num_walsh << "\n";
    
    // Decode at least 2 interleaver blocks (90 Walsh symbols = 180 soft bits)
    int walsh_to_decode = std::min(num_walsh, 90);
    
    for (int w = 0; w < walsh_to_decode; w++) {
        int pos = best_offset + w * 64;
        
        // Use decoder's automatic block tracking
        auto res = decoder.decode(&symbols_4800[pos]);
        Walsh75Decoder::gray_decode(res.data, res.soft, soft_bits);
        
        if (w < 10 || (w >= 42 && w <= 47)) {
            std::cout << "  Walsh " << std::setw(2) << w << ": " << res.data
                      << " mag=" << std::fixed << std::setprecision(0) << res.magnitude
                      << "\n";
        } else if (w == 10) {
            std::cout << "  ...\n";
        }
    }
    
    std::cout << "Total soft bits: " << soft_bits.size() << "\n";
    
    // Step 5: Deinterleave
    std::cout << "\n--- Step 5: Deinterleave ---\n";
    
    // M75NS interleaver: 10 rows × 9 cols = 90 bits
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver deinterleaver(params);
    
    std::cout << "Interleaver: " << params.rows << "×" << params.cols 
              << " = " << params.rows * params.cols << " bits\n";
    
    // We need 90 soft bits per block
    int num_blocks = soft_bits.size() / 90;
    std::cout << "Interleaver blocks available: " << num_blocks << "\n";
    
    std::vector<soft_bit_t> deinterleaved;
    
    for (int blk = 0; blk < num_blocks; blk++) {
        // Extract block
        std::vector<soft_bit_t> block(soft_bits.begin() + blk * 90,
                                       soft_bits.begin() + (blk + 1) * 90);
        
        // Deinterleave
        auto deint_block = deinterleaver.deinterleave(block);
        
        // Append
        deinterleaved.insert(deinterleaved.end(), deint_block.begin(), deint_block.end());
    }
    
    std::cout << "Deinterleaved bits: " << deinterleaved.size() << "\n";
    
    // Step 6: Viterbi decode
    std::cout << "\n--- Step 6: Viterbi Decode ---\n";
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    
    // Viterbi expects soft bits in pairs (g1, g2)
    // Rate 1/2: 90 coded bits → 45 data bits (minus 6 tail bits = 39)
    viterbi.decode_block(deinterleaved, decoded_bits, true);
    
    std::cout << "Decoded bits: " << decoded_bits.size() << "\n";
    
    // Convert to bytes
    auto bytes = bits_to_bytes(decoded_bits);
    
    std::cout << "\n--- Results ---\n";
    std::cout << "Decoded bytes: " << bytes.size() << "\n";
    std::cout << "Hex: ";
    for (auto b : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    }
    std::cout << std::dec << "\n";
    
    std::cout << "ASCII: ";
    for (auto b : bytes) {
        if (b >= 32 && b < 127) {
            std::cout << (char)b;
        } else {
            std::cout << ".";
        }
    }
    std::cout << "\n";
    
    std::cout << "\nExpected: Hello (48 65 6c 6c 6f)\n";
    
    // Check if we got "Hello"
    std::string expected = "Hello";
    bool found = false;
    for (size_t i = 0; i + expected.size() <= bytes.size(); i++) {
        if (memcmp(&bytes[i], expected.data(), expected.size()) == 0) {
            found = true;
            std::cout << "\n*** SUCCESS: Found 'Hello' at offset " << i << " ***\n";
            break;
        }
    }
    
    if (!found) {
        std::cout << "\n*** 'Hello' not found in output ***\n";
        
        // Debug: show soft bit pattern
        std::cout << "\nFirst 40 soft bits: ";
        for (int i = 0; i < 40 && i < (int)soft_bits.size(); i++) {
            std::cout << (soft_bits[i] > 0 ? "+" : "-");
        }
        std::cout << "\n";
        
        std::cout << "First 40 deinterleaved: ";
        for (int i = 0; i < 40 && i < (int)deinterleaved.size(); i++) {
            std::cout << (deinterleaved[i] > 0 ? "+" : "-");
        }
        std::cout << "\n";
    }
    
    return found ? 0 : 1;
}
