/**
 * @file exhaustive_test_unified.cpp
 * @brief Unified Exhaustive Modem Test Suite - Pure JSON Output
 * 
 * M110A Modem - MIL-STD-188-110A Compatible HF Modem
 * Copyright (c) 2024-2025 Alex Pennington
 * Email: alex.pennington@organicengineer.com
 * 
 * Tests modem across all modes, SNR levels, and channel conditions.
 * All output is JSON Lines (JSONL) format for machine consumption.
 */

#include "test_framework.h"
#include "direct_backend.h"
#include "server_backend.h"
#include "json_output.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

using namespace test_framework;
using namespace test_output;
using namespace std::chrono;
using namespace m110a;

// Global JSON output
static JsonOutput out;

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
        out.test(mode.name, "snr", snr, passed, ber);
        return passed;
    };
    
    if (!test_snr(high)) return high;
    if (test_snr(low)) return low;
    
    while (high - low > 1.0f) {
        float mid = (high + low) / 2.0f;
        if (test_snr(mid)) high = mid;
        else low = mid;
    }
    return high;
}

float run_progressive_freq_test(ITestBackend& backend, const ModeInfo& mode,
                                 const std::vector<uint8_t>& test_data) {
    float low = 0.0f;
    float high = 150.0f;
    
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
        out.test(mode.name, "freq", freq, passed, ber);
        return passed;
    };
    
    if (!test_freq(0.0f)) return 0.0f;
    
    float probe = 10.0f;
    while (probe <= high && test_freq(probe)) {
        low = probe;
        probe *= 2.0f;
    }
    if (probe > high) probe = high;
    high = probe;
    
    while (high - low > 1.0f) {
        float mid = (high + low) / 2.0f;
        if (test_freq(mid)) low = mid;
        else high = mid;
    }
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
        out.test(mode.name, "multipath", (double)delay, passed, ber);
        return passed;
    };
    
    if (!test_mp(0)) return 0;
    
    int probe = 20;
    while (probe <= high && test_mp(probe)) {
        low = probe;
        probe *= 2;
    }
    if (probe > high) probe = high;
    high = probe;
    
    while (high - low > 5) {
        int mid = (high + low) / 2;
        if (test_mp(mid)) low = mid;
        else high = mid;
    }
    return low;
}

