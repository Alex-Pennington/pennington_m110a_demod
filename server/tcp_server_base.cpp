/**
 * @file tcp_server_base.cpp
 * @brief Robust TCP Server Base Layer Implementation
 */

#include "tcp_server_base.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace tcp_base {

// ============================================================
// Error Handling
// ============================================================

const char* error_to_string(SocketError err) {
    switch (err) {
        case SocketError::OK: return "OK";
        case SocketError::WINSOCK_INIT_FAILED: return "Winsock initialization failed";
        case SocketError::SOCKET_CREATE_FAILED: return "Socket creation failed";
        case SocketError::BIND_FAILED: return "Bind failed";
        case SocketError::LISTEN_FAILED: return "Listen failed";
        case SocketError::ACCEPT_FAILED: return "Accept failed";
        case SocketError::SEND_FAILED: return "Send failed";
        case SocketError::RECV_FAILED: return "Receive failed";
        case SocketError::CONNECTION_CLOSED: return "Connection closed";
        case SocketError::WOULD_BLOCK: return "Would block";
        case SocketError::TIMEOUT: return "Timeout";
        default: return "Unknown error";
    }
}

// ============================================================
// Platform Socket Utilities
// ============================================================

static bool g_winsock_initialized = false;

bool socket_init() {
#ifdef _WIN32
    if (!g_winsock_initialized) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return false;
        }
        g_winsock_initialized = true;
    }
#endif
    return true;
}

void socket_cleanup() {
#ifdef _WIN32
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = false;
    }
#endif
}

socket_t create_listener(uint16_t port, SocketError* err) {
    if (err) *err = SocketError::OK;
    
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) {
        if (err) *err = SocketError::SOCKET_CREATE_FAILED;
        return INVALID_SOCK;
    }
    
    // Allow address reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCK_ERROR) {
        if (err) *err = SocketError::BIND_FAILED;
        close_socket(sock);
        return INVALID_SOCK;
    }
    
    // Listen
    if (listen(sock, 5) == SOCK_ERROR) {
        if (err) *err = SocketError::LISTEN_FAILED;
        close_socket(sock);
        return INVALID_SOCK;
    }
    
    // Set non-blocking
    if (!set_nonblocking(sock)) {
        close_socket(sock);
        return INVALID_SOCK;
    }
    
    return sock;
}

socket_t accept_client(socket_t listener, std::string* client_addr, SocketError* err) {
    if (err) *err = SocketError::OK;
    
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    
    socket_t client = accept(listener, (sockaddr*)&addr, &len);
    if (client == INVALID_SOCK) {
#ifdef _WIN32
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
        } else {
            if (err) *err = SocketError::ACCEPT_FAILED;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
        } else {
            if (err) *err = SocketError::ACCEPT_FAILED;
        }
#endif
        return INVALID_SOCK;
    }
    
    // Set client to non-blocking too
    set_nonblocking(client);
    
    if (client_addr) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
        *client_addr = ip_str;
    }
    
    return client;
}

bool set_nonblocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

void close_socket(socket_t sock) {
    if (sock == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// ============================================================
// Send/Receive
// ============================================================

int send_data(socket_t sock, const void* data, size_t len, SocketError* err) {
    if (err) *err = SocketError::OK;
    
#ifdef _WIN32
    int result = send(sock, (const char*)data, (int)len, 0);
    if (result == SOCK_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
            return 0;
        }
        if (err) *err = SocketError::SEND_FAILED;
        return -1;
    }
#else
    ssize_t result = ::send(sock, data, len, 0);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
            return 0;
        }
        if (err) *err = SocketError::SEND_FAILED;
        return -1;
    }
#endif
    return (int)result;
}

bool send_line(socket_t sock, const std::string& line) {
    std::string msg = line + "\n";
    const char* ptr = msg.c_str();
    size_t remaining = msg.size();
    
    while (remaining > 0) {
        int sent = send_data(sock, ptr, remaining, nullptr);
        if (sent < 0) return false;
        if (sent == 0) {
            // Would block - simple retry (could add select() here)
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
            continue;
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

int recv_data(socket_t sock, void* buffer, size_t max_len, SocketError* err) {
    if (err) *err = SocketError::OK;
    
#ifdef _WIN32
    int result = recv(sock, (char*)buffer, (int)max_len, 0);
    if (result == SOCK_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
            return 0;
        }
        if (err) *err = SocketError::RECV_FAILED;
        return -1;
    }
    if (result == 0) {
        if (err) *err = SocketError::CONNECTION_CLOSED;
        return -1;
    }
#else
    ssize_t result = ::recv(sock, buffer, max_len, 0);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (err) *err = SocketError::WOULD_BLOCK;
            return 0;
        }
        if (err) *err = SocketError::RECV_FAILED;
        return -1;
    }
    if (result == 0) {
        if (err) *err = SocketError::CONNECTION_CLOSED;
        return -1;
    }
#endif
    return (int)result;
}

bool recv_line(socket_t sock, std::string& line, int timeout_ms) {
    line.clear();
    char buf[256];
    int elapsed = 0;
    
    while (elapsed < timeout_ms) {
        SocketError err;
        int n = recv_data(sock, buf, sizeof(buf) - 1, &err);
        
        if (n > 0) {
            buf[n] = '\0';
            line += buf;
            
            // Check for newline
            size_t pos = line.find('\n');
            if (pos != std::string::npos) {
                line = line.substr(0, pos);
                // Trim \r if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return true;
            }
        } else if (n < 0) {
            return false;  // Error or connection closed
        } else {
            // Would block - wait a bit
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
            elapsed += 10;
        }
    }
    return false;  // Timeout
}

