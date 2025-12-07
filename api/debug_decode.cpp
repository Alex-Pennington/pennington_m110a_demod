/**
 * @file debug_decode.cpp
 * @brief Debug the decode process
 */

#include "api/modem.h"
#include "m110a/msdmt_decoder.h"
#include <iostream>
#include <iomanip>

using namespace m110a;
using namespace m110a::api;

int main() {
    std::cout << "=== Debug Decode ===\n\n";
    
    // Load reference file
    auto ref = load_pcm("refrence_pcm/tx_2400S_20251206_202547_345.pcm");
    if (!ref.ok()) {
        std::cout << "Failed to load reference\n";
        return 1;
    }
    
    std::cout << "Loaded " << ref.value().size() << " samples\n\n";
    
    // Create decoder
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    MSDMTDecoder decoder(cfg);
    
    // Decode reference directly (no roundtrip)
    std::cout << "=== Direct decode ===\n";
    auto result1 = decoder.decode(ref.value());
    std::cout << "Preamble found: " << result1.preamble_found << "\n";
    std::cout << "Start sample: " << result1.start_sample << "\n";
    std::cout << "Phase offset: " << result1.phase_offset << "\n";
    std::cout << "Mode: " << result1.mode_name << "\n";
    std::cout << "D1: " << result1.d1 << " (corr=" << result1.d1_corr << ")\n";
    std::cout << "D2: " << result1.d2 << " (corr=" << result1.d2_corr << ")\n";
    std::cout << "Data symbols: " << result1.data_symbols.size() << "\n";
    
    // Show first few data symbols
    std::cout << "First 10 data symbols:\n";
    for (int i = 0; i < 10 && i < (int)result1.data_symbols.size(); i++) {
        auto s = result1.data_symbols[i];
        float mag = std::sqrt(s.real()*s.real() + s.imag()*s.imag());
        float phase = std::atan2(s.imag(), s.real()) * 180.0f / 3.14159f;
        std::cout << "  [" << i << "] " << s.real() << " + " << s.imag() << "i"
                  << " (mag=" << mag << " phase=" << phase << ")\n";
    }
    
    // Save and reload
    save_pcm("test_debug.pcm", ref.value());
    auto ref2 = load_pcm("test_debug.pcm");
    
    std::cout << "\n=== Roundtrip decode ===\n";
    auto result2 = decoder.decode(ref2.value());
    std::cout << "Preamble found: " << result2.preamble_found << "\n";
    std::cout << "Start sample: " << result2.start_sample << "\n";
    std::cout << "Phase offset: " << result2.phase_offset << "\n";
    std::cout << "Mode: " << result2.mode_name << "\n";
    std::cout << "D1: " << result2.d1 << " (corr=" << result2.d1_corr << ")\n";
    std::cout << "D2: " << result2.d2 << " (corr=" << result2.d2_corr << ")\n";
    std::cout << "Data symbols: " << result2.data_symbols.size() << "\n";
    
    // Show first few data symbols
    std::cout << "First 10 data symbols:\n";
    for (int i = 0; i < 10 && i < (int)result2.data_symbols.size(); i++) {
        auto s = result2.data_symbols[i];
        float mag = std::sqrt(s.real()*s.real() + s.imag()*s.imag());
        float phase = std::atan2(s.imag(), s.real()) * 180.0f / 3.14159f;
        std::cout << "  [" << i << "] " << s.real() << " + " << s.imag() << "i"
                  << " (mag=" << mag << " phase=" << phase << ")\n";
    }
    
    // Compare results
    std::cout << "\n=== Comparison ===\n";
    std::cout << "Start sample diff: " << (result2.start_sample - result1.start_sample) << "\n";
    std::cout << "Phase diff: " << (result2.phase_offset - result1.phase_offset) << " rad\n";
    
    return 0;
}
