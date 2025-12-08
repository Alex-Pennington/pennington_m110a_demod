/**
 * @file test_loopback.cpp
 * @brief External loopback test via MS-DMT server interface
 * 
 * Tests the full TX → PCM file → RX path through the server:
 *   1. Connect to server
 *   2. Send test data, trigger TX, record to PCM
 *   3. Inject PCM file for RX decode
 *   4. Compare decoded data to original
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>

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

std::string recv_line(socket_t sock, int timeout_ms = 5000) {
    // Set receive timeout
#ifdef _WIN32
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

std::vector<uint8_t> recv_data(socket_t sock, int timeout_ms = 5000) {
#ifdef _WIN32
    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::vector<uint8_t> data(4096);
    int received = recv(sock, reinterpret_cast<char*>(data.data()), 4096, 0);
    if (received > 0) {
        data.resize(received);
        return data;
    }
    return {};
}

void send_command(socket_t sock, const std::string& cmd) {
    std::string msg = cmd + "\n";
    send(sock, msg.c_str(), static_cast<int>(msg.size()), 0);
}

// Wait for a specific status message, return any extra info
std::string wait_for_status(socket_t sock, const std::string& expected, int timeout_ms = 10000) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::string line = recv_line(sock, 1000);
        if (line.find(expected) != std::string::npos) {
            return line;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            return "";
        }
    }
}

// Find the most recently created PCM file in the output directory
std::string find_latest_pcm(const std::string& prefix) {
    std::string dir = "./tx_pcm_out/";
    std::string pattern = prefix;
    
    // Simple approach: list files and find matching one
    // For Windows, we'll construct the expected filename pattern
#ifdef _WIN32
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((dir + prefix + "*.pcm").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return "";
    }
    
    std::string latest = ffd.cFileName;
    while (FindNextFileA(hFind, &ffd)) {
        std::string fname = ffd.cFileName;
        if (fname > latest) {
            latest = fname;
        }
    }
    FindClose(hFind);
    return dir + latest;
#else
    // For Linux, use glob or similar
    return "";
#endif
}

struct TestResult {
    std::string mode;
    std::string message;
    bool tx_success;
    bool rx_success;
    bool data_match;
    std::string pcm_file;
    std::string decoded;
};

TestResult run_loopback_test(socket_t ctrl_sock, socket_t data_sock, 
                             const std::string& mode, const std::string& test_message) {
    TestResult result;
    result.mode = mode;
    result.message = test_message;
    result.tx_success = false;
    result.rx_success = false;
    result.data_match = false;
    
    std::string prefix = "loopback_" + mode;
    
    // 1. Set data rate
    send_command(ctrl_sock, "CMD:DATA RATE:" + mode);
    std::string resp = recv_line(ctrl_sock);
    if (resp.find("OK:DATA RATE") == std::string::npos) {
        std::cerr << "Failed to set mode: " << resp << "\n";
        return result;
    }
    
    // 2. Enable recording with prefix
    send_command(ctrl_sock, "CMD:RECORD TX:ON");
    recv_line(ctrl_sock);
    
    send_command(ctrl_sock, "CMD:RECORD PREFIX:" + prefix);
    recv_line(ctrl_sock);
    
    // 3. Send test data
    send(data_sock, test_message.c_str(), static_cast<int>(test_message.size()), 0);
    
    // 4. Trigger TX
    send_command(ctrl_sock, "CMD:SENDBUFFER");
    
    // Wait for TX complete
    std::string status;
    bool got_transmit = false;
    bool got_idle = false;
    
    for (int i = 0; i < 10; i++) {
        status = recv_line(ctrl_sock, 2000);
        if (status.find("TX:TRANSMIT") != std::string::npos) got_transmit = true;
        if (status.find("TX:IDLE") != std::string::npos) got_idle = true;
        if (status.find("OK:SENDBUFFER") != std::string::npos) break;
    }
    
    if (!got_idle) {
        std::cerr << "TX did not complete\n";
        return result;
    }
    result.tx_success = true;
    
    // 5. Find the PCM file
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Let file be written
    result.pcm_file = find_latest_pcm(prefix);
    
    if (result.pcm_file.empty()) {
        std::cerr << "Could not find PCM file\n";
        return result;
    }
    
    // Get absolute path
    char abs_path[MAX_PATH];
    GetFullPathNameA(result.pcm_file.c_str(), MAX_PATH, abs_path, nullptr);
    result.pcm_file = abs_path;
    
    std::cout << "  TX complete, PCM: " << result.pcm_file << "\n";
    
    // 6. Inject PCM for RX
    send_command(ctrl_sock, "CMD:RXAUDIOINJECT:" + result.pcm_file);
    
    // Wait for RX status messages
    bool got_rx_mode = false;
    bool got_no_dcd = false;
    
    for (int i = 0; i < 20; i++) {
        status = recv_line(ctrl_sock, 2000);
        if (status.empty()) continue;
        
        if (status.find("STATUS:RX:") != std::string::npos && 
            status.find("NO DCD") == std::string::npos) {
            got_rx_mode = true;
        }
        if (status.find("NO DCD") != std::string::npos) {
            got_no_dcd = true;
        }
        if (status.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
            break;
        }
    }
    
    // 7. Read decoded data from data port
    auto decoded_data = recv_data(data_sock, 2000);
    result.decoded = std::string(decoded_data.begin(), decoded_data.end());
    
    if (!decoded_data.empty()) {
        result.rx_success = true;
    }
    
    // 8. Compare
    result.data_match = (result.decoded == test_message);
    
    return result;
}

int main(int argc, char* argv[]) {
    std::cout << "==============================================\n";
    std::cout << "M110A External Loopback Test\n";
    std::cout << "TX -> PCM File -> RX via Server Interface\n";
    std::cout << "==============================================\n\n";
    
    if (!init_sockets()) {
        std::cerr << "Failed to initialize sockets\n";
        return 1;
    }
    
    // Connect to server
    std::cout << "Connecting to server...\n";
    socket_t ctrl_sock = connect_to(HOST, CONTROL_PORT);
    if (ctrl_sock == INVALID_SOCK) {
        std::cerr << "Failed to connect to control port. Is server running?\n";
        cleanup_sockets();
        return 1;
    }
    
    socket_t data_sock = connect_to(HOST, DATA_PORT);
    if (data_sock == INVALID_SOCK) {
        std::cerr << "Failed to connect to data port\n";
        close_socket(ctrl_sock);
        cleanup_sockets();
        return 1;
    }
    
    // Wait for MODEM READY
    std::string ready = recv_line(ctrl_sock);
    if (ready.find("MODEM READY") == std::string::npos) {
        std::cerr << "Did not receive MODEM READY\n";
        close_socket(ctrl_sock);
        close_socket(data_sock);
        cleanup_sockets();
        return 1;
    }
    std::cout << "Connected and ready.\n\n";
    
    // Test message
    const std::string test_message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Test modes (start with faster ones)
    std::vector<std::string> modes = {"2400S", "1200S", "600S"};
    
    std::vector<TestResult> results;
    
    for (const auto& mode : modes) {
        std::cout << "--- Testing " << mode << " ---\n";
        
        // Need fresh data socket connection for each test
        close_socket(data_sock);
        data_sock = connect_to(HOST, DATA_PORT);
        if (data_sock == INVALID_SOCK) {
            std::cerr << "Failed to reconnect data socket\n";
            continue;
        }
        
        auto result = run_loopback_test(ctrl_sock, data_sock, mode, test_message);
        results.push_back(result);
        
        std::cout << "  TX: " << (result.tx_success ? "OK" : "FAIL") << "\n";
        std::cout << "  RX: " << (result.rx_success ? "OK" : "FAIL");
        if (result.rx_success) {
            std::cout << " (decoded " << result.decoded.size() << " bytes)";
        }
        std::cout << "\n";
        std::cout << "  Match: " << (result.data_match ? "YES" : "NO") << "\n";
        
        if (!result.decoded.empty() && !result.data_match) {
            std::cout << "  Expected: \"" << test_message << "\"\n";
            std::cout << "  Got:      \"" << result.decoded << "\"\n";
        }
        std::cout << "\n";
    }
    
    // Summary
    std::cout << "==============================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "==============================================\n";
    std::cout << std::left;
    std::cout << "Mode      TX    RX    Match\n";
    std::cout << "-------------------------------\n";
    
    int passed = 0;
    for (const auto& r : results) {
        std::cout << std::setw(10) << r.mode
                  << std::setw(6) << (r.tx_success ? "OK" : "FAIL")
                  << std::setw(6) << (r.rx_success ? "OK" : "FAIL")
                  << (r.data_match ? "YES" : "NO") << "\n";
        if (r.data_match) passed++;
    }
    
    std::cout << "-------------------------------\n";
    std::cout << "Passed: " << passed << "/" << results.size() << "\n";
    
    // Cleanup
    close_socket(ctrl_sock);
    close_socket(data_sock);
    cleanup_sockets();
    
    return (passed == results.size()) ? 0 : 1;
}
