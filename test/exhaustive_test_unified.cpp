/**
 * @file exhaustive_test.cpp
 * @brief Unified Exhaustive Modem Test Suite
 * 
 * M110A Modem - MIL-STD-188-110A Compatible HF Modem
 * Copyright (c) 2024-2025 Alex Pennington
 * Email: alex.pennington@organicengineer.com
 * 
 * Tests modem across all modes, SNR levels, and channel conditions.
 * Uses the unified test framework with DirectBackend (API) or ServerBackend (TCP).
 * 
 * Usage:
 *   exhaustive_test.exe [options]
 * 
 * Options:
 *   --iterations N  Number of test iterations (default: 1)
 *   --duration N    Run for N seconds (overrides iterations)
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
 *   --json          Machine-readable JSON lines output
 *   --help          Show this help
 */

#include "test_framework.h"
#include "direct_backend.h"
#include "server_backend.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

using namespace test_framework;
using namespace std::chrono;
using namespace m110a;

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
// Reference Sample Tests (Brain Modem Compatibility Validation)
// ============================================================

std::vector<ReferenceTestResult> run_reference_tests(ITestBackend& backend, 
                                                      const std::string& ref_dir) {
    std::vector<ReferenceTestResult> results;
    
    // Expected test message from all Brain Modem reference samples
    const std::string expected_message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Find all PCM files in reference directory
    // Use platform-specific directory listing
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    std::string pattern = ref_dir + "\\*.pcm";
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
    
    std::vector<std::string> pcm_files;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                pcm_files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    std::sort(pcm_files.begin(), pcm_files.end());
#else
    // Unix/Linux directory listing
    std::vector<std::string> pcm_files;
    // Would use opendir/readdir here
#endif
    
    if (pcm_files.empty()) {
        std::cout << "ERROR: No PCM files found in " << ref_dir << "\n";
        return results;
    }
    
    std::cout << "\nRunning Brain Modem Reference Sample Tests...\n";
    std::cout << "Testing " << pcm_files.size() << " reference samples\n";
    std::cout << "Expected message: \"" << expected_message << "\"\n\n";
    
    for (const auto& filename : pcm_files) {
        std::string pcm_file = ref_dir + "/" + filename;
        
        // Extract expected mode from filename (e.g., tx_150S_... -> 150S)
        std::string expected_mode;
        size_t start = filename.find("_") + 1;
        size_t end = filename.find("_", start);
        if (start != std::string::npos && end != std::string::npos) {
            expected_mode = filename.substr(start, end - start);
        }
        
        std::cout << "Testing " << filename << " (Expected: " << expected_mode << ")... " << std::flush;
        
        ReferenceTestResult result;
        result.expected_mode = expected_mode;
        
        bool success = backend.run_reference_test(pcm_file, expected_message, result);
        results.push_back(result);
        
        if (success) {
            std::cout << "PASS";
            if (!result.detected_mode.empty()) {
                std::cout << " (Detected: " << result.detected_mode << ")";
            }
            std::cout << "\n";
        } else {
            std::cout << "FAIL";
            if (!result.detected_mode.empty()) {
                std::cout << " (Detected: " << result.detected_mode << ")";
            }
            if (result.ber > 0.0 && result.ber < 1.0) {
                std::cout << " BER=" << std::fixed << std::setprecision(4) << result.ber;
            }
            std::cout << "\n";
            if (!result.decoded_message.empty() && result.decoded_message.find("ERROR") == std::string::npos) {
                std::cout << "    Decoded: \"" << result.decoded_message << "\"\n";
            } else if (!result.decoded_message.empty()) {
                std::cout << "    " << result.decoded_message << "\n";
            }
        }
    }
    
    return results;
}