void run_progressive_tests(ITestBackend& backend, const ModeInfo& mode,
                           const std::vector<uint8_t>& test_data,
                           bool test_snr, bool test_freq, bool test_multipath) {
    if (test_snr) {
        backend.reset_state();
        float limit = run_progressive_snr_test(backend, mode, test_data);
        out.result(mode.name, "snr", limit, "dB");
    }
    
    if (test_freq) {
        backend.reset_state();
        float limit = run_progressive_freq_test(backend, mode, test_data);
        out.result(mode.name, "freq", limit, "Hz");
    }
    
    if (test_multipath) {
        backend.reset_state();
        int limit = run_progressive_multipath_test(backend, mode, test_data);
        out.result(mode.name, "multipath", (double)limit, "samples");
    }
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    // Force unbuffered stdout for real-time JSON streaming
    std::cout.setf(std::ios::unitbuf);
    setvbuf(stdout, NULL, _IONBF, 0);
    
    // Configuration
    int max_iterations = 1;
    int duration_seconds = 0;
    std::string mode_filter;
    std::vector<std::string> mode_list;
    bool use_server = false;
    std::string host = "127.0.0.1";
    int control_port = 4999;
    bool progressive_mode = false;
    bool prog_snr = false, prog_freq = false, prog_multipath = false;
    std::string equalizer = "DFE";
    std::string afc_mode = "MOOSE";
    bool use_auto_detect = false;
    
    // Helper to split comma-separated string
    auto split_csv = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> result;
        std::istringstream iss(s);
        std::string token;
        while (std::getline(iss, token, ',')) {
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
        } else if (arg == "--modes" && i + 1 < argc) {
            mode_list = split_csv(argv[++i]);
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
            prog_snr = true;
            // prog_freq and prog_multipath disabled - channel simulator unreliable
            // prog_freq = prog_multipath = true;
        } else if (arg == "--prog-snr") {
            progressive_mode = true;
            prog_snr = true;
        } else if (arg == "--prog-freq") {
            progressive_mode = true;
            prog_freq = true;
        } else if (arg == "--prog-multipath") {
            progressive_mode = true;
            prog_multipath = true;
        } else if ((arg == "--eq" || arg == "--equalizer") && i + 1 < argc) {
            equalizer = argv[++i];
            for (auto& c : equalizer) c = std::toupper(c);
        } else if (arg == "--afc" && i + 1 < argc) {
            afc_mode = argv[++i];
            for (auto& c : afc_mode) c = std::toupper(c);
        } else if (arg == "--use-auto-detect") {
            use_auto_detect = true;
        } else if (arg == "--help" || arg == "-h") {
            // Help goes to stderr so it doesn't pollute JSON output
            std::cerr << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --iterations N    Number of test iterations (default: 1)\n"
                      << "  --duration N      Run for N seconds\n"
                      << "  --mode MODE       Test only specific mode\n"
                      << "  --modes LIST      Comma-separated list of modes\n"
                      << "  --server          Use server backend\n"
                      << "  --host IP         Server IP (default: 127.0.0.1)\n"
                      << "  --port N          Server port (default: 4999)\n"
                      << "  --progressive     Run progressive tests (SNR, freq, multipath)\n"
                      << "  --prog-snr        Progressive SNR test only\n"
                      << "  --prog-freq       Progressive freq offset test only\n"
                      << "  --prog-multipath  Progressive multipath test only\n"
                      << "  --eq TYPE         Equalizer: NONE, DFE, DFE_RLS, MLSE_L2, etc.\n"
                      << "  --afc TYPE        AFC: LEGACY, MULTICHANNEL, EXTENDED, MOOSE\n"
                      << "  --use-auto-detect Use auto mode detection\n"
                      << "  --help            Show this help\n\n"
                      << "Output: Pure JSON Lines (JSONL) to stdout\n";
            return 0;
        }
    }
    
    // Create backend
    std::unique_ptr<ITestBackend> backend;
    if (use_server) {
        backend = std::make_unique<ServerBackend>(host, control_port, control_port - 1);
    } else {
        backend = std::make_unique<DirectBackend>(42, use_auto_detect);
    }
    
    // Determine test type string
    std::string test_type;
    if (progressive_mode) {
        test_type = "progressive";
        if (prog_snr && !prog_freq && !prog_multipath) test_type = "progressive_snr";
        else if (!prog_snr && prog_freq && !prog_multipath) test_type = "progressive_freq";
        else if (!prog_snr && !prog_freq && prog_multipath) test_type = "progressive_multipath";
    } else {
        test_type = "exhaustive";
    }
    
    // Emit start event with all metadata
    out.start("exhaustive_test",
              backend->backend_name(),
              afc_mode,
              equalizer,
              mode_filter,
              test_type);
    
    // Emit config
    out.config(42, use_auto_detect);
    
    // Connect
    if (!backend->connect()) {
        out.error("Cannot connect to backend");
        out.end(1);
        return 1;
    }
    
    // Set equalizer
    if (!backend->set_equalizer(equalizer)) {
        out.error("Invalid equalizer: " + equalizer);
        out.end(1);
        return 1;
    }
    
    // Set AFC mode (DirectBackend only)
    if (!use_server) {
        auto* direct = dynamic_cast<DirectBackend*>(backend.get());
        if (direct && !direct->set_afc_mode(afc_mode)) {
            out.error("Invalid AFC mode: " + afc_mode);
            out.end(1);
            return 1;
        }
    }
    
    // Test data
    std::string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    
    // Get modes
    auto all_modes = get_all_modes();
    std::vector<ModeInfo> modes;
    
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
        out.error("No modes match filter");
        out.end(1);
        return 1;
    }
    
    // ================================================================
    // Progressive Mode
    // ================================================================
    if (progressive_mode) {
        for (const auto& mode : modes) {
            run_progressive_tests(*backend, mode, test_data,
                                  prog_snr, prog_freq, prog_multipath);
        }
        
        backend->disconnect();
        out.end(0);
        return 0;
    }
    
    // ================================================================
    // Standard Exhaustive Test Mode
    // ================================================================
    auto channels = get_standard_channels();
    auto start_time = steady_clock::now();
    steady_clock::time_point end_time;
    bool use_duration = duration_seconds > 0;
    
    if (use_duration) {
        end_time = start_time + seconds(duration_seconds);
        max_iterations = 999999;
    }
    
    int iteration = 0;
    bool should_stop = false;
    
    while (!should_stop) {
        iteration++;
        
        if (use_duration) {
            if (steady_clock::now() >= end_time) break;
        } else {
            if (iteration > max_iterations) break;
        }
        
        for (const auto& mode : modes) {
            for (const auto& channel : channels) {
                if (use_duration && steady_clock::now() >= end_time) {
                    should_stop = true;
                    break;
                }
                
                double ber;
                bool passed = backend->run_test(mode, channel, test_data, ber);
                out.test(mode.name, channel.name, passed, ber, iteration);
            }
            if (should_stop) break;
        }
    }
    
    backend->disconnect();
    out.end(0);
    return 0;
}
