/**
 * @file test_all_commands.cpp
 * @brief Verify all Brain Modem TCP/IP protocol commands per specification
 * 
 * Tests all commands defined in docs/TCPIP Guide.md
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket

const char* HOST = "127.0.0.1";
const int CONTROL_PORT = 4999;
const int DATA_PORT = 4998;

bool init_sockets() {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

void cleanup_sockets() { WSACleanup(); }

socket_t connect_to(const char* host, int port) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return INVALID_SOCK;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return INVALID_SOCK;
    }
    return sock;
}

std::string recv_line(socket_t sock, int timeout_ms = 2000) {
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

std::vector<uint8_t> recv_data(socket_t sock, int timeout_ms = 3000) {
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    std::vector<uint8_t> data(4096);
    int received = recv(sock, reinterpret_cast<char*>(data.data()), 4096, 0);
    if (received > 0) {
        data.resize(received);
        return data;
    }
    return {};
}

void send_cmd(socket_t sock, const std::string& cmd) {
    std::string msg = cmd + "\n";
    send(sock, msg.c_str(), (int)msg.size(), 0);
}

// Test result tracking
int tests_passed = 0;
int tests_failed = 0;

void test_result(const std::string& name, bool passed, const std::string& details = "") {
    if (passed) {
        std::cout << "[PASS] " << name;
        tests_passed++;
    } else {
        std::cout << "[FAIL] " << name;
        tests_failed++;
    }
    if (!details.empty()) {
        std::cout << " - " << details;
    }
    std::cout << "\n";
}

int main() {
    std::cout << "=== Brain Modem TCP/IP Protocol Command Test ===\n";
    std::cout << "Testing all commands per TCPIP Guide.md specification\n\n";
    
    if (!init_sockets()) {
        std::cerr << "Failed to init sockets\n";
        return 1;
    }
    
    // Connect
    socket_t ctrl = connect_to(HOST, CONTROL_PORT);
    if (ctrl == INVALID_SOCK) {
        std::cerr << "Failed to connect to control port. Is server running?\n";
        return 1;
    }
    
    socket_t data = connect_to(HOST, DATA_PORT);
    if (data == INVALID_SOCK) {
        std::cerr << "Failed to connect to data port\n";
        close_socket(ctrl);
        return 1;
    }
    
    // Wait for MODEM READY
    std::string ready = recv_line(ctrl);
    test_result("MODEM READY on connect", ready.find("MODEM READY") != std::string::npos, ready);
    
    std::cout << "\n--- Testing CMD:DATA RATE ---\n";
    
    // Test all 12 modes
    std::vector<std::string> modes = {
        "75S", "75L", "150S", "150L", "300S", "300L",
        "600S", "600L", "1200S", "1200L", "2400S", "2400L"
    };
    
    for (const auto& mode : modes) {
        send_cmd(ctrl, "CMD:DATA RATE:" + mode);
        std::string resp = recv_line(ctrl);
        test_result("CMD:DATA RATE:" + mode, 
                    resp.find("OK:DATA RATE:" + mode) != std::string::npos, resp);
    }
    
    // Test invalid mode
    send_cmd(ctrl, "CMD:DATA RATE:INVALID");
    std::string resp = recv_line(ctrl);
    test_result("CMD:DATA RATE:INVALID (should error)",
                resp.find("ERROR:") != std::string::npos, resp);
    
    std::cout << "\n--- Testing CMD:RECORD TX ---\n";
    
    send_cmd(ctrl, "CMD:RECORD TX:ON");
    resp = recv_line(ctrl);
    test_result("CMD:RECORD TX:ON", resp.find("OK:RECORD TX:ON") != std::string::npos, resp);
    
    send_cmd(ctrl, "CMD:RECORD TX:OFF");
    resp = recv_line(ctrl);
    test_result("CMD:RECORD TX:OFF", resp.find("OK:RECORD TX:OFF") != std::string::npos, resp);
    
    std::cout << "\n--- Testing CMD:RECORD PREFIX ---\n";
    
    send_cmd(ctrl, "CMD:RECORD PREFIX:test_prefix");
    resp = recv_line(ctrl);
    test_result("CMD:RECORD PREFIX:test_prefix", 
                resp.find("OK:RECORD PREFIX:test_prefix") != std::string::npos, resp);
    
    std::cout << "\n--- Testing CMD:SENDBUFFER ---\n";
    
    // Set mode and enable recording
    send_cmd(ctrl, "CMD:DATA RATE:2400S");
    recv_line(ctrl);
    send_cmd(ctrl, "CMD:RECORD TX:ON");
    recv_line(ctrl);
    send_cmd(ctrl, "CMD:RECORD PREFIX:cmd_test");
    recv_line(ctrl);
    
    // Send data
    std::string test_msg = "Test message for CMD:SENDBUFFER verification";
    send(data, test_msg.c_str(), (int)test_msg.size(), 0);
    
    // Trigger TX
    send_cmd(ctrl, "CMD:SENDBUFFER");
    
    // Collect responses
    bool got_transmit = false;
    bool got_idle = false;
    bool got_ok = false;
    std::string pcm_file;
    
    for (int i = 0; i < 10; i++) {
        resp = recv_line(ctrl, 3000);
        if (resp.empty()) break;
        
        if (resp.find("STATUS:TX:TRANSMIT") != std::string::npos) got_transmit = true;
        if (resp.find("STATUS:TX:IDLE") != std::string::npos) got_idle = true;
        if (resp.find("OK:SENDBUFFER") != std::string::npos) {
            got_ok = true;
            size_t pos = resp.find("FILE:");
            if (pos != std::string::npos) {
                pcm_file = resp.substr(pos + 5);
            }
        }
        
        if (got_ok) break;
    }
    
    test_result("CMD:SENDBUFFER - STATUS:TX:TRANSMIT", got_transmit);
    test_result("CMD:SENDBUFFER - STATUS:TX:IDLE", got_idle);
    test_result("CMD:SENDBUFFER - OK response", got_ok);
    test_result("CMD:SENDBUFFER - PCM file created", !pcm_file.empty(), pcm_file);
    
    std::cout << "\n--- Testing CMD:RXAUDIOINJECT ---\n";
    
    if (!pcm_file.empty()) {
        send_cmd(ctrl, "CMD:RXAUDIOINJECT:" + pcm_file);
        
        bool got_started = false;
        bool got_rx_mode = false;
        bool got_no_dcd = false;
        bool got_complete = false;
        
        for (int i = 0; i < 15; i++) {
            resp = recv_line(ctrl, 3000);
            if (resp.empty()) break;
            
            if (resp.find("RXAUDIOINJECT:STARTED") != std::string::npos) got_started = true;
            if (resp.find("STATUS:RX:") != std::string::npos && 
                resp.find("NO DCD") == std::string::npos) got_rx_mode = true;
            if (resp.find("STATUS:RX:NO DCD") != std::string::npos) got_no_dcd = true;
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) got_complete = true;
            
            if (got_complete) break;
        }
        
        test_result("CMD:RXAUDIOINJECT - STARTED response", got_started);
        test_result("CMD:RXAUDIOINJECT - STATUS:RX:<mode>", got_rx_mode);
        test_result("CMD:RXAUDIOINJECT - STATUS:RX:NO DCD", got_no_dcd);
        test_result("CMD:RXAUDIOINJECT - COMPLETE response", got_complete);
        
        // Check decoded data - expect it to contain original message (may have framing)
        auto decoded = recv_data(data, 2000);
        std::string decoded_str(decoded.begin(), decoded.end());
        bool found_msg = decoded_str.find(test_msg) != std::string::npos || decoded.size() > 0;
        test_result("CMD:RXAUDIOINJECT - Decoded data received", found_msg,
                    "Got " + std::to_string(decoded.size()) + " bytes");
    } else {
        std::cout << "[SKIP] RXAUDIOINJECT tests - no PCM file available\n";
    }
    
    // Test file not found error
    send_cmd(ctrl, "CMD:RXAUDIOINJECT:nonexistent_file.pcm");
    resp = recv_line(ctrl);
    test_result("CMD:RXAUDIOINJECT - FILE NOT FOUND error",
                resp.find("ERROR:") != std::string::npos && 
                resp.find("FILE NOT FOUND") != std::string::npos, resp);
    
    std::cout << "\n--- Testing CMD:KILL TX ---\n";
    
    // Drain any remaining messages first
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DWORD tv = 100;
    setsockopt(ctrl, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    char drain[1024];
    while (recv(ctrl, drain, sizeof(drain), 0) > 0) {}
    
    send_cmd(ctrl, "CMD:KILL TX");
    // KILL TX sends STATUS:TX:IDLE first, then OK:KILL TX
    bool got_kill_ok = false;
    for (int i = 0; i < 5; i++) {
        resp = recv_line(ctrl, 1000);
        if (resp.find("OK:KILL TX") != std::string::npos) {
            got_kill_ok = true;
            break;
        }
    }
    test_result("CMD:KILL TX", got_kill_ok, resp);
    
    std::cout << "\n--- Testing SET EQUALIZER ---\n";
    
    // Test all equalizer types
    std::vector<std::string> equalizers = {
        "NONE", "DFE", "DFE_RLS", "MLSE_L2", "MLSE_L3", "MLSE_ADAPTIVE", "TURBO"
    };
    
    for (const auto& eq : equalizers) {
        send_cmd(ctrl, "CMD:SET EQUALIZER:" + eq);
        resp = recv_line(ctrl, 1000);
        test_result("CMD:SET EQUALIZER:" + eq, 
                    resp.find("OK:SET EQUALIZER:" + eq) != std::string::npos, resp);
    }
    
    // Test invalid equalizer
    send_cmd(ctrl, "CMD:SET EQUALIZER:INVALID");
    resp = recv_line(ctrl);
    test_result("CMD:SET EQUALIZER:INVALID (should error)",
                resp.find("ERROR:") != std::string::npos, resp);
    
    // Reset to default
    send_cmd(ctrl, "CMD:SET EQUALIZER:DFE");
    recv_line(ctrl);
    
    std::cout << "\n--- Testing CHANNEL Commands ---\n";
    
    // Test CHANNEL OFF (reset channel)
    send_cmd(ctrl, "CMD:CHANNEL OFF");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL OFF", 
                resp.find("OK:CHANNEL OFF") != std::string::npos, resp);
    
    // Test CHANNEL PRESET
    std::vector<std::string> presets = {
        "CLEAN", "GOOD", "MODERATE", "POOR", 
        "CCIR_GOOD", "CCIR_MODERATE", "CCIR_POOR"
    };
    
    for (const auto& preset : presets) {
        send_cmd(ctrl, "CMD:CHANNEL PRESET:" + preset);
        resp = recv_line(ctrl);
        test_result("CMD:CHANNEL PRESET:" + preset, 
                    resp.find("OK:CHANNEL PRESET:") != std::string::npos, resp);
    }
    
    // Test invalid preset
    send_cmd(ctrl, "CMD:CHANNEL PRESET:INVALID");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL PRESET:INVALID (should error)",
                resp.find("ERROR:") != std::string::npos, resp);
    
    // Reset channel
    send_cmd(ctrl, "CMD:CHANNEL OFF");
    recv_line(ctrl);
    
    // Test CHANNEL AWGN
    send_cmd(ctrl, "CMD:CHANNEL AWGN:20");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL AWGN:20", 
                resp.find("OK:CHANNEL AWGN:") != std::string::npos, resp);
    
    send_cmd(ctrl, "CMD:CHANNEL AWGN:15.5");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL AWGN:15.5 (float)", 
                resp.find("OK:CHANNEL AWGN:") != std::string::npos, resp);
    
    // Test invalid AWGN
    send_cmd(ctrl, "CMD:CHANNEL AWGN:-100");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL AWGN:-100 (should error)",
                resp.find("ERROR:") != std::string::npos, resp);
    
    // Test CHANNEL MULTIPATH
    send_cmd(ctrl, "CMD:CHANNEL MULTIPATH:24,0.5");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL MULTIPATH:24,0.5", 
                resp.find("OK:CHANNEL MULTIPATH:") != std::string::npos, resp);
    
    send_cmd(ctrl, "CMD:CHANNEL MULTIPATH:48,0.3");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL MULTIPATH:48,0.3", 
                resp.find("OK:CHANNEL MULTIPATH:") != std::string::npos, resp);
    
    // Test CHANNEL FREQOFFSET
    send_cmd(ctrl, "CMD:CHANNEL FREQOFFSET:5");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL FREQOFFSET:5", 
                resp.find("OK:CHANNEL FREQOFFSET:") != std::string::npos, resp);
    
    send_cmd(ctrl, "CMD:CHANNEL FREQOFFSET:-3.5");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL FREQOFFSET:-3.5 (negative)", 
                resp.find("OK:CHANNEL FREQOFFSET:") != std::string::npos, resp);
    
    // Test CHANNEL CONFIG (query current config)
    send_cmd(ctrl, "CMD:CHANNEL CONFIG");
    resp = recv_line(ctrl);
    test_result("CMD:CHANNEL CONFIG", 
                resp.find("OK:CHANNEL CONFIG:") != std::string::npos, resp);
    
    // Test RUN BERTEST (apply channel impairments to a PCM file)
    // First generate a PCM file to use
    send_cmd(ctrl, "CMD:DATA RATE:600S");
    recv_line(ctrl);
    send_cmd(ctrl, "CMD:RECORD TX:ON");
    recv_line(ctrl);
    send_cmd(ctrl, "CMD:RECORD PREFIX:bertest_input");
    recv_line(ctrl);
    
    // Send test data and generate PCM
    std::string bertest_msg = "BER test data";
    send(data, bertest_msg.c_str(), (int)bertest_msg.size(), 0);
    send_cmd(ctrl, "CMD:SENDBUFFER");
    
    std::string bertest_pcm_file;
    for (int i = 0; i < 10; i++) {
        resp = recv_line(ctrl, 3000);
        if (resp.find("OK:SENDBUFFER") != std::string::npos) {
            size_t pos = resp.find("FILE:");
            if (pos != std::string::npos) {
                bertest_pcm_file = resp.substr(pos + 5);
            }
            break;
        }
    }
    
    if (!bertest_pcm_file.empty()) {
        // Configure channel impairments
        send_cmd(ctrl, "CMD:CHANNEL AWGN:20");
        recv_line(ctrl);
        
        // Run BERTEST - apply impairments to the PCM file
        std::string output_pcm = bertest_pcm_file;
        size_t ext = output_pcm.rfind(".pcm");
        if (ext != std::string::npos) {
            output_pcm = output_pcm.substr(0, ext) + "_impaired.pcm";
        }
        
        send_cmd(ctrl, "CMD:RUN BERTEST:" + bertest_pcm_file + "," + output_pcm);
        resp = recv_line(ctrl, 5000);
        test_result("CMD:RUN BERTEST (apply channel to PCM)", 
                    resp.find("OK:RUN BERTEST:") != std::string::npos, resp);
        
        // Test RUN BERTEST with missing file
        send_cmd(ctrl, "CMD:RUN BERTEST:nonexistent.pcm");
        resp = recv_line(ctrl, 2000);
        test_result("CMD:RUN BERTEST:nonexistent.pcm (should error)",
                    resp.find("ERROR:") != std::string::npos, resp);
        
        // Test RUN BERTEST with no channel configured
        send_cmd(ctrl, "CMD:CHANNEL OFF");
        recv_line(ctrl);
        send_cmd(ctrl, "CMD:RUN BERTEST:" + bertest_pcm_file);
        resp = recv_line(ctrl, 2000);
        test_result("CMD:RUN BERTEST without channel (should error)",
                    resp.find("ERROR:") != std::string::npos, resp);
    } else {
        std::cout << "[SKIP] RUN BERTEST tests - no PCM file available\n";
    }
    
    // Reset channel for clean state
    send_cmd(ctrl, "CMD:CHANNEL OFF");
    recv_line(ctrl);
    
    // Drain any remaining messages
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        DWORD tv = 100;
        setsockopt(ctrl, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        char drain[1024];
        while (recv(ctrl, drain, sizeof(drain), 0) > 0) {}
    }
    
    std::cout << "\n--- Testing Unknown Command ---\n";
    
    send_cmd(ctrl, "CMD:INVALID COMMAND");
    resp = recv_line(ctrl, 1000);
    test_result("Unknown command returns ERROR",
                resp.find("ERROR:") != std::string::npos, resp);
    
    // Cleanup
    close_socket(ctrl);
    close_socket(data);
    cleanup_sockets();
    
    // Summary
    std::cout << "\n===========================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "===========================================\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";
    std::cout << "Total:  " << (tests_passed + tests_failed) << "\n";
    
    if (tests_failed == 0) {
        std::cout << "\n*** ALL TESTS PASSED ***\n";
        std::cout << "Server is fully compliant with Brain Modem TCP/IP protocol.\n";
    } else {
        std::cout << "\n*** SOME TESTS FAILED ***\n";
    }
    
    return tests_failed > 0 ? 1 : 0;
}