void print_reference_test_summary(const std::vector<ReferenceTestResult>& results) {
    int total = (int)results.size();
    int passed = 0;
    int mode_match = 0;
    int message_match = 0;
    
    for (const auto& r : results) {
        if (r.passed) passed++;
        if (r.mode_match) mode_match++;
        if (r.message_match) message_match++;
    }
    
    std::cout << "\n==============================================\n";
    std::cout << "REFERENCE SAMPLE TEST SUMMARY\n";
    std::cout << "==============================================\n";
    std::cout << "Total Samples:     " << total << "\n";
    std::cout << "Passed:            " << passed << " (" 
              << std::fixed << std::setprecision(1) 
              << (100.0 * passed / total) << "%)\n";
    std::cout << "Mode Detection:    " << mode_match << " (" 
              << (100.0 * mode_match / total) << "%)\n";
    std::cout << "Message Match:     " << message_match << " (" 
              << (100.0 * message_match / total) << "%)\n";
    
    if (passed == total) {
        std::cout << "\n*** ALL REFERENCE TESTS PASSED ***\n";
        std::cout << "Brain Modem interoperability VERIFIED\n";
    } else {
        std::cout << "\n*** SOME REFERENCE TESTS FAILED ***\n";
        std::cout << "Failed samples:\n";
        for (const auto& r : results) {
            if (!r.passed) {
                std::cout << "  - " << r.filename << " (Expected: " << r.expected_mode;
                if (!r.detected_mode.empty()) {
                    std::cout << ", Detected: " << r.detected_mode;
                }
                std::cout << ")\n";
            }
        }
    }
    std::cout << "==============================================\n";
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    // Force unbuffered stdout for real-time JSON output
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    // Always output version header first - critical for record keeping
    std::cerr << "==============================================\n";
    std::cerr << m110a::version_header() << "\n";
    std::cerr << "==============================================\n";
    std::cerr << m110a::build_info() << "\n";
    std::cerr << "Test: PhoenixNest M110A Exhaustive\n";
    std::cerr << "==============================================\n" << std::flush;
    
    // Configuration
    int max_iterations = 1;
    int duration_seconds = 0;  // 0 = use iterations instead
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
    bool reference_mode = false;  // Test Brain Modem reference samples
    std::string reference_dir = "../refrence_pcm";
    std::string equalizer = "DFE";  // Default equalizer
    int parallel_threads = 1;  // Number of parallel threads (1 = sequential)
    bool use_auto_detect = false;  // Use auto-detection (default: known mode)
    bool json_output = false;  // Machine-readable JSON output
    
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
        } else if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_seconds = std::stoi(argv[++i]);
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
        } else if ((arg == "--parallel" || arg == "-j") && i + 1 < argc) {
            parallel_threads = std::stoi(argv[++i]);
            if (parallel_threads < 1) parallel_threads = 1;
            if (parallel_threads > 32) parallel_threads = 32;  // Reasonable limit
        } else if (arg == "--reference" || arg == "--ref") {
            reference_mode = true;
        } else if (arg == "--ref-dir" && i + 1 < argc) {
            reference_dir = argv[++i];
            reference_mode = true;
        } else if (arg == "--use-auto-detect") {
            use_auto_detect = true;
        } else if (arg == "--json") {
            json_output = true;
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
            std::cout << "  --duration N    Run for N seconds (overrides iterations)\n";
            std::cout << "  -d N            Short form of --duration\n";
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
            std::cout << "                         MLSE_ADAPTIVE, TURBO\n\n";
            std::cout << "Performance Options:\n";
            std::cout << "  --parallel N    Run N tests in parallel (Direct API only)\n";
            std::cout << "  -j N            Short form of --parallel\n\n";
            std::cout << "Output Options:\n";
            std::cout << "  --json          Machine-readable JSON lines output\n\n";
            std::cout << "Reference Sample Test Options:\n";
            std::cout << "  --reference     Test Brain Modem reference samples for interoperability\n";
            std::cout << "  --ref           Short form of --reference\n";
            std::cout << "  --ref-dir DIR   Reference sample directory (default: ../refrence_pcm)\n\n";
            std::cout << "Mode Detection Options:\n";
            std::cout << "  --use-auto-detect  Use auto mode detection instead of known mode\n";
            std::cout << "                     (slower, tests AFC+detection under stress)\n";
            std::cout << "                     Default: known mode (faster, AFC-friendly)\n";
            return 0;
        }
    }
    
    // Auto-generate report filename
    if (report_file.empty()) {
        // Create reports folder next to executable if it doesn't exist
        std::string reports_dir = "reports";
        if (!fs::exists(reports_dir)) {
            fs::create_directories(reports_dir);
        }
        
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&now_time);
        std::ostringstream ss;
        if (progressive_mode) {
            ss << reports_dir << "/progressive_"
               << (use_server ? "server_" : "direct_")
               << std::put_time(tm, "%Y%m%d_%H%M%S") << ".md";
        } else {
            ss << reports_dir << "/exhaustive_" 
               << (use_server ? "server_" : "direct_")
               << std::put_time(tm, "%Y%m%d_%H%M%S") << ".md";
        }
        report_file = ss.str();
    }
    
    // Create backend
    std::unique_ptr<ITestBackend> backend;
    if (use_server) {
        backend = std::make_unique<ServerBackend>(host, control_port, control_port - 1);
    } else {
        backend = std::make_unique<DirectBackend>(42, use_auto_detect);
    }
    
    // Build equalizer list (use eq_list if provided, else single equalizer)
    if (eq_list.empty()) {
        eq_list.push_back(equalizer);
    }
    
    // Print header (only if not JSON mode)
    if (!json_output) {
        std::cout << "==============================================\n";
        std::cout << m110a::version_header() << "\n";
        std::cout << "==============================================\n";
        std::cout << m110a::build_info() << "\n";
        std::cout << "Backend: " << backend->backend_name() << "\n";
        std::cout << "Mode Detection: " << (use_auto_detect ? "AUTO (tests AFC+detection)" : "KNOWN (AFC-friendly)") << "\n";
        
        std::cout << "Equalizers: ";
        for (size_t i = 0; i < eq_list.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << eq_list[i];
        }
        std::cout << "\n";
        
        if (progressive_mode) {
            std::cout << "Mode: PROGRESSIVE (find mode limits)\n";
            std::cout << "Tests: ";
            if (prog_snr) std::cout << "SNR ";
            if (prog_freq) std::cout << "Freq ";
            if (prog_multipath) std::cout << "Multipath ";
            std::cout << "\n";
        } else if (duration_seconds > 0) {
            std::cout << "Duration: " << duration_seconds << " seconds\n";
        } else {
            std::cout << "Iterations: " << max_iterations << "\n";
        }
        if (!mode_filter.empty()) {
            std::cout << "Mode Filter: " << mode_filter << "\n";
        }
        
        // Show parallel info (only for direct backend)
        if (parallel_threads > 1 && !use_server) {
            std::cout << "Parallel: " << parallel_threads << " threads\n";
        } else if (parallel_threads > 1 && use_server) {
            std::cout << "Note: Parallel execution not supported with server backend\n";
            parallel_threads = 1;
        }
        std::cout << "\n";
    } else {
        // JSON start message - include version info for record keeping
        std::cout << "{\"type\":\"start\",\"backend\":\"" << backend->backend_name() << "\""
                  << ",\"version\":\"" << m110a::version() << "\""
                  << ",\"build\":" << m110a::BUILD_NUMBER
                  << ",\"commit\":\"" << m110a::GIT_COMMIT << "\""
                  << ",\"branch\":\"" << m110a::GIT_BRANCH << "\""
                  << ",\"mode_detection\":\"" << (use_auto_detect ? "AUTO" : "KNOWN") << "\""
                  << ",\"equalizers\":[";
        for (size_t i = 0; i < eq_list.size(); i++) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << eq_list[i] << "\"";
        }
        std::cout << "]";
        if (duration_seconds > 0) {
            std::cout << ",\"duration_sec\":" << duration_seconds;
        } else {
            std::cout << ",\"iterations\":" << max_iterations;
        }
        if (!mode_filter.empty()) {
            std::cout << ",\"mode_filter\":\"" << mode_filter << "\"";
        }
        std::cout << "}\n" << std::flush;
        
        // Suppress parallel warning in JSON mode
        if (parallel_threads > 1 && use_server) {
            parallel_threads = 1;
        }
    }
    
    // Connect
    if (!backend->connect()) {
        if (json_output) {
            std::cout << "{\"type\":\"error\",\"message\":\"Cannot connect to backend\"}\n" << std::flush;
        } else {
            std::cerr << "ERROR: Cannot connect to backend\n";
            if (use_server) {
                std::cerr << "Make sure the server is running: m110a_server.exe\n";
            }
        }
        return 1;
    }
    
    if (!json_output) {
        std::cout << "Connected.\n";
    } else {
        std::cout << "{\"type\":\"info\",\"message\":\"Connected\"}\n" << std::flush;
    }
    
    // Validate all equalizers (must be after connect for server backend)
    for (const auto& eq : eq_list) {
        if (!backend->set_equalizer(eq)) {
            if (json_output) {
                std::cout << "{\"type\":\"error\",\"message\":\"Invalid equalizer: " << eq << "\"}\n" << std::flush;
            } else {
                std::cerr << "Invalid equalizer type: " << eq << "\n";
                std::cerr << "Valid types: NONE, DFE, DFE_RLS, MLSE_L2, MLSE_L3, MLSE_ADAPTIVE, TURBO\n";
            }
            return 1;
        }
    }
    // Set back to first equalizer
    backend->set_equalizer(eq_list[0]);
    
    if (!json_output) {
        std::cout << "\n";
    }
    
    // ================================================================
    // Reference Sample Test Mode
    // ================================================================
    if (reference_mode) {
        auto start_time = steady_clock::now();
        
        std::cout << "Mode: REFERENCE SAMPLE TEST (Brain Modem Compatibility)\n";
        std::cout << "Directory: " << reference_dir << "\n\n";
        
        // Test with each equalizer
        std::map<std::string, std::vector<ReferenceTestResult>> all_eq_results;
        
        for (const auto& eq : eq_list) {
            if (eq_list.size() > 1) {
                std::cout << "\n*** Testing with Equalizer: " << eq << " ***\n";
            }
            backend->set_equalizer(eq);
            
            auto results = run_reference_tests(*backend, reference_dir);
            all_eq_results[eq] = results;
            
            if (eq_list.size() > 1) {
                print_reference_test_summary(results);
            }
        }
        
        auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
        
        // Print final summary
        if (eq_list.size() == 1) {
            print_reference_test_summary(all_eq_results[eq_list[0]]);
        } else {
            std::cout << "\n==============================================\n";
            std::cout << "MULTI-EQUALIZER REFERENCE TEST SUMMARY\n";
            std::cout << "==============================================\n";
            for (const auto& [eq, results] : all_eq_results) {
                int passed = 0;
                for (const auto& r : results) {
                    if (r.passed) passed++;
                }
                std::cout << eq << ": " << passed << "/" << results.size() 
                          << " (" << (100.0 * passed / results.size()) << "%)\n";
            }
        }
        
        std::cout << "\nTotal Duration: " << total_elapsed << " seconds\n";
        
        backend->disconnect();
        
        // Return success if all tests passed
        int total_passed = 0, total_tests = 0;
        for (const auto& [_, results] : all_eq_results) {
            total_tests += (int)results.size();
            for (const auto& r : results) {
                if (r.passed) total_passed++;
            }
        }
        
        return (total_passed == total_tests) ? 0 : 1;
    }
    
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
        if (json_output) {
            std::cout << "{\"type\":\"error\",\"message\":\"No modes match filter\"}\n" << std::flush;
        } else {
            std::cerr << "ERROR: No modes match filter\n";
        }
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
                write_progressive_csv_header(csv_file, mode_filter, prog_snr, prog_freq, prog_multipath,
                                             "");
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
        
        // Generate markdown report
        generate_progressive_markdown_report(report_file, all_eq_results, 
            (int)total_elapsed, backend->backend_name(), use_auto_detect,
            prog_snr, prog_freq, prog_multipath,
            "");
        
        backend->disconnect();
        return 0;
    }
    
    // ================================================================
    // Standard Exhaustive Test Mode
    // ================================================================
    
    TestResults results;
    auto start_time = steady_clock::now();
    steady_clock::time_point end_time;
    bool use_duration = duration_seconds > 0;
    
    if (use_duration) {
        end_time = start_time + seconds(duration_seconds);
        max_iterations = 999999;  // Effectively unlimited
    }
    
    // Parallel execution (only for iteration mode, not duration mode)
    if (parallel_threads > 1 && !use_server && !use_duration) {
        // Build list of all test combinations for parallel execution
        struct TestJob {
            std::string eq;
            ModeInfo mode;
            ChannelCondition channel;
            std::string record_name;
        };
        
        std::vector<TestJob> all_jobs;
        for (int iter = 0; iter < max_iterations; iter++) {
            for (const auto& eq : eq_list) {
                for (const auto& mode : modes) {
                    for (const auto& channel : channels) {
                        TestJob job;
                        job.eq = eq;
                        job.mode = mode;
                        job.channel = channel;
                        job.record_name = (eq_list.size() > 1) ? eq + ":" + mode.name : mode.name;
                        all_jobs.push_back(job);
                    }
                }
            }
        }
        if (!json_output) {
            std::cout << "Running " << all_jobs.size() << " tests with " << parallel_threads << " threads...\n";
        }
        
        ParallelProgress progress;
        progress.init((int)all_jobs.size());
        
        // Create thread pool
        ThreadPool pool(parallel_threads);
        
        // Create worker backends (one per thread)
        std::vector<std::unique_ptr<ITestBackend>> worker_backends;
        for (int i = 0; i < parallel_threads; i++) {
            worker_backends.push_back(backend->clone());
        }
        
        std::atomic<size_t> job_index{0};
        std::atomic<int> next_worker{0};
        
        // Enqueue all jobs
        for (size_t i = 0; i < all_jobs.size(); i++) {
            pool.enqueue([&, i]() {
                const auto& job = all_jobs[i];
                
                // Get a worker backend (round-robin)
                int worker_id = next_worker++ % parallel_threads;
                auto* worker = worker_backends[worker_id].get();
                
                worker->set_equalizer(job.eq);
                
                double ber;
                bool passed = worker->run_test(job.mode, job.channel, test_data, ber);
                
                results.record(job.record_name, job.channel.name, passed, ber);
                progress.record(passed);
                
                // Print progress every 10 tests
                if (progress.completed % 10 == 0) {
                    progress.print_status();
                }
            });
        }
        
        pool.wait_all();
        progress.print_status();
        if (!json_output) std::cout << "\n";
        
    } else {
        // Sequential execution
        int iteration = 0;
        bool should_stop = false;
        
        while (!should_stop) {
            iteration++;
            
            // Check termination condition
            if (use_duration) {
                if (steady_clock::now() >= end_time) break;
            } else {
                if (iteration > max_iterations) break;
            }
            
            for (const auto& eq : eq_list) {
                backend->set_equalizer(eq);
                
                for (const auto& mode : modes) {
                    // No skipping - this is EXHAUSTIVE testing
                    
                    for (const auto& channel : channels) {
                        // Check time again for duration mode
                        if (use_duration && steady_clock::now() >= end_time) {
                            should_stop = true;
                            break;
                        }
                        
                        auto now = steady_clock::now();
                        auto elapsed = (int)duration_cast<seconds>(now - start_time).count();
                        
                        // Include eq in display
                        std::string mode_with_eq = (eq_list.size() > 1) ? eq + ":" + mode.name : mode.name;
                        
                        double ber;
                        bool passed = backend->run_test(mode, channel, test_data, ber);
                        
                        // Record with eq prefix if multiple equalizers
                        std::string record_name = (eq_list.size() > 1) ? eq + ":" + mode.name : mode.name;
                        results.record(record_name, channel.name, passed, ber);
                        
                        // Output progress
                        if (json_output) {
                            std::cout << "{\"type\":\"test\""
                                      << ",\"elapsed\":" << elapsed
                                      << ",\"mode\":\"" << mode_with_eq << "\""
                                      << ",\"channel\":\"" << channel.name << "\""
                                      << ",\"tests\":" << results.total_tests
                                      << ",\"passed\":" << results.total_passed()
                                      << ",\"rate\":" << std::fixed << std::setprecision(1) << results.overall_pass_rate()
                                      << ",\"result\":\"" << (passed ? "PASS" : "FAIL") << "\""
                                      << ",\"ber\":" << std::scientific << std::setprecision(6) << ber
                                      << ",\"iter\":" << iteration
                                      << ",\"max_iter\":" << max_iterations
                                      << "}\n" << std::flush;
                        } else {
                            print_progress(elapsed, mode_with_eq, channel.name, results.total_tests,
                                           results.overall_pass_rate(), iteration, max_iterations);
                        }
                    }
                    if (should_stop) break;
                }
                if (should_stop) break;
            }
        }
    }
    
    results.iterations = max_iterations;
    results.duration_seconds = (int)duration_cast<seconds>(
        steady_clock::now() - start_time).count();
    
    // Calculate rating
    std::string rating;
    double rate = results.overall_pass_rate();
    if (rate >= 95.0) rating = "EXCELLENT";
    else if (rate >= 80.0) rating = "GOOD";
    else if (rate >= 60.0) rating = "FAIR";
    else rating = "NEEDS WORK";
    
    // Print results
    if (json_output) {
        // Mode stats
        for (const auto& [mode, stats] : results.mode_stats) {
            std::cout << "{\"type\":\"mode_stats\""
                      << ",\"mode\":\"" << mode << "\""
                      << ",\"passed\":" << stats.passed
                      << ",\"failed\":" << stats.failed
                      << ",\"total\":" << stats.total
                      << ",\"rate\":" << std::fixed << std::setprecision(1) << stats.pass_rate()
                      << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << stats.avg_ber()
                      << "}\n" << std::flush;
        }
        
        // Channel stats
        for (const auto& [channel, stats] : results.channel_stats) {
            std::cout << "{\"type\":\"channel_stats\""
                      << ",\"channel\":\"" << channel << "\""
                      << ",\"passed\":" << stats.passed
                      << ",\"failed\":" << stats.failed
                      << ",\"total\":" << stats.total
                      << ",\"rate\":" << std::fixed << std::setprecision(1) << stats.pass_rate()
                      << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << stats.avg_ber()
                      << "}\n" << std::flush;
        }
        
        // Final summary
        double avg_ber = 0.0;
        int ber_count = 0;
        for (const auto& [_, stats] : results.mode_stats) {
            if (stats.ber_tests > 0) {
                avg_ber += stats.total_ber;
                ber_count += stats.ber_tests;
            }
        }
        if (ber_count > 0) avg_ber /= ber_count;
        
        std::cout << "{\"type\":\"done\""
                  << ",\"duration\":" << results.duration_seconds
                  << ",\"iterations\":" << results.iterations
                  << ",\"tests\":" << results.total_tests
                  << ",\"passed\":" << results.total_passed()
                  << ",\"failed\":" << results.total_failed()
                  << ",\"rate\":" << std::fixed << std::setprecision(1) << results.overall_pass_rate()
                  << ",\"avg_ber\":" << std::scientific << std::setprecision(6) << avg_ber
                  << ",\"rating\":\"" << rating << "\""
                  << ",\"report\":\"" << report_file << "\""
                  << "}\n" << std::flush;
    } else {
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
    }
    
    // Generate report
    generate_markdown_report(report_file, results, backend->backend_name(),
                            "");
    
    backend->disconnect();
    
    return results.overall_pass_rate() >= 80.0 ? 0 : 1;
}
