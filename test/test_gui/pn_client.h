#pragma once
/**
 * @file pn_client.h
 * @brief PhoenixNest modem server manager and TCP client
 */

#include "utils.h"
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace test_gui {

class PnClient {
public:
    PnClient() = default;
    ~PnClient() { 
        disconnect();
        stop_server();
    }
    
    // Server management
    bool start_server(const std::string& exe_dir, int ctrl_port, int data_port) {
        if (server_running_ && server_pid_ != 0) {
#ifdef _WIN32
            DWORD exitCode;
            if (GetExitCodeProcess(server_process_, &exitCode) && exitCode == STILL_ACTIVE) {
                return true; // Already running
            } else {
                CloseHandle(server_process_);
                server_process_ = NULL;
                server_pid_ = 0;
                server_running_ = false;
            }
#endif
        }
        
        ctrl_port_ = ctrl_port;
        data_port_ = data_port;
        
        // Find server executable (use / which works on both Windows and Linux)
        std::vector<std::string> paths = {
            exe_dir + "/m110a_server.exe",
            exe_dir + "/../server/m110a_server.exe",
        };
        
        std::string server_exe;
        for (const auto& p : paths) {
            if (fs::exists(p)) {
                server_exe = fs::absolute(p).string();
                break;
            }
        }
        
        if (server_exe.empty()) {
            last_error_ = "m110a_server.exe not found";
            return false;
        }
        
#ifdef _WIN32
        std::string cmd = "\"" + server_exe + "\" --control-port " + std::to_string(ctrl_port) + 
                          " --data-port " + std::to_string(data_port);
        
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        char cmdLine[1024];
        strncpy(cmdLine, cmd.c_str(), sizeof(cmdLine) - 1);
        cmdLine[sizeof(cmdLine) - 1] = '\0';
        
        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 
                          CREATE_NEW_CONSOLE | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            server_process_ = pi.hProcess;
            server_pid_ = pi.dwProcessId;
            server_running_ = true;
            CloseHandle(pi.hThread);
            
            Sleep(500); // Let server start
            return true;
        } else {
            last_error_ = "CreateProcess failed: " + std::to_string(GetLastError());
            return false;
        }
#else
        last_error_ = "Not implemented on this platform";
        return false;
#endif
    }
    
    void stop_server() {
        if (!server_running_ || server_pid_ == 0) {
            server_running_ = false;
            server_pid_ = 0;
            return;
        }
        
#ifdef _WIN32
        if (server_process_ != NULL) {
            TerminateProcess(server_process_, 0);
            WaitForSingleObject(server_process_, 3000);
            CloseHandle(server_process_);
            server_process_ = NULL;
        }
#endif
        server_pid_ = 0;
        server_running_ = false;
        disconnect();
    }
    
    bool is_server_running() {
#ifdef _WIN32
        if (server_running_ && server_process_ != NULL) {
            DWORD exitCode;
            if (GetExitCodeProcess(server_process_, &exitCode) && exitCode == STILL_ACTIVE) {
                return true;
            } else {
                CloseHandle(server_process_);
                server_process_ = NULL;
                server_pid_ = 0;
                server_running_ = false;
            }
        }
#endif
        return server_running_;
    }
    
    // Client connection
    bool connect() {
        if (connected_) return true;
        
        // Control port
        ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (ctrl_sock_ == INVALID_SOCKET) return false;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ctrl_port_);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
        
        set_timeout(ctrl_sock_, 5000);
        
        if (::connect(ctrl_sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        // Read welcome
        char buf[1024];
        int n = recv(ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::cout << "[PN] Control connected: " << buf << std::flush;
        }
        
        // Data port
        data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (data_sock_ == INVALID_SOCKET) {
            closesocket(ctrl_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        addr.sin_port = htons(data_port_);
        set_timeout(data_sock_, 5000);
        
        if (::connect(data_sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(ctrl_sock_);
            closesocket(data_sock_);
            ctrl_sock_ = INVALID_SOCKET;
            data_sock_ = INVALID_SOCKET;
            return false;
        }
        
        std::cout << "[PN] Data port connected\n";
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
        std::cout << "[PN] SEND: " << cmd << "\n";
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
            std::cout << "[PN] RECV: " << result.substr(0, 60) << "\n";
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
    unsigned long get_server_pid() const { return server_pid_; }
    int get_ctrl_port() const { return ctrl_port_; }
    int get_data_port() const { return data_port_; }
    std::string get_last_error() const { return last_error_; }
    
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
    
    // Server process
#ifdef _WIN32
    HANDLE server_process_ = NULL;
#endif
    unsigned long server_pid_ = 0;
    bool server_running_ = false;
    
    // Client sockets
    SOCKET ctrl_sock_ = INVALID_SOCKET;
    SOCKET data_sock_ = INVALID_SOCKET;
    std::string host_ = "127.0.0.1";
    int ctrl_port_ = 5100;
    int data_port_ = 5101;
    bool connected_ = false;
    std::string last_error_;
};

} // namespace test_gui
