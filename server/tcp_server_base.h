/**
 * @file tcp_server_base.h
 * @brief Robust TCP Server Base Layer
 * 
 * Platform-independent TCP socket handling with:
 * - Non-blocking I/O
 * - Connection management
 * - Error recovery
 * - Dual-port architecture (control + data)
 * 
 * Used by both Phoenix Nest and Brain Core servers.
 */

#ifndef TCP_SERVER_BASE_H
#define TCP_SERVER_BASE_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>

// Platform-specific socket types
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    #define SOCK_ERROR SOCKET_ERROR
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    #define INVALID_SOCK -1
    #define SOCK_ERROR -1
#endif

namespace tcp_base {

// ============================================================
// Error Codes
// ============================================================

enum class SocketError {
    OK = 0,
    WINSOCK_INIT_FAILED,
    SOCKET_CREATE_FAILED,
    BIND_FAILED,
    LISTEN_FAILED,
    ACCEPT_FAILED,
    SEND_FAILED,
    RECV_FAILED,
    CONNECTION_CLOSED,
    WOULD_BLOCK,
    TIMEOUT
};

/// Convert error to string
const char* error_to_string(SocketError err);

// ============================================================
// Socket Utilities
// ============================================================

/// Initialize platform sockets (call once at startup)
bool socket_init();

/// Cleanup platform sockets (call once at shutdown)
void socket_cleanup();

/// Create a non-blocking listener socket
/// @return socket handle or INVALID_SOCK on failure
socket_t create_listener(uint16_t port, SocketError* err = nullptr);

/// Accept a connection (non-blocking)
/// @return client socket or INVALID_SOCK if none pending
socket_t accept_client(socket_t listener, std::string* client_addr = nullptr, SocketError* err = nullptr);

/// Set socket to non-blocking mode
bool set_nonblocking(socket_t sock);

/// Close a socket safely
void close_socket(socket_t sock);

/// Check if socket is valid
inline bool is_valid(socket_t sock) { return sock != INVALID_SOCK; }

// ============================================================
// Non-blocking Send/Receive
// ============================================================

/// Send data (non-blocking, may partial send)
/// @return bytes sent, 0 if would block, -1 on error
int send_data(socket_t sock, const void* data, size_t len, SocketError* err = nullptr);

/// Send string with newline (for control port)
bool send_line(socket_t sock, const std::string& line);

/// Receive data (non-blocking)
/// @return bytes received, 0 if would block, -1 on error/closed
int recv_data(socket_t sock, void* buffer, size_t max_len, SocketError* err = nullptr);

/// Receive a line of text (blocking with timeout)
/// @return true if line received, false on timeout/error
bool recv_line(socket_t sock, std::string& line, int timeout_ms = 5000);

// ============================================================
// Dual-Port Server Base Class
// ============================================================

/**
 * @class ServerBase
 * @brief Abstract base for dual-port TCP servers
 * 
 * Manages two ports:
 * - Control port: ASCII line-based commands
 * - Data port: Binary data transfer
 * 
 * Derived classes implement command handling.
 */
class ServerBase {
public:
    ServerBase();
    virtual ~ServerBase();
    
    // Non-copyable
    ServerBase(const ServerBase&) = delete;
    ServerBase& operator=(const ServerBase&) = delete;
    
    /// Configure ports (call before start)
    void set_ports(uint16_t control_port, uint16_t data_port);
    
    /// Start server (opens listeners)
    bool start();
    
    /// Stop server (closes all sockets)
    void stop();
    
    /// Poll for activity (call in main loop)
    /// Accepts connections, receives data, calls handlers
    void poll();
    
    /// Check if running
    bool is_running() const { return running_.load(); }
    
    /// Check if clients connected
    bool has_control_client() const { return is_valid(control_client_); }
    bool has_data_client() const { return is_valid(data_client_); }
    
    /// Send to control client
    bool send_control(const std::string& line);
    
    /// Send to data client
    bool send_data(const std::vector<uint8_t>& data);
    bool send_data(const void* data, size_t len);
    
    /// Get received data from data port (clears internal buffer)
    std::vector<uint8_t> get_data_received();
    
protected:
    // Override in derived class
    virtual void on_control_connected() {}
    virtual void on_data_connected() {}
    virtual void on_control_disconnected() {}
    virtual void on_data_disconnected() {}
    virtual void on_command(const std::string& cmd) = 0;  // Pure virtual
    virtual void on_data_received(const std::vector<uint8_t>& data) {}
    
    /// Send ready message (override for custom greeting)
    virtual std::string get_ready_message() { return "READY"; }
    
private:
    void accept_pending();
    void read_control();
    void read_data();
    void disconnect_control();
    void disconnect_data();
    
    uint16_t control_port_ = 0;
    uint16_t data_port_ = 0;
    
    socket_t control_listen_ = INVALID_SOCK;
    socket_t data_listen_ = INVALID_SOCK;
    socket_t control_client_ = INVALID_SOCK;
    socket_t data_client_ = INVALID_SOCK;
    
    std::atomic<bool> running_{false};
    
    // Partial line buffer for control port
    std::string control_buffer_;
    
    // Data receive buffer
    std::vector<uint8_t> data_buffer_;
    std::mutex data_mutex_;
};

} // namespace tcp_base

#endif // TCP_SERVER_BASE_H
