/**
 * Exhaustive Modem Test Suite
 * Runs comprehensive tests across all modes, SNR levels, and channel conditions
 */

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>
#include <sstream>

#include "api/modem.h"

using namespace m110a::api;
using namespace std::chrono;

// Test statistics
struct TestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
    double total_ber = 0.0;
    int ber_tests = 0;
    
    void record(bool success, double ber = -1.0) {
        total++;
        if (success) passed++;
        else failed++;
        if (ber >= 0) {
            total_ber += ber;
            ber_tests++;
        }
    }
    
    double avg_ber() const { return ber_tests > 0 ? total_ber / ber_tests : 0.0; }
};

// Global stats per category
std::map<std::string, TestStats> category_stats;

// Add AWGN noise
void add_awgn(std::vector<float>& samples, float snr_db, std::mt19937& rng) {
    float signal_power = 0.0f;
    for (float s : samples) signal_power += s * s;
    signal_power /= samples.size();
    
    float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
    float noise_std = std::sqrt(noise_power);
    
    std::normal_distribution<float> noise(0.0f, noise_std);
    for (float& s : samples) s += noise(rng);
}

// Add static multipath
void add_multipath(std::vector<float>& samples, int delay_samples, float echo_gain) {
    std::vector<float> output(samples.size(), 0.0f);
    for (size_t i = 0; i < samples.size(); i++) {
        output[i] = samples[i];
        if (i >= static_cast<size_t>(delay_samples)) {
            output[i] += echo_gain * samples[i - delay_samples];
        }
    }
    samples = output;
}

// Add frequency offset
void add_freq_offset(std::vector<float>& samples, float offset_hz, float sample_rate = 48000.0f) {
    float phase = 0.0f;
    float phase_inc = 2.0f * 3.14159265f * offset_hz / sample_rate;
    for (float& s : samples) {
        s *= std::cos(phase);
        phase += phase_inc;
        if (phase > 6.28318f) phase -= 6.28318f;
    }
}

// Calculate BER
double calculate_ber(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    if (tx.empty() || rx.empty()) return 1.0;
    
    size_t min_len = std::min(tx.size(), rx.size());
    int bit_errors = 0;
    int total_bits = 0;
    
    for (size_t i = 0; i < min_len; i++) {
        uint8_t diff = tx[i] ^ rx[i];
        for (int b = 0; b < 8; b++) {
            if (diff & (1 << b)) bit_errors++;
        }
        total_bits += 8;
    }
    
    return static_cast<double>(bit_errors) / total_bits;
}

// All modes to test
std::vector<std::pair<Mode, std::string>> get_all_modes() {
    return {
        {Mode::M75_SHORT, "75S"},
        {Mode::M75_LONG, "75L"},
        {Mode::M150_SHORT, "150S"},
        {Mode::M150_LONG, "150L"},
        {Mode::M300_SHORT, "300S"},
        {Mode::M300_LONG, "300L"},
        {Mode::M600_SHORT, "600S"},
        {Mode::M600_LONG, "600L"},
        {Mode::M1200_SHORT, "1200S"},
        {Mode::M1200_LONG, "1200L"},
        {Mode::M2400_SHORT, "2400S"},
        {Mode::M2400_LONG, "2400L"},
        {Mode::M4800_SHORT, "4800S"},
    };
}

// Test clean loopback
void test_clean_loopback(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["clean_loopback"].record(false);
        return;
    }
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    auto result = decode(pcm_result.value(), cfg);
    
    bool success = !result.data.empty() && 
                   result.data.size() >= data.size() &&
                   std::equal(data.begin(), data.end(), result.data.begin());
    
    double ber = calculate_ber(data, result.data);
    category_stats["clean_loopback"].record(success, ber);
}

// Test with AWGN
void test_awgn(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name, 
               float snr_db, std::mt19937& rng) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["awgn"].record(false);
        return;
    }
    
    auto pcm = pcm_result.value();
    add_awgn(pcm, snr_db, rng);
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    auto result = decode(pcm, cfg);
    
    double ber = calculate_ber(data, result.data);
    bool success = ber < 0.01;  // Less than 1% BER is passing
    
    category_stats["awgn"].record(success, ber);
}

