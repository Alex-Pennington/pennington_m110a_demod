/**
 * @file main.cpp
 * @brief Entry point for M110A Test GUI Server
 * 
 * Web-based GUI for M110A Exhaustive Test Suite
 * 
 * Usage:
 *   test_gui.exe [--port N]
 *   Then open http://localhost:8080 in browser
 */

#include "server.h"
#include <iostream>
#include <string>

void print_help(const char* prog) {
    std::cout << "M110A Test GUI Server\n\n";
    std::cout << "Usage: " << prog << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --port N, -p N   HTTP port (default: 8080)\n";
    std::cout << "  --help, -h       Show this help\n\n";
    std::cout << "Features:\n";
    std::cout << "  - Comprehensive exhaustive test configuration\n";
    std::cout << "  - Backend selection (Direct API / TCP Server)\n";
    std::cout << "  - Parallelization options\n";
    std::cout << "  - Channel condition testing (AWGN, Multipath, Freq Offset)\n";
    std::cout << "  - Real-time progress and results\n";
    std::cout << "  - Cross-modem interop testing (Brain <-> PhoenixNest)\n";
    std::cout << "  - Report generation and export\n";
}

int main(int argc, char* argv[]) {
    int port = 8080;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        }
    }
    
    test_gui::TestGuiServer server(port);
    server.start();
    
    return 0;
}
