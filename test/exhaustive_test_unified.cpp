/**
 * @file exhaustive_test.cpp
 * @brief Unified Exhaustive Modem Test Suite
 * 
 * Tests modem across all modes, SNR levels, and channel conditions.
 * Uses the unified test framework with DirectBackend (API) or ServerBackend (TCP).
 * 
 * Usage:
 *   exhaustive_test.exe [options]
 * 
 * Options:
 *   --iterations N  Number of test iterations (default: 1)
 *   --mode MODE     Test only specific mode (e.g., 600S, 1200L)
 *   --report FILE   Output report file (auto-generated if not specified)
 *   --server        Use server backend instead of direct API
 *   --host IP       Server IP (default: 127.0.0.1)
 *   --port N        Server control port (default: 4999)
 *   --progressive   Run progressive difficulty tests
 *   --prog-snr      Progressive SNR test only
 *   --prog-freq     Progressive frequency offset test only
 *   --prog-multipath Progressive multipath test only
 *   --csv FILE      Output progressive results to CSV
 *   --help          Show this help
 */

#include "test_framework.h"
#include "direct_backend.h"
#include "server_backend.h"

#include <iostream>
#include <memory>

using namespace test_framework;
using namespace std::chrono;

// ============================================================
// Progressive Tests
// ============================================================

float run_progressive_snr_test(ITestBackend& backend, const ModeInfo& mode,
                                const std::vector<uint8_t>& test_data) {
    float high = 30.0f;
    float low = -10.0f;
    
    auto test_snr = [&](float snr) -> bool {
        ChannelCondition cond;
        cond.name = "snr_test";
        std::ostringstream cmd;
        cmd << "CMD:CHANNEL AWGN:" << std::fixed << std::setprecision(1) << snr;
        cond.setup_cmd = cmd.str();
        cond.snr_db = snr;
        cond.expected_ber_threshold = 0.01f;
        
        double ber;
        bool passed = backend.run_test(mode, cond, test_data, ber);
        
        std::cout << "\r  SNR " << std::setw(5) << std::fixed << std::setprecision(1) 
                  << snr << " dB: " << (passed ? "PASS" : "FAIL") 
                  << " (BER=" << std::scientific << std::setprecision(2) << ber << ")   " << std::flush;
        return passed;
    };
    
    if (!test_snr(high)) {
        std::cout << "\n  WARNING: Even " << high << " dB fails!\n";
        return high;
    }
    
    if (test_snr(low)) {
        std::cout << "\n  NOTE: Even " << low << " dB passes - very robust!\n";
        return low;
    }
    
    while (high - low > 1.0f) {
        float mid = (high + low) / 2.0f;
        if (test_snr(mid)) {
            high = mid;
        } else {
            low = mid;
        }
    }
    
    std::cout << "\n";
    return high;
}

