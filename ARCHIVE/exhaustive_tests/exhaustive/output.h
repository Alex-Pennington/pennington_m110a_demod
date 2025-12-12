/**
 * @file output.h
 * @brief Output interfaces for exhaustive tests - Human readable and JSON
 * 
 * Strategy pattern: swap HumanOutput for JsonOutput based on --json flag
 */

#ifndef EXHAUSTIVE_OUTPUT_H
#define EXHAUSTIVE_OUTPUT_H

#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>

namespace exhaustive {

// ============================================================
// Output Interface
// ============================================================

class IOutput {
public:
    virtual ~IOutput() = default;
    
    // Test lifecycle
    virtual void on_start(const std::string& backend_name, 
                         const std::string& mode_detection,
                         const std::vector<std::string>& equalizers,
                         int iterations_or_duration,
                         bool is_duration,
                         const std::string& mode_filter) = 0;
    
    virtual void on_test_begin(int elapsed_sec, 
                               const std::string& mode,
                               const std::string& channel,
                               int iter, int max_iter) = 0;
    
    virtual void on_test_result(int elapsed_sec,
                                const std::string& mode,
                                const std::string& channel,
                                int total_tests,
                                int total_passed,
                                double pass_rate,
                                bool passed,
                                double ber,
                                int iter, int max_iter) = 0;
    
    virtual void on_progress(int elapsed_sec,
                            int total_tests,
                            int total_passed,
                            double pass_rate,
                            int remaining_sec) = 0;
    
    virtual void on_done(int duration_sec,
                        int iterations,
                        int total_tests,
                        int total_passed,
                        int total_failed,
                        double pass_rate,
                        double avg_ber,
                        const std::string& rating,
                        const std::string& report_file) = 0;
    
    // Mode/channel statistics
    virtual void on_mode_stats(const std::string& mode, 
                              int passed, int failed, int total,
                              double rate, double avg_ber) = 0;
    
    virtual void on_channel_stats(const std::string& channel,
                                 int passed, int failed, int total,
                                 double rate, double avg_ber) = 0;
    
    // Progressive test results
    virtual void on_progressive_result(const std::string& mode,
                                       double min_snr_db,
                                       double max_freq_hz,
                                       int max_multipath_samples) = 0;
    
    // Messages
    virtual void on_info(const std::string& message) = 0;
    virtual void on_error(const std::string& message) = 0;
};

// ============================================================
// Human-Readable Output (Console)
// ============================================================

class HumanOutput : public IOutput {
public:
    void on_start(const std::string& backend_name,
                 const std::string& mode_detection,
                 const std::vector<std::string>& equalizers,
                 int iterations_or_duration,
                 bool is_duration,
                 const std::string& mode_filter) override {
        std::cout << "==============================================\n";
        std::cout << "M110A Exhaustive Test Suite\n";
        std::cout << "==============================================\n";
        std::cout << "Backend: " << backend_name << "\n";
        std::cout << "Mode Detection: " << mode_detection << "\n";
        std::cout << "Equalizers: ";
        for (size_t i = 0; i < equalizers.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << equalizers[i];
        }
        std::cout << "\n";
        if (is_duration) {
            std::cout << "Duration: " << iterations_or_duration << " seconds\n";
        } else {
            std::cout << "Iterations: " << iterations_or_duration << "\n";
        }
        if (!mode_filter.empty()) {
            std::cout << "Mode Filter: " << mode_filter << "\n";
        }
        std::cout << "\n";
    }
    
    void on_test_begin(int elapsed_sec,
                      const std::string& mode,
                      const std::string& channel,
                      int iter, int max_iter) override {
        (void)elapsed_sec; (void)mode; (void)channel; (void)iter; (void)max_iter;
        // Silent - we show result instead
    }
    
    void on_test_result(int elapsed_sec,
                       const std::string& mode,
                       const std::string& channel,
                       int total_tests,
                       int total_passed,
                       double pass_rate,
                       bool passed,
                       double ber,
                       int iter, int max_iter) override {
        (void)passed; (void)ber; (void)total_passed;
        std::cout << "\r[" << std::setw(3) << elapsed_sec << "s] "
                  << std::setw(6) << mode << " + " << std::setw(12) << channel
                  << " | Tests: " << std::setw(4) << total_tests
                  << " | Pass: " << std::fixed << std::setprecision(1) << pass_rate << "%"
                  << " | Iter " << iter << "/" << max_iter << "   " << std::flush;
    }
    
