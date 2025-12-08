/**
 * @file debug_loopback.cpp
 * @brief Debug version of loopback test with verbose output
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

void cleanup_sockets() {
    WSACleanup();
}

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

int main() {
    std::cout << "=== DEBUG LOOPBACK TEST ===\n\n";
    
    if (!init_sockets()) {
        std::cerr << "Failed to init sockets\n";
        return 1;
    }
    
    // Connect
    std::cout << "Connecting to control port...\n";
    socket_t ctrl = connect_to(HOST, CONTROL_PORT);
    if (ctrl == INVALID_SOCK) {
        std::cerr << "Failed to connect to control port\n";
        return 1;
    }
    std::cout << "  Connected.\n";
    
    std::cout << "Connecting to data port...\n";
    socket_t data = connect_to(HOST, DATA_PORT);
    if (data == INVALID_SOCK) {
        std::cerr << "Failed to connect to data port\n";
        close_socket(ctrl);
        return 1;
    }
    std::cout << "  Connected.\n";
    
    // Wait for ready
    std::string ready = recv_line(ctrl);
    std::cout << "Server: " << ready << "\n";
    
    // Set mode - test 300S specifically
    std::cout << "\n--- Setting mode 300S ---\n";
    send_cmd(ctrl, "CMD:DATA RATE:300S");
    std::string resp = recv_line(ctrl);
    std::cout << "Response: " << resp << "\n";
    
    // Enable recording
    send_cmd(ctrl, "CMD:RECORD TX:ON");
    resp = recv_line(ctrl);
    std::cout << "Record TX: " << resp << "\n";
    
    send_cmd(ctrl, "CMD:RECORD PREFIX:debug_test");
    resp = recv_line(ctrl);
    std::cout << "Record Prefix: " << resp << "\n";
    
    // Send test data
    std::string test_msg = "Hello, this is a debug test message 12345!";
    std::cout << "\nSending " << test_msg.size() << " bytes: \"" << test_msg << "\"\n";
    int sent = send(data, test_msg.c_str(), (int)test_msg.size(), 0);
    std::cout << "Sent " << sent << " bytes to data socket\n";
    
    // Trigger TX
    std::cout << "\n--- Triggering TX ---\n";
    send_cmd(ctrl, "CMD:SENDBUFFER");
    
    // Read all status messages
    std::string pcm_file;
    for (int i = 0; i < 10; i++) {
        resp = recv_line(ctrl, 3000);
        if (resp.empty()) break;
        std::cout << "  " << resp << "\n";
        
        // Extract PCM file
        size_t pos = resp.find("FILE:");
        if (pos != std::string::npos) {
            pcm_file = resp.substr(pos + 5);
            std::cout << "  --> Extracted PCM: " << pcm_file << "\n";
        }
        
        if (resp.find("OK:SENDBUFFER") != std::string::npos) {
            break;
        }
    }
    
    if (pcm_file.empty()) {
        std::cerr << "ERROR: No PCM file found!\n";
        close_socket(ctrl);
        close_socket(data);
        cleanup_sockets();
        return 1;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Inject for RX
    std::cout << "\n--- Injecting PCM for RX ---\n";
    send_cmd(ctrl, "CMD:RXAUDIOINJECT:" + pcm_file);
    
    // Read all RX status messages
    for (int i = 0; i < 15; i++) {
        resp = recv_line(ctrl, 3000);
        if (resp.empty()) break;
        std::cout << "  " << resp << "\n";
        
        if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
            break;
        }
    }
    
    // Read decoded data
    std::cout << "\n--- Reading decoded data ---\n";
    auto rx_data = recv_data(data, 3000);
    std::cout << "Received " << rx_data.size() << " bytes\n";
    
    if (!rx_data.empty()) {
        std::string rx_str(rx_data.begin(), rx_data.end());
        std::cout << "Data: \"" << rx_str << "\"\n";
        
        // Hex dump
        std::cout << "Hex: ";
        for (size_t i = 0; i < rx_data.size(); i++) {
            printf("%02X ", rx_data[i]);
            if ((i+1) % 16 == 0) std::cout << "\n     ";
        }
        std::cout << "\n";
        
        if (rx_str == test_msg) {
            std::cout << "\n*** MATCH! ***\n";
        } else {
            std::cout << "\n*** MISMATCH ***\n";
            std::cout << "Expected " << test_msg.size() << " bytes: \"" << test_msg << "\"\n";
            std::cout << "Got " << rx_data.size() << " bytes: \"" << rx_str.substr(0, test_msg.size()) << "...\"\n";
            
            // Show extra bytes
            if (rx_data.size() > test_msg.size()) {
                std::cout << "Extra bytes: ";
                for (size_t i = test_msg.size(); i < rx_data.size(); i++) {
                    printf("%02X ", rx_data[i]);
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cout << "ERROR: No data received!\n";
    }
    
    close_socket(ctrl);
    close_socket(data);
    cleanup_sockets();
    
    return 0;
}
