/**
 * @file compare_pcm.cpp
 * @brief Compare our generated PCM with reference PCM
 */

#include "api/modem.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <fstream>

using namespace m110a::api;

void analyze_pcm(const std::string& name, const Samples& samples) {
    if (samples.empty()) {
        std::cout << name << ": empty\n";
        return;
    }
    
    float min_s = samples[0], max_s = samples[0];
    double sum = 0, sum_sq = 0;
    
    for (const auto& s : samples) {
        min_s = std::min(min_s, s);
        max_s = std::max(max_s, s);
        sum += s;
        sum_sq += s * s;
    }
    
    double mean = sum / samples.size();
    double rms = std::sqrt(sum_sq / samples.size());
    double variance = (sum_sq / samples.size()) - (mean * mean);
    double dc_offset = mean;
    
    std::cout << name << " (" << samples.size() << " samples):\n";
    std::cout << "  Range:     [" << min_s << ", " << max_s << "]\n";
    std::cout << "  DC Offset: " << dc_offset << "\n";
    std::cout << "  RMS:       " << rms << "\n";
    std::cout << "  Variance:  " << variance << "\n";
    
    // First 10 samples
    std::cout << "  First 10:  ";
    for (int i = 0; i < 10 && i < (int)samples.size(); i++) {
        std::cout << std::fixed << std::setprecision(4) << samples[i] << " ";
    }
    std::cout << "\n\n";
}

int main() {
    std::cout << "=== PCM Analysis ===\n\n";
    
    // Load reference 2400S
    auto ref = load_pcm("refrence_pcm/tx_2400S_20251206_202547_345.pcm");
    if (ref.ok()) {
        analyze_pcm("Reference 2400S", ref.value());
    } else {
        std::cout << "Failed to load reference: " << ref.error().message << "\n";
    }
    
    // Generate our 2400S
    std::string message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    auto ours = encode(message, Mode::M2400_SHORT);
    if (ours.ok()) {
        analyze_pcm("Our 2400S (float)", ours.value());
        
        // Save and reload
        save_pcm("test_compare.pcm", ours.value());
        auto reloaded = load_pcm("test_compare.pcm");
        if (reloaded.ok()) {
            analyze_pcm("Our 2400S (via PCM)", reloaded.value());
        }
    }
    
    // Now test decode
    std::cout << "=== Decode Tests ===\n\n";
    
    // Decode reference
    if (ref.ok()) {
        auto result = decode(ref.value());
        std::cout << "Reference: " << (result.success ? "OK" : "FAIL");
        if (result.success) {
            std::string decoded = result.as_string();
            std::cout << " \"" << decoded.substr(0, 40) << "...\"";
        }
        std::cout << "\n";
    }
    
    // Decode ours (float) - MAKE A COPY first
    if (ours.ok()) {
        Samples copy1 = ours.value();
        auto result = decode(copy1);
        std::cout << "Ours (float): " << (result.success ? "OK" : "FAIL");
        if (result.success) {
            std::string decoded = result.as_string();
            std::cout << " \"" << decoded.substr(0, 40) << "...\"";
        }
        std::cout << "\n";
        
        // Check if decode mutated the samples
        bool mutated = false;
        for (size_t i = 0; i < copy1.size() && i < ours.value().size(); i++) {
            if (copy1[i] != ours.value()[i]) { mutated = true; break; }
        }
        std::cout << "  (samples mutated: " << (mutated ? "YES" : "NO") << ")\n";
    }
    
    // Decode ours (via PCM)
    auto reloaded = load_pcm("test_compare.pcm");
    if (reloaded.ok()) {
        auto result = decode(reloaded.value());
        std::cout << "Ours (PCM): " << (result.success ? "OK" : "FAIL");
        if (result.success) {
            std::string decoded = result.as_string();
            std::cout << " \"" << decoded.substr(0, 40) << "...\"";
        }
        std::cout << "\n";
    }
    
    // Try the SAME decode (float) a second time
    if (ours.ok()) {
        auto result = decode(ours.value());
        std::cout << "Ours (float) 2nd: " << (result.success ? "OK" : "FAIL");
        if (result.success) {
            std::string decoded = result.as_string();
            std::cout << " \"" << decoded.substr(0, 40) << "...\"";
        }
        std::cout << "\n";
    }
    
    // Test: save reference to PCM and reload
    if (ref.ok()) {
        save_pcm("test_ref_roundtrip.pcm", ref.value());
        auto ref_reload = load_pcm("test_ref_roundtrip.pcm");
        if (ref_reload.ok()) {
            // Compare sample values
            std::cout << "\nReference roundtrip comparison:\n";
            std::cout << "  Sizes: orig=" << ref.value().size() << " reload=" << ref_reload.value().size() << "\n";
            bool match = true;
            for (size_t i = 0; i < std::min(ref.value().size(), ref_reload.value().size()); i++) {
                if (std::abs(ref.value()[i] - ref_reload.value()[i]) > 0.001f) {
                    std::cout << "  First mismatch at " << i << ": " << ref.value()[i] << " vs " << ref_reload.value()[i] << "\n";
                    match = false;
                    break;
                }
            }
            if (match) std::cout << "  All samples match within tolerance\n";
            
            auto result = decode(ref_reload.value());
            std::cout << "Reference (roundtrip PCM): " << (result.success ? "OK" : "FAIL");
            if (result.success) {
                std::string decoded = result.as_string();
                std::cout << " \"" << decoded.substr(0, 40) << "...\"";
            }
            std::cout << "\n";
        }
    }
    
    return 0;
}
