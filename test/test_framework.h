/**
 * @file test_framework.h
 * @brief Unified Exhaustive Test Framework
 * 
 * Shared components for both direct API and server-based testing:
 *   - TestStats: Test result tracking
 *   - ModeInfo: Mode definitions and timing
 *   - ChannelCondition: Channel impairment definitions
 *   - ITestBackend: Abstract interface for test execution
 *   - Report generation utilities
 * 
 * Usage:
 *   #define TEST_FRAMEWORK_IMPLEMENTATION  // In ONE .cpp file only
 *   #include "test_framework.h"
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

#include "../api/version.h"

namespace test_framework {

// ============================================================
// Test Statistics
// ============================================================

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
    double pass_rate() const { return total > 0 ? 100.0 * passed / total : 0.0; }
    
    void reset() {
        total = passed = failed = ber_tests = 0;
        total_ber = 0.0;
    }
};

// ============================================================
// Mode Information
// ============================================================

struct ModeInfo {
    std::string cmd;       // Command string (e.g., "600S", "2400L")
    std::string name;      // Display name (usually same as cmd)
    int tx_time_ms;        // Approximate TX time for 54 bytes
    int data_rate_bps;     // Data rate in bits per second
    
    ModeInfo() : tx_time_ms(2000), data_rate_bps(0) {}
    ModeInfo(const std::string& c, const std::string& n, int tx, int rate)
        : cmd(c), name(n), tx_time_ms(tx), data_rate_bps(rate) {}
};

inline std::vector<ModeInfo> get_all_modes() {
    return {
        {"75S",   "75S",   10000, 75},
        {"75L",   "75L",   80000, 75},
        {"150S",  "150S",  5000,  150},
        {"150L",  "150L",  40000, 150},
        {"300S",  "300S",  3000,  300},
        {"300L",  "300L",  20000, 300},
        {"600S",  "600S",  2000,  600},
        {"600L",  "600L",  15000, 600},
        {"1200S", "1200S", 2000,  1200},
        {"1200L", "1200L", 15000, 1200},
        {"2400S", "2400S", 2000,  2400},
        {"2400L", "2400L", 15000, 2400},
    };
}

inline std::vector<ModeInfo> filter_modes(const std::vector<ModeInfo>& all_modes, 
                                           const std::string& filter) {
    if (filter.empty()) return all_modes;
    
    std::vector<ModeInfo> result;
    for (const auto& m : all_modes) {
        if (filter == "SHORT" && m.cmd.back() == 'S') {
            result.push_back(m);
        } else if (filter == "LONG" && m.cmd.back() == 'L') {
            result.push_back(m);
        } else if (m.cmd == filter || m.name == filter) {
            result.push_back(m);
        }
    }
    return result;
}

// ============================================================
// Channel Conditions
// ============================================================

struct ChannelCondition {
    std::string name;                   // Display name
    std::string setup_cmd;              // Server command (empty for clean)
    float expected_ber_threshold;       // Max acceptable BER
    
    // Parameters for direct API use
    float snr_db = 100.0f;              // SNR (100 = no AWGN)
    float freq_offset_hz = 0.0f;        // Frequency offset
    int multipath_delay_samples = 0;    // Multipath delay
    float multipath_gain = 0.5f;        // Echo gain
    
    ChannelCondition() : expected_ber_threshold(0.0) {}
    ChannelCondition(const std::string& n, const std::string& cmd, float ber_thresh)
        : name(n), setup_cmd(cmd), expected_ber_threshold(ber_thresh) {}
};

inline std::vector<ChannelCondition> get_standard_channels() {
    std::vector<ChannelCondition> channels;
    
    // Clean channel
    {
        ChannelCondition c("clean", "", 0.0);
        channels.push_back(c);
    }
    
    // AWGN channels
    for (float snr : {30.0f, 25.0f, 20.0f, 15.0f}) {
        ChannelCondition c;
        c.name = "awgn_" + std::to_string((int)snr) + "db";
        c.setup_cmd = "CMD:CHANNEL AWGN:" + std::to_string((int)snr);
        c.snr_db = snr;
        c.expected_ber_threshold = (snr >= 25.0f) ? 0.001 : (snr >= 20.0f) ? 0.01 : 0.05;
        channels.push_back(c);
    }
    
    // Multipath channels
    for (int delay : {24, 48}) {
        ChannelCondition c;
        c.name = "mp_" + std::to_string(delay) + "samp";
        c.setup_cmd = "CMD:CHANNEL MULTIPATH:" + std::to_string(delay);
        c.multipath_delay_samples = delay;
        c.snr_db = 30.0f;  // Add some AWGN
        c.expected_ber_threshold = 0.05;
        channels.push_back(c);
    }
    
    // Frequency offset channels
    for (float freq : {1.0f, 5.0f}) {
        ChannelCondition c;
        c.name = "foff_" + std::to_string((int)freq) + "hz";
        c.setup_cmd = "CMD:CHANNEL FREQOFFSET:" + std::to_string((int)freq);
        c.freq_offset_hz = freq;
        c.snr_db = 30.0f;
        c.expected_ber_threshold = (freq <= 2.0f) ? 0.05 : 0.10;
        channels.push_back(c);
    }
    
    // Preset channels
    {
        ChannelCondition c("moderate_hf", "CMD:CHANNEL PRESET:MODERATE", 0.05);
        c.snr_db = 20.0f;
        c.multipath_delay_samples = 24;
        c.freq_offset_hz = 1.0f;
        channels.push_back(c);
    }
    {
        ChannelCondition c("poor_hf", "CMD:CHANNEL PRESET:POOR", 0.10);
        c.snr_db = 15.0f;
        c.multipath_delay_samples = 48;
        c.freq_offset_hz = 3.0f;
        channels.push_back(c);
    }
    
    return channels;
}

// ============================================================
// BER Calculation
// ============================================================

inline double calculate_ber(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    if (tx.empty() || rx.empty()) return 1.0;
    
    // Strip trailing zeros from rx (padding/EOM)
    std::vector<uint8_t> rx_stripped = rx;
    while (!rx_stripped.empty() && rx_stripped.back() == 0x00) {
        rx_stripped.pop_back();
    }
    
    if (rx_stripped.empty() && !tx.empty()) return 1.0;
    
    size_t compare_len = tx.size();
    int bit_errors = 0;
    int total_bits = (int)(compare_len * 8);
    
    for (size_t i = 0; i < compare_len; i++) {
        uint8_t tx_byte = tx[i];
        uint8_t rx_byte = (i < rx_stripped.size()) ? rx_stripped[i] : 0x00;
        uint8_t diff = tx_byte ^ rx_byte;
        for (int b = 0; b < 8; b++) {
            if (diff & (1 << b)) bit_errors++;
        }
    }
    
    return total_bits > 0 ? (double)bit_errors / total_bits : 0.0;
}

// ============================================================
// Test Result Storage
// ============================================================

struct TestResults {
    std::map<std::string, TestStats> channel_stats;
    std::map<std::string, TestStats> mode_stats;
    std::map<std::string, std::map<std::string, TestStats>> mode_channel_stats;
    
    int total_tests = 0;
    int iterations = 0;
    int duration_seconds = 0;
    
    void record(const std::string& mode, const std::string& channel, 
                bool passed, double ber) {
        channel_stats[channel].record(passed, ber);
        mode_stats[mode].record(passed, ber);
        mode_channel_stats[mode][channel].record(passed, ber);
        total_tests++;
    }
    
    double overall_pass_rate() const {
        int total = 0, passed = 0;
        for (const auto& [_, stats] : channel_stats) {
            total += stats.total;
            passed += stats.passed;
        }
        return total > 0 ? 100.0 * passed / total : 0.0;
    }
    
    int total_passed() const {
        int passed = 0;
        for (const auto& [_, stats] : channel_stats) {
            passed += stats.passed;
        }
        return passed;
    }
    
    int total_failed() const {
        return total_tests - total_passed();
    }
    
    std::string rating() const {
        double rate = overall_pass_rate();
        if (rate >= 95.0) return "EXCELLENT";
        if (rate >= 80.0) return "GOOD";
        if (rate >= 60.0) return "FAIR";
        return "NEEDS WORK";
    }
    
    void reset() {
        channel_stats.clear();
        mode_stats.clear();
        mode_channel_stats.clear();
        total_tests = iterations = duration_seconds = 0;
    }
};

// ============================================================
// Progressive Test Results
// ============================================================

struct ProgressiveResult {
    std::string mode_name;
    
    float snr_limit_db = 0.0f;
    bool snr_tested = false;
    
    float freq_offset_limit_hz = 0.0f;
    bool freq_tested = false;
    
    int multipath_limit_samples = 0;
    bool multipath_tested = false;
};

// ============================================================
// Abstract Test Backend Interface
// ============================================================

class ITestBackend {
public:
    virtual ~ITestBackend() = default;
    
    // Lifecycle
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() = 0;
    
    // Core test operation: encode data, apply channel, decode, return BER
    virtual bool run_test(const ModeInfo& mode, 
                          const ChannelCondition& channel,
                          const std::vector<uint8_t>& test_data,
                          double& ber_out) = 0;
    
    // Optional: Set equalizer type
    virtual bool set_equalizer(const std::string& eq_type) { 
        (void)eq_type; 
        return true; 
    }
    
    // Optional: Reset backend state (reseed RNG, clear caches, etc.)
    virtual void reset_state() {}
    
    // Backend name for reporting
    virtual std::string backend_name() const = 0;
};

// ============================================================
// Console Output Helpers
// ============================================================

inline void print_progress(int elapsed_sec, const std::string& mode, 
                            const std::string& channel, int tests, 
                            double pass_rate, int iter, int max_iter) {
    std::cout << "\r[" << std::setw(3) << elapsed_sec << "s] "
              << std::setw(6) << mode << " + " << std::setw(12) << channel
              << " | Tests: " << std::setw(4) << tests
              << " | Pass: " << std::fixed << std::setprecision(1) << pass_rate << "%"
              << " | Iter " << iter << "/" << max_iter << "   " << std::flush;
}

inline void print_results_by_mode(const TestResults& results) {
    std::cout << "\n--- BY MODE ---\n";
    std::cout << std::left << std::setw(12) << "Mode"
              << std::right << std::setw(8) << "Passed"
              << std::setw(8) << "Failed"
              << std::setw(8) << "Total"
              << std::setw(10) << "Rate"
              << std::setw(12) << "Avg BER"
              << "\n";
    std::cout << std::string(58, '-') << "\n";
    
    for (const auto& [mode, stats] : results.mode_stats) {
        std::cout << std::left << std::setw(12) << mode
                  << std::right << std::setw(8) << stats.passed
                  << std::setw(8) << stats.failed
                  << std::setw(8) << stats.total
                  << std::setw(9) << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << stats.avg_ber()
                  << "\n";
    }
}

inline void print_results_by_channel(const TestResults& results) {
    std::cout << "\n--- BY CHANNEL ---\n";
    std::cout << std::left << std::setw(20) << "Channel"
              << std::right << std::setw(8) << "Passed"
              << std::setw(8) << "Failed"
              << std::setw(8) << "Total"
              << std::setw(10) << "Rate"
              << std::setw(12) << "Avg BER"
              << "\n";
    std::cout << std::string(66, '-') << "\n";
    
    for (const auto& [channel, stats] : results.channel_stats) {
        std::cout << std::left << std::setw(20) << channel
                  << std::right << std::setw(8) << stats.passed
                  << std::setw(8) << stats.failed
                  << std::setw(8) << stats.total
                  << std::setw(9) << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << stats.avg_ber()
                  << "\n";
    }
}

inline void print_mode_channel_matrix(const TestResults& results) {
    std::cout << "\n--- MODE × CHANNEL MATRIX (Pass Rates) ---\n\n";
    
    std::vector<std::string> channel_names;
    for (const auto& [ch, _] : results.channel_stats) {
        channel_names.push_back(ch);
    }
    
    // Header row
    std::cout << std::left << std::setw(8) << "Mode";
    for (const auto& ch : channel_names) {
        std::string abbrev = ch.length() > 8 ? ch.substr(0, 8) : ch;
        std::cout << std::right << std::setw(9) << abbrev;
    }
    std::cout << std::right << std::setw(9) << "TOTAL" << "\n";
    std::cout << std::string(8 + 9 * (channel_names.size() + 1), '-') << "\n";
    
    // Data rows
    for (const auto& [mode, ch_map] : results.mode_channel_stats) {
        std::cout << std::left << std::setw(8) << mode;
        for (const auto& ch : channel_names) {
            auto it = ch_map.find(ch);
            if (it != ch_map.end() && it->second.total > 0) {
                std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) 
                          << it->second.pass_rate() << "%";
            } else {
                std::cout << std::right << std::setw(9) << "-";
            }
        }
        auto mode_it = results.mode_stats.find(mode);
        if (mode_it != results.mode_stats.end()) {
            std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) 
                      << mode_it->second.pass_rate() << "%";
        }
        std::cout << "\n";
    }
    
    // Channel totals row
    std::cout << std::left << std::setw(8) << "TOTAL";
    for (const auto& ch : channel_names) {
        auto it = results.channel_stats.find(ch);
        if (it != results.channel_stats.end() && it->second.total > 0) {
            std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0)
                      << it->second.pass_rate() << "%";
        } else {
            std::cout << std::right << std::setw(9) << "-";
        }
    }
    std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) 
              << results.overall_pass_rate() << "%\n";
}

inline void print_summary(const TestResults& results) {
    std::cout << "\n";
    std::cout << std::string(66, '-') << "\n";
    std::cout << std::left << std::setw(20) << "OVERALL"
              << std::right << std::setw(8) << results.total_passed()
              << std::setw(8) << results.total_failed()
              << std::setw(8) << results.total_tests
              << std::setw(9) << std::fixed << std::setprecision(1) 
              << results.overall_pass_rate() << "%"
              << "\n";
    
    std::cout << "\n*** " << results.rating() << ": " 
              << std::fixed << std::setprecision(1) << results.overall_pass_rate() 
              << "% pass rate ***\n";
}

// ============================================================
// Report Generation
// ============================================================

inline void generate_markdown_report(const std::string& filename, 
                                      const TestResults& results,
                                      const std::string& backend_name) {
    std::ofstream report(filename);
    if (!report.is_open()) {
        std::cerr << "Cannot create report: " << filename << std::endl;
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&now_time);
    
    report << "# M110A Modem Exhaustive Test Report\\n\\n";
    report << "## Test Information\\n";
    report << "| Field | Value |\\n";
    report << "|-------|-------|\\n";
    report << "| **Version** | " << m110a::version() << " |\\n";
    report << "| **Branch** | " << m110a::GIT_BRANCH << " |\\n";
    report << "| **Build** | " << m110a::BUILD_NUMBER << " |\\n";
    report << "| **Commit** | " << m110a::GIT_COMMIT << " |\\n";
    report << "| **Build Date** | " << m110a::BUILD_DATE << " " << m110a::BUILD_TIME << " |\\n";
    report << "| **Backend** | " << backend_name << " |\\n";
    report << "| **Test Date** | " << std::put_time(tm, "%B %d, %Y %H:%M") << " |\\n";
    report << "| **Duration** | " << results.duration_seconds << " seconds |\\n";
    report << "| **Iterations** | " << results.iterations << " |\\n";
    report << "| **Total Tests** | " << results.total_tests << " |\\n";
    report << "| **Rating** | " << results.rating() << " |\\n\\n";
    
    report << "---\n\n";
    report << "## Summary\n\n";
    report << "| Metric | Value |\n";
    report << "|--------|-------|\n";
    report << "| **Overall Pass Rate** | " << std::fixed << std::setprecision(1) 
           << results.overall_pass_rate() << "% |\n";
    report << "| **Total Passed** | " << results.total_passed() << " |\n";
    report << "| **Total Failed** | " << results.total_failed() << " |\n\n";
    
    report << "---\n\n";
    report << "## Results by Mode\n\n";
    report << "| Mode | Passed | Failed | Total | Pass Rate | Avg BER |\n";
    report << "|------|--------|--------|-------|-----------|--------|\n";
    
    for (const auto& [mode, stats] : results.mode_stats) {
        report << "| " << mode
               << " | " << stats.passed
               << " | " << stats.failed
               << " | " << stats.total
               << " | " << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
               << " | " << std::scientific << std::setprecision(2) << stats.avg_ber()
               << " |\n";
    }
    
    report << "\n---\n\n";
    report << "## Results by Channel Condition\n\n";
    report << "| Channel | Passed | Failed | Total | Pass Rate | Avg BER |\n";
    report << "|---------|--------|--------|-------|-----------|--------|\n";
    
    for (const auto& [channel, stats] : results.channel_stats) {
        report << "| " << channel
               << " | " << stats.passed
               << " | " << stats.failed
               << " | " << stats.total
               << " | " << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
               << " | " << std::scientific << std::setprecision(2) << stats.avg_ber()
               << " |\n";
    }
    
    report << "\n---\n\n";
    report << "## Mode × Channel Matrix (Pass Rates)\n\n";
    
    std::vector<std::string> channel_names;
    for (const auto& [ch, _] : results.channel_stats) {
        channel_names.push_back(ch);
    }
    
    // Header row
    report << "| Mode |";
    for (const auto& ch : channel_names) {
        report << " " << ch << " |";
    }
    report << " **Total** |\n";
    
    // Separator
    report << "|------|";
    for (size_t i = 0; i < channel_names.size(); i++) {
        report << ":------:|";
    }
    report << ":------:|\n";
    
    // Data rows
    for (const auto& [mode, ch_map] : results.mode_channel_stats) {
        report << "| **" << mode << "** |";
        for (const auto& ch : channel_names) {
            auto it = ch_map.find(ch);
            if (it != ch_map.end() && it->second.total > 0) {
                report << " " << std::fixed << std::setprecision(0) << it->second.pass_rate() << "% |";
            } else {
                report << " - |";
            }
        }
        auto mode_it = results.mode_stats.find(mode);
        if (mode_it != results.mode_stats.end()) {
            report << " **" << std::fixed << std::setprecision(0) << mode_it->second.pass_rate() << "%** |";
        } else {
            report << " - |";
        }
        report << "\n";
    }
    
    // Channel totals row
    report << "| **Total** |";
    for (const auto& ch : channel_names) {
        auto it = results.channel_stats.find(ch);
        if (it != results.channel_stats.end() && it->second.total > 0) {
            report << " **" << std::fixed << std::setprecision(0) << it->second.pass_rate() << "%** |";
        } else {
            report << " - |";
        }
    }
    report << " **" << std::fixed << std::setprecision(0) << results.overall_pass_rate() << "%** |\n";
    
    report << "\n---\n\n";
    report << "*Generated by unified test framework via " << backend_name << "*\n";
    
    report.close();
    std::cout << "\nReport saved to: " << filename << std::endl;
}

// ============================================================
// CSV Output for Progressive Tests
// ============================================================

inline void write_progressive_csv_header(const std::string& filename, 
                                          const std::string& mode_filter,
                                          bool snr, bool freq, bool multipath) {
    std::ofstream csv(filename);
    if (!csv.is_open()) return;
    
    csv << "# M110A Modem Progressive Test Results\n";
    csv << "# Version: " << m110a::version() << "\n";
    csv << "# Branch: " << m110a::GIT_BRANCH << "\n";
    csv << "# Build: " << m110a::BUILD_NUMBER << "\n";
    csv << "# Commit: " << m110a::GIT_COMMIT << "\n";
    csv << "# Date: " << m110a::BUILD_DATE << " " << m110a::BUILD_TIME << "\n";
    csv << "# Mode Filter: " << (mode_filter.empty() ? "ALL" : mode_filter) << "\n";
    
    csv << "Mode,Data_Rate_BPS";
    if (snr) csv << ",Min_SNR_dB";
    if (freq) csv << ",Max_Freq_Offset_Hz";
    if (multipath) csv << ",Max_Multipath_Samples,Max_Multipath_ms";
    csv << "\n";
}

inline void append_progressive_csv_row(const std::string& filename,
                                         const ProgressiveResult& result,
                                         int data_rate,
                                         bool snr, bool freq, bool multipath) {
    std::ofstream csv(filename, std::ios::app);
    if (!csv.is_open()) return;
    
    csv << result.mode_name << "," << data_rate;
    if (snr) csv << "," << std::fixed << std::setprecision(2) << result.snr_limit_db;
    if (freq) csv << "," << std::fixed << std::setprecision(1) << result.freq_offset_limit_hz;
    if (multipath) {
        csv << "," << result.multipath_limit_samples;
        csv << "," << std::fixed << std::setprecision(2) << (result.multipath_limit_samples / 48.0);
    }
    csv << "\n";
}

} // namespace test_framework

#endif // TEST_FRAMEWORK_H
