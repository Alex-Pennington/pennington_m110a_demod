/**
 * Test Full MS-DMT Decode Pipeline (Phase 5)
 * 
 * Complete decode chain:
 * 1. Preamble detection → mode identification
 * 2. Data symbol extraction
 * 3. Descrambling (complex conjugate method)  
 * 4. Soft bit demapping
 * 5. Deinterleaving (MS-DMT matrix method)
 * 6. Repetition combining (for 150/300bps modes)
 * 7. Viterbi decoding
 * 8. Bit packing to bytes
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
    if (!file) return {};
    
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

// Get ModeId from mode name string
ModeId get_mode_id(const std::string& name) {
    if (name == "M150S") return ModeId::M150S;
    if (name == "M150L") return ModeId::M150L;
    if (name == "M300S") return ModeId::M300S;
    if (name == "M300L") return ModeId::M300L;
    if (name == "M600S") return ModeId::M600S;
    if (name == "M600L") return ModeId::M600L;
    if (name == "M1200S") return ModeId::M1200S;
    if (name == "M1200L") return ModeId::M1200L;
    if (name == "M2400S") return ModeId::M2400S;
    if (name == "M2400L") return ModeId::M2400L;
    if (name == "M4800S") return ModeId::M4800S;
    return ModeId::M2400S; // Default
}

// Get repetition factor for mode
int get_repetition(const std::string& mode) {
    if (mode.find("150") != std::string::npos) return 8;
    if (mode.find("300") != std::string::npos) return 4;
    if (mode.find("600") != std::string::npos) return 2;
    return 1; // 1200, 2400, 4800
}

// Get mode parameters
struct ModeParams {
    int unknown_len;  // Data symbols per pattern
    int known_len;    // Probe symbols per pattern  
    int bits_per_symbol;
    int repetition;
};

ModeParams get_mode_params(const std::string& mode) {
    ModeParams p;
    p.repetition = get_repetition(mode);
    
    if (mode.find("75") != std::string::npos) { p.unknown_len = 32; p.known_len = 0; p.bits_per_symbol = 1; }
    else if (mode.find("150") != std::string::npos) { p.unknown_len = 20; p.known_len = 20; p.bits_per_symbol = 1; }
    else if (mode.find("300") != std::string::npos) { p.unknown_len = 20; p.known_len = 20; p.bits_per_symbol = 1; }
    else if (mode.find("600") != std::string::npos) { p.unknown_len = 20; p.known_len = 20; p.bits_per_symbol = 1; }
    else if (mode.find("1200") != std::string::npos) { p.unknown_len = 20; p.known_len = 20; p.bits_per_symbol = 2; }
    else if (mode.find("2400") != std::string::npos) { p.unknown_len = 32; p.known_len = 16; p.bits_per_symbol = 3; }
    else if (mode.find("4800") != std::string::npos) { p.unknown_len = 32; p.known_len = 16; p.bits_per_symbol = 3; }
    else { p.unknown_len = 20; p.known_len = 20; p.bits_per_symbol = 3; }
    
    return p;
}

// Inverse Gray code for 8-PSK
int inv_gray_8psk(int pos) {
    static const int inv_gray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    return inv_gray[pos & 7];
}

// Descramble symbols and generate soft bits
void descramble_to_soft_bits(const std::vector<complex_t>& symbols,
                              int unknown_len, int known_len,
                              int bits_per_sym,
                              std::vector<soft_bit_t>& soft_bits) {
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
            
            // Find angle and magnitude
            float angle = std::atan2(sym.imag(), sym.real());
            float mag = std::abs(sym);
            
            // Confidence based on magnitude
            float conf = mag * 30.0f;
            conf = std::min(conf, 127.0f);
            
            if (bits_per_sym == 3) {
                // 8-PSK: 3 soft bits per symbol
                int pos = static_cast<int>(std::round(angle * 4.0f / PI));
                pos = ((pos % 8) + 8) % 8;
                int tribit = inv_gray_8psk(pos);
                
                // MS-DMT convention: +soft means bit=0, -soft means bit=1
                soft_bits.push_back((tribit & 4) ? -conf : conf);  // bit 2 (MSB)
                soft_bits.push_back((tribit & 2) ? -conf : conf);  // bit 1
                soft_bits.push_back((tribit & 1) ? -conf : conf);  // bit 0 (LSB)
            } else if (bits_per_sym == 2) {
                // QPSK: 2 soft bits per symbol
                // Real part positive = bit 0, negative = bit 1
                soft_bits.push_back(sym.real() * conf);
                soft_bits.push_back(sym.imag() * conf);
            } else {
                // BPSK: 1 soft bit per symbol
                soft_bits.push_back(sym.real() * conf);
            }
        }
        
        // Skip known (probe) symbols - still advance scrambler
        for (int i = 0; i < known_len; i++) {
            scr.next_tribit();
        }
        
        sym_idx += pattern_len;
        if (known_len == 0) break; // 75bps modes
    }
}

// Apply repetition combining
std::vector<soft_bit_t> combine_repetitions(const std::vector<soft_bit_t>& input, int repetition) {
    if (repetition <= 1) return input;
    
    int output_len = input.size() / repetition;
    std::vector<soft_bit_t> output(output_len);
    
    for (int i = 0; i < output_len; i++) {
        float sum = 0;
        for (int r = 0; r < repetition; r++) {
            int idx = i * repetition + r;
            if (idx < static_cast<int>(input.size())) {
                sum += input[idx];
            }
        }
        // Clamp to soft_bit_t range
        sum = std::max(-127.0f, std::min(127.0f, sum / sqrtf(static_cast<float>(repetition))));
        output[i] = static_cast<soft_bit_t>(sum);
    }
    
    return output;
}

// Pack decoded bits into bytes
std::vector<uint8_t> pack_bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    uint8_t current = 0;
    int bit_count = 0;
    
    for (uint8_t bit : bits) {
        current = (current << 1) | (bit & 1);
        bit_count++;
        if (bit_count == 8) {
            bytes.push_back(current);
            current = 0;
            bit_count = 0;
        }
    }
    
    return bytes;
}

// Print bytes as hex and ASCII
void print_bytes(const std::vector<uint8_t>& bytes, int max_bytes = 64) {
    std::cout << "Hex: ";
    for (int i = 0; i < std::min(max_bytes, static_cast<int>(bytes.size())); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(bytes[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    std::cout << "ASCII: ";
    for (int i = 0; i < std::min(max_bytes, static_cast<int>(bytes.size())); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) {
            std::cout << c;
        } else {
            std::cout << '.';
        }
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== MS-DMT Full Decode Pipeline Test (Phase 5) ===" << std::endl;
    std::cout << std::endl;
    
    std::string base = "/mnt/user-data/uploads/MIL-STD-188-110A_";
    
    // Test each mode
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"600bps_Short", "M600S"},
        {"1200bps_Short", "M1200S"},
        {"2400bps_Short", "M2400S"},
    };
    
    // Decoder configuration
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    for (const auto& [file, expected_mode] : test_files) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Testing: " << file << std::endl;
        std::cout << "========================================" << std::endl;
        
        int sr;
        auto samples = read_wav(base + file + ".wav", sr);
        if (samples.empty()) {
            std::cerr << "Failed to load " << file << std::endl;
            continue;
        }
        
        // Step 1: Preamble detection and mode identification
        auto result = decoder.decode(samples);
        std::cout << "\n[1] Preamble Detection" << std::endl;
        std::cout << "    Mode: " << result.mode_name << " (correlation=" 
                  << std::fixed << std::setprecision(3) << result.correlation << ")" << std::endl;
        std::cout << "    Data symbols: " << result.data_symbols.size() << std::endl;
        
        // Step 2: Get mode parameters
        auto params = get_mode_params(result.mode_name);
        std::cout << "\n[2] Mode Parameters" << std::endl;
        std::cout << "    Pattern: " << params.unknown_len << " data + " 
                  << params.known_len << " probe" << std::endl;
        std::cout << "    Bits/symbol: " << params.bits_per_symbol << std::endl;
        std::cout << "    Repetition: " << params.repetition << "x" << std::endl;
        
        // Step 3: Descramble and generate soft bits
        std::vector<soft_bit_t> soft_bits;
        descramble_to_soft_bits(result.data_symbols, 
                                params.unknown_len, params.known_len,
                                params.bits_per_symbol, soft_bits);
        std::cout << "\n[3] Soft Bit Generation" << std::endl;
        std::cout << "    Soft bits: " << soft_bits.size() << std::endl;
        
        // Step 4: Apply repetition combining (for 150-600bps modes)
        auto combined = combine_repetitions(soft_bits, params.repetition);
        std::cout << "\n[4] Repetition Combining" << std::endl;
        std::cout << "    After combining: " << combined.size() << " bits" << std::endl;
        
        // Step 5: Deinterleave
        // Get interleaver for this mode
        ModeId mode_id = get_mode_id(result.mode_name);
        const auto& mode_cfg = ModeDatabase::get(mode_id);
        
        std::cout << "\n[5] Deinterleaving" << std::endl;
        std::cout << "    Matrix: " << mode_cfg.interleaver.rows << "x" 
                  << mode_cfg.interleaver.cols << std::endl;
        std::cout << "    Block size: " << mode_cfg.interleaver.block_size() << std::endl;
        
        // Create deinterleaver
        MultiModeInterleaver deinterleaver(mode_id);
        
        // Process complete blocks
        std::vector<soft_bit_t> deinterleaved;
        int block_size = deinterleaver.block_size();
        int num_blocks = combined.size() / block_size;
        
        for (int b = 0; b < num_blocks; b++) {
            std::vector<soft_bit_t> block(combined.begin() + b * block_size,
                                          combined.begin() + (b + 1) * block_size);
            auto di_block = deinterleaver.deinterleave(block);
            deinterleaved.insert(deinterleaved.end(), di_block.begin(), di_block.end());
        }
        
        std::cout << "    Blocks processed: " << num_blocks << std::endl;
        std::cout << "    Deinterleaved bits: " << deinterleaved.size() << std::endl;
        
        // Step 6: Viterbi decode
        std::cout << "\n[6] Viterbi Decoding" << std::endl;
        
        ViterbiDecoder viterbi;
        std::vector<uint8_t> decoded_bits;
        
        // For 4800bps (uncoded), skip Viterbi
        if (result.mode_name == "M4800S") {
            std::cout << "    (Uncoded mode - direct hard decisions)" << std::endl;
            for (auto sb : deinterleaved) {
                decoded_bits.push_back(sb > 0 ? 1 : 0);
            }
        } else {
            // Viterbi decode - process pairs of soft bits
            for (size_t i = 0; i + 1 < deinterleaved.size(); i += 2) {
                int bit = viterbi.decode_soft(deinterleaved[i], deinterleaved[i + 1]);
                if (bit >= 0) {
                    decoded_bits.push_back(bit);
                }
            }
            
            // Flush remaining bits
            auto remaining = viterbi.flush_decoder();
            decoded_bits.insert(decoded_bits.end(), remaining.begin(), remaining.end());
        }
        
        std::cout << "    Decoded bits: " << decoded_bits.size() << std::endl;
        
        // Step 7: Pack bits to bytes
        auto decoded_bytes = pack_bits_to_bytes(decoded_bits);
        std::cout << "\n[7] Bit Packing" << std::endl;
        std::cout << "    Decoded bytes: " << decoded_bytes.size() << std::endl;
        
        // Step 8: Print results
        std::cout << "\n[8] Decoded Data (first 64 bytes)" << std::endl;
        print_bytes(decoded_bytes, 64);
        
        // Look for patterns
        int null_count = 0;
        int printable_count = 0;
        for (uint8_t b : decoded_bytes) {
            if (b == 0) null_count++;
            if (b >= 32 && b < 127) printable_count++;
        }
        
        std::cout << "\n    Statistics:" << std::endl;
        std::cout << "    Null bytes: " << null_count << " (" 
                  << (100.0 * null_count / decoded_bytes.size()) << "%)" << std::endl;
        std::cout << "    Printable: " << printable_count << " ("
                  << (100.0 * printable_count / decoded_bytes.size()) << "%)" << std::endl;
    }
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    
    return 0;
}