    void on_progress(int elapsed_sec,
                    int total_tests,
                    int total_passed,
                    double pass_rate,
                    int remaining_sec) override {
        (void)total_passed; (void)remaining_sec;
        std::cout << "\r[" << std::setw(3) << elapsed_sec << "s] "
                  << "Tests: " << total_tests
                  << " | Pass: " << std::fixed << std::setprecision(1) << pass_rate << "%"
                  << "   " << std::flush;
    }
    
    void on_done(int duration_sec,
                int iterations,
                int total_tests,
                int total_passed,
                int total_failed,
                double pass_rate,
                double avg_ber,
                const std::string& rating,
                const std::string& report_file) override {
        std::cout << "\n\n";
        std::cout << "==============================================\n";
        std::cout << "EXHAUSTIVE TEST RESULTS\n";
        std::cout << "==============================================\n";
        std::cout << "Duration: " << duration_sec << " seconds\n";
        std::cout << "Iterations: " << iterations << "\n";
        std::cout << "Total Tests: " << total_tests << "\n";
        std::cout << "Passed: " << total_passed << "\n";
        std::cout << "Failed: " << total_failed << "\n";
        std::cout << "Pass Rate: " << std::fixed << std::setprecision(1) << pass_rate << "%\n";
        std::cout << "Avg BER: " << std::scientific << std::setprecision(2) << avg_ber << "\n";
        std::cout << "Rating: " << rating << "\n";
        if (!report_file.empty()) {
            std::cout << "\nReport saved to: " << report_file << "\n";
        }
    }
    
    void on_mode_stats(const std::string& mode,
                      int passed, int failed, int total,
                      double rate, double avg_ber) override {
        std::cout << std::left << std::setw(12) << mode
                  << std::right << std::setw(8) << passed
                  << std::setw(8) << failed
                  << std::setw(8) << total
                  << std::setw(9) << std::fixed << std::setprecision(1) << rate << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << avg_ber
                  << "\n";
    }
    
    void on_channel_stats(const std::string& channel,
                         int passed, int failed, int total,
                         double rate, double avg_ber) override {
        std::cout << std::left << std::setw(20) << channel
                  << std::right << std::setw(8) << passed
                  << std::setw(8) << failed
                  << std::setw(8) << total
                  << std::setw(9) << std::fixed << std::setprecision(1) << rate << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << avg_ber
                  << "\n";
    }
    
    void on_progressive_result(const std::string& mode,
                              double min_snr_db,
                              double max_freq_hz,
                              int max_multipath_samples) override {
        std::cout << std::setw(8) << mode << " | "
                  << std::setw(10) << std::fixed << std::setprecision(1) << min_snr_db << " dB | "
                  << std::setw(10) << max_freq_hz << " Hz | "
                  << std::setw(10) << max_multipath_samples << " samp\n";
    }
    
    void on_info(const std::string& message) override {
        std::cout << message << "\n";
    }
    
    void on_error(const std::string& message) override {
        std::cerr << "ERROR: " << message << "\n";
    }
};

// ============================================================
// JSON Lines Output (Machine Readable)
// ============================================================

class JsonOutput : public IOutput {
public:
    void on_start(const std::string& backend_name,
                 const std::string& mode_detection,
                 const std::vector<std::string>& equalizers,
                 int iterations_or_duration,
                 bool is_duration,
                 const std::string& mode_filter) override {
        std::cout << "{\"type\":\"start\""
                  << ",\"backend\":\"" << escape(backend_name) << "\""
                  << ",\"mode_detection\":\"" << escape(mode_detection) << "\""
                  << ",\"equalizers\":[";
        for (size_t i = 0; i < equalizers.size(); i++) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << escape(equalizers[i]) << "\"";
        }
        std::cout << "]";
        if (is_duration) {
            std::cout << ",\"duration_sec\":" << iterations_or_duration;
        } else {
            std::cout << ",\"iterations\":" << iterations_or_duration;
        }
        if (!mode_filter.empty()) {
            std::cout << ",\"mode_filter\":\"" << escape(mode_filter) << "\"";
        }
        std::cout << "}\n" << std::flush;
    }
    
    void on_test_begin(int elapsed_sec,
                      const std::string& mode,
                      const std::string& channel,
                      int iter, int max_iter) override {
        std::cout << "{\"type\":\"test_begin\""
                  << ",\"elapsed\":" << elapsed_sec
                  << ",\"mode\":\"" << escape(mode) << "\""
                  << ",\"channel\":\"" << escape(channel) << "\""
                  << ",\"iter\":" << iter
                  << ",\"max_iter\":" << max_iter
                  << "}\n" << std::flush;
    }
    
