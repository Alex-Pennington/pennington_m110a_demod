#pragma once
/**
 * @file test_config.h
 * @brief Test configuration structures
 */

#include <string>
#include <vector>
#include <map>

namespace test_gui {

// Backend selection for test execution
enum class TestBackend {
    DIRECT_API,      // Call encode/decode directly
    TCP_LOCAL,       // Connect to local m110a_server.exe
    TCP_REMOTE       // Connect to remote server
};

// Parallelization mode
enum class ParallelMode {
    BY_MODE,         // Run different modes in parallel
    BY_CATEGORY,     // Run different test categories in parallel
    BY_ITERATION     // Run iterations in parallel
};

// Test category flags
struct TestCategories {
    bool clean_loopback = true;
    bool awgn = true;
    bool multipath = true;
    bool freq_offset = true;
    bool message_sizes = true;
    bool random_data = true;
    bool dfe_equalizer = false;
    bool mlse_equalizer = false;
};

// Channel parameters
struct ChannelParams {
    std::vector<float> snr_levels = {30.0f, 25.0f, 20.0f, 15.0f};
    std::vector<int> mp_delays = {20, 30, 48};
    float echo_gain = 0.5f;
    std::vector<float> freq_offsets = {1.0f, 2.0f, 5.0f};
};

// Message configuration  
struct MessageConfig {
    std::string test_message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG";
    std::vector<int> sizes = {10, 50, 100, 200};
};

// Output options
struct OutputOptions {
    bool generate_report = true;
    bool export_csv = true;
    bool verbose = false;
    bool save_pcm = false;
};

// Complete test configuration
struct TestConfig {
    // Backend
    TestBackend backend = TestBackend::DIRECT_API;
    std::string tcp_host = "127.0.0.1";
    int tcp_ctrl_port = 5100;
    int tcp_data_port = 5101;
    int tcp_timeout_ms = 5000;
    
    // Parallelization
    int num_workers = 1;
    int batch_size = 10;
    ParallelMode parallel_mode = ParallelMode::BY_MODE;
    
    // Duration
    int duration_seconds = 180;  // 3 minutes default
    int rng_seed = 42;
    
    // Modes to test
    std::vector<std::string> modes = {
        "75S", "75L", "150S", "150L", "300S", "300L",
        "600S", "600L", "1200S", "1200L", "2400S", "2400L", "4800S"
    };
    
    // Test categories
    TestCategories categories;
    
    // Channel parameters
    ChannelParams channel;
    
    // Message configuration
    MessageConfig message;
    
    // Output options
    OutputOptions output;
};

// Test result statistics per category
struct CategoryStats {
    std::string name;
    int total = 0;
    int passed = 0;
    int failed = 0;
    double total_ber = 0.0;
    int ber_count = 0;
    
    void record(bool success, double ber = -1.0) {
        total++;
        if (success) passed++;
        else failed++;
        if (ber >= 0) {
            total_ber += ber;
            ber_count++;
        }
    }
    
    double pass_rate() const { return total > 0 ? 100.0 * passed / total : 0.0; }
    double avg_ber() const { return ber_count > 0 ? total_ber / ber_count : 0.0; }
};

// Overall test results
struct TestResults {
    std::map<std::string, CategoryStats> categories;
    int total_tests = 0;
    int total_passed = 0;
    int iterations = 0;
    double elapsed_seconds = 0.0;
    std::string rating;  // EXCELLENT, GOOD, FAIR, NEEDS WORK
    
    void calculate_rating() {
        double rate = total_tests > 0 ? 100.0 * total_passed / total_tests : 0.0;
        if (rate >= 95.0) rating = "EXCELLENT";
        else if (rate >= 80.0) rating = "GOOD";
        else if (rate >= 60.0) rating = "FAIR";
        else rating = "NEEDS WORK";
    }
};

} // namespace test_gui
