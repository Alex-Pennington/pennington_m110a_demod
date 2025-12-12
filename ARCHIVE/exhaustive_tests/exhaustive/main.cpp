/**
 * @file main.cpp
 * @brief Entry point for M110A Exhaustive Test Suite
 * 
 * M110A Modem - MIL-STD-188-110A Compatible HF Modem
 * Copyright (c) 2024-2025 Alex Pennington
 * Email: alex.pennington@organicengineer.com
 * 
 * Usage:
 *   exhaustive_test.exe [options]
 *   exhaustive_test.exe --json          # Machine-readable output
 *   exhaustive_test.exe --duration 180  # Run for 3 minutes
 *   exhaustive_test.exe --progressive   # Find mode limits
 */

#include "cli.h"
#include "output.h"
#include "exhaustive_runner.h"
#include "progressive_runner.h"

// Backend implementations - include ONE of these
#define TEST_FRAMEWORK_IMPLEMENTATION
#include "../test_framework.h"
#include "../direct_backend.h"
#include "../server_backend.h"

#include "../common/license.h"
#include "../api/version.h"

#include <iostream>
#include <memory>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

using namespace exhaustive;
using namespace test_framework;
using namespace m110a;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Enable ANSI escape codes on Windows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    
    // Parse command line
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        return 1;
    }
    
    // Create output handler (human or JSON)
    auto output = create_output(cfg.json_output);
    
    // License check
    if (!LicenseManager::has_valid_license()) {
        output->on_error("No valid license found");
        output->on_info("Please ensure license.key is in the application directory");
        return 1;
    }
    
    // Version info (only for human output)
    if (!cfg.json_output && !cfg.quiet) {
        std::cout << m110a::version_header() << "\n";
        std::cout << m110a::build_info() << "\n";
    }
    
    // Create backend
    std::unique_ptr<ITestBackend> backend;
    if (cfg.use_server) {
        backend = std::make_unique<ServerBackend>(cfg.server_host, cfg.server_port);
    } else {
        auto direct = std::make_unique<DirectBackend>();
        direct->set_mode_detection(cfg.use_auto_detect ? "AUTO" : "KNOWN");
        backend = std::move(direct);
    }
    
    // Connect
    if (!backend->connect()) {
        output->on_error("Failed to connect to backend");
        return 1;
    }
    output->on_info("Connected.");
    
    // Generate report filename if not specified
    if (cfg.report_file.empty()) {
        // Create reports directory if needed
        fs::path reports_dir = "reports";
        if (!fs::exists(reports_dir)) {
            fs::create_directories(reports_dir);
        }
        
        // Generate timestamped filename
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&now_time);
        
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm);
        
        std::string prefix = cfg.progressive_mode ? "progressive" : "exhaustive";
        std::string backend_name = cfg.use_server ? "server" : "direct";
        cfg.report_file = "reports/" + prefix + "_" + backend_name + "_" + buf + ".md";
    }
    
    // Run tests
    if (cfg.progressive_mode) {
        ProgressiveRunner runner(*backend, *output, cfg);
        auto results = runner.run();
        
        // Generate report
        std::map<std::string, std::map<std::string, ProgressiveResult>> all_results;
        for (const auto& eq : cfg.equalizers) {
            std::map<std::string, ProgressiveResult> eq_results;
            for (const auto& [name, result] : results) {
                // Filter by equalizer prefix if multiple
                if (cfg.equalizers.size() > 1) {
                    if (name.find(eq + ":") == 0) {
                        std::string mode_name = name.substr(eq.length() + 1);
                        eq_results[mode_name] = result;
                    }
                } else {
                    eq_results[name] = result;
                }
            }
            all_results[eq] = eq_results;
        }
        
        generate_progressive_markdown_report(
            cfg.report_file,
            all_results,
            0,  // Duration filled in by runner
            backend->backend_name(),
            cfg.use_auto_detect,
            cfg.prog_snr, cfg.prog_freq, cfg.prog_multipath,
            LicenseManager::get_hardware_id()
        );
        
        // CSV output
        if (!cfg.csv_file.empty()) {
            write_progressive_csv_header(cfg.csv_file, cfg.mode_filter,
                cfg.prog_snr, cfg.prog_freq, cfg.prog_multipath,
                LicenseManager::get_hardware_id());
            
            for (const auto& [name, result] : results) {
                append_progressive_csv_row(cfg.csv_file, result, 0,
                    cfg.prog_snr, cfg.prog_freq, cfg.prog_multipath);
            }
            output->on_info("CSV saved to: " + cfg.csv_file);
        }
        
    } else {
        // Exhaustive testing
        ExhaustiveRunner runner(*backend, *output, cfg);
        
        TestResults results;
        if (cfg.parallel_threads > 1 && !cfg.use_server) {
            results = runner.run_parallel();
        } else {
            results = runner.run();
        }
        
        // Generate markdown report
        generate_markdown_report(
            cfg.report_file,
            results,
            backend->backend_name(),
            cfg.use_auto_detect,
            LicenseManager::get_hardware_id()
        );
        
        output->on_info("Report saved to: " + cfg.report_file);
    }
    
    backend->disconnect();
    
    return 0;
}
