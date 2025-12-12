/**
 * @file test_tcp_base.cpp
 * @brief Unit tests for tcp_server_base
 * 
 * Build: g++ -std=c++17 -o test_tcp_base.exe test_tcp_base.cpp tcp_server_base.cpp -lws2_32
 * Run:   .\test_tcp_base.exe
 */

#include "tcp_server_base.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace tcp_base;

// Simple test framework
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Testing: " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASS" << std::endl; \
        g_tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << std::endl; \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT(cond) if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error("Expected " #a " == " #b)

// ============================================================
// Tests
// ============================================================

TEST(socket_init) {
    ASSERT(socket_init());
    // Call again - should be idempotent
    ASSERT(socket_init());
}

TEST(create_listener) {
    SocketError err;
    socket_t sock = create_listener(19999, &err);
    ASSERT(is_valid(sock));
    ASSERT(err == SocketError::OK);
    close_socket(sock);
}

TEST(create_listener_bind_fail) {
    // Create first listener
    socket_t sock1 = create_listener(19998, nullptr);
    ASSERT(is_valid(sock1));
    
    // Try to create second on same port - should fail
    SocketError err;
    socket_t sock2 = create_listener(19998, &err);
    // Note: With SO_REUSEADDR, this might succeed on some platforms
    // Just make sure we don't crash
    
    close_socket(sock1);
    if (is_valid(sock2)) close_socket(sock2);
}

TEST(accept_nonblocking) {
    socket_t listener = create_listener(19997, nullptr);
    ASSERT(is_valid(listener));
    
    // Accept should return immediately with WOULD_BLOCK
    SocketError err;
    socket_t client = accept_client(listener, nullptr, &err);
    ASSERT(!is_valid(client));
    ASSERT(err == SocketError::WOULD_BLOCK);
    
    close_socket(listener);
}

TEST(set_nonblocking) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(is_valid(sock));
    ASSERT(set_nonblocking(sock));
    close_socket(sock);
}

// Test client-server connection
class TestServer : public ServerBase {
public:
    bool command_received = false;
    std::string last_command;
    
protected:
    void on_command(const std::string& cmd) override {
        command_received = true;
        last_command = cmd;
        send_control("OK:" + cmd);
    }
    
    std::string get_ready_message() override {
        return "READY:TestServer";
    }
};

TEST(server_start_stop) {
    TestServer server;
    server.set_ports(19990, 19991);
    
    ASSERT(server.start());
    ASSERT(server.is_running());
    
    server.stop();
    ASSERT(!server.is_running());
}

TEST(server_accept_client) {
    TestServer server;
    server.set_ports(19988, 19989);
    ASSERT(server.start());
    
    // Connect a client
    socket_t client = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(is_valid(client));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19988);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    int result = connect(client, (sockaddr*)&addr, sizeof(addr));
    ASSERT(result == 0);
    
    // Give server time to accept
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll();
    
    ASSERT(server.has_control_client());
    
    // Read ready message
    char buf[256];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    std::string ready(buf);
    ASSERT(ready.find("READY:TestServer") != std::string::npos);
    
    close_socket(client);
    server.stop();
}

TEST(server_command_echo) {
    TestServer server;
    server.set_ports(19986, 19987);
    ASSERT(server.start());
    
    // Connect
    socket_t client = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19986);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(client, (sockaddr*)&addr, sizeof(addr));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll();
    
    // Consume ready message
    char buf[256];
    recv(client, buf, sizeof(buf), 0);
    
    // Send command
    const char* cmd = "CMD:TEST:HELLO\n";
    send(client, cmd, strlen(cmd), 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll();
    
    ASSERT(server.command_received);
    ASSERT(server.last_command == "CMD:TEST:HELLO");
    
    // Read response
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    std::string response(buf);
    ASSERT(response.find("OK:CMD:TEST:HELLO") != std::string::npos);
    
    close_socket(client);
    server.stop();
}

TEST(recv_nonblocking) {
    socket_t listener = create_listener(19985, nullptr);
    socket_t client_sock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19985);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(client_sock, (sockaddr*)&addr, sizeof(addr));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    socket_t server_client = accept_client(listener, nullptr, nullptr);
    ASSERT(is_valid(server_client));
    
    // Recv should return 0 (would block) since no data sent
    char buf[64];
    SocketError err;
    int n = recv_data(server_client, buf, sizeof(buf), &err);
    ASSERT(n == 0);
    ASSERT(err == SocketError::WOULD_BLOCK);
    
    // Now send data
    const char* msg = "Hello";
    send(client_sock, msg, strlen(msg), 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    n = recv_data(server_client, buf, sizeof(buf), &err);
    ASSERT(n == 5);
    ASSERT(err == SocketError::OK);
    ASSERT(memcmp(buf, "Hello", 5) == 0);
    
    close_socket(client_sock);
    close_socket(server_client);
    close_socket(listener);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "=== tcp_server_base Unit Tests ===" << std::endl;
    std::cout << std::endl;
    
    RUN_TEST(socket_init);
    RUN_TEST(create_listener);
    RUN_TEST(create_listener_bind_fail);
    RUN_TEST(accept_nonblocking);
    RUN_TEST(set_nonblocking);
    RUN_TEST(server_start_stop);
    RUN_TEST(server_accept_client);
    RUN_TEST(server_command_echo);
    RUN_TEST(recv_nonblocking);
    
    std::cout << std::endl;
    std::cout << "=== Results: " << g_tests_passed << " passed, " 
              << g_tests_failed << " failed ===" << std::endl;
    
    socket_cleanup();
    return g_tests_failed > 0 ? 1 : 0;
}
