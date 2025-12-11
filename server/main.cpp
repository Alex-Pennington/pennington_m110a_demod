// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file main.cpp
 * @brief Brain Modem Compatible Server - Main Entry Point
 * 
 * Usage:
 *   m110a_server [options]
 * 
 * Options:
 *   --testdevices     Run with mock audio devices (for testing)
 *   --data-port N     Set data port (default: 4998)
 *   --control-port N  Set control port (default: 4999)
 *   --no-discovery    Disable UDP discovery broadcasts
 *   --help            Show this help
 */

#include "brain_server.h"
#include "api/modem.h"
#include "api/version.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

using namespace m110a;
using namespace m110a::server;

// Global server instance for signal handling
static std::atomic<bool> g_running{true};
static BrainServer* g_server = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
    g_running.store(false);
    if (g_server) {
        g_server->stop();
    }
}

void print_usage(const char* program) {
    std::cout << m110a::version_header() << "\n";
    std::cout << m110a::build_info() << "\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --testdevices      Run with mock audio devices (for testing)\n";
    std::cout << "  --data-port N      Set data port (default: 4998)\n";
    std::cout << "  --control-port N   Set control port (default: 4999)\n";
    std::cout << "  --discovery-port N Set discovery port (default: 5000)\n";
    std::cout << "  --no-discovery     Disable UDP discovery broadcasts\n";
    std::cout << "  --output-dir DIR   Set PCM output directory (default: ./tx_pcm_out/)\n";
    std::cout << "  --quiet            Reduce logging output\n";
    std::cout << "  --help             Show this help\n";
    std::cout << "\n";
    std::cout << "Network Ports:\n";
    std::cout << "  TCP 4998  Data port - raw binary message bytes\n";
    std::cout << "  TCP 4999  Control port - ASCII commands and status\n";
    std::cout << "  UDP 5000  Discovery - broadcasts 'helo' for auto-discovery\n";
    std::cout << "\n";
    std::cout << "Example:\n";
    std::cout << "  " << program << " --testdevices\n";
}

int main(int argc, char* argv[]) {
    ServerConfig config;
    bool test_mode = false;
    bool quiet = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--testdevices" || arg == "rlba") {
            test_mode = true;
        }
        else if (arg == "--no-discovery") {
            config.enable_discovery = false;
        }
        else if (arg == "--quiet" || arg == "-q") {
            quiet = true;
            config.log_commands = false;
            config.log_status = false;
        }
        else if (arg == "--data-port" && i + 1 < argc) {
            config.data_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--control-port" && i + 1 < argc) {
            config.control_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--discovery-port" && i + 1 < argc) {
            config.discovery_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--output-dir" && i + 1 < argc) {
            config.pcm_output_dir = argv[++i];
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
    }
    
    // Print banner
    if (!quiet) {
        std::cout << "================================================\n";
        std::cout << m110a::version_header() << "\n";
        std::cout << "Phoenix Nest M110A - Brain Modem Compatible Interface\n";
        std::cout << "================================================\n";
        std::cout << m110a::copyright_notice() << "\n";
        std::cout << m110a::build_info() << "\n";
        if (test_mode) {
            std::cout << "Mode: Test (mock audio devices)\n";
        }
        std::cout << "================================================\n\n";
    }
    
    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Create and configure server
    BrainServer server;
    g_server = &server;
    
    server.configure(config);
    
    // Set up callbacks for logging
    if (!quiet) {
        server.on_data_received([](const std::vector<uint8_t>& data) {
            std::cout << "[DATA] Received " << data.size() << " bytes\n";
        });
    }
    
    // Start server
    if (!server.start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    if (!quiet) {
        std::cout << "\nServer running. Press Ctrl+C to stop.\n\n";
    }
    
    // Main loop - wait for shutdown signal
    while (g_running.load() && server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    server.stop();
    g_server = nullptr;
    
    if (!quiet) {
        std::cout << "Server shutdown complete.\n";
    }
    
    return 0;
}