    void on_test_result(int elapsed_sec,
                       const std::string& mode,
                       const std::string& channel,
                       int total_tests,
                       int total_passed,
                       double pass_rate,
                       bool passed,
                       double ber,
                       int iter, int max_iter) override {
        std::cout << "{\"type\":\"test\""
                  << ",\"elapsed\":" << elapsed_sec
                  << ",\"mode\":\"" << escape(mode) << "\""
                  << ",\"channel\":\"" << escape(channel) << "\""
                  << ",\"tests\":" << total_tests
                  << ",\"passed\":" << total_passed
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << pass_rate
                  << ",\"result\":\"" << (passed ? "PASS" : "FAIL") << "\""
                  << ",\"ber\":" << std::scientific << std::setprecision(6) << ber
                  << ",\"iter\":" << iter
                  << ",\"max_iter\":" << max_iter
                  << "}\n" << std::flush;
    }
    
    void on_progress(int elapsed_sec,
                    int total_tests,
                    int total_passed,
                    double pass_rate,
                    int remaining_sec) override {
        std::cout << "{\"type\":\"progress\""
                  << ",\"elapsed\":" << elapsed_sec
                  << ",\"tests\":" << total_tests
                  << ",\"passed\":" << total_passed
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << pass_rate
                  << ",\"remaining\":" << remaining_sec
                  << "}\n" << std::flush;
    }
    
    void on_done(int duration_sec,
                int iterations,
                int total_tests,
                int total_passed,
                int total_failed,
                double pass_rate,
                double avg_ber,
                const std::string& rating,
                const std::string& report_file) override {
        std::cout << "{\"type\":\"done\""
                  << ",\"duration\":" << duration_sec
                  << ",\"iterations\":" << iterations
                  << ",\"tests\":" << total_tests
                  << ",\"passed\":" << total_passed
                  << ",\"failed\":" << total_failed
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << pass_rate
                  << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << avg_ber
                  << ",\"rating\":\"" << escape(rating) << "\"";
        if (!report_file.empty()) {
            std::cout << ",\"report\":\"" << escape(report_file) << "\"";
        }
        std::cout << "}\n" << std::flush;
    }
    
    void on_mode_stats(const std::string& mode,
                      int passed, int failed, int total,
                      double rate, double avg_ber) override {
        std::cout << "{\"type\":\"mode_stats\""
                  << ",\"mode\":\"" << escape(mode) << "\""
                  << ",\"passed\":" << passed
                  << ",\"failed\":" << failed
                  << ",\"total\":" << total
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << rate
                  << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << avg_ber
                  << "}\n" << std::flush;
    }
    
    void on_channel_stats(const std::string& channel,
                         int passed, int failed, int total,
                         double rate, double avg_ber) override {
        std::cout << "{\"type\":\"channel_stats\""
                  << ",\"channel\":\"" << escape(channel) << "\""
                  << ",\"passed\":" << passed
                  << ",\"failed\":" << failed
                  << ",\"total\":" << total
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << rate
                  << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << avg_ber
                  << "}\n" << std::flush;
    }
    
    void on_progressive_result(const std::string& mode,
                              double min_snr_db,
                              double max_freq_hz,
                              int max_multipath_samples) override {
        std::cout << "{\"type\":\"progressive\""
                  << ",\"mode\":\"" << escape(mode) << "\""
                  << ",\"min_snr_db\":" << std::fixed << std::setprecision(1) << min_snr_db
                  << ",\"max_freq_hz\":" << std::fixed << std::setprecision(1) << max_freq_hz
                  << ",\"max_multipath_samples\":" << max_multipath_samples
                  << "}\n" << std::flush;
    }
    
    void on_info(const std::string& message) override {
        std::cout << "{\"type\":\"info\",\"message\":\"" << escape(message) << "\"}\n" << std::flush;
    }
    
    void on_error(const std::string& message) override {
        std::cout << "{\"type\":\"error\",\"message\":\"" << escape(message) << "\"}\n" << std::flush;
    }

private:
    static std::string escape(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }
};

// ============================================================
// Factory
// ============================================================

inline std::unique_ptr<IOutput> create_output(bool json_mode) {
    if (json_mode) {
        return std::make_unique<JsonOutput>();
    }
    return std::make_unique<HumanOutput>();
}

} // namespace exhaustive

#endif // EXHAUSTIVE_OUTPUT_H
