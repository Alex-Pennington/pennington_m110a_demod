/**
 * @file msdmt_server.cpp
 * @brief MS-DMT Compatible Network Interface Server Implementation
 */

#include "msdmt_server.h"
#include "api/modem.h"
#include "api/channel_sim.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>

// Platform-specific socket includes
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    #define SOCK_ERROR SOCKET_ERROR
    #define close_socket closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    #define INVALID_SOCK -1
    #define SOCK_ERROR -1
    #define close_socket close
#endif

namespace m110a {
namespace server {

// ============================================================
// Socket Implementation Structure
// ============================================================

struct MSDMTServer::SocketImpl {
    socket_t data_listen_socket = INVALID_SOCK;
    socket_t control_listen_socket = INVALID_SOCK;
    socket_t discovery_socket = INVALID_SOCK;
    
    bool winsock_initialized = false;
    
    SocketImpl() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
            winsock_initialized = true;
        }
#endif
    }
    
    ~SocketImpl() {
        if (data_listen_socket != INVALID_SOCK) close_socket(data_listen_socket);
        if (control_listen_socket != INVALID_SOCK) close_socket(control_listen_socket);
        if (discovery_socket != INVALID_SOCK) close_socket(discovery_socket);
#ifdef _WIN32
        if (winsock_initialized) WSACleanup();
#endif
    }
};

// ============================================================
// Mode Parsing Utilities
// ============================================================

DataRateMode parse_data_rate_mode(const std::string& mode_str) {
    std::string mode = mode_str;
    // Trim and uppercase
    mode.erase(0, mode.find_first_not_of(" \t\n\r"));
    mode.erase(mode.find_last_not_of(" \t\n\r") + 1);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::toupper);
    
    if (mode == "75S")   return DataRateMode::M75_SHORT;
    if (mode == "75L")   return DataRateMode::M75_LONG;
    if (mode == "150S")  return DataRateMode::M150_SHORT;
    if (mode == "150L")  return DataRateMode::M150_LONG;
    if (mode == "300S")  return DataRateMode::M300_SHORT;
    if (mode == "300L")  return DataRateMode::M300_LONG;
    if (mode == "600S")  return DataRateMode::M600_SHORT;
    if (mode == "600L")  return DataRateMode::M600_LONG;
    if (mode == "1200S") return DataRateMode::M1200_SHORT;
    if (mode == "1200L") return DataRateMode::M1200_LONG;
    if (mode == "2400S") return DataRateMode::M2400_SHORT;
    if (mode == "2400L") return DataRateMode::M2400_LONG;
    
    return DataRateMode::UNKNOWN;
}

std::string data_rate_mode_to_string(DataRateMode mode) {
    switch (mode) {
        case DataRateMode::M75_SHORT:   return "75S";
        case DataRateMode::M75_LONG:    return "75L";
        case DataRateMode::M150_SHORT:  return "150S";
        case DataRateMode::M150_LONG:   return "150L";
        case DataRateMode::M300_SHORT:  return "300S";
        case DataRateMode::M300_LONG:   return "300L";
        case DataRateMode::M600_SHORT:  return "600S";
        case DataRateMode::M600_LONG:   return "600L";
        case DataRateMode::M1200_SHORT: return "1200S";
        case DataRateMode::M1200_LONG:  return "1200L";
        case DataRateMode::M2400_SHORT: return "2400S";
        case DataRateMode::M2400_LONG:  return "2400L";
        default:                        return "UNKNOWN";
    }
}

std::string data_rate_mode_to_status_string(DataRateMode mode) {
    switch (mode) {
        case DataRateMode::M75_SHORT:   return "75 BPS SHORT";
        case DataRateMode::M75_LONG:    return "75 BPS LONG";
        case DataRateMode::M150_SHORT:  return "150 BPS SHORT";
        case DataRateMode::M150_LONG:   return "150 BPS LONG";
        case DataRateMode::M300_SHORT:  return "300 BPS SHORT";
        case DataRateMode::M300_LONG:   return "300 BPS LONG";
        case DataRateMode::M600_SHORT:  return "600 BPS SHORT";
        case DataRateMode::M600_LONG:   return "600 BPS LONG";
        case DataRateMode::M1200_SHORT: return "1200 BPS SHORT";
        case DataRateMode::M1200_LONG:  return "1200 BPS LONG";
        case DataRateMode::M2400_SHORT: return "2400 BPS SHORT";
        case DataRateMode::M2400_LONG:  return "2400 BPS LONG";
        default:                        return "UNKNOWN";
    }
}

