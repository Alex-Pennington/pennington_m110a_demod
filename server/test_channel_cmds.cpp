/**
 * @file test_channel_cmds.cpp
 * @brief Test the new channel simulation server commands
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>

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

socket_t connect_to_server(const char* host, int port) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return INVALID_SOCK;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close_socket(sock);
        return INVALID_SOCK;
    }
    return sock;
}

void send_cmd(socket_t sock, const std::string& cmd) {
    std::string full_cmd = cmd + "\n";
    send(sock, full_cmd.c_str(), full_cmd.length(), 0);
    std::cout << ">>> " << cmd << std::endl;
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    char buffer[4096] = {0};
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        std::string response(buffer);
        // Print each line with prefix
        size_t pos = 0;
        while (pos < response.length()) {
            size_t end = response.find('\n', pos);
            if (end == std::string::npos) end = response.length();
            std::cout << "<<< " << response.substr(pos, end - pos) << std::endl;
            pos = end + 1;
        }
    }
    std::cout << std::endl;
}

int main() {
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    std::cout << "==============================================\n";
    std::cout << "Channel Simulation Command Test\n";
    std::cout << "==============================================\n\n";

    socket_t sock = connect_to_server("127.0.0.1", 4999);
    if (sock == INVALID_SOCK) {
        std::cerr << "Cannot connect to server port 4999\n";
        return 1;
    }
    
    // Wait for MODEM READY
    char buffer[1024];
    recv(sock, buffer, sizeof(buffer), 0);
    std::cout << "Connected to server\n\n";
    
    // Test channel commands
    std::cout << "--- Test: Check Initial Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    std::cout << "--- Test: Enable AWGN ---\n";
    send_cmd(sock, "CMD:CHANNEL AWGN:20");
    
    std::cout << "--- Test: Check Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    std::cout << "--- Test: Enable Multipath ---\n";
    send_cmd(sock, "CMD:CHANNEL MULTIPATH:48,0.5");
    
    std::cout << "--- Test: Check Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    std::cout << "--- Test: Enable Freq Offset ---\n";
    send_cmd(sock, "CMD:CHANNEL FREQOFFSET:3.5");
    
    std::cout << "--- Test: Full Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    std::cout << "--- Test: Use Preset MODERATE ---\n";
    send_cmd(sock, "CMD:CHANNEL PRESET:MODERATE");
    
    std::cout << "--- Test: Check Preset Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    std::cout << "--- Test: Use Preset CCIR_POOR ---\n";
    send_cmd(sock, "CMD:CHANNEL PRESET:CCIR_POOR");
    
    std::cout << "--- Test: Disable Channel ---\n";
    send_cmd(sock, "CMD:CHANNEL OFF");
    
    std::cout << "--- Test: Final Config ---\n";
    send_cmd(sock, "CMD:CHANNEL CONFIG");
    
    close_socket(sock);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    std::cout << "==============================================\n";
    std::cout << "Channel Command Tests Complete\n";
    std::cout << "==============================================\n";
    
    return 0;
}
