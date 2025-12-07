/**
 * Verify Encode/Decode Chain via Loopback
 * 
 * Tests: encode → interleave → modulate → demodulate → deinterleave → decode
 */

#include "m110a/mode_config.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <iomanip>
#include <random>

using namespace m110a;

int main() {
    std::cout << "=== Encode/Decode Loopback Test ===" << std::endl;
    
    // Test data: ASCII message
    std::string message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789";
    
    std::cout << "\nOriginal message (" << message.size() << " bytes):" << std::endl;
    std::cout << "\"" << message << "\"" << std::endl;
    
    // Convert to bits
    std::vector<uint8_t> input_bits;
    for (char c : message) {
        for (int b = 7; b >= 0; b--) {
            input_bits.push_back((c >> b) & 1);
        }
    }
    std::cout << "\nInput bits: " << input_bits.size() << std::endl;
    
    // Test M2400S mode
    ModeId mode = ModeId::M2400S;
    const auto& cfg = ModeDatabase::get(mode);
    
    std::cout << "\n=== Testing " << cfg.name << " ===" << std::endl;
    std::cout << "Interleaver: " << cfg.interleaver.rows << "x" << cfg.interleaver.cols << std::endl;
    std::cout << "Block size: " << cfg.interleaver.block_size() << " bits" << std::endl;
    
    // Step 1: Viterbi ENCODE
    ConvEncoder encoder;
    std::vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    std::cout << "\n[1] Viterbi Encode: " << input_bits.size() << " -> " << encoded.size() << " bits" << std::endl;
    
    // Step 2: INTERLEAVE
    MultiModeInterleaver interleaver(mode);
    
    // Pad to block size
    int block_size = interleaver.block_size();
    while (encoded.size() % block_size != 0) {
        encoded.push_back(0);
    }
    
    // Convert to soft bits for interleaver
    std::vector<soft_bit_t> soft_encoded;
    for (uint8_t b : encoded) {
        // MS-DMT convention: +soft = bit 0, -soft = bit 1
        soft_encoded.push_back(b ? -100 : 100);  // Clean soft bits
    }
    
    // Interleave
    std::vector<soft_bit_t> interleaved;
    for (size_t i = 0; i < soft_encoded.size(); i += block_size) {
        std::vector<soft_bit_t> block(soft_encoded.begin() + i,
                                      soft_encoded.begin() + i + block_size);
        auto int_block = interleaver.interleave(block);
        interleaved.insert(interleaved.end(), int_block.begin(), int_block.end());
    }
    std::cout << "[2] Interleave: " << soft_encoded.size() << " -> " << interleaved.size() << " bits" << std::endl;
    
    // Step 3: DEINTERLEAVE (what RX would do)
    std::vector<soft_bit_t> deinterleaved;
    for (size_t i = 0; i < interleaved.size(); i += block_size) {
        std::vector<soft_bit_t> block(interleaved.begin() + i,
                                      interleaved.begin() + i + block_size);
        auto di_block = interleaver.deinterleave(block);
        deinterleaved.insert(deinterleaved.end(), di_block.begin(), di_block.end());
    }
    std::cout << "[3] Deinterleave: " << interleaved.size() << " -> " << deinterleaved.size() << " bits" << std::endl;
    
    // Verify interleave/deinterleave roundtrip
    int mismatch = 0;
    for (size_t i = 0; i < soft_encoded.size() && i < deinterleaved.size(); i++) {
        if (soft_encoded[i] != deinterleaved[i]) mismatch++;
    }
    std::cout << "    Interleaver roundtrip mismatches: " << mismatch << std::endl;
    
    // Step 4: Viterbi DECODE
    ViterbiDecoder decoder;
    std::vector<uint8_t> decoded_bits;
    decoder.decode_block(deinterleaved, decoded_bits, true);
    std::cout << "[4] Viterbi Decode: " << deinterleaved.size() << " -> " << decoded_bits.size() << " bits" << std::endl;
    
    // Step 5: Pack bits to bytes
    std::vector<uint8_t> decoded_bytes;
    uint8_t current = 0;
    int bit_count = 0;
    for (uint8_t b : decoded_bits) {
        current = (current << 1) | (b & 1);
        bit_count++;
        if (bit_count == 8) {
            decoded_bytes.push_back(current);
            current = 0;
            bit_count = 0;
        }
    }
    
    // Step 6: Compare
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Decoded bytes: " << decoded_bytes.size() << std::endl;
    
    // Print as string (first message.size() bytes)
    std::string decoded_message;
    for (size_t i = 0; i < std::min(message.size(), decoded_bytes.size()); i++) {
        char c = decoded_bytes[i];
        if (c >= 32 && c < 127) {
            decoded_message += c;
        } else {
            decoded_message += '.';
        }
    }
    std::cout << "Decoded: \"" << decoded_message << "\"" << std::endl;
    
    // Count errors
    int byte_errors = 0;
    int bit_errors = 0;
    for (size_t i = 0; i < std::min(input_bits.size(), decoded_bits.size()); i++) {
        if (input_bits[i] != decoded_bits[i]) bit_errors++;
    }
    for (size_t i = 0; i < std::min(message.size(), decoded_bytes.size()); i++) {
        if (static_cast<uint8_t>(message[i]) != decoded_bytes[i]) byte_errors++;
    }
    
    std::cout << "\nBit errors: " << bit_errors << " / " << input_bits.size() << std::endl;
    std::cout << "Byte errors: " << byte_errors << " / " << message.size() << std::endl;
    
    // Test roundtrip success
    bool success = (byte_errors == 0) && (decoded_message.substr(0, 10) == message.substr(0, 10));
    std::cout << "\n" << (success ? "✓ LOOPBACK TEST PASSED" : "✗ LOOPBACK TEST FAILED") << std::endl;
    
    return success ? 0 : 1;
}