float run_progressive_freq_test(ITestBackend& backend, const ModeInfo& mode,
                                 const std::vector<uint8_t>& test_data) {
    float low = 0.0f;
    float high = 50.0f;
    
    auto test_freq = [&](float freq) -> bool {
        ChannelCondition cond;
        cond.name = "freq_test";
        if (freq > 0.1f) {
            std::ostringstream cmd;
            cmd << "CMD:CHANNEL FREQOFFSET:" << std::fixed << std::setprecision(1) << freq;
            cond.setup_cmd = cmd.str();
        }
        cond.freq_offset_hz = freq;
        cond.snr_db = 30.0f;
        cond.expected_ber_threshold = 0.01f;
        
        double ber;
        bool passed = backend.run_test(mode, cond, test_data, ber);
        
        std::cout << "\r  Freq +/-" << std::setw(4) << std::fixed << std::setprecision(1) 
                  << freq << " Hz: " << (passed ? "PASS" : "FAIL") 
                  << " (BER=" << std::scientific << std::setprecision(2) << ber << ")   " << std::flush;
        return passed;
    };
    
    if (!test_freq(0.0f)) {
        std::cout << "\n  WARNING: Even 0 Hz offset fails!\n";
        return 0.0f;
    }
    
    float probe = 10.0f;
    while (probe <= high && test_freq(probe)) {
        low = probe;
        probe *= 2.0f;
    }
    if (probe > high) probe = high;
    high = probe;
    
    while (high - low > 1.0f) {
        float mid = (high + low) / 2.0f;
        if (test_freq(mid)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    
    std::cout << "\n";
    return low;
}

int run_progressive_multipath_test(ITestBackend& backend, const ModeInfo& mode,
                                    const std::vector<uint8_t>& test_data) {
    int low = 0;
    int high = 200;
    
    auto test_mp = [&](int delay) -> bool {
        ChannelCondition cond;
        cond.name = "mp_test";
        if (delay > 0) {
            cond.setup_cmd = "CMD:CHANNEL MULTIPATH:" + std::to_string(delay);
        }
        cond.multipath_delay_samples = delay;
        cond.snr_db = 30.0f;
        cond.expected_ber_threshold = 0.01f;
        
        double ber;
        bool passed = backend.run_test(mode, cond, test_data, ber);
        
        std::cout << "\r  Multipath " << std::setw(3) << delay << " samples: "
                  << (passed ? "PASS" : "FAIL") 
                  << " (BER=" << std::scientific << std::setprecision(2) << ber << ")   " << std::flush;
        return passed;
    };
    
    if (!test_mp(0)) {
        std::cout << "\n  WARNING: Even clean channel fails!\n";
        return 0;
    }
    
    int probe = 20;
    while (probe <= high && test_mp(probe)) {
        low = probe;
        probe *= 2;
    }
    if (probe > high) probe = high;
    high = probe;
    
    while (high - low > 5) {
        int mid = (high + low) / 2;
        if (test_mp(mid)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    
    std::cout << "\n";
    return low;
}

ProgressiveResult run_progressive_tests(ITestBackend& backend, const ModeInfo& mode,
                                         const std::vector<uint8_t>& test_data,
                                         bool test_snr, bool test_freq, bool test_multipath) {
    ProgressiveResult result;
    result.mode_name = mode.name;
    
    std::cout << "\n=== Progressive Tests for " << mode.name << " ===\n";
    
    if (test_snr) {
        backend.reset_state();  // Ensure consistent RNG state
        std::cout << "SNR Sensitivity:\n";
        result.snr_limit_db = run_progressive_snr_test(backend, mode, test_data);
        result.snr_tested = true;
        std::cout << "  -> Limit: " << result.snr_limit_db << " dB\n";
    }
    
    if (test_freq) {
        backend.reset_state();  // Ensure consistent RNG state
        std::cout << "Frequency Offset Tolerance:\n";
        result.freq_offset_limit_hz = run_progressive_freq_test(backend, mode, test_data);
        result.freq_tested = true;
        std::cout << "  -> Limit: +/-" << result.freq_offset_limit_hz << " Hz\n";
    }
    
    if (test_multipath) {
        backend.reset_state();  // Ensure consistent RNG state
        std::cout << "Multipath Tolerance:\n";
        result.multipath_limit_samples = run_progressive_multipath_test(backend, mode, test_data);
        result.multipath_tested = true;
        std::cout << "  -> Limit: " << result.multipath_limit_samples << " samples ("
                  << (result.multipath_limit_samples / 48.0) << " ms)\n";
    }
    
    return result;
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    // Configuration
    int max_iterations = 1;
    std::string mode_filter;
    std::vector<std::string> mode_list;  // List of specific modes
    std::vector<std::string> eq_list;    // List of equalizers to test
    std::string report_file;
    std::string csv_file;
    bool use_server = false;
    std::string host = "127.0.0.1";
    int control_port = 4999;
    bool progressive_mode = false;
    bool prog_snr = false, prog_freq = false, prog_multipath = false;
    std::string equalizer = "DFE";  // Default equalizer
    
    // Helper to split comma-separated string
    auto split_csv = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> result;
        std::istringstream iss(s);
        std::string token;
        while (std::getline(iss, token, ',')) {
            // Trim whitespace and convert to uppercase
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos) {
                token = token.substr(start, end - start + 1);
                for (auto& c : token) c = std::toupper(c);
                result.push_back(token);
            }
        }
        return result;
    };
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--iterations" || arg == "-n") && i + 1 < argc) {
            max_iterations = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_filter = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_file = argv[++i];
        } else if (arg == "--server") {
            use_server = true;
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
            use_server = true;
        } else if (arg == "--port" && i + 1 < argc) {
            control_port = std::stoi(argv[++i]);
            use_server = true;
        } else if (arg == "--progressive" || arg == "-p") {
            progressive_mode = true;
            prog_snr = prog_freq = prog_multipath = true;
        } else if (arg == "--prog-snr") {
            progressive_mode = true;
            prog_snr = true;
        } else if (arg == "--prog-freq") {
            progressive_mode = true;
            prog_freq = true;
        } else if (arg == "--prog-multipath") {
            progressive_mode = true;
            prog_multipath = true;
        } else if ((arg == "--csv" || arg == "-c") && i + 1 < argc) {
            csv_file = argv[++i];
        } else if ((arg == "--eq" || arg == "--equalizer") && i + 1 < argc) {
            equalizer = argv[++i];
            // Convert to uppercase
            for (auto& c : equalizer) c = std::toupper(c);
        } else if (arg == "--modes" && i + 1 < argc) {
            mode_list = split_csv(argv[++i]);
        } else if (arg == "--eqs" && i + 1 < argc) {
            eq_list = split_csv(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << m110a::version_header() << "\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Backend Options:\n";
            std::cout << "  --server        Use server backend instead of direct API\n";
            std::cout << "  --host IP       Server IP (default: 127.0.0.1)\n";
            std::cout << "  --port N        Server control port (default: 4999)\n\n";
            std::cout << "Standard Test Options:\n";
            std::cout << "  --iterations N  Number of test iterations (default: 1)\n";
            std::cout << "  -n N            Short form of --iterations\n";
            std::cout << "  --mode MODE     Test only specific mode (e.g., 600S, 1200L)\n";
            std::cout << "                  Use 'SHORT' for all short, 'LONG' for all long\n";
            std::cout << "  --modes LIST    Comma-separated list of modes (e.g., 600S,1200L,2400S)\n";
            std::cout << "  --report FILE   Output report file\n\n";
            std::cout << "Progressive Test Options:\n";
            std::cout << "  --progressive   Run all progressive tests (SNR, freq, multipath)\n";
            std::cout << "  -p              Short form of --progressive\n";
            std::cout << "  --prog-snr      Progressive SNR test only\n";
            std::cout << "  --prog-freq     Progressive frequency offset test only\n";
            std::cout << "  --prog-multipath Progressive multipath test only\n";
            std::cout << "  --csv FILE      Output progressive results to CSV file\n";
            std::cout << "  -c FILE         Short form of --csv\n\n";
            std::cout << "Equalizer Options:\n";
            std::cout << "  --eq TYPE       Set equalizer type (default: DFE)\n";
            std::cout << "  --eqs LIST      Comma-separated list of equalizers\n";
            std::cout << "                  Types: NONE, DFE, DFE_RLS, MLSE_L2, MLSE_L3,\n";
            std::cout << "                         MLSE_ADAPTIVE, TURBO\n";
            return 0;
        }
    }
    
    // Auto-generate report filename
    if (report_file.empty()) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&now_time);
        std::ostringstream ss;
        ss << "../docs/test_reports/exhaustive_" 
           << (use_server ? "server_" : "direct_")
           << std::put_time(tm, "%Y%m%d_%H%M%S") << ".md";
        report_file = ss.str();
    }
    
    // Create backend
    std::unique_ptr<ITestBackend> backend;
    if (use_server) {
        backend = std::make_unique<ServerBackend>(host, control_port, control_port - 1);
    } else {
        backend = std::make_unique<DirectBackend>();
    }
    
    // Print header
    std::cout << "==============================================\n";
    std::cout << m110a::version_header() << "\n";
    std::cout << "==============================================\n";
    std::cout << m110a::build_info() << "\n";
    std::cout << "Backend: " << backend->backend_name() << "\n";
    
    // Build equalizer list (use eq_list if provided, else single equalizer)
    if (eq_list.empty()) {
        eq_list.push_back(equalizer);
    }
    std::cout << "Equalizers: ";
    for (size_t i = 0; i < eq_list.size(); i++) {
        if (i > 0) std::cout << ", ";
        std::cout << eq_list[i];
    }
    std::cout << "\n";
    
    // Validate all equalizers
    for (const auto& eq : eq_list) {
        if (!backend->set_equalizer(eq)) {
            std::cerr << "Invalid equalizer type: " << eq << "\n";
            std::cerr << "Valid types: NONE, DFE, DFE_RLS, MLSE_L2, MLSE_L3, MLSE_ADAPTIVE, TURBO\n";
            return 1;
        }
    }
    // Set back to first equalizer
    backend->set_equalizer(eq_list[0]);
    
    if (progressive_mode) {
        std::cout << "Mode: PROGRESSIVE (find mode limits)\n";
        std::cout << "Tests: ";
        if (prog_snr) std::cout << "SNR ";
        if (prog_freq) std::cout << "Freq ";
        if (prog_multipath) std::cout << "Multipath ";
        std::cout << "\n";
    } else {
        std::cout << "Iterations: " << max_iterations << "\n";
    }
    if (!mode_filter.empty()) {
        std::cout << "Mode Filter: " << mode_filter << "\n";
    }
    std::cout << "\n";
    
    // Connect
    if (!backend->connect()) {
        std::cerr << "ERROR: Cannot connect to backend\n";
        if (use_server) {
            std::cerr << "Make sure the server is running: m110a_server.exe\n";
        }
        return 1;
    }
    
    std::cout << "Connected.\n\n";
    
    // Test data
    std::string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    
    // Get modes
    auto all_modes = get_all_modes();
    std::vector<ModeInfo> modes;
    
    // If mode_list provided, use it; otherwise use mode_filter
    if (!mode_list.empty()) {
        for (const auto& m : all_modes) {
            std::string upper_cmd = m.cmd;
            for (auto& c : upper_cmd) c = std::toupper(c);
            for (const auto& want : mode_list) {
                if (upper_cmd == want || m.name == want) {
                    modes.push_back(m);
                    break;
                }
            }
        }
    } else {
        modes = filter_modes(all_modes, mode_filter);
    }
    
    if (modes.empty()) {
        std::cerr << "ERROR: No modes match filter\n";
        return 1;
    }
    
    auto channels = get_standard_channels();
    
    // ================================================================
    // Progressive Mode
    // ================================================================
    if (progressive_mode) {
        auto start_time = steady_clock::now();
        
        // Results grouped by equalizer
        std::map<std::string, std::map<std::string, ProgressiveResult>> all_eq_results;
        
        for (const auto& eq : eq_list) {
            std::cout << "\n*** Testing with Equalizer: " << eq << " ***\n";
            backend->set_equalizer(eq);
            
            std::map<std::string, ProgressiveResult> progressive_results;
            
            // Write CSV header
            if (!csv_file.empty() && eq == eq_list[0]) {
                write_progressive_csv_header(csv_file, mode_filter, prog_snr, prog_freq, prog_multipath);
                std::cout << "CSV file initialized: " << csv_file << "\n\n";
            }
            
            for (const auto& mode : modes) {
                auto result = run_progressive_tests(*backend, mode, test_data, 
                                                    prog_snr, prog_freq, prog_multipath);
                result.mode_name = eq + ":" + mode.name;  // Prefix with eq name
                progressive_results[mode.name] = result;
                
                // Append to CSV
                if (!csv_file.empty()) {
                    // Modify result name for CSV
                    ProgressiveResult csv_result = result;
                    csv_result.mode_name = eq + "_" + mode.name;
                    append_progressive_csv_row(csv_file, csv_result, mode.data_rate_bps,
                                               prog_snr, prog_freq, prog_multipath);
                }
            }
            
            all_eq_results[eq] = progressive_results;
        }
        
        auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
        
        // Print summary
        std::cout << "\n==============================================\n";
        std::cout << "PROGRESSIVE TEST RESULTS\n";
        std::cout << "==============================================\n";
        std::cout << "Duration: " << total_elapsed << " seconds\n\n";
        
        for (const auto& [eq, progressive_results] : all_eq_results) {
            std::cout << "\n--- Equalizer: " << eq << " ---\n";
            std::cout << std::setw(8) << "Mode" << " | ";
            if (prog_snr) std::cout << std::setw(12) << "Min SNR (dB)" << " | ";
            if (prog_freq) std::cout << std::setw(14) << "Max Freq (Hz)" << " | ";
            if (prog_multipath) std::cout << std::setw(16) << "Max Multipath" << " | ";
            std::cout << "\n";
            
            std::cout << std::string(8, '-') << "-+-";
            if (prog_snr) std::cout << std::string(12, '-') << "-+-";
            if (prog_freq) std::cout << std::string(14, '-') << "-+-";
            if (prog_multipath) std::cout << std::string(16, '-') << "-+-";
            std::cout << "\n";
            
            for (const auto& [name, result] : progressive_results) {
                std::cout << std::setw(8) << name << " | ";
                if (prog_snr) {
                    std::cout << std::setw(12) << result.snr_limit_db << " | ";
                }
                if (prog_freq) {
                    std::cout << std::setw(8) << "+/-" << result.freq_offset_limit_hz << " Hz | ";
                }
                if (prog_multipath) {
                    std::cout << std::setw(6) << result.multipath_limit_samples << " samples | ";
                }
                std::cout << "\n";
            }
        }
        
        if (!csv_file.empty()) {
            std::cout << "\nCSV saved to: " << csv_file << "\n";
        }
        
        backend->disconnect();
        return 0;
    }
    
    // ================================================================
    // Standard Exhaustive Test Mode
    // ================================================================
    
    TestResults results;
    auto start_time = steady_clock::now();
    
    int iteration = 0;
    
    while (iteration < max_iterations) {
        iteration++;
        
        for (const auto& eq : eq_list) {
            backend->set_equalizer(eq);
            
            for (const auto& mode : modes) {
                // Skip slow modes sometimes (only if we have multiple iterations)
                if (max_iterations > 1) {
                    if ((mode.cmd == "75S" || mode.cmd == "75L") && iteration % 5 != 0) continue;
                    if ((mode.cmd == "150L" || mode.cmd == "300L") && iteration % 3 != 0) continue;
                }
                
                for (const auto& channel : channels) {
                    // Skip some channels sometimes (only if we have multiple iterations)
                    if (max_iterations > 1 && iteration % 2 != 0 && 
                        (channel.name == "foff_5hz" || channel.name == "poor_hf")) continue;
                    
                    auto now = steady_clock::now();
                    auto elapsed = (int)duration_cast<seconds>(now - start_time).count();
                    
                    // Include eq in display
                    std::string mode_with_eq = (eq_list.size() > 1) ? eq + ":" + mode.name : mode.name;
                    print_progress(elapsed, mode_with_eq, channel.name, results.total_tests,
                                   results.overall_pass_rate(), iteration, max_iterations);
                    
                    double ber;
                    bool passed = backend->run_test(mode, channel, test_data, ber);
                    
                    // Record with eq prefix if multiple equalizers
                    std::string record_name = (eq_list.size() > 1) ? eq + ":" + mode.name : mode.name;
                    results.record(record_name, channel.name, passed, ber);
                }
            }
        }
    }
    
    results.iterations = iteration;
    results.duration_seconds = (int)duration_cast<seconds>(
        steady_clock::now() - start_time).count();
    
    // Print results
    std::cout << "\n\n";
    std::cout << "==============================================\n";
    std::cout << "EXHAUSTIVE TEST RESULTS\n";
    std::cout << "==============================================\n";
    std::cout << "Duration: " << results.duration_seconds << " seconds\n";
    std::cout << "Iterations: " << results.iterations << "\n";
    std::cout << "Total Tests: " << results.total_tests << "\n";
    
    print_results_by_mode(results);
    print_results_by_channel(results);
    print_mode_channel_matrix(results);
    print_summary(results);
    
    // Generate report
    generate_markdown_report(report_file, results, backend->backend_name());
    
    backend->disconnect();
    
    return results.overall_pass_rate() >= 80.0 ? 0 : 1;
}