/// Convert DataRateMode to m110a::api::Mode
static m110a::api::Mode to_api_mode(DataRateMode mode) {
    switch (mode) {
        case DataRateMode::M75_SHORT:   return m110a::api::Mode::M75_SHORT;
        case DataRateMode::M75_LONG:    return m110a::api::Mode::M75_LONG;
        case DataRateMode::M150_SHORT:  return m110a::api::Mode::M150_SHORT;
        case DataRateMode::M150_LONG:   return m110a::api::Mode::M150_LONG;
        case DataRateMode::M300_SHORT:  return m110a::api::Mode::M300_SHORT;
        case DataRateMode::M300_LONG:   return m110a::api::Mode::M300_LONG;
        case DataRateMode::M600_SHORT:  return m110a::api::Mode::M600_SHORT;
        case DataRateMode::M600_LONG:   return m110a::api::Mode::M600_LONG;
        case DataRateMode::M1200_SHORT: return m110a::api::Mode::M1200_SHORT;
        case DataRateMode::M1200_LONG:  return m110a::api::Mode::M1200_LONG;
        case DataRateMode::M2400_SHORT: return m110a::api::Mode::M2400_SHORT;
        case DataRateMode::M2400_LONG:  return m110a::api::Mode::M2400_LONG;
        default:                        return m110a::api::Mode::M2400_SHORT;
    }
}

// ============================================================
// Command Parsing
// ============================================================

Command parse_command(const std::string& cmd_str) {
    Command cmd;
    cmd.raw = cmd_str;
    
    // Trim whitespace and newlines
    std::string str = cmd_str;
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    str.erase(str.find_last_not_of(" \t\n\r") + 1);
    
    // Must start with "CMD:"
    if (str.substr(0, 4) != "CMD:") {
        return cmd;
    }
    
    str = str.substr(4); // Remove "CMD:" prefix
    
    // Find first colon for parameter separation
    size_t colon_pos = str.find(':');
    if (colon_pos != std::string::npos) {
        cmd.type = str.substr(0, colon_pos);
        cmd.parameter = str.substr(colon_pos + 1);
    } else {
        cmd.type = str;
        cmd.parameter = "";
    }
    
    // Uppercase the command type
    std::transform(cmd.type.begin(), cmd.type.end(), cmd.type.begin(), ::toupper);
    
    cmd.valid = true;
    return cmd;
}

// ============================================================
// Status Message Formatting
// ============================================================

std::string format_status(StatusCategory category, const std::string& details) {
    std::string cat_str;
    switch (category) {
        case StatusCategory::TX:    cat_str = "TX"; break;
        case StatusCategory::RX:    cat_str = "RX"; break;
        case StatusCategory::MODEM: cat_str = "MODEM"; break;
        case StatusCategory::ERR:   cat_str = "ERROR"; break;
    }
    return "STATUS:" + cat_str + ":" + details + "\n";
}

std::string format_ok(const std::string& command, const std::string& details) {
    if (details.empty()) {
        return "OK:" + command + "\n";
    }
    return "OK:" + command + ":" + details + "\n";
}

std::string format_error(const std::string& command, const std::string& details) {
    return "ERROR:" + command + ":" + details + "\n";
}

// ============================================================
// Client Connection Implementation
// ============================================================

bool ClientConnection::send(const std::vector<uint8_t>& data) {
    if (!connected || socket_fd < 0) return false;
    
    int result = ::send(socket_fd, reinterpret_cast<const char*>(data.data()), 
                        static_cast<int>(data.size()), 0);
    return result != SOCK_ERROR;
}

bool ClientConnection::send(const std::string& text) {
    std::vector<uint8_t> data(text.begin(), text.end());
    return send(data);
}

std::vector<uint8_t> ClientConnection::receive(size_t max_bytes) {
    std::vector<uint8_t> buffer(max_bytes);
    int received = ::recv(socket_fd, reinterpret_cast<char*>(buffer.data()), 
                          static_cast<int>(max_bytes), 0);
    if (received > 0) {
        buffer.resize(received);
        return buffer;
    }
    return {};
}

