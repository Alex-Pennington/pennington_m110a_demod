/**
 * @file server_backend.h
 * @brief Server-Based Backend for Test Framework
 * 
 * Implements ITestBackend using TCP connection to M110A server.
 * Channel impairments are applied by the server.
 */

#ifndef SERVER_BACKEND_H
#define SERVER_BACKEND_H

#include "test_framework.h"

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

#include <thread>
#include <cstdio>

namespace test_framework {

class ServerBackend : public ITestBackend {
public:
    ServerBackend(const std::string& host = "127.0.0.1", 
                  int control_port = 4999, 
                  int data_port = 4998)
        : host_(host), control_port_(control_port), data_port_(data_port),
          control_sock_(INVALID_SOCK), data_sock_(INVALID_SOCK) {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }
    
    ~ServerBackend() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool connect() override {
        control_sock_ = connect_socket(control_port_);
        if (control_sock_ == INVALID_SOCK) return false;
        
        data_sock_ = connect_socket(data_port_);
        if (data_sock_ == INVALID_SOCK) {
            close_socket(control_sock_);
            control_sock_ = INVALID_SOCK;
            return false;
        }
        
        // Wait for MODEM READY
        std::string ready = receive_line(control_sock_, 2000);
        return ready.find("MODEM READY") != std::string::npos;
    }
    
    void disconnect() override {
        if (control_sock_ != INVALID_SOCK) {
            close_socket(control_sock_);
            control_sock_ = INVALID_SOCK;
        }
        if (data_sock_ != INVALID_SOCK) {
            close_socket(data_sock_);
            data_sock_ = INVALID_SOCK;
        }
    }
    
    bool is_connected() override {
        if (control_sock_ == INVALID_SOCK || data_sock_ == INVALID_SOCK) {
            return false;
        }
        // Try a simple command to verify connection
        std::string resp = send_command("CMD:GET MODE", 500);
        return resp.find("OK:") != std::string::npos || 
               resp.find("MODE:") != std::string::npos ||
               resp.find("ERROR:") != std::string::npos;  // ERROR still means connected
    }
    
    bool set_equalizer(const std::string& eq_type) override {
        std::string resp = send_command("CMD:SET EQUALIZER:" + eq_type);
        return resp.find("OK:") != std::string::npos;
    }
    
    bool run_test(const ModeInfo& mode, 
                  const ChannelCondition& channel,
                  const std::vector<uint8_t>& test_data,
                  double& ber_out) override {
        ber_out = 1.0;
        
        // 1. Set mode
        std::string resp = send_command("CMD:DATA RATE:" + mode.cmd);
        if (resp.find("OK:") == std::string::npos) {
            return false;
        }
        
        // 2. Enable recording
        send_command("CMD:RECORD TX:ON");
        send_command("CMD:RECORD PREFIX:" + mode.name + "_" + channel.name);
        
        // 3. Send test data
        send_data(test_data);
        
        // 4. Trigger TX
        last_pcm_file_.clear();
        resp = send_command("CMD:SENDBUFFER", mode.tx_time_ms + 2000);
        if (resp.find("OK:") == std::string::npos) {
            return false;
        }
        
        // Wait for file to be written
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 5. Get PCM filename
        if (last_pcm_file_.empty()) {
            return false;
        }
        
        // 6. Configure channel
        send_command("CMD:CHANNEL OFF");
        if (!channel.setup_cmd.empty()) {
            resp = send_command(channel.setup_cmd);
            if (resp.find("OK:") == std::string::npos) {
                return false;
            }
        }
        
        // 7. Inject PCM for decode
        if (!inject_and_wait_decode(last_pcm_file_, mode.tx_time_ms + 5000)) {
            return false;
        }
        
        // 8. Receive decoded data
        std::vector<uint8_t> rx_data = receive_data(2000);
        
        // 9. Calculate BER
        ber_out = calculate_ber(test_data, rx_data);
        
        // 10. Cleanup
        send_command("CMD:CHANNEL OFF");
        
        // Keep last 2 PCM files, delete older ones
        cleanup_pcm_files();
        
        return ber_out <= channel.expected_ber_threshold;
    }
    