// ============================================================
// ServerBase Implementation
// ============================================================

ServerBase::ServerBase() {
    socket_init();
}

ServerBase::~ServerBase() {
    stop();
}

void ServerBase::set_ports(uint16_t control_port, uint16_t data_port) {
    control_port_ = control_port;
    data_port_ = data_port;
}

bool ServerBase::start() {
    if (running_.load()) return true;
    
    // Create control listener
    SocketError err;
    control_listen_ = create_listener(control_port_, &err);
    if (!is_valid(control_listen_)) {
        std::cerr << "[TCP] Failed to create control listener on port " 
                  << control_port_ << ": " << error_to_string(err) << std::endl;
        return false;
    }
    std::cout << "[TCP] Control port listening on " << control_port_ << std::endl;
    
    // Create data listener
    data_listen_ = create_listener(data_port_, &err);
    if (!is_valid(data_listen_)) {
        std::cerr << "[TCP] Failed to create data listener on port " 
                  << data_port_ << ": " << error_to_string(err) << std::endl;
        close_socket(control_listen_);
        control_listen_ = INVALID_SOCK;
        return false;
    }
    std::cout << "[TCP] Data port listening on " << data_port_ << std::endl;
    
    running_.store(true);
    return true;
}

void ServerBase::stop() {
    running_.store(false);
    
    disconnect_control();
    disconnect_data();
    
    if (is_valid(control_listen_)) {
        close_socket(control_listen_);
        control_listen_ = INVALID_SOCK;
    }
    if (is_valid(data_listen_)) {
        close_socket(data_listen_);
        data_listen_ = INVALID_SOCK;
    }
}

void ServerBase::poll() {
    if (!running_.load()) return;
    
    accept_pending();
    read_control();
    read_data();
}

void ServerBase::accept_pending() {
    // Accept control client
    if (!is_valid(control_client_) && is_valid(control_listen_)) {
        std::string addr;
        SocketError err;
        socket_t client = accept_client(control_listen_, &addr, &err);
        if (is_valid(client)) {
            control_client_ = client;
            control_buffer_.clear();
            std::cout << "[TCP] Control client connected from " << addr << std::endl;
            
            // Send ready message
            send_line(control_client_, get_ready_message());
            on_control_connected();
        }
    }
    
    // Accept data client
    if (!is_valid(data_client_) && is_valid(data_listen_)) {
        std::string addr;
        SocketError err;
        socket_t client = accept_client(data_listen_, &addr, &err);
        if (is_valid(client)) {
            data_client_ = client;
            std::cout << "[TCP] Data client connected from " << addr << std::endl;
            on_data_connected();
        }
    }
}

void ServerBase::read_control() {
    if (!is_valid(control_client_)) return;
    
    char buf[1024];
    SocketError err;
    int n = recv_data(control_client_, buf, sizeof(buf) - 1, &err);
    
    if (n > 0) {
        buf[n] = '\0';
        control_buffer_ += buf;
        
        // Process complete lines
        size_t pos;
        while ((pos = control_buffer_.find('\n')) != std::string::npos) {
            std::string line = control_buffer_.substr(0, pos);
            control_buffer_ = control_buffer_.substr(pos + 1);
            
            // Trim \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // Handle command
            if (!line.empty()) {
                on_command(line);
            }
        }
    } else if (n < 0) {
        // Connection closed or error
        std::cout << "[TCP] Control client disconnected" << std::endl;
        disconnect_control();
    }
}

void ServerBase::read_data() {
    if (!is_valid(data_client_)) return;
    
    uint8_t buf[4096];
    SocketError err;
    int n = recv_data(data_client_, buf, sizeof(buf), &err);
    
    if (n > 0) {
        std::vector<uint8_t> data(buf, buf + n);
        
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            data_buffer_.insert(data_buffer_.end(), data.begin(), data.end());
        }
        
        on_data_received(data);
    } else if (n < 0) {
        std::cout << "[TCP] Data client disconnected" << std::endl;
        disconnect_data();
    }
}

void ServerBase::disconnect_control() {
    if (is_valid(control_client_)) {
        close_socket(control_client_);
        control_client_ = INVALID_SOCK;
        control_buffer_.clear();
        on_control_disconnected();
    }
}

void ServerBase::disconnect_data() {
    if (is_valid(data_client_)) {
        close_socket(data_client_);
        data_client_ = INVALID_SOCK;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            data_buffer_.clear();
        }
        on_data_disconnected();
    }
}

bool ServerBase::send_control(const std::string& line) {
    if (!is_valid(control_client_)) return false;
    return send_line(control_client_, line);
}

bool ServerBase::send_data(const std::vector<uint8_t>& data) {
    return send_data(data.data(), data.size());
}

bool ServerBase::send_data(const void* data, size_t len) {
    if (!is_valid(data_client_)) return false;
    
    const uint8_t* ptr = (const uint8_t*)data;
    size_t remaining = len;
    
    while (remaining > 0) {
        int sent = tcp_base::send_data(data_client_, ptr, remaining, nullptr);
        if (sent < 0) return false;
        if (sent == 0) {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
            continue;
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

std::vector<uint8_t> ServerBase::get_data_received() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<uint8_t> result = std::move(data_buffer_);
    data_buffer_.clear();
    return result;
}

} // namespace tcp_base