std::string ClientConnection::receive_line() {
    std::string line;
    char c;
    while (::recv(socket_fd, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

void ClientConnection::close() {
    if (socket_fd >= 0) {
        close_socket(socket_fd);
        socket_fd = -1;
    }
    connected = false;
}

// ============================================================
// MSDMTServer Implementation
// ============================================================

MSDMTServer::MSDMTServer() 
    : sockets_(std::make_unique<SocketImpl>()) {
}

MSDMTServer::~MSDMTServer() {
    stop();
}

void MSDMTServer::configure(const ServerConfig& config) {
    config_ = config;
}

bool MSDMTServer::start() {
    if (running_.load()) {
        return true; // Already running
    }
    
#ifdef _WIN32
    if (!sockets_->winsock_initialized) {
        std::cerr << "Failed to initialize Winsock\n";
        return false;
    }
#endif
    
    // Create data listen socket
    sockets_->data_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets_->data_listen_socket == INVALID_SOCK) {
        std::cerr << "Failed to create data listen socket\n";
        return false;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(sockets_->data_listen_socket, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    // Bind data socket
    sockaddr_in data_addr{};
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = htons(config_.data_port);
    
    if (bind(sockets_->data_listen_socket, 
             reinterpret_cast<sockaddr*>(&data_addr), sizeof(data_addr)) == SOCK_ERROR) {
        std::cerr << "Failed to bind data port " << config_.data_port << "\n";
        return false;
    }
    
    if (listen(sockets_->data_listen_socket, 5) == SOCK_ERROR) {
        std::cerr << "Failed to listen on data port\n";
        return false;
    }
    
    // Create control listen socket
    sockets_->control_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets_->control_listen_socket == INVALID_SOCK) {
        std::cerr << "Failed to create control listen socket\n";
        return false;
    }
    
    setsockopt(sockets_->control_listen_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    // Bind control socket
    sockaddr_in control_addr{};
    control_addr.sin_family = AF_INET;
    control_addr.sin_addr.s_addr = INADDR_ANY;
    control_addr.sin_port = htons(config_.control_port);
    
    if (bind(sockets_->control_listen_socket,
             reinterpret_cast<sockaddr*>(&control_addr), sizeof(control_addr)) == SOCK_ERROR) {
        std::cerr << "Failed to bind control port " << config_.control_port << "\n";
        return false;
    }
    
    if (listen(sockets_->control_listen_socket, 5) == SOCK_ERROR) {
        std::cerr << "Failed to listen on control port\n";
        return false;
    }
    
    // Create discovery socket (UDP)
    if (config_.enable_discovery) {
        sockets_->discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets_->discovery_socket != INVALID_SOCK) {
            int broadcast = 1;
            setsockopt(sockets_->discovery_socket, SOL_SOCKET, SO_BROADCAST,
                       reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
        }
    }
    
    running_.store(true);
    
    // Start threads
    data_accept_thread_ = std::thread(&MSDMTServer::run_data_accept_loop, this);
    control_accept_thread_ = std::thread(&MSDMTServer::run_control_accept_loop, this);
    
    if (config_.enable_discovery) {
        discovery_thread_ = std::thread(&MSDMTServer::run_discovery_loop, this);
    }
    
    processing_thread_ = std::thread(&MSDMTServer::run_processing_loop, this);
    
    std::cout << "MS-DMT Server started\n";
    std::cout << "  Data port:     " << config_.data_port << "\n";
    std::cout << "  Control port:  " << config_.control_port << "\n";
    if (config_.enable_discovery) {
        std::cout << "  Discovery:     UDP " << config_.discovery_port << "\n";
    }
    
    return true;
}

void MSDMTServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Close listening sockets to unblock accept()
    if (sockets_->data_listen_socket != INVALID_SOCK) {
        close_socket(sockets_->data_listen_socket);
        sockets_->data_listen_socket = INVALID_SOCK;
    }
    if (sockets_->control_listen_socket != INVALID_SOCK) {
        close_socket(sockets_->control_listen_socket);
        sockets_->control_listen_socket = INVALID_SOCK;
    }
    if (sockets_->discovery_socket != INVALID_SOCK) {
        close_socket(sockets_->discovery_socket);
        sockets_->discovery_socket = INVALID_SOCK;
    }
    
    // Wait for threads
    if (data_accept_thread_.joinable()) data_accept_thread_.join();
    if (control_accept_thread_.joinable()) control_accept_thread_.join();
    if (discovery_thread_.joinable()) discovery_thread_.join();
    if (processing_thread_.joinable()) processing_thread_.join();
    
    // Close client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : data_clients_) {
            client->close();
        }
        for (auto& client : control_clients_) {
            client->close();
        }
        data_clients_.clear();
        control_clients_.clear();
    }
    
    std::cout << "MS-DMT Server stopped\n";
}

size_t MSDMTServer::tx_buffer_size() const {
    std::lock_guard<std::mutex> lock(tx_buffer_mutex_);
    return tx_buffer_.size();
}

void MSDMTServer::clear_tx_buffer() {
    std::lock_guard<std::mutex> lock(tx_buffer_mutex_);
    tx_buffer_.clear();
}

void MSDMTServer::broadcast_status(StatusCategory category, const std::string& details) {
    std::string msg = format_status(category, details);
    
    if (config_.log_status) {
        std::cout << "[STATUS] " << msg;
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : control_clients_) {
        if (client->connected) {
            client->send(msg);
        }
    }
}

void MSDMTServer::broadcast_data(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : data_clients_) {
        if (client->connected) {
            client->send(data);
        }
    }
}

