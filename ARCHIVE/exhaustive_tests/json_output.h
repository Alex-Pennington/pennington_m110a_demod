/**
 * @file json_output.h
 * @brief Pure JSON output for all test applications
 * 
 * Outputs JSON Lines (JSONL) format - one JSON object per line.
 * Each line is a complete, self-contained JSON object.
 * 
 * Output structure:
 *   1. Start event (metadata)
 *   2. Config event (test parameters)  
 *   3. Test events (streaming results)
 *   4. End event (completion signal)
 * 
 * No summary - consuming tools handle aggregation.
 */

#ifndef TEST_JSON_OUTPUT_H
#define TEST_JSON_OUTPUT_H

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <chrono>
#include <ctime>
#include "api/version.h"

namespace test_output {

class JsonOutput {
public:
    explicit JsonOutput(std::ostream& os = std::cout) : os_(os) {
        os_.setf(std::ios::unitbuf);  // Unbuffered for real-time streaming
    }
    
    // ========================================
    // Session lifecycle
    // ========================================
    
    /**
     * Start event - all metadata about this test run
     */
    void start(const std::string& app_name,
               const std::string& backend,
               const std::string& afc = "",
               const std::string& eq = "",
               const std::string& mode_filter = "",
               const std::string& test_type = "") {
        os_ << "{\"type\":\"start\""
            << ",\"app\":\"" << escape(app_name) << "\""
            << ",\"ts\":\"" << timestamp() << "\""
            << ",\"version\":\"" << m110a::version() << "\""
            << ",\"build\":" << m110a::BUILD_NUMBER
            << ",\"commit\":\"" << m110a::GIT_COMMIT << "\""
            << ",\"backend\":\"" << escape(backend) << "\"";
        if (!afc.empty()) {
            os_ << ",\"afc\":\"" << escape(afc) << "\"";
        }
        if (!eq.empty()) {
            os_ << ",\"eq\":\"" << escape(eq) << "\"";
        }
        if (!mode_filter.empty()) {
            os_ << ",\"mode_filter\":\"" << escape(mode_filter) << "\"";
        }
        if (!test_type.empty()) {
            os_ << ",\"test_type\":\"" << escape(test_type) << "\"";
        }
        os_ << "}\n" << std::flush;
    }
    
    /**
     * End event - signals completion
     */
    void end(int exit_code = 0) {
        os_ << "{\"type\":\"end\""
            << ",\"ts\":\"" << timestamp() << "\""
            << ",\"exit_code\":" << exit_code
            << "}\n" << std::flush;
    }
    
    // ========================================
    // Configuration
    // ========================================
    
    /**
     * Config event - test parameters for reproducibility
     */
    void config(int seed, bool auto_detect,
                float snr_min = -10, float snr_max = 30,
                float freq_min = 0, float freq_max = 150) {
        os_ << "{\"type\":\"config\""
            << ",\"seed\":" << seed
            << ",\"auto_detect\":" << (auto_detect ? "true" : "false")
            << ",\"snr_range\":[" << snr_min << "," << snr_max << "]"
            << ",\"freq_range\":[" << freq_min << "," << freq_max << "]"
            << "}\n" << std::flush;
    }
    
    // ========================================
    // Test results (streaming)
    // ========================================
    
    /**
     * Test event - numeric value test (SNR, freq offset, etc.)
     */
    void test(const std::string& mode,
              const std::string& test_name,
              double value,
              bool pass,
              double ber,
              int ms = 0) {
        os_ << "{\"type\":\"test\""
            << ",\"mode\":\"" << escape(mode) << "\""
            << ",\"test\":\"" << escape(test_name) << "\""
            << ",\"value\":" << std::fixed << std::setprecision(1) << value
            << ",\"pass\":" << (pass ? "true" : "false")
            << ",\"ber\":" << format_ber(ber);
        if (ms > 0) {
            os_ << ",\"ms\":" << ms;
        }
        os_ << "}\n" << std::flush;
    }
    
    /**
     * Test event - channel condition test
     */
    void test(const std::string& mode,
              const std::string& channel,
              bool pass,
              double ber,
              int iteration = 0) {
        os_ << "{\"type\":\"test\""
            << ",\"mode\":\"" << escape(mode) << "\""
            << ",\"channel\":\"" << escape(channel) << "\""
            << ",\"pass\":" << (pass ? "true" : "false")
            << ",\"ber\":" << format_ber(ber);
        if (iteration > 0) {
            os_ << ",\"iter\":" << iteration;
        }
        os_ << "}\n" << std::flush;
    }
    
    /**
     * Result event - limit found for a test type
     */
    void result(const std::string& mode,
                const std::string& test_name,
                double limit,
                const std::string& unit) {
        os_ << "{\"type\":\"result\""
            << ",\"mode\":\"" << escape(mode) << "\""
            << ",\"test\":\"" << escape(test_name) << "\""
            << ",\"limit\":" << std::fixed << std::setprecision(1) << limit
            << ",\"unit\":\"" << escape(unit) << "\""
            << "}\n" << std::flush;
    }
    
    // ========================================
    // Messages
    // ========================================
    
    void info(const std::string& message) {
        os_ << "{\"type\":\"info\""
            << ",\"msg\":\"" << escape(message) << "\""
            << "}\n" << std::flush;
    }
    
    void warning(const std::string& message) {
        os_ << "{\"type\":\"warning\""
            << ",\"msg\":\"" << escape(message) << "\""
            << "}\n" << std::flush;
    }
    
    void error(const std::string& message) {
        os_ << "{\"type\":\"error\""
            << ",\"msg\":\"" << escape(message) << "\""
            << "}\n" << std::flush;
    }
    
private:
    std::ostream& os_;
    
    /**
     * Format BER value - use fixed for clean values, scientific for tiny ones
     */
    std::string format_ber(double ber) {
        std::ostringstream ss;
        if (ber == 0.0) {
            ss << "0";
        } else if (ber >= 1.0) {
            ss << "1";
        } else if (ber >= 0.0001) {
            ss << std::fixed << std::setprecision(6) << ber;
        } else {
            ss << std::scientific << std::setprecision(2) << ber;
        }
        return ss.str();
    }
    
    std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::gmtime(&t);
        std::ostringstream ss;
        ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }
    
    std::string escape(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }
};

} // namespace test_output

#endif // TEST_JSON_OUTPUT_H
