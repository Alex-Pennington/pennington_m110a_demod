// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Phoenix Nest LLC
/**
 * @file brain_server_main.cpp
 * @brief Brain Core TCP Server - Main Entry Point
 * 
 * Standalone TCP server for the Brain Modem (m188110a) core.
 * Uses the robust tcp_server_base for connection handling.
 */

#include "brain_tcp_server.h"
#include <iostream>
#include <csignal>
#include <cstring>

static brain_server::BrainServer* g_server = nullptr;

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        std::cout << "\n[SHUTDOWN] Received signal, stopping server..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[SHUTDOWN] Received signal, stopping server..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
    }
}
#endif

static void print_usage(const char* prog) {
    std::cout << "Brain Core TCP Server (tcp_base edition)\n"
              << "Usage: " << prog << " [options]\n"
              << "\nOptions:\n"
              << "  -c, --control-port PORT  Control port (default: 3999)\n"
              << "  -d, --data-port PORT     Data port (default: 3998)\n"
              << "  -o, --output DIR         PCM output directory (default: ./tx_pcm_out/)\n"
              << "  -h, --help               Show this help\n"
              << "\nExamples:\n"
              << "  " << prog << "                        # Default ports\n"
              << "  " << prog << " -c 4099 -d 4098       # Custom ports\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t control_port = brain_server::DEFAULT_CONTROL_PORT;
    uint16_t data_port = brain_server::DEFAULT_DATA_PORT;
    std::string pcm_dir = "./tx_pcm_out/";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if ((arg == "-c" || arg == "--control-port") && i + 1 < argc) {
            control_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if ((arg == "-d" || arg == "--data-port") && i + 1 < argc) {
            data_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            pcm_dir = argv[++i];
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Setup signal handlers
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif
    
    std::cout << "=== Brain Core TCP Server (tcp_base) ===" << std::endl;
    std::cout << "Control port: " << control_port << std::endl;
    std::cout << "Data port: " << data_port << std::endl;
    std::cout << "PCM output: " << pcm_dir << std::endl;
    std::cout << std::endl;
    
    // Create and start server
    brain_server::BrainServer server;
    g_server = &server;
    
    server.set_ports(control_port, data_port);
    server.set_pcm_output_dir(pcm_dir);
    
    if (!server.start()) {
        std::cerr << "Failed to start server!" << std::endl;
        return 1;
    }
    
    std::cout << "[SERVER] Running. Press Ctrl+C to stop." << std::endl;
    
    // Main loop
    while (server.is_running()) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[SERVER] Shutdown complete." << std::endl;
    g_server = nullptr;
    return 0;
}
