// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file main.cpp
 * @brief M110A Modem TCP Server - Main Entry Point
 * 
 * Updated to use phoenix_tcp_server.h which is built on tcp_server_base
 * for robust persistent connection handling.
 * 
 * Usage:
 *   m110a_server [options]
 * 
 * Options:
 *   --testdevices     Run with mock audio devices (for testing)
 *   --data-port N     Set data port (default: 4998)
 *   --control-port N  Set control port (default: 4999)
 *   --no-discovery    Disable UDP discovery broadcasts (ignored - no longer used)
 *   --help            Show this help
 */

#include "phoenix_tcp_server.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// Global for signal handling
static std::atomic<bool> g_running{true};
static phoenix_server::PhoenixServer* g_server = nullptr;

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
    std::cout << "  --no-discovery     Disable UDP discovery (no-op, legacy)\n";
    std::cout << "  --output-dir DIR   Set PCM output directory (default: ./tx_pcm_out/)\n";
    std::cout << "  --quiet            Reduce logging output\n";
    std::cout << "  --help             Show this help\n";
    std::cout << "\n";
    std::cout << "Network Ports:\n";
    std::cout << "  TCP 4998  Data port - raw binary message bytes\n";
    std::cout << "  TCP 4999  Control port - ASCII commands and status\n";
    std::cout << "\n";
    std::cout << "Example:\n";
    std::cout << "  " << program << " --testdevices\n";
}

int main(int argc, char* argv[]) {
    uint16_t control_port = phoenix_server::DEFAULT_CONTROL_PORT;
    uint16_t data_port = phoenix_server::DEFAULT_DATA_PORT;
    std::string output_dir = "./tx_pcm_out/";
    bool quiet = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--testdevices" || arg == "rlba") {
            // Test mode flag - accepted for compatibility
        }
        else if (arg == "--no-discovery") {
            // Legacy flag, no longer used (no UDP discovery)
        }
        else if (arg == "--quiet" || arg == "-q") {
            quiet = true;
        }
        else if (arg == "--data-port" && i + 1 < argc) {
            data_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--control-port" && i + 1 < argc) {
            control_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if ((arg == "--discovery-port") && i + 1 < argc) {
            // Legacy flag, skip the argument
            ++i;
        }
        else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
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
        std::cout << "Phoenix Nest M110A Server (tcp_base)\n";
        std::cout << "================================================\n";
        std::cout << m110a::copyright_notice() << "\n";
        std::cout << m110a::build_info() << "\n";
        std::cout << "================================================\n\n";
    }
    
    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Initialize sockets
    if (!tcp_base::socket_init()) {
        std::cerr << "Failed to initialize sockets\n";
        return 1;
    }
    
    // Create and configure server
    phoenix_server::PhoenixServer server;
    g_server = &server;
    
    server.configure_ports(control_port, data_port);
    server.set_pcm_output_dir(output_dir);
    server.set_quiet(quiet);
    
    // Start server
    if (!server.start()) {
        std::cerr << "Failed to start server\n";
        tcp_base::socket_cleanup();
        return 1;
    }
    
    if (!quiet) {
        std::cout << "Control port: " << control_port << "\n";
        std::cout << "Data port:    " << data_port << "\n";
        std::cout << "\nServer running. Press Ctrl+C to stop.\n\n";
    }
    
    // Main loop with polling
    while (g_running.load() && server.is_running()) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    server.stop();
    g_server = nullptr;
    tcp_base::socket_cleanup();
    
    if (!quiet) {
        std::cout << "Server shutdown complete.\n";
    }
    
    return 0;
}
