/**
 * Exhaustive Modem Test Suite
 * Runs comprehensive tests across all modes, SNR levels, and channel conditions
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>
#include <sstream>
#include <ctime>

#include "api/modem.h"
#include "api/version.h"

using namespace m110a::api;
using namespace std::chrono;

// Global tracking for real-time display
static std::string g_last_test_name;
static std::string g_last_result;
static int g_total_passed = 0;
static int g_total_tests = 0;

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
    g_last_test_name = mode_name + " clean";
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["clean_loopback"].record(false);
        g_last_result = "FAIL(encode)";
        g_total_tests++;
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
    g_last_result = success ? "PASS" : "FAIL";
    g_total_tests++;
    if (success) g_total_passed++;
}

// Test with AWGN
void test_awgn(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name, 
               float snr_db, std::mt19937& rng) {
    g_last_test_name = mode_name + " AWGN@" + std::to_string((int)snr_db) + "dB";
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["awgn"].record(false);
        g_last_result = "FAIL(encode)";
        g_total_tests++;
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
    g_last_result = success ? "PASS" : "FAIL";
    g_total_tests++;
    if (success) g_total_passed++;
}

// Test with multipath
void test_multipath(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name,
                    int delay_samples, float echo_gain, std::mt19937& rng) {
    g_last_test_name = mode_name + " MP@" + std::to_string(delay_samples) + "samp";
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["multipath"].record(false);
        g_last_result = "FAIL(encode)";
        g_total_tests++;
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
    g_last_result = success ? "PASS" : "FAIL";
    g_total_tests++;
    if (success) g_total_passed++;
}

// Test with frequency offset
void test_freq_offset(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name,
                      float offset_hz, std::mt19937& rng) {
    g_last_test_name = mode_name + " foff@" + std::to_string((int)offset_hz) + "Hz";
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["freq_offset"].record(false);
        g_last_result = "FAIL(encode)";
        g_total_tests++;
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
    g_last_result = success ? "PASS" : "FAIL";
    g_total_tests++;
    if (success) g_total_passed++;
}

// Test different message sizes
void test_message_sizes(Mode mode, const std::string& mode_name, std::mt19937& rng) {
    std::vector<int> sizes = {10, 50, 100, 200, 500};
    
    for (int size : sizes) {
        g_last_test_name = mode_name + " size=" + std::to_string(size);
        std::vector<uint8_t> data(size);
        std::uniform_int_distribution<int> dist(32, 126);
        for (int i = 0; i < size; i++) data[i] = dist(rng);
        
        auto pcm_result = encode(data, mode);
        if (!pcm_result) {
            category_stats["msg_sizes"].record(false);
            g_last_result = "FAIL(encode)";
            g_total_tests++;
            continue;
        }
        
        RxConfig cfg;
        cfg.equalizer = Equalizer::DFE;
        auto result = decode(pcm_result.value(), cfg);
        
        double ber = calculate_ber(data, result.data);
        bool success = ber == 0.0;
        
        category_stats["msg_sizes"].record(success, ber);
        g_last_result = success ? "PASS" : "FAIL";
        g_total_tests++;
        if (success) g_total_passed++;
    }
}

// Random data stress test
void test_random_data(Mode mode, const std::string& mode_name, std::mt19937& rng) {
    g_last_test_name = mode_name + " random";
    std::vector<uint8_t> data(100);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : data) b = dist(rng);
    
    auto pcm_result = encode(data, mode);
    if (!pcm_result) {
        category_stats["random_data"].record(false);
        g_last_result = "FAIL(encode)";
        g_total_tests++;
        return;
    }
    
    RxConfig cfg;
    cfg.equalizer = Equalizer::DFE;
    auto result = decode(pcm_result.value(), cfg);
    
    double ber = calculate_ber(data, result.data);
    bool success = ber == 0.0;
    
    category_stats["random_data"].record(success, ber);
    g_last_result = success ? "PASS" : "FAIL";
    g_total_tests++;
    if (success) g_total_passed++;
}

// DFE vs MLSE comparison
void test_equalizer_compare(const std::vector<uint8_t>& data, Mode mode, const std::string& mode_name,
                            int delay_samples, std::mt19937& rng) {
    auto pcm_result = encode(data, mode);
    if (!pcm_result) return;
    
    std::vector<float> pcm_mp = pcm_result.value();
    add_multipath(pcm_mp, delay_samples, 0.5f);
    add_awgn(pcm_mp, 25.0f, rng);
    
    // Test DFE
    g_last_test_name = mode_name + " DFE";
    RxConfig cfg_dfe;
    cfg_dfe.equalizer = Equalizer::DFE;
    auto result_dfe = decode(pcm_mp, cfg_dfe);
    double ber_dfe = calculate_ber(data, result_dfe.data);
    bool dfe_pass = ber_dfe < 0.05;
    category_stats["dfe_eq"].record(dfe_pass, ber_dfe);
    g_last_result = dfe_pass ? "PASS" : "FAIL";
    g_total_tests++;
    if (dfe_pass) g_total_passed++;
    
    // Test MLSE
    g_last_test_name = mode_name + " MLSE";
    RxConfig cfg_mlse;
    cfg_mlse.equalizer = Equalizer::MLSE_L3;
    auto result_mlse = decode(pcm_mp, cfg_mlse);
    double ber_mlse = calculate_ber(data, result_mlse.data);
    bool mlse_pass = ber_mlse < 0.1;
    category_stats["mlse_eq"].record(mlse_pass, ber_mlse);
    g_last_result = mlse_pass ? "PASS" : "FAIL";
    g_total_tests++;
    if (mlse_pass) g_total_passed++;
}

int main(int argc, char* argv[]) {
    // Parse command line args
    int duration_minutes = 3;
    std::string mode_filter;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) {
            duration_minutes = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_filter = argv[++i];
        } else if (arg == "--quick") {
            duration_minutes = 1;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --duration N  Test duration in minutes (default: 3)\n";
            std::cout << "  --mode MODE   Test only specific mode (e.g., 600S, 1200L)\n";
            std::cout << "                Use 'SHORT' for all short, 'LONG' for all long\n";
            std::cout << "  --quick       Run for 1 minute only\n";
            return 0;
        }
    }
    
    std::cout << "==============================================\n";
    std::cout << "M110A Exhaustive Test Suite\n";
    std::cout << "==============================================\n";
    std::cout << "Version: " << m110a::version_full() << "\n";
    std::cout << "Duration: " << duration_minutes << " minutes\n";
    if (!mode_filter.empty()) {
        std::cout << "Mode Filter: " << mode_filter << "\n";
    }
    std::cout << "\n";
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + minutes(duration_minutes);
    
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    
    // Standard test data
    std::string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    
    auto modes = get_all_modes();
    
    // Filter modes if specified
    std::vector<std::pair<Mode, std::string>> filtered_modes;
    for (const auto& [mode, name] : modes) {
        if (mode_filter.empty()) {
            filtered_modes.push_back({mode, name});
        } else if (mode_filter == "SHORT" && name.back() == 'S') {
            filtered_modes.push_back({mode, name});
        } else if (mode_filter == "LONG" && name.back() == 'L') {
            filtered_modes.push_back({mode, name});
        } else if (name == mode_filter) {
            filtered_modes.push_back({mode, name});
        }
    }
    
    if (filtered_modes.empty()) {
        std::cerr << "ERROR: No modes match filter '" << mode_filter << "'\n";
        return 1;
    }
    modes = filtered_modes;
    
    // SNR levels to test
    std::vector<float> snr_levels = {30.0f, 25.0f, 20.0f, 15.0f, 12.0f};
    
    // Multipath delays (samples at 48kHz)
    std::vector<int> mp_delays = {10, 20, 30, 40, 48, 60};
    
    // Frequency offsets (Hz)
    std::vector<float> freq_offsets = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    
    int iteration = 0;
    int total_tests = 0;
    
    // Helper to print progress
    auto print_progress = [&]() {
        auto now = steady_clock::now();
        auto elapsed = duration_cast<seconds>(now - start_time).count();
        auto remaining = duration_cast<seconds>(end_time - now).count();
        double rate = g_total_tests > 0 ? 100.0 * g_total_passed / g_total_tests : 0.0;
        
        std::cout << "\r[" << std::setw(3) << elapsed << "s] "
                  << std::setw(20) << std::left << g_last_test_name << std::right
                  << " " << std::setw(4) << g_last_result
                  << " | " << std::setw(4) << g_total_tests << " tests"
                  << " | " << std::fixed << std::setprecision(1) << rate << "%"
                  << " | " << remaining << "s left   " << std::flush;
    };
    
    while (steady_clock::now() < end_time) {
        iteration++;
        
        // Cycle through modes
        for (const auto& [mode, name] : modes) {
            // Skip slow modes more often to get variety
            if ((mode == Mode::M75_SHORT || mode == Mode::M75_LONG) && iteration % 5 != 0) continue;
            if ((mode == Mode::M150_LONG || mode == Mode::M300_LONG) && iteration % 3 != 0) continue;
            
            // 1. Clean loopback
            test_clean_loopback(test_data, mode, name);
            total_tests++;
            print_progress();
            
            // 2. AWGN at various SNR
            float snr = snr_levels[iteration % snr_levels.size()];
            test_awgn(test_data, mode, name, snr, rng);
            total_tests++;
            print_progress();
            
            // 3. Multipath
            int delay = mp_delays[iteration % mp_delays.size()];
            test_multipath(test_data, mode, name, delay, 0.5f, rng);
            total_tests++;
            print_progress();
            
            // 4. Frequency offset
            float freq_off = freq_offsets[iteration % freq_offsets.size()];
            test_freq_offset(test_data, mode, name, freq_off, rng);
            total_tests++;
            print_progress();
            
            // 5. Message sizes (less frequent)
            if (iteration % 10 == 0) {
                test_message_sizes(mode, name, rng);
                total_tests += 5;
                print_progress();
            }
            
            // 6. Random data
            test_random_data(mode, name, rng);
            total_tests++;
            print_progress();
            
            // 7. Equalizer comparison (less frequent)
            if (iteration % 5 == 0) {
                test_equalizer_compare(test_data, mode, name, 48, rng);
                total_tests += 2;
                print_progress();
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
    std::string rating;
    if (grand_rate >= 95.0) {
        rating = "EXCELLENT";
        std::cout << "*** EXCELLENT: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 80.0) {
        rating = "GOOD";
        std::cout << "*** GOOD: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 60.0) {
        rating = "FAIR";
        std::cout << "*** FAIR: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else {
        rating = "NEEDS WORK";
        std::cout << "*** NEEDS WORK: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    }
    
    // Generate report file
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_info = std::localtime(&now_time);
    
    std::ostringstream date_str;
    date_str << std::put_time(tm_info, "%Y-%m-%d");
    
    std::string version_str = m110a::api::version();
    std::string report_filename = "docs/test_reports/exhaustive_test_report_" + date_str.str() + "_v" + version_str + ".md";
    
    std::ofstream report(report_filename);
    if (report.is_open()) {
        report << "# M110A Modem Exhaustive Test Report\n\n";
        report << "## Test Information\n";
        report << "| Field | Value |\n";
        report << "|-------|-------|\n";
        report << "| **Date** | " << std::put_time(tm_info, "%B %d, %Y") << " |\n";
        report << "| **Version** | " << version_str << " |\n";
        report << "| **Duration** | " << total_elapsed << " seconds |\n";
        report << "| **Iterations** | " << iteration << " |\n";
        report << "| **Total Tests** | " << total_tests << " |\n";
        report << "| **Rating** | " << rating << " |\n\n";
        
        report << "---\n\n";
        report << "## Summary\n\n";
        report << "| Metric | Value |\n";
        report << "|--------|-------|\n";
        report << "| **Overall Pass Rate** | " << std::fixed << std::setprecision(1) << grand_rate << "% |\n";
        report << "| **Total Passed** | " << grand_passed << " |\n";
        report << "| **Total Failed** | " << (grand_total - grand_passed) << " |\n\n";
        
        report << "---\n\n";
        report << "## Detailed Results by Category\n\n";
        report << "| Category | Passed | Failed | Total | Pass Rate | Avg BER |\n";
        report << "|----------|--------|--------|-------|-----------|--------|\n";
        
        for (const auto& [key, display_name] : category_names) {
            if (category_stats.find(key) == category_stats.end()) continue;
            const auto& stats = category_stats[key];
            double rate = stats.total > 0 ? 100.0 * stats.passed / stats.total : 0.0;
            report << "| " << display_name 
                   << " | " << stats.passed 
                   << " | " << stats.failed 
                   << " | " << stats.total 
                   << " | " << std::fixed << std::setprecision(1) << rate << "%" 
                   << " | " << std::scientific << std::setprecision(2) << stats.avg_ber() 
                   << " |\n";
        }
        
        report << "\n---\n\n";
        report << "## Test Configuration\n\n";
        report << "### Modes Tested\n";
        report << "- M75_SHORT, M75_LONG (Walsh orthogonal coding)\n";
        report << "- M150_SHORT, M150_LONG (BPSK 8x repetition)\n";
        report << "- M300_SHORT, M300_LONG (BPSK 4x repetition)\n";
        report << "- M600_SHORT, M600_LONG (BPSK 2x repetition)\n";
        report << "- M1200_SHORT, M1200_LONG (QPSK)\n";
        report << "- M2400_SHORT, M2400_LONG (8-PSK)\n";
        report << "- M4800_SHORT (8-PSK uncoded)\n\n";
        
        report << "### Channel Conditions Tested\n";
        report << "- **SNR Levels**: 30dB, 25dB, 20dB, 15dB, 12dB\n";
        report << "- **Multipath Delays**: 10, 20, 30, 40, 48, 60 samples (at 48kHz)\n";
        report << "- **Echo Gain**: -6dB (0.5 linear)\n";
        report << "- **Frequency Offsets**: 0.5Hz, 1.0Hz, 2.0Hz, 5.0Hz\n\n";
        
        report << "---\n\n";
        report << "## Known Issues\n\n";
        report << "- **Frequency Offset**: Pass rate remains low (~2%) - requires AFC implementation\n\n";
        
        report.close();
        std::cout << "\nReport saved to: " << report_filename << "\n";
    } else {
        std::cerr << "Warning: Could not create report file: " << report_filename << "\n";
    }
    
    return grand_rate >= 80.0 ? 0 : 1;
}
