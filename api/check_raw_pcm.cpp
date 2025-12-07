/**
 * @file check_raw_pcm.cpp
 * @brief Check raw int16 values in PCM files
 */

#include <iostream>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <vector>

int main() {
    // Read original reference file
    std::ifstream ref("refrence_pcm/tx_2400S_20251206_202547_345.pcm", std::ios::binary);
    if (!ref) {
        std::cout << "Cannot open reference file\n";
        return 1;
    }
    
    ref.seekg(0, std::ios::end);
    size_t size = ref.tellg();
    ref.seekg(0, std::ios::beg);
    
    size_t num_samples = size / 2;
    std::vector<int16_t> raw(num_samples);
    ref.read(reinterpret_cast<char*>(raw.data()), size);
    
    // Find min/max
    int16_t min_val = raw[0], max_val = raw[0];
    for (auto v : raw) {
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
    }
    
    std::cout << "Reference PCM raw values:\n";
    std::cout << "  Min int16: " << min_val << " (" << (min_val / 32768.0f) << " float)\n";
    std::cout << "  Max int16: " << max_val << " (" << (max_val / 32768.0f) << " float)\n";
    std::cout << "  First 10: ";
    for (int i = 0; i < 10; i++) {
        std::cout << raw[i] << " ";
    }
    std::cout << "\n\n";
    
    // Now read our roundtrip file
    std::ifstream ours("test_ref_roundtrip.pcm", std::ios::binary);
    if (!ours) {
        std::cout << "Cannot open roundtrip file\n";
        return 1;
    }
    
    std::vector<int16_t> raw2(num_samples);
    ours.read(reinterpret_cast<char*>(raw2.data()), size);
    
    min_val = raw2[0]; max_val = raw2[0];
    for (auto v : raw2) {
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
    }
    
    std::cout << "Roundtrip PCM raw values:\n";
    std::cout << "  Min int16: " << min_val << " (" << (min_val / 32768.0f) << " float)\n";
    std::cout << "  Max int16: " << max_val << " (" << (max_val / 32768.0f) << " float)\n";
    std::cout << "  First 10: ";
    for (int i = 0; i < 10; i++) {
        std::cout << raw2[i] << " ";
    }
    std::cout << "\n\n";
    
    // Compare
    int diff_count = 0;
    int max_diff = 0;
    for (size_t i = 0; i < num_samples; i++) {
        int d = std::abs(raw[i] - raw2[i]);
        if (d > 0) {
            diff_count++;
            max_diff = std::max(max_diff, d);
        }
    }
    std::cout << "Comparison:\n";
    std::cout << "  Samples with diff: " << diff_count << " / " << num_samples << "\n";
    std::cout << "  Max diff: " << max_diff << " LSBs\n";
    
    return 0;
}