void MSDMTServer::on_data_received(OnDataReceivedCallback callback) {
    on_data_received_ = callback;
}

void MSDMTServer::on_command_received(OnCommandReceivedCallback callback) {
    on_command_received_ = callback;
}

void MSDMTServer::on_client_connected(OnClientConnectedCallback callback) {
    on_client_connected_ = callback;
}

void MSDMTServer::on_client_disconnected(OnClientDisconnectedCallback callback) {
    on_client_disconnected_ = callback;
}

// ============================================================
// Thread Loop Implementations
// ============================================================

void MSDMTServer::run_data_accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        socket_t client_socket = accept(sockets_->data_listen_socket,
                                        reinterpret_cast<sockaddr*>(&client_addr),
                                        &addr_len);
        
        if (client_socket == INVALID_SOCK) {
            if (running_.load()) {
                std::cerr << "Data accept failed\n";
            }
            continue;
        }
        
        auto client = std::make_shared<ClientConnection>();
        client->socket_fd = static_cast<int>(client_socket);
        client->address = inet_ntoa(client_addr.sin_addr);
        client->port = ntohs(client_addr.sin_port);
        client->connected = true;
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            data_clients_.push_back(client);
        }
        
        if (on_client_connected_) {
            on_client_connected_(client->address, client->port);
        }
        
        std::cout << "[DATA] Client connected: " << client->address << ":" << client->port << "\n";
        
        // Handle client in separate thread
        std::thread(&MSDMTServer::handle_data_client, this, client).detach();
    }
}

void MSDMTServer::run_control_accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        socket_t client_socket = accept(sockets_->control_listen_socket,
                                        reinterpret_cast<sockaddr*>(&client_addr),
                                        &addr_len);
        
        if (client_socket == INVALID_SOCK) {
            if (running_.load()) {
                std::cerr << "Control accept failed\n";
            }
            continue;
        }
        
        auto client = std::make_shared<ClientConnection>();
        client->socket_fd = static_cast<int>(client_socket);
        client->address = inet_ntoa(client_addr.sin_addr);
        client->port = ntohs(client_addr.sin_port);
        client->connected = true;
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            control_clients_.push_back(client);
        }
        
        if (on_client_connected_) {
            on_client_connected_(client->address, client->port);
        }
        
        std::cout << "[CTRL] Client connected: " << client->address << ":" << client->port << "\n";
        
        // Send MODEM READY
        client->send("MODEM READY\n");
        
        // Handle client in separate thread
        std::thread(&MSDMTServer::handle_control_client, this, client).detach();
    }
}

void MSDMTServer::run_discovery_loop() {
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcast_addr.sin_port = htons(config_.discovery_port);
    
    const char* helo = "helo";
    
    while (running_.load()) {
        sendto(sockets_->discovery_socket, helo, 4, 0,
               reinterpret_cast<sockaddr*>(&broadcast_addr), sizeof(broadcast_addr));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.discovery_interval_ms));
    }
}