    std::string backend_name() const override {
        return "Server (" + host_ + ":" + std::to_string(control_port_) + ")";
    }
    
    // Reconnect helper
    bool reconnect() {
        disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return connect();
    }

private:
    std::string host_;
    int control_port_;
    int data_port_;
    socket_t control_sock_;
    socket_t data_sock_;
    std::string last_pcm_file_;
    
    // PCM file cleanup tracking
    std::string prev_pcm_file_;
    std::string prev_prev_pcm_file_;
    
    socket_t connect_socket(int port) {
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCK) return INVALID_SOCK;
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
        
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close_socket(sock);
            return INVALID_SOCK;
        }
        
        return sock;
    }
    
    std::string receive_line(socket_t sock, int timeout_ms) {
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
    
    std::string send_command(const std::string& cmd, int timeout_ms = 1000) {
        std::string full_cmd = cmd + "\n";
        send(control_sock_, full_cmd.c_str(), (int)full_cmd.length(), 0);
        
        std::string response;
        auto start = std::chrono::steady_clock::now();
        
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < timeout_ms) {
            std::string line = receive_line(control_sock_, 200);
            if (!line.empty()) {
                response += line + "\n";
                
                // Check for terminal responses
                if (line.find("OK:") == 0 || line.find("ERROR:") == 0) {
                    // Extract PCM filename if present
                    size_t file_pos = line.find("FILE:");
                    if (file_pos != std::string::npos) {
                        last_pcm_file_ = line.substr(file_pos + 5);
                        while (!last_pcm_file_.empty() && 
                               (last_pcm_file_.back() == ' ' || 
                                last_pcm_file_.back() == '\n' || 
                                last_pcm_file_.back() == '\r')) {
                            last_pcm_file_.pop_back();
                        }
                    }
                    break;
                }
            }
        }
        
        return response;
    }
    
    bool send_data(const std::vector<uint8_t>& data) {
        int sent = send(data_sock_, reinterpret_cast<const char*>(data.data()), 
                       (int)data.size(), 0);
        return sent == (int)data.size();
    }
    
    std::vector<uint8_t> receive_data(int timeout_ms = 2000) {
#ifdef _WIN32
        DWORD tv = timeout_ms;
        setsockopt(data_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(data_sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        
        std::vector<uint8_t> result(4096);
        int received = recv(data_sock_, reinterpret_cast<char*>(result.data()), 4096, 0);
        if (received > 0) {
            result.resize(received);
            return result;
        }
        return {};
    }
    
    bool inject_and_wait_decode(const std::string& pcm_file, int timeout_ms) {
        std::string full_cmd = "CMD:RXAUDIOINJECT:" + pcm_file + "\n";
        send(control_sock_, full_cmd.c_str(), (int)full_cmd.length(), 0);
        
        auto start = std::chrono::steady_clock::now();
        
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < timeout_ms) {
#ifdef _WIN32
            DWORD tv = 500;
            setsockopt(control_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
            std::string line;
            char c;
            while (recv(control_sock_, &c, 1, 0) == 1) {
                if (c == '\n') break;
                if (c != '\r') line += c;
            }
            
            if (!line.empty()) {
                if (line.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    return true;
                }
                if (line.find("ERROR:") != std::string::npos) {
                    return false;
                }
            }
        }
        
        return false;
    }
    
    void cleanup_pcm_files() {
        if (!prev_prev_pcm_file_.empty()) {
            std::remove(prev_prev_pcm_file_.c_str());
        }
        prev_prev_pcm_file_ = prev_pcm_file_;
        prev_pcm_file_ = last_pcm_file_;
    }
};

} // namespace test_framework

#endif // SERVER_BACKEND_H
