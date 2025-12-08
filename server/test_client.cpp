/**
 * @file test_client.cpp
 * @brief Simple test client for MS-DMT compatible server
 * 
 * Tests the basic protocol flow:
 *   1. Connect to control and data ports
 *   2. Wait for MODEM READY
 *   3. Set data rate
 *   4. Send test data
 *   5. Trigger transmission
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    #define close_socket closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    #define INVALID_SOCK -1
    #define close_socket close
#endif

const char* HOST = "127.0.0.1";
const int DATA_PORT = 4998;
const int CONTROL_PORT = 4999;

bool init_sockets() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

socket_t connect_to(const char* host, int port) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return INVALID_SOCK;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(sock);
        return INVALID_SOCK;
    }
    
    return sock;
}

std::string recv_line(socket_t sock) {
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void send_command(socket_t sock, const std::string& cmd) {
    std::string msg = cmd + "\n";
    send(sock, msg.c_str(), static_cast<int>(msg.size()), 0);
    std::cout << ">>> " << cmd << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "==============================================\n";
    std::cout << "M110A Server Test Client\n";
    std::cout << "==============================================\n\n";
    
    if (!init_sockets()) {
        std::cerr << "Failed to initialize sockets\n";
        return 1;
    }
    
    // Connect to control port
    std::cout << "Connecting to control port " << CONTROL_PORT << "...\n";
    socket_t ctrl_sock = connect_to(HOST, CONTROL_PORT);
    if (ctrl_sock == INVALID_SOCK) {
        std::cerr << "Failed to connect to control port\n";
        cleanup_sockets();
        return 1;
    }
    std::cout << "Connected to control port\n";
    
    // Connect to data port
    std::cout << "Connecting to data port " << DATA_PORT << "...\n";
    socket_t data_sock = connect_to(HOST, DATA_PORT);
    if (data_sock == INVALID_SOCK) {
        std::cerr << "Failed to connect to data port\n";
        close_socket(ctrl_sock);
        cleanup_sockets();
        return 1;
    }
    std::cout << "Connected to data port\n\n";
    
    // Wait for MODEM READY
    std::cout << "Waiting for MODEM READY...\n";
    std::string response = recv_line(ctrl_sock);
    std::cout << "<<< " << response << "\n";
    
    if (response != "MODEM READY") {
        std::cerr << "Unexpected response: " << response << "\n";
    }
    
    std::cout << "\n--- Test 1: Set Data Rate ---\n";
    send_command(ctrl_sock, "CMD:DATA RATE:600S");
    response = recv_line(ctrl_sock);
    std::cout << "<<< " << response << "\n";
    
    std::cout << "\n--- Test 2: Enable Recording ---\n";
    send_command(ctrl_sock, "CMD:RECORD TX:ON");
    response = recv_line(ctrl_sock);
    std::cout << "<<< " << response << "\n";
    
    std::cout << "\n--- Test 3: Set Record Prefix ---\n";
    send_command(ctrl_sock, "CMD:RECORD PREFIX:test_client");
    response = recv_line(ctrl_sock);
    std::cout << "<<< " << response << "\n";
    
    std::cout << "\n--- Test 4: Send Data ---\n";
    const char* test_message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::cout << "Sending: \"" << test_message << "\"\n";
    send(data_sock, test_message, static_cast<int>(strlen(test_message)), 0);
    
    std::cout << "\n--- Test 5: Trigger Transmission ---\n";
    send_command(ctrl_sock, "CMD:SENDBUFFER");
    
    // Wait for status messages
    for (int i = 0; i < 3; i++) {
        response = recv_line(ctrl_sock);
        if (!response.empty()) {
            std::cout << "<<< " << response << "\n";
        }
    }
    
    std::cout << "\n--- Test 6: Try Different Modes ---\n";
    const char* modes[] = {"75S", "150S", "300S", "600S", "1200S", "2400S"};
    for (const char* mode : modes) {
        send_command(ctrl_sock, std::string("CMD:DATA RATE:") + mode);
        response = recv_line(ctrl_sock);
        std::cout << "<<< " << response << "\n";
    }
    
    std::cout << "\n--- Test 7: Invalid Command ---\n";
    send_command(ctrl_sock, "CMD:INVALID_COMMAND:TEST");
    response = recv_line(ctrl_sock);
    std::cout << "<<< " << response << "\n";
    
    std::cout << "\n==============================================\n";
    std::cout << "Test Complete\n";
    std::cout << "==============================================\n";
    
    // Cleanup
    close_socket(ctrl_sock);
    close_socket(data_sock);
    cleanup_sockets();
    
    return 0;
}