// Test with multipath
void test_multipath(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name,
                    int delay_samples, float echo_gain, std::mt19937& rng) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["multipath"].record(false);
        return;
    }
    
    auto pcm = pcm_result.value();
    add_multipath(pcm, delay_samples, echo_gain);
    add_awgn(pcm, 30.0f, rng);  // Add some noise
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    auto result = decode(pcm, cfg);
    
    double ber = calculate_ber(data, result.data);
    bool success = ber < 0.05;  // Less than 5% BER with DFE
    
    category_stats["multipath"].record(success, ber);
}

// Test with frequency offset
void test_freq_offset(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name,
                      float offset_hz, std::mt19937& rng) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["freq_offset"].record(false);
        return;
    }
    
    auto pcm = pcm_result.value();
    add_freq_offset(pcm, offset_hz);
    add_awgn(pcm, 25.0f, rng);
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    cfg.phase_tracking = true;
    auto result = decode(pcm, cfg);
    
    double ber = calculate_ber(data, result.data);
    bool success = ber < 0.05;
    
    category_stats["freq_offset"].record(success, ber);
}

// Test different message sizes
void test_message_sizes(Mode mode, const std::string& mode_name, std::mt19937& rng) {
    std::vector<int> sizes = {10, 50, 100, 200, 500};
    
    for (int size : sizes) {
        std::vector<uint8_t> data(size);
        std::uniform_int_distribution<int> dist(32, 126);
        for (int i = 0; i < size; i++) data[i] = dist(rng);
        
        auto pcm_result = encode(data, mode);
        if (!pcm_result) {
            category_stats["msg_sizes"].record(false);
            continue;
        }
        
        RxConfig cfg;
        cfg.equalizer = Equalizer::DFE;
        auto result = decode(pcm_result.value(), cfg);
        
        double ber = calculate_ber(data, result.data);
        bool success = ber == 0.0;
        
        category_stats["msg_sizes"].record(success, ber);
    }
}

// Random data stress test
void test_random_data(Mode mode, const std::string& mode_name, std::mt19937& rng) {
    std::vector<uint8_t> data(100);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : data) b = dist(rng);
    
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["random_data"].record(false);
        return;
    }
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    auto result = decode(pcm_result.value(), cfg);
    
    double ber = calculate_ber(data, result.data);
    bool success = ber == 0.0;
    
    category_stats["random_data"].record(success, ber);
}

