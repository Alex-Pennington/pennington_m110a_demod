/**
 * @file cli.h
 * @brief Command-line parsing and configuration for exhaustive tests
 */

#ifndef EXHAUSTIVE_CLI_H
#define EXHAUSTIVE_CLI_H

#include <string>
#include <vector>
#include <iostream>
#include <cstring>

namespace exhaustive {

// ============================================================
// Configuration
// ============================================================

struct Config {
    // Test selection
    int max_iterations = 1;
    int duration_seconds = 0;      // 0 = use iterations
    std::string mode_filter;       // Empty = all modes
    
    // Backend
    bool use_server = false;
    std::string server_host = "127.0.0.1";
    int server_port = 4999;
    
    // Parallelization
    int parallel_threads = 1;
    
    // Progressive mode
    bool progressive_mode = false;
    bool prog_snr = false;
    bool prog_freq = false;
    bool prog_multipath = false;
    
    // Equalizers
    std::vector<std::string> equalizers = {"DFE"};
    
    // Output
    std::string report_file;       // Auto-generated if empty
    std::string csv_file;
    bool json_output = false;      // JSON lines mode for machine parsing
    bool use_auto_detect = false;  // AUTO vs KNOWN mode detection
    bool quiet = false;
    
    // Test data
    std::string test_message = "THE QUICK BROWN FOX JUMPED OVER THE LAZY DOGS BACK 1234567890";
};

// ============================================================
// CLI Parsing
// ============================================================

inline void print_usage(const char* prog) {
    std::cout << "M110A Exhaustive Test Suite\n\n";
    std::cout << "Usage: " << prog << " [options]\n\n";
    std::cout << "Test Selection:\n";
    std::cout << "  --iterations N    Number of test iterations (default: 1)\n";
    std::cout << "  --duration N      Run for N seconds (overrides iterations)\n";
    std::cout << "  --mode MODE       Test only specific mode (e.g., 600S, 1200L, SHORT, LONG)\n";
    std::cout << "\n";
    std::cout << "Backend:\n";
    std::cout << "  --server          Use TCP server backend instead of direct API\n";
    std::cout << "  --host IP         Server IP address (default: 127.0.0.1)\n";
    std::cout << "  --port N          Server control port (default: 4999)\n";
    std::cout << "\n";
    std::cout << "Parallelization:\n";
    std::cout << "  --parallel N      Use N threads (default: 1, direct API only)\n";
    std::cout << "\n";
    std::cout << "Progressive Mode (find mode limits):\n";
    std::cout << "  --progressive     Run all progressive tests\n";
    std::cout << "  --prog-snr        Progressive SNR test only\n";
    std::cout << "  --prog-freq       Progressive frequency offset test only\n";
    std::cout << "  --prog-multipath  Progressive multipath test only\n";
    std::cout << "\n";
    std::cout << "Equalizers:\n";
    std::cout << "  --eq EQ           Use equalizer: DFE, MLSE, or BOTH (default: DFE)\n";
    std::cout << "\n";
    std::cout << "Output:\n";
    std::cout << "  --report FILE     Output report file (auto-generated if not specified)\n";
    std::cout << "  --csv FILE        Output progressive results to CSV\n";
    std::cout << "  --json            Output JSON lines (machine-readable)\n";
    std::cout << "  --auto-detect     Use AUTO mode detection (vs KNOWN)\n";
    std::cout << "  --quiet           Minimal output\n";
    std::cout << "\n";
    std::cout << "Other:\n";
    std::cout << "  --help            Show this help\n";
}

inline bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        }
        else if (arg == "--iterations" && i + 1 < argc) {
            cfg.max_iterations = std::stoi(argv[++i]);
        }
        else if (arg == "--duration" && i + 1 < argc) {
            cfg.duration_seconds = std::stoi(argv[++i]);
        }
        else if (arg == "--mode" && i + 1 < argc) {
            cfg.mode_filter = argv[++i];
        }
        else if (arg == "--server") {
            cfg.use_server = true;
        }
        else if (arg == "--host" && i + 1 < argc) {
            cfg.server_host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc) {
            cfg.server_port = std::stoi(argv[++i]);
        }
        else if (arg == "--parallel" && i + 1 < argc) {
            cfg.parallel_threads = std::stoi(argv[++i]);
        }
        else if (arg == "--progressive") {
            cfg.progressive_mode = true;
            cfg.prog_snr = cfg.prog_freq = cfg.prog_multipath = true;
        }
        else if (arg == "--prog-snr") {
            cfg.progressive_mode = true;
            cfg.prog_snr = true;
        }
        else if (arg == "--prog-freq") {
            cfg.progressive_mode = true;
            cfg.prog_freq = true;
        }
        else if (arg == "--prog-multipath") {
            cfg.progressive_mode = true;
            cfg.prog_multipath = true;
        }
        else if (arg == "--eq" && i + 1 < argc) {
            std::string eq = argv[++i];
            if (eq == "BOTH") {
                cfg.equalizers = {"DFE", "MLSE"};
            } else {
                cfg.equalizers = {eq};
            }
        }
        else if (arg == "--report" && i + 1 < argc) {
            cfg.report_file = argv[++i];
        }
        else if (arg == "--csv" && i + 1 < argc) {
            cfg.csv_file = argv[++i];
        }
        else if (arg == "--json") {
            cfg.json_output = true;
        }
        else if (arg == "--auto-detect") {
            cfg.use_auto_detect = true;
        }
        else if (arg == "--quiet") {
            cfg.quiet = true;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    
    // Validate
    if (cfg.parallel_threads > 1 && cfg.use_server) {
        std::cerr << "Warning: Parallel execution not supported with server backend, using 1 thread\n";
        cfg.parallel_threads = 1;
    }
    
    return true;
}

} // namespace exhaustive

#endif // EXHAUSTIVE_CLI_H
