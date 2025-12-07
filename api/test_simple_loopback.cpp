/**
 * @file test_simple_loopback.cpp
 * @brief Simple loopback test to debug TX/RX issues
 */

#include "api/modem.h"
#include "modem/m110a_codec.h"
#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <iomanip>

using namespace m110a;
using namespace m110a::api;

int main() {
    std::cout << "=== Simple Loopback Test ===\n\n";
    
    std::string message = "MIL-STD-188-110A Modem API Test - Phoenix Nest LLC";
    std::cout << "Message: \"" << message << "\" (" << message.size() << " bytes)\n\n";
    
    // Use M2400S mode
    Mode mode = Mode::M2400_SHORT;
    ModeId mode_id = ModeId::M2400S;
    
    std::cout << "=== TX Path ===\n";
    
    // Encode using API
    auto encode_result = encode(message, mode);
    if (!encode_result.ok()) {
        std::cout << "Encode failed: " << encode_result.error().message << "\n";
        return 1;
    }
    
    const auto& samples = encode_result.value();
    std::cout << "TX samples: " << samples.size() << "\n";
    std::cout << "TX duration: " << (samples.size() / 48000.0f) << " sec\n\n";
    
    // Check sample amplitude range
    float min_sample = 0, max_sample = 0;
    for (const auto& s : samples) {
        min_sample = std::min(min_sample, s);
        max_sample = std::max(max_sample, s);
    }
    std::cout << "Sample range: [" << min_sample << ", " << max_sample << "]\n";
    
    // Save to PCM and load back
    std::string pcm_file = "test_simple.pcm";
    auto save_result = save_pcm(pcm_file, samples);
    if (!save_result.ok()) {
        std::cout << "Save PCM failed: " << save_result.error().message << "\n";
        return 1;
    }
    std::cout << "Saved to: " << pcm_file << "\n";
    
    auto load_result = load_pcm(pcm_file);
    if (!load_result.ok()) {
        std::cout << "Load PCM failed: " << load_result.error().message << "\n";
        return 1;
    }
    std::cout << "Loaded " << load_result.value().size() << " samples\n";
    
    // Compare a few samples
    std::cout << "\nFirst 10 samples comparison:\n";
    for (int i = 0; i < 10; i++) {
        float orig = samples[i];
        float loaded = load_result.value()[i];
        std::cout << "  [" << i << "] orig=" << orig << " loaded=" << loaded 
                  << " diff=" << (orig - loaded) << "\n";
    }
    std::cout << "\n";
    
    std::cout << "=== RX Path (via PCM) ===\n";
    
    // Decode using API (using loaded samples, not original)
    auto decode_result = decode(load_result.value());
    
    std::cout << "Decode success: " << decode_result.success << "\n";
    std::cout << "Detected mode: " << mode_name(decode_result.mode) << "\n";
    std::cout << "Bytes decoded: " << decode_result.data.size() << "\n";
    
    // Also try decoding the original samples directly
    std::cout << "\n=== RX Path (direct samples) ===\n";
    auto decode_direct = decode(samples);
    
    std::cout << "Decode success: " << decode_direct.success << "\n";
    std::cout << "Detected mode: " << mode_name(decode_direct.mode) << "\n";
    std::cout << "Bytes decoded: " << decode_direct.data.size() << "\n";
    if (!decode_direct.data.empty()) {
        std::cout << "Decoded (hex): ";
        for (size_t i = 0; i < std::min(size_t(20), decode_direct.data.size()); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (int)decode_direct.data[i] << " ";
        }
        std::cout << std::dec << "\n";
        std::string decoded_str = decode_direct.as_string();
        std::cout << "Decoded (str): \"" << decoded_str.substr(0, 50) << "\"\n";
        if (decoded_str.find(message) == 0) {
            std::cout << "✓ Direct samples MATCH!\n";
        } else {
            std::cout << "✗ Direct samples MISMATCH\n";
        }
    }
    
    // Via PCM output
    std::cout << "\n=== Via PCM output ===\n";
    
    if (!decode_result.data.empty()) {
        std::cout << "Decoded (hex): ";
        for (size_t i = 0; i < std::min(size_t(20), decode_result.data.size()); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (int)decode_result.data[i] << " ";
        }
        std::cout << std::dec << "\n";
        
        std::string decoded_str = decode_result.as_string();
        std::cout << "Decoded (str): \"" << decoded_str.substr(0, 20) << "\"\n";
        
        // Compare
        std::cout << "\nExpected (hex): ";
        for (char c : message) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)(uint8_t)c << " ";
        }
        std::cout << std::dec << "\n";
        
        if (decoded_str.find(message) == 0) {
            std::cout << "\n✓ MATCH!\n";
            return 0;
        } else {
            std::cout << "\n✗ MISMATCH\n";
        }
    }
    
    // Try direct codec test
    std::cout << "\n=== Direct Codec Test ===\n";
    
    M110ACodec codec(mode_id);
    
    // Encode message
    std::vector<uint8_t> data(message.begin(), message.end());
    auto symbols = codec.encode_with_probes(data);
    std::cout << "Codec encoded " << data.size() << " bytes -> " << symbols.size() << " symbols\n";
    
    // Decode symbols directly (no RF path)
    auto decoded = codec.decode_with_probes(symbols);
    std::cout << "Codec decoded -> " << decoded.size() << " bytes\n";
    
    if (!decoded.empty()) {
        std::cout << "Decoded (hex): ";
        for (size_t i = 0; i < std::min(size_t(10), decoded.size()); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (int)decoded[i] << " ";
        }
        std::cout << std::dec << "\n";
        
        std::string decoded_str(decoded.begin(), decoded.end());
        std::cout << "Decoded (str): \"" << decoded_str.substr(0, 10) << "\"\n";
        
        if (decoded_str.find(message) == 0) {
            std::cout << "✓ Direct codec MATCH\n";
        } else {
            std::cout << "✗ Direct codec MISMATCH\n";
        }
    }
    
    return 1;
}