// DFE vs MLSE comparison
void test_equalizer_compare(const std::vector<uint8_t>& data, Mode mode, 
                            int delay_samples, std::mt19937& rng) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) return;
    
    std::vector<float> pcm_mp = pcm_result.value();
    add_multipath(pcm_mp, delay_samples, 0.5f);
    add_awgn(pcm_mp, 25.0f, rng);
    
    // Test DFE
    RxConfig cfg_dfe;
    cfg_dfe.equalizer = Equalizer::DFE;
    auto result_dfe = decode(pcm_mp, cfg_dfe);
    double ber_dfe = calculate_ber(data, result_dfe.data);
    
    // Test MLSE
    RxConfig cfg_mlse;
    cfg_mlse.equalizer = Equalizer::MLSE_L3;
    auto result_mlse = decode(pcm_mp, cfg_mlse);
    double ber_mlse = calculate_ber(data, result_mlse.data);
    
    category_stats["dfe_eq"].record(ber_dfe < 0.05, ber_dfe);
    category_stats["mlse_eq"].record(ber_mlse < 0.1, ber_mlse);
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "M110A Exhaustive Test Suite\n";
    std::cout << "==============================================\n";
    std::cout << "Running for ~10 minutes...\n\n";
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + minutes(10);
    
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    
    // Standard test data
    std::string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    
    auto modes = get_all_modes();
    
    // SNR levels to test
    std::vector<float> snr_levels = {30.0f, 25.0f, 20.0f, 15.0f, 12.0f};
    
    // Multipath delays (samples at 48kHz)
    std::vector<int> mp_delays = {10, 20, 30, 40, 48, 60};
    
    // Frequency offsets (Hz)
    std::vector<float> freq_offsets = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    
    int iteration = 0;
    int total_tests = 0;
    
    while (steady_clock::now() < end_time) {
        iteration++;
        
        auto now = steady_clock::now();
        auto elapsed = duration_cast<seconds>(now - start_time).count();
        auto remaining = duration_cast<seconds>(end_time - now).count();
        
        std::cout << "\r[" << std::setw(3) << elapsed << "s] Iteration " << iteration 
                  << " | Tests: " << total_tests 
                  << " | Remaining: " << remaining << "s   " << std::flush;
        
        // Cycle through modes
        for (const auto& [mode, name] : modes) {
            // Skip slow modes more often to get variety
            if ((mode == Mode::M75_SHORT || mode == Mode::M75_LONG) && iteration % 5 != 0) continue;
            if ((mode == Mode::M150_LONG || mode == Mode::M300_LONG) && iteration % 3 != 0) continue;
            
            // 1. Clean loopback
            test_clean_loopback(test_data, mode, name);
            total_tests++;
            
            // 2. AWGN at various SNR
            float snr = snr_levels[iteration % snr_levels.size()];
            test_awgn(test_data, mode, name, snr, rng);
            total_tests++;
            
            // 3. Multipath
            int delay = mp_delays[iteration % mp_delays.size()];
            test_multipath(test_data, mode, name, delay, 0.5f, rng);
            total_tests++;
            
            // 4. Frequency offset
            float freq_off = freq_offsets[iteration % freq_offsets.size()];
            test_freq_offset(test_data, mode, name, freq_off, rng);
            total_tests++;
            
            // 5. Message sizes (less frequent)
            if (iteration % 10 == 0) {
                test_message_sizes(mode, name, rng);
                total_tests += 5;
            }
            
            // 6. Random data
            test_random_data(mode, name, rng);
            total_tests++;
            
            // 7. Equalizer comparison (less frequent)
            if (iteration % 5 == 0) {
                test_equalizer_compare(test_data, mode, 48, rng);
                total_tests += 2;
            }
            
            // Check time
            if (steady_clock::now() >= end_time) break;
        }
    }
    
    auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
    
    std::cout << "\n\n";
    std::cout << "==============================================\n";
    std::cout << "EXHAUSTIVE TEST RESULTS\n";
    std::cout << "==============================================\n";
    std::cout << "Duration: " << total_elapsed << " seconds\n";
    std::cout << "Iterations: " << iteration << "\n";
    std::cout << "Total Tests: " << total_tests << "\n\n";
    
    std::cout << std::left << std::setw(20) << "Category" 
              << std::right << std::setw(8) << "Passed"
              << std::setw(8) << "Failed"
              << std::setw(8) << "Total"
              << std::setw(10) << "Rate"
              << std::setw(12) << "Avg BER"
              << "\n";
    std::cout << std::string(66, '-') << "\n";
    
    int grand_total = 0;
    int grand_passed = 0;
    
    std::vector<std::pair<std::string, std::string>> category_names = {
        {"clean_loopback", "Clean Loopback"},
        {"awgn", "AWGN Channel"},
        {"multipath", "Multipath"},
        {"freq_offset", "Freq Offset"},
        {"msg_sizes", "Message Sizes"},
        {"random_data", "Random Data"},
        {"dfe_eq", "DFE Equalizer"},
        {"mlse_eq", "MLSE Equalizer"},
    };
    
    for (const auto& [key, display_name] : category_names) {
        if (category_stats.find(key) == category_stats.end()) continue;
        
        const auto& stats = category_stats[key];
        double rate = stats.total > 0 ? 100.0 * stats.passed / stats.total : 0.0;
        
        std::cout << std::left << std::setw(20) << display_name
                  << std::right << std::setw(8) << stats.passed
                  << std::setw(8) << stats.failed
                  << std::setw(8) << stats.total
                  << std::setw(9) << std::fixed << std::setprecision(1) << rate << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << stats.avg_ber()
                  << "\n";
        
        grand_total += stats.total;
        grand_passed += stats.passed;
    }
    
    std::cout << std::string(66, '-') << "\n";
    double grand_rate = grand_total > 0 ? 100.0 * grand_passed / grand_total : 0.0;
    std::cout << std::left << std::setw(20) << "TOTAL"
              << std::right << std::setw(8) << grand_passed
              << std::setw(8) << (grand_total - grand_passed)
              << std::setw(8) << grand_total
              << std::setw(9) << std::fixed << std::setprecision(1) << grand_rate << "%"
              << "\n";
    
    std::cout << "\n";
    if (grand_rate >= 95.0) {
        std::cout << "*** EXCELLENT: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 80.0) {
        std::cout << "*** GOOD: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 60.0) {
        std::cout << "*** FAIR: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else {
        std::cout << "*** NEEDS WORK: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    }
    
    return grand_rate >= 80.0 ? 0 : 1;
}