void MSDMTServer::run_processing_loop() {
    while (running_.load()) {
        // Process any pending RX data
        {
            std::lock_guard<std::mutex> lock(rx_data_mutex_);
            while (!rx_data_queue_.empty()) {
                auto data = rx_data_queue_.front();
                rx_data_queue_.pop();
                broadcast_data(data);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void MSDMTServer::handle_data_client(std::shared_ptr<ClientConnection> client) {
    while (running_.load() && client->connected) {
        auto data = client->receive(4096);
        if (data.empty()) {
            break; // Client disconnected
        }
        
        // Add to TX buffer
        {
            std::lock_guard<std::mutex> lock(tx_buffer_mutex_);
            tx_buffer_.insert(tx_buffer_.end(), data.begin(), data.end());
        }
        
        if (on_data_received_) {
            on_data_received_(data);
        }
    }
    
    client->close();
    
    if (on_client_disconnected_) {
        on_client_disconnected_(client->address, client->port);
    }
    
    std::cout << "[DATA] Client disconnected: " << client->address << ":" << client->port << "\n";
    
    // Remove from client list
    std::lock_guard<std::mutex> lock(clients_mutex_);
    data_clients_.erase(
        std::remove(data_clients_.begin(), data_clients_.end(), client),
        data_clients_.end());
}

void MSDMTServer::handle_control_client(std::shared_ptr<ClientConnection> client) {
    while (running_.load() && client->connected) {
        std::string line = client->receive_line();
        if (line.empty()) {
            // Check if still connected
            auto check = client->receive(1);
            if (check.empty()) {
                break; // Client disconnected
            }
            continue;
        }
        
        if (config_.log_commands) {
            std::cout << "[CMD] " << line << "\n";
        }
        
        Command cmd = parse_command(line);
        if (cmd.valid) {
            if (on_command_received_) {
                on_command_received_(cmd);
            }
            process_command(client, cmd);
        }
    }
    
    client->close();
    
    if (on_client_disconnected_) {
        on_client_disconnected_(client->address, client->port);
    }
    
    std::cout << "[CTRL] Client disconnected: " << client->address << ":" << client->port << "\n";
    
    // Remove from client list
    std::lock_guard<std::mutex> lock(clients_mutex_);
    control_clients_.erase(
        std::remove(control_clients_.begin(), control_clients_.end(), client),
        control_clients_.end());
}

void MSDMTServer::process_command(std::shared_ptr<ClientConnection> client, const Command& cmd) {
    if (cmd.type == "DATA RATE") {
        cmd_data_rate(client, cmd.parameter);
    } else if (cmd.type == "SENDBUFFER") {
        cmd_send_buffer(client);
    } else if (cmd.type == "RECORD TX") {
        bool enable = (cmd.parameter == "ON" || cmd.parameter == "on");
        cmd_record_tx(client, enable);
    } else if (cmd.type == "RECORD PREFIX") {
        cmd_record_prefix(client, cmd.parameter);
    } else if (cmd.type == "RXAUDIOINJECT") {
        cmd_rx_audio_inject(client, cmd.parameter);
    } else if (cmd.type == "KILL TX") {
        cmd_kill_tx(client);
    } else if (cmd.type == "CHANNEL CONFIG") {
        cmd_channel_config(client, cmd.parameter);
    } else if (cmd.type == "CHANNEL PRESET") {
        cmd_channel_preset(client, cmd.parameter);
    } else if (cmd.type == "CHANNEL AWGN") {
        cmd_channel_awgn(client, cmd.parameter);
    } else if (cmd.type == "CHANNEL MULTIPATH") {
        cmd_channel_multipath(client, cmd.parameter);
    } else if (cmd.type == "CHANNEL FREQOFFSET") {
        cmd_channel_freq_offset(client, cmd.parameter);
    } else if (cmd.type == "CHANNEL OFF") {
        cmd_channel_off(client);
    } else if (cmd.type == "CHANNEL APPLY" || cmd.type == "RUN BERTEST") {
        cmd_run_ber_test(client, cmd.parameter);
    } else {
        client->send(format_error(cmd.type, "UNKNOWN COMMAND"));
    }
}

// ============================================================
// Command Handlers
// ============================================================

void MSDMTServer::cmd_data_rate(std::shared_ptr<ClientConnection> client, const std::string& param) {
    DataRateMode mode = parse_data_rate_mode(param);
    if (mode == DataRateMode::UNKNOWN) {
        client->send(format_error("DATA RATE", "INVALID MODE: " + param));
        return;
    }
    
    current_mode_ = mode;
    client->send(format_ok("DATA RATE", data_rate_mode_to_string(mode)));
}

void MSDMTServer::cmd_send_buffer(std::shared_ptr<ClientConnection> client) {
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(tx_buffer_mutex_);
        data = std::move(tx_buffer_);
        tx_buffer_.clear();
    }
    
    if (data.empty()) {
        client->send(format_ok("SENDBUFFER", "EMPTY"));
        return;
    }
    
    state_.store(ModemState::TRANSMITTING);
    broadcast_status(StatusCategory::TX, "TRANSMIT");
    
    // Encode using modem API
    auto api_mode = to_api_mode(current_mode_);
    auto encode_result = m110a::api::encode(data, api_mode, 48000.0f);
    
    if (!encode_result.ok()) {
        state_.store(ModemState::IDLE);
        broadcast_status(StatusCategory::TX, "IDLE");
        client->send(format_error("SENDBUFFER", "ENCODE FAILED"));
        return;
    }
    
    auto samples = encode_result.value();
    
    // Save to file if recording enabled
    if (recording_enabled_) {
        std::string filename = generate_pcm_filename(recording_prefix_, config_.pcm_output_dir);
        auto int16_samples = samples_to_int16(samples);
        write_pcm_file(filename, int16_samples);
        std::cout << "[TX] Saved: " << filename << " (" << int16_samples.size() << " samples)\n";
    }
    
    state_.store(ModemState::IDLE);
    broadcast_status(StatusCategory::TX, "IDLE");
    
    client->send(format_ok("SENDBUFFER", std::to_string(data.size()) + " bytes"));
}

void MSDMTServer::cmd_record_tx(std::shared_ptr<ClientConnection> client, bool enable) {
    recording_enabled_ = enable;
    client->send(format_ok("RECORD TX", enable ? "ON" : "OFF"));
}

void MSDMTServer::cmd_record_prefix(std::shared_ptr<ClientConnection> client, const std::string& prefix) {
    recording_prefix_ = prefix;
    client->send(format_ok("RECORD PREFIX", prefix));
}

void MSDMTServer::cmd_rx_audio_inject(std::shared_ptr<ClientConnection> client, const std::string& filepath) {
    // Check if file exists
    std::ifstream test(filepath, std::ios::binary);
    if (!test.good()) {
        client->send(format_error("RXAUDIOINJECT", "FILE NOT FOUND:" + filepath));
        return;
    }
    test.close();
    
    client->send(format_ok("RXAUDIOINJECT", "STARTED:" + filepath));
    
    state_.store(ModemState::INJECTING);
    
    // Read PCM file
    auto int16_samples = read_pcm_file(filepath);
    auto samples = samples_to_float(int16_samples);
    
    // Apply channel impairments if enabled
    if (channel_sim_enabled_) {
        std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        
        if (channel_freq_offset_enabled_) {
            m110a::api::channel::add_freq_offset(samples, channel_freq_offset_hz_, 48000.0f);
        }
        if (channel_multipath_enabled_) {
            m110a::api::channel::add_multipath(samples, channel_multipath_delay_, channel_multipath_gain_);
        }
        if (channel_awgn_enabled_) {
            m110a::api::channel::add_awgn(samples, channel_snr_db_, rng);
        }
    }
    
    // Decode using modem API
    m110a::api::RxConfig rx_cfg;
    rx_cfg.sample_rate = 48000;
    
    auto result = m110a::api::decode(samples, rx_cfg);
    
    if (result.success && !result.data.empty()) {
        // Report detected mode
        broadcast_status(StatusCategory::RX, data_rate_mode_to_status_string(current_mode_));
        
        // Send decoded data to data clients
        broadcast_data(result.data);
        
        // Signal end of reception
        broadcast_status(StatusCategory::RX, "NO DCD");
    } else {
        broadcast_status(StatusCategory::RX, "NO DCD");
    }
    
    state_.store(ModemState::IDLE);
    
    std::ostringstream ss;
    ss << "COMPLETE:" << int16_samples.size() << " samples";
    if (channel_sim_enabled_) {
        ss << " (channel sim applied)";
    }
    client->send(format_ok("RXAUDIOINJECT", ss.str()));
}

void MSDMTServer::cmd_kill_tx(std::shared_ptr<ClientConnection> client) {
    state_.store(ModemState::IDLE);
    clear_tx_buffer();
    broadcast_status(StatusCategory::TX, "IDLE");
    client->send(format_ok("KILL TX", ""));
}

// ============================================================
// Channel Simulation Command Handlers
// ============================================================

void MSDMTServer::cmd_channel_config(std::shared_ptr<ClientConnection> client, const std::string& param) {
    // Return current channel config
    std::ostringstream ss;
    ss << "CHANNEL CONFIG:\n";
    ss << "  Enabled: " << (channel_sim_enabled_ ? "YES" : "NO") << "\n";
    ss << "  AWGN: " << (channel_awgn_enabled_ ? "ON" : "OFF");
    if (channel_awgn_enabled_) ss << " (SNR=" << channel_snr_db_ << "dB)";
    ss << "\n";
    ss << "  Multipath: " << (channel_multipath_enabled_ ? "ON" : "OFF");
    if (channel_multipath_enabled_) ss << " (delay=" << channel_multipath_delay_ 
                                        << " samples, gain=" << channel_multipath_gain_ << ")";
    ss << "\n";
    ss << "  FreqOffset: " << (channel_freq_offset_enabled_ ? "ON" : "OFF");
    if (channel_freq_offset_enabled_) ss << " (" << channel_freq_offset_hz_ << "Hz)";
    ss << "\n";
    
    client->send(format_ok("CHANNEL CONFIG", ss.str()));
}

void MSDMTServer::cmd_channel_preset(std::shared_ptr<ClientConnection> client, const std::string& preset) {
    std::string p = preset;
    std::transform(p.begin(), p.end(), p.begin(), ::toupper);
    
    m110a::api::channel::ChannelConfig cfg;
    std::string preset_name;
    
    if (p == "GOOD" || p == "GOOD_HF") {
        cfg = m110a::api::channel::channel_good_hf();
        preset_name = "GOOD_HF";
    } else if (p == "MODERATE" || p == "MODERATE_HF") {
        cfg = m110a::api::channel::channel_moderate_hf();
        preset_name = "MODERATE_HF";
    } else if (p == "POOR" || p == "POOR_HF") {
        cfg = m110a::api::channel::channel_poor_hf();
        preset_name = "POOR_HF";
    } else if (p == "CCIR_GOOD") {
        cfg = m110a::api::channel::channel_ccir_good();
        preset_name = "CCIR_GOOD";
    } else if (p == "CCIR_MODERATE") {
        cfg = m110a::api::channel::channel_ccir_moderate();
        preset_name = "CCIR_MODERATE";
    } else if (p == "CCIR_POOR") {
        cfg = m110a::api::channel::channel_ccir_poor();
        preset_name = "CCIR_POOR";
    } else if (p == "CLEAN" || p == "OFF") {
        channel_sim_enabled_ = false;
        channel_awgn_enabled_ = false;
        channel_multipath_enabled_ = false;
        channel_freq_offset_enabled_ = false;
        client->send(format_ok("CHANNEL PRESET", "CLEAN (no impairments)"));
        return;
    } else {
        client->send(format_error("CHANNEL PRESET", "Unknown preset. Use: GOOD, MODERATE, POOR, CCIR_GOOD, CCIR_MODERATE, CCIR_POOR, CLEAN"));
        return;
    }
    
    // Apply preset
    channel_sim_enabled_ = true;
    channel_awgn_enabled_ = cfg.awgn_enabled;
    channel_snr_db_ = cfg.snr_db;
    channel_multipath_enabled_ = cfg.multipath_enabled;
    channel_multipath_delay_ = cfg.multipath_delay_samples;
    channel_multipath_gain_ = cfg.multipath_gain;
    channel_freq_offset_enabled_ = cfg.freq_offset_enabled;
    channel_freq_offset_hz_ = cfg.freq_offset_hz;
    
    std::ostringstream ss;
    ss << preset_name << " (SNR=" << cfg.snr_db << "dB, MP=" 
       << cfg.multipath_delay_samples << "samp, FOFF=" << cfg.freq_offset_hz << "Hz)";
    client->send(format_ok("CHANNEL PRESET", ss.str()));
}

void MSDMTServer::cmd_channel_awgn(std::shared_ptr<ClientConnection> client, const std::string& snr_db) {
    try {
        float snr = std::stof(snr_db);
        if (snr < 0.0f || snr > 60.0f) {
            client->send(format_error("CHANNEL AWGN", "SNR must be 0-60 dB"));
            return;
        }
        channel_awgn_enabled_ = true;
        channel_snr_db_ = snr;
        channel_sim_enabled_ = true;
        
        std::ostringstream ss;
        ss << "AWGN enabled at " << snr << " dB SNR";
        client->send(format_ok("CHANNEL AWGN", ss.str()));
    } catch (...) {
        client->send(format_error("CHANNEL AWGN", "Invalid SNR value"));
    }
}

void MSDMTServer::cmd_channel_multipath(std::shared_ptr<ClientConnection> client, const std::string& params) {
    // Parse: "delay_samples,gain" or just "delay_samples" (default gain 0.5)
    int delay = 48;
    float gain = 0.5f;
    
    size_t comma = params.find(',');
    try {
        if (comma != std::string::npos) {
            delay = std::stoi(params.substr(0, comma));
            gain = std::stof(params.substr(comma + 1));
        } else {
            delay = std::stoi(params);
        }
        
        if (delay < 1 || delay > 500) {
            client->send(format_error("CHANNEL MULTIPATH", "Delay must be 1-500 samples"));
            return;
        }
        if (gain < 0.0f || gain > 1.0f) {
            client->send(format_error("CHANNEL MULTIPATH", "Gain must be 0.0-1.0"));
            return;
        }
        
        channel_multipath_enabled_ = true;
        channel_multipath_delay_ = delay;
        channel_multipath_gain_ = gain;
        channel_sim_enabled_ = true;
        
        std::ostringstream ss;
        ss << "Multipath enabled: delay=" << delay << " samples (" 
           << (delay / 48.0f) << "ms), gain=" << gain;
        client->send(format_ok("CHANNEL MULTIPATH", ss.str()));
    } catch (...) {
        client->send(format_error("CHANNEL MULTIPATH", "Invalid parameters. Use: delay_samples[,gain]"));
    }
}

void MSDMTServer::cmd_channel_freq_offset(std::shared_ptr<ClientConnection> client, const std::string& offset_hz) {
    try {
        float offset = std::stof(offset_hz);
        if (offset < -50.0f || offset > 50.0f) {
            client->send(format_error("CHANNEL FREQOFFSET", "Offset must be -50 to +50 Hz"));
            return;
        }
        
        channel_freq_offset_enabled_ = true;
        channel_freq_offset_hz_ = offset;
        channel_sim_enabled_ = true;
        
        std::ostringstream ss;
        ss << "Frequency offset enabled: " << offset << " Hz";
        client->send(format_ok("CHANNEL FREQOFFSET", ss.str()));
    } catch (...) {
        client->send(format_error("CHANNEL FREQOFFSET", "Invalid offset value"));
    }
}

void MSDMTServer::cmd_channel_off(std::shared_ptr<ClientConnection> client) {
    channel_sim_enabled_ = false;
    channel_awgn_enabled_ = false;
    channel_multipath_enabled_ = false;
    channel_freq_offset_enabled_ = false;
    
    client->send(format_ok("CHANNEL OFF", "All channel impairments disabled"));
}

void MSDMTServer::cmd_run_ber_test(std::shared_ptr<ClientConnection> client, const std::string& params) {
    // Apply channel impairments to a PCM file and save to a new file
    // Parameters: "input_file,output_file" or just "input_file" (overwrites)
    
    std::string input_file, output_file;
    
    size_t comma = params.find(',');
    if (comma != std::string::npos) {
        input_file = params.substr(0, comma);
        output_file = params.substr(comma + 1);
        // Trim whitespace
        output_file.erase(0, output_file.find_first_not_of(" \t"));
        output_file.erase(output_file.find_last_not_of(" \t") + 1);
    } else {
        input_file = params;
        output_file = params;  // Overwrite
    }
    
    // Trim input filename
    input_file.erase(0, input_file.find_first_not_of(" \t"));
    input_file.erase(input_file.find_last_not_of(" \t") + 1);
    
    if (input_file.empty()) {
        client->send(format_error("RUN BERTEST", "No input file specified. Usage: CMD:RUN BERTEST:input.pcm[,output.pcm]"));
        return;
    }
    
    // Read input PCM
    auto int16_samples = read_pcm_file(input_file);
    if (int16_samples.empty()) {
        client->send(format_error("RUN BERTEST", "Cannot read file: " + input_file));
        return;
    }
    
    auto samples = samples_to_float(int16_samples);
    
    // Check if channel sim is enabled
    if (!channel_sim_enabled_) {
        client->send(format_error("RUN BERTEST", "No channel impairments configured. Use CMD:CHANNEL commands first."));
        return;
    }
    
    // Apply channel impairments
    std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    
    std::ostringstream applied;
    if (channel_freq_offset_enabled_) {
        m110a::api::channel::add_freq_offset(samples, channel_freq_offset_hz_, 48000.0f);
        applied << "FOFF=" << channel_freq_offset_hz_ << "Hz ";
    }
    if (channel_multipath_enabled_) {
        m110a::api::channel::add_multipath(samples, channel_multipath_delay_, channel_multipath_gain_);
        applied << "MP=" << channel_multipath_delay_ << "samp ";
    }
    if (channel_awgn_enabled_) {
        m110a::api::channel::add_awgn(samples, channel_snr_db_, rng);
        applied << "AWGN=" << channel_snr_db_ << "dB ";
    }
    
    // Write output PCM
    auto output_samples = samples_to_int16(samples);
    if (!write_pcm_file(output_file, output_samples)) {
        client->send(format_error("RUN BERTEST", "Cannot write file: " + output_file));
        return;
    }
    
    std::ostringstream ss;
    ss << "Applied [" << applied.str() << "] to " << int16_samples.size() 
       << " samples -> " << output_file;
    client->send(format_ok("RUN BERTEST", ss.str()));
}

// ============================================================
// Utility Functions
// ============================================================

std::vector<int16_t> read_pcm_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<int16_t> samples(size / 2);
    file.read(reinterpret_cast<char*>(samples.data()), size);
    return samples;
}

bool write_pcm_file(const std::string& filepath, const std::vector<int16_t>& samples) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(samples.data()), 
               samples.size() * sizeof(int16_t));
    return true;
}

std::vector<float> samples_to_float(const std::vector<int16_t>& samples) {
    std::vector<float> result(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        result[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    return result;
}

std::vector<int16_t> samples_to_int16(const std::vector<float>& samples) {
    std::vector<int16_t> result(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float val = samples[i] * 32767.0f;
        val = std::max(-32768.0f, std::min(32767.0f, val));
        result[i] = static_cast<int16_t>(val);
    }
    return result;
}

std::string generate_pcm_filename(const std::string& prefix, const std::string& output_dir) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm* tm = std::localtime(&time);
    
    std::ostringstream ss;
    ss << output_dir;
    if (!prefix.empty()) {
        ss << prefix << "_";
    }
    ss << std::put_time(tm, "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    ss << ".pcm";
    
    return ss.str();
}

} // namespace server
} // namespace m110a
