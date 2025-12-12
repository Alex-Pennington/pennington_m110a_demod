#pragma once
/**
 * @file brain_client.h
 * @brief G4GUO modem TCP client
 */

#include "utils.h"
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

namespace test_gui {

class BrainClient {
public:
    BrainClient() = default;
    ~BrainClient() { disconnect(); }
    
    bool connect(const std::string& host, int ctrl_port, int data_port) {
        disconnect();
        
        host_ = host;
        ctrl_port_ = ctrl_port;
        data_port_ = data_port;
        
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        // Connect control port
        if (getaddrinfo(host.c_str(), std::to_string(ctrl_port).c_str(), &hints, &result) != 0) {
            return false;
        }
        
        ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (ctrl_sock_ == INVALID_SOCKET) {
            freeaddrinfo(result);
            return false;
        }
        
        set_timeout(ctrl_sock_, 5000);
        
        if (::connect(ctrl_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            return false;
        }
        freeaddrinfo(result);
        
        // Read welcome message
        char buf[1024];
        int n = recv(ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            welcome_msg_ = buf;
            std::cout << "[BRAIN] Control connected: " << welcome_msg_ << std::flush;
        }
        
        // Connect data port
        if (getaddrinfo(host.c_str(), std::to_string(data_port).c_str(), &hints, &result) != 0) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (data_sock_ == INVALID_SOCKET) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            return false;
        }
        
        set_timeout(data_sock_, 5000);
        
        if (::connect(data_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(ctrl_sock_);
            closesocket(data_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            data_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            return false;
        }
        freeaddrinfo(result);
        
        std::cout << "[BRAIN] Data port connected\n";
        connected_ = true;
        return true;
    }
    
    void disconnect() {
        if (ctrl_sock_ != INVALID_SOCKET) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
        }
        if (data_sock_ != INVALID_SOCKET) {
            closesocket(data_sock_);
            data_sock_ = INVALID_SOCKET;
        }
        connected_ = false;
    }
    
    bool send_cmd(const std::string& cmd) {
        if (ctrl_sock_ == INVALID_SOCKET) return false;
        std::string msg = cmd + "\n";
        std::cout << "[BRAIN] SEND: " << cmd << "\n";
        return send(ctrl_sock_, msg.c_str(), (int)msg.size(), 0) > 0;
    }
    
    std::string recv_ctrl(int timeout_ms = 5000) {
        if (ctrl_sock_ == INVALID_SOCKET) return "";
        set_timeout(ctrl_sock_, timeout_ms);
        
        char buf[4096];
        int n = recv(ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::string result(buf);
            std::cout << "[BRAIN] RECV: " << result.substr(0, 60) << "\n";
            return result;
        }
        return "";
    }
    
    bool send_data(const std::string& data) {
        if (data_sock_ == INVALID_SOCKET) return false;
        return send(data_sock_, data.c_str(), (int)data.size(), 0) > 0;
    }
    
    std::vector<uint8_t> recv_data(int timeout_ms = 10000) {
        std::vector<uint8_t> data;
        if (data_sock_ == INVALID_SOCKET) return data;
        set_timeout(data_sock_, timeout_ms);
        
        char buf[8192];
        while (true) {
            int n = recv(data_sock_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            data.insert(data.end(), buf, buf + n);
        }
        return data;
    }
    
    bool is_connected() const { return connected_; }
    std::string get_welcome() const { return welcome_msg_; }
    std::string get_host() const { return host_; }
    int get_ctrl_port() const { return ctrl_port_; }
    int get_data_port() const { return data_port_; }
    
private:
    void set_timeout(SOCKET sock, int ms) {
#ifdef _WIN32
        DWORD timeout = ms;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    }
    
    SOCKET ctrl_sock_ = INVALID_SOCKET;
    SOCKET data_sock_ = INVALID_SOCKET;
    std::string host_ = "localhost";
    int ctrl_port_ = 3999;
    int data_port_ = 3998;
    bool connected_ = false;
    std::string welcome_msg_;
};

} // namespace test_gui
