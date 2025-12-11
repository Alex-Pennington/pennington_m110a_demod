#pragma once
/**
 * @file server.h
 * @brief HTTP server for test GUI - parses JSON output from exhaustive_test.exe
 */

#include "utils.h"
#include "brain_client.h"
#include "pn_client.h"
#include "test_config.h"
#include "html_content.h"

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <fstream>
#include <random>
#include <ctime>
#include <cstdio>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

namespace test_gui {

class TestGuiServer {
public:
    TestGuiServer(int port = 8080) : port_(port), running_(false) {
#ifdef _WIN32
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        exe_dir_ = path;
        size_t last_slash = exe_dir_.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            exe_dir_ = exe_dir_.substr(0, last_slash + 1);
        }
#else
        exe_dir_ = "./";
#endif
    }
    
    bool start() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
#endif
        
        server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock_ == INVALID_SOCKET) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        int opt = 1;
        setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(server_sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Bind failed on port " << port_ << "\n";
            return false;
        }
        
        if (listen(server_sock_, 5) < 0) {
            std::cerr << "Listen failed\n";
            return false;
        }
        
        running_ = true;
        std::cout << "Test GUI Server running at http://localhost:" << port_ << "\n";
        std::cout << "Press Ctrl+C to stop.\n\n";
        
#ifdef _WIN32
        std::string url = "http://localhost:" + std::to_string(port_);
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
        
        while (running_) {
            SOCKET client = accept(server_sock_, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                std::thread(&TestGuiServer::handle_client, this, client).detach();
            }
        }
        
        return true;
    }
    
    void stop() {
        running_ = false;
        closesocket(server_sock_);
    }
    
private:
    void handle_client(SOCKET client) {
        char buf[8192];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closesocket(client);
            return;
        }
        buf[n] = '\0';
        
        std::string request(buf, n);
        std::string method, path;
        std::istringstream iss(request);
        iss >> method >> path;
        
        // Route requests
        if (path == "/" || path == "/index.html") {
            send_html(client, HTML_PAGE);
        }
        else if (path.find("/pn-server-start?") == 0) {
            handle_pn_server_start(client, path);
        }
        else if (path == "/pn-server-stop") {
            handle_pn_server_stop(client);
        }
        else if (path == "/pn-server-status") {
            handle_pn_server_status(client);
        }
        else if (path.find("/brain-connect?") == 0) {
            handle_brain_connect(client, path);
        }
        else if (path == "/brain-disconnect") {
            handle_brain_disconnect(client);
        }
        else if (path.find("/run-exhaustive?") == 0) {
            handle_run_exhaustive(client, path);
        }
        else if (path == "/run-interop") {
            handle_run_interop(client);
        }
        else if (path == "/stop-test") {
            stop_test_ = true;
            send_json(client, "{\"success\":true}");
        }
        else if (path.find("/test-connection?") == 0) {
            handle_test_connection(client, path);
        }
        else if (path == "/list-reports") {
            handle_list_reports(client);
        }
        else if (path.find("/report/") == 0) {
            handle_view_report(client, path);
        }
        else if (path == "/export-report" || path == "/export-csv" || path == "/export-json") {
            handle_export(client, path);
        }
        // MELPe Vocoder routes
        else if (path.find("/melpe-audio?") == 0) {
            handle_melpe_audio(client, path);
        }
        else if (path.find("/melpe-run?") == 0) {
            handle_melpe_run(client, path);
        }
        else if (path.find("/melpe-output?") == 0) {
            handle_melpe_output(client, path);
        }
        else if (path == "/melpe-list-recordings") {
            handle_melpe_recordings(client);
        }
        else if (path == "/melpe-save-recording" && method == "POST") {
            handle_melpe_save_recording(client, request);
        }
        else {
            send_404(client);
        }
        
        closesocket(client);
    }
    
    // ============ MELPE HELPER FUNCTIONS ============
    
    // Find melpe test audio directory - works in both dev and deployed scenarios
    std::string find_melpe_audio_dir() {
        std::vector<std::string> candidates = {
            exe_dir_ + "examples/melpe_test_audio/",
            exe_dir_ + "examples\\melpe_test_audio\\",
            exe_dir_ + "../examples/melpe_test_audio/",
            exe_dir_ + "..\\examples\\melpe_test_audio\\",
            exe_dir_ + "../../src/melpe_core/test_audio/",
            exe_dir_ + "..\\..\\src\\melpe_core\\test_audio\\",
            exe_dir_ + "../src/melpe_core/test_audio/",
            exe_dir_ + "..\\src\\melpe_core\\test_audio\\",
        };
        
        for (const auto& dir : candidates) {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                try {
                    fs::path p = fs::canonical(dir);
                    std::string result = p.string();
                    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
                        result += fs::path::preferred_separator;
                    }
                    return result;
                } catch (...) {
                    return dir;
                }
            }
        }
        return "";
    }
    
    // Find melpe_vocoder.exe - works in both dev and deployed scenarios
    std::string find_melpe_exe() {
        std::vector<std::string> candidates = {
            exe_dir_ + "melpe_vocoder.exe",
            exe_dir_ + "..\\bin\\melpe_vocoder.exe",
            exe_dir_ + "../bin/melpe_vocoder.exe",
            exe_dir_ + "..\\release\\bin\\melpe_vocoder.exe",
            exe_dir_ + "../release/bin/melpe_vocoder.exe",
            exe_dir_ + "..\\..\\src\\melpe_core\\build\\melpe_vocoder.exe",
            exe_dir_ + "../../src/melpe_core/build/melpe_vocoder.exe",
        };
        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                try {
                    return fs::canonical(candidate).string();
                } catch (...) {
                    return candidate;
                }
            }
        }
        return "";
    }
    
    // Get recordings directory (creates if needed)
    std::string get_recordings_dir() {
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            audio_dir = exe_dir_;
        }
        std::string rec_dir = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator);
        
        if (!fs::exists(rec_dir)) {
            fs::create_directories(rec_dir);
        }
        return rec_dir;
    }
    
    // Simple base64 decoder
    std::vector<unsigned char> base64_decode(const std::string& encoded) {
        static const std::string base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::vector<unsigned char> result;
        int val = 0, valb = -8;
        for (unsigned char c : encoded) {
            if (c == '=') break;
            size_t idx = base64_chars.find(c);
            if (idx == std::string::npos) continue;
            val = (val << 6) + (int)idx;
            valb += 6;
            if (valb >= 0) {
                result.push_back((unsigned char)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }
    
    // ============ MELPE VOCODER HANDLERS ============
    
    void handle_melpe_audio(SOCKET client, const std::string& path) {
        // Parse filename from query string
        std::string filename;
        size_t pos = path.find("file=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            filename = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        // Validate filename (no path traversal)
        if (filename.empty() || filename.find("..") != std::string::npos || 
            filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos) {
            send_404(client);
            return;
        }
        
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            send_404(client);
            return;
        }
        
        // Try main audio directory first
        std::string filepath = audio_dir + filename;
        std::ifstream file(filepath, std::ios::binary);
        
        // If not found, try recordings subdirectory
        if (!file.is_open()) {
            filepath = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator) + filename;
            file.open(filepath, std::ios::binary);
        }
        
        if (!file.is_open()) {
            send_404(client);
            return;
        }
        
        std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/octet-stream\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
        std::string headers = response.str();
        send(client, headers.c_str(), (int)headers.size(), 0);
        send(client, content.data(), (int)content.size(), 0);
    }
    
    void handle_melpe_run(SOCKET client, const std::string& path) {
        // Parse parameters from query string
        std::string input_file, rate = "2400";
        
        size_t pos = path.find("input=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            input_file = url_decode(path.substr(pos + 6, end - pos - 6));
        }
        
        pos = path.find("rate=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            rate = path.substr(pos + 5, end - pos - 5);
        }
        
        // Validate
        if (input_file.empty() || input_file.find("..") != std::string::npos) {
            send_json(client, "{\"success\":false,\"message\":\"Invalid input file\"}");
            return;
        }
        
        if (rate != "600" && rate != "1200" && rate != "2400") {
            send_json(client, "{\"success\":false,\"message\":\"Invalid rate. Use 600, 1200, or 2400\"}");
            return;
        }
        
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"MELPe test audio directory not found\"}");
            return;
        }
        
        std::string melpe_exe = find_melpe_exe();
        if (melpe_exe.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"melpe_vocoder.exe not found\"}");
            return;
        }
        
        // Create output filename
        std::string output_file = "melpe_output_" + rate + "bps.raw";
        std::string input_path = audio_dir + input_file;
        
        // If not found in main dir, try recordings subdirectory
        if (!fs::exists(input_path)) {
            input_path = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator) + input_file;
        }
        
        std::string output_path = exe_dir_ + output_file;
        
        if (!fs::exists(input_path)) {
            send_json(client, "{\"success\":false,\"message\":\"Input file not found: " + input_file + "\"}");
            return;
        }
        
        // Build command - run melpe_vocoder.exe in loopback mode
        std::string cmd = "cmd /c \"\"" + melpe_exe + "\" -r " + rate + 
                          " -m C -i \"" + input_path + "\" -o \"" + output_path + "\"\" 2>&1";
        
        // Execute vocoder
#ifdef _WIN32
        FILE* proc = _popen(cmd.c_str(), "r");
#else
        FILE* proc = popen(cmd.c_str(), "r");
#endif
        
        std::string output;
        if (proc) {
            char buf[256];
            while (fgets(buf, sizeof(buf), proc)) {
                output += buf;
            }
#ifdef _WIN32
            _pclose(proc);
#else
            pclose(proc);
#endif
            
            if (fs::exists(output_path)) {
                auto input_size = fs::file_size(input_path);
                auto output_size = fs::file_size(output_path);
                std::ostringstream json;
                json << "{\"success\":true,\"message\":\"Processed " 
                     << std::fixed << std::setprecision(1) << (input_size / 2 / 8000.0) 
                     << "s of audio at " << rate << " bps\""
                     << ",\"output_file\":\"" << output_file << "\""
                     << ",\"input_size\":" << input_size
                     << ",\"output_size\":" << output_size
                     << "}";
                send_json(client, json.str());
            } else {
                // Escape output for JSON
                std::string escaped;
                for (char c : output) {
                    if (c == '"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else if (c == '\n') escaped += "\\n";
                    else if (c == '\r') continue;
                    else escaped += c;
                }
                send_json(client, "{\"success\":false,\"message\":\"Vocoder failed: " + escaped + "\"}");
            }
        } else {
            send_json(client, "{\"success\":false,\"message\":\"Could not start melpe_vocoder.exe\"}");
        }
    }
    
    void handle_melpe_output(SOCKET client, const std::string& path) {
        std::string filename;
        size_t pos = path.find("file=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            filename = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        // Validate filename (must be our output file pattern)
        if (filename.empty() || filename.find("..") != std::string::npos ||
            filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos ||
            filename.find("melpe_output_") != 0) {
            send_404(client);
            return;
        }
        
        std::string filepath = exe_dir_ + filename;
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            send_404(client);
            return;
        }
        
        std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/octet-stream\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
        std::string headers = response.str();
        send(client, headers.c_str(), (int)headers.size(), 0);
        send(client, content.data(), (int)content.size(), 0);
    }
    
    void handle_melpe_recordings(SOCKET client) {
        std::string rec_dir = get_recordings_dir();
        
        std::ostringstream json;
        json << "{\"recordings\":[";
        
        bool first = true;
        
        try {
            for (const auto& entry : fs::directory_iterator(rec_dir)) {
                if (entry.path().extension() == ".pcm" || entry.path().extension() == ".raw") {
                    if (!first) json << ",";
                    first = false;
                    
                    std::string fname = entry.path().filename().string();
                    auto filesize = fs::file_size(entry);
                    double duration = (filesize / 2) / 8000.0;  // 16-bit samples at 8kHz
                    
                    // Extract base name (remove timestamp and extension)
                    std::string name = entry.path().stem().string();
                    size_t underscore = name.rfind('_');
                    if (underscore != std::string::npos && underscore > 0) {
                        // Check if what follows looks like a timestamp
                        std::string suffix = name.substr(underscore + 1);
                        if (suffix.find("8k") != std::string::npos || suffix.length() > 10) {
                            name = name.substr(0, underscore);
                        }
                    }
                    
                    json << "{\"filename\":\"" << fname << "\","
                         << "\"name\":\"" << name << "\","
                         << "\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
                }
            }
        } catch (...) {}
        
        json << "]}";
        send_json(client, json.str());
    }
    
    void handle_melpe_save_recording(SOCKET client, const std::string& request) {
        // Find the body (after headers)
        size_t body_start = request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            send_json(client, "{\"success\":false,\"message\":\"Invalid request\"}");
            return;
        }
        
        std::string body = request.substr(body_start + 4);
        
        // Parse JSON (simple parsing)
        std::string filename;
        std::string pcm_data_b64;
        
        // Extract filename
        size_t fn_pos = body.find("\"filename\"");
        if (fn_pos != std::string::npos) {
            size_t colon = body.find(':', fn_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = body.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                filename = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        
        // Extract pcm_data (base64)
        size_t pcm_pos = body.find("\"pcm_data\"");
        if (pcm_pos != std::string::npos) {
            size_t colon = body.find(':', pcm_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = body.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                pcm_data_b64 = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        
        // Validate filename
        if (filename.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm* tm_info = std::localtime(&time);
            char buf[64];
            std::strftime(buf, sizeof(buf), "recording_%Y%m%d_%H%M%S.pcm", tm_info);
            filename = buf;
        }
        
        // Security: only allow safe filename characters
        for (char c : filename) {
            if (!isalnum(c) && c != '_' && c != '-' && c != '.') {
                send_json(client, "{\"success\":false,\"message\":\"Invalid filename characters\"}");
                return;
            }
        }
        
        // Ensure .pcm extension
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".pcm") {
            filename += ".pcm";
        }
        
        // Decode base64 PCM data
        std::vector<unsigned char> pcm_data = base64_decode(pcm_data_b64);
        
        if (pcm_data.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"No audio data received\"}");
            return;
        }
        
        // Save to recordings directory
        std::string rec_dir = get_recordings_dir();
        std::string filepath = rec_dir + filename;
        
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            send_json(client, "{\"success\":false,\"message\":\"Failed to create file\"}");
            return;
        }
        
        file.write(reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size());
        file.close();
        
        double duration = (pcm_data.size() / 2) / 8000.0;
        
        std::ostringstream json;
        json << "{\"success\":true,\"filename\":\"" << filename << "\",\"size\":" << pcm_data.size() 
             << ",\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
        send_json(client, json.str());
    }
    
    // PhoenixNest server control
    void handle_pn_server_start(SOCKET client, const std::string& path) {
        auto params = parse_query_string(path);
        int ctrl_port = params.count("ctrl") ? std::stoi(params["ctrl"]) : 5100;
        int data_port = params.count("data") ? std::stoi(params["data"]) : 5101;
        
        if (pn_client_.start_server(exe_dir_, ctrl_port, data_port)) {
            std::ostringstream json;
            json << "{\"success\":true,\"pid\":" << pn_client_.get_server_pid() << "}";
            send_json(client, json.str());
        } else {
            send_json(client, "{\"success\":false,\"message\":\"" + json_escape(pn_client_.get_last_error()) + "\"}");
        }
    }
    
    void handle_pn_server_stop(SOCKET client) {
        pn_client_.stop_server();
        send_json(client, "{\"success\":true}");
    }
    
    void handle_pn_server_status(SOCKET client) {
        bool running = pn_client_.is_server_running();
        std::ostringstream json;
        json << "{\"running\":" << (running ? "true" : "false");
        if (running) {
            json << ",\"pid\":" << pn_client_.get_server_pid();
        }
        json << "}";
        send_json(client, json.str());
    }
    
    // Brain modem connection
    void handle_brain_connect(SOCKET client, const std::string& path) {
        auto params = parse_query_string(path);
        std::string host = params.count("host") ? params["host"] : "localhost";
        int ctrl_port = params.count("ctrl") ? std::stoi(params["ctrl"]) : 3999;
        int data_port = params.count("data") ? std::stoi(params["data"]) : 3998;
        
        if (brain_client_.connect(host, ctrl_port, data_port)) {
            send_json(client, "{\"success\":true,\"message\":\"" + json_escape(brain_client_.get_welcome()) + "\"}");
        } else {
            send_json(client, "{\"success\":false,\"message\":\"Connection failed\"}");
        }
    }
    
    void handle_brain_disconnect(SOCKET client) {
        brain_client_.disconnect();
        send_json(client, "{\"success\":true}");
    }
    
    // TCP connection test
    void handle_test_connection(SOCKET client, const std::string& path) {
        auto params = parse_query_string(path);
        std::string host = params.count("host") ? params["host"] : "127.0.0.1";
        int port = params.count("port") ? std::stoi(params["port"]) : 5100;
        
        SOCKET test_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (test_sock == INVALID_SOCKET) {
            send_json(client, "{\"success\":false,\"error\":\"Socket creation failed\"}");
            return;
        }
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
#ifdef _WIN32
        DWORD timeout = 3000;
        setsockopt(test_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(test_sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#endif
        
        if (connect(test_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(test_sock);
            send_json(client, "{\"success\":false,\"error\":\"Cannot connect\"}");
            return;
        }
        
        char buf[256];
        int n = recv(test_sock, buf, sizeof(buf) - 1, 0);
        closesocket(test_sock);
        
        if (n > 0) {
            buf[n] = '\0';
            std::string version(buf);
            while (!version.empty() && (version.back() == '\n' || version.back() == '\r')) {
                version.pop_back();
            }
            send_json(client, "{\"success\":true,\"version\":\"" + json_escape(version) + "\"}");
        } else {
            send_json(client, "{\"success\":true,\"version\":\"Connected\"}");
        }
    }
    
    // ================================================================
    // Run exhaustive test - Supports both PhoenixNest and Brain backends
    // ================================================================
    void handle_run_exhaustive(SOCKET client, const std::string& path) {
        auto params = parse_query_string(path);
        
        // Send SSE headers
        send_sse_headers(client);
        
        stop_test_ = false;
        
        // Get backend selection (default to phoenixnest)
        std::string backend = params.count("backend") ? params["backend"] : "phoenixnest";
        bool use_brain = (backend == "brain");
        
        // Find test executable - all executables are in release/bin/ together
        std::string exe_name = use_brain ? "brain_exhaustive_test.exe" : "exhaustive_test.exe";
        std::string test_exe = exe_dir_ + exe_name;
        
        if (!fs::exists(test_exe)) {
            send_sse(client, "{\"output\":\"ERROR: " + exe_name + " not found in " + json_escape(exe_dir_) + "\",\"done\":true}");
            return;
        }
        
        // Parse parameters
        int duration_sec = params.count("duration") ? std::stoi(params["duration"]) : 180;
        std::string modes_str = params.count("modes") ? url_decode(params["modes"]) : "";
        
        // Build command with --json flag for machine-readable output
        std::ostringstream cmd;
        cmd << "\"" << test_exe << "\" --json --duration " << duration_sec;
        
        if (!modes_str.empty() && modes_str != "all") {
            // Use --modes for comma-separated list, --mode for single mode
            if (modes_str.find(',') != std::string::npos) {
                cmd << " --modes " << modes_str;
            } else {
                cmd << " --mode " << modes_str;
            }
        }
        
        // Redirect stderr to stdout so we see all output
        cmd << " 2>&1";
        
        std::string backend_label = use_brain ? "Brain" : "PhoenixNest";
        send_sse(client, "{\"output\":\"=== " + backend_label + " Exhaustive Test ===\",\"type\":\"header\"}");
        send_sse(client, "{\"output\":\"Executing: " + json_escape(cmd.str()) + "\",\"type\":\"header\"}");
        
#ifdef _WIN32
        FILE* pipe = _popen(cmd.str().c_str(), "r");
        if (!pipe) {
            send_sse(client, "{\"output\":\"ERROR: Could not start exhaustive_test.exe\",\"done\":true}");
            return;
        }
        
        char buffer[2048];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr && !stop_test_) {
            std::string line(buffer);
            
            // Remove trailing newline
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            
            if (line.empty()) continue;
            
            // Skip non-JSON lines (debug output like [RX DEBUG], [RX] extract_data, etc.)
            if (line[0] != '{') {
                // Optionally log to console for debugging
                // std::cout << "[SKIPPED] " << line << "\n";
                continue;
            }
            
            // The exhaustive_test outputs JSON lines - forward them directly
            // but wrap in our SSE format
            if (line[0] == '{') {
                // Parse the JSON to extract key fields for our UI
                std::string type = extract_json_value(line, "type");
                
                if (type == "test") {
                    // Individual test result
                    std::string mode = extract_json_value(line, "mode");
                    std::string channel = extract_json_value(line, "channel");
                    std::string tests = extract_json_value(line, "tests");
                    std::string passed = extract_json_value(line, "passed");
                    std::string rate = extract_json_value(line, "rate");
                    std::string result = extract_json_value(line, "result");
                    std::string ber = extract_json_value(line, "ber");
                    std::string elapsed = extract_json_value(line, "elapsed");
                    
                    // Forward to browser with our SSE format
                    std::ostringstream sse;
                    sse << "{\"output\":\"[" << elapsed << "s] " << mode << " + " << channel 
                        << " | " << result << "\""
                        << ",\"tests\":" << tests
                        << ",\"passed\":" << passed
                        << ",\"rate\":" << rate
                        << ",\"elapsed\":\"" << elapsed << "s\""
                        << ",\"currentTest\":\"" << mode << " + " << channel << "\""
                        << ",\"ber\":" << ber
                        << "}";
                    send_sse(client, sse.str());
                    
                    // Store for export
                    last_results_.total_tests = std::stoi(tests);
                    last_results_.total_passed = std::stoi(passed);
                }
                else if (type == "mode_stats") {
                    std::string mode = extract_json_value(line, "mode");
                    std::string passed = extract_json_value(line, "passed");
                    std::string failed = extract_json_value(line, "failed");
                    std::string total = extract_json_value(line, "total");
                    std::string rate = extract_json_value(line, "rate");
                    
                    std::ostringstream sse;
                    sse << "{\"output\":\"Mode " << mode << ": " << passed << "/" 
                        << total << " (" << rate << "%)\""
                        << ",\"type\":\"mode_stat\""
                        << ",\"mode\":\"" << mode << "\""
                        << ",\"passed\":" << passed
                        << ",\"failed\":" << failed
                        << ",\"total\":" << total
                        << ",\"rate\":" << rate
                        << "}";
                    send_sse(client, sse.str());
                }
                else if (type == "channel_stats") {
                    std::string channel = extract_json_value(line, "channel");
                    std::string passed = extract_json_value(line, "passed");
                    std::string failed = extract_json_value(line, "failed");
                    std::string total = extract_json_value(line, "total");
                    std::string rate = extract_json_value(line, "rate");
                    std::string avg_ber = extract_json_value(line, "avg_ber");
                    
                    std::ostringstream sse;
                    sse << "{\"output\":\"Channel " << channel << ": " << rate << "%\""
                        << ",\"type\":\"channel_stat\""
                        << ",\"channel\":\"" << channel << "\""
                        << ",\"passed\":" << passed
                        << ",\"failed\":" << failed
                        << ",\"total\":" << total
                        << ",\"rate\":" << rate
                        << ",\"avgBer\":" << avg_ber
                        << "}";
                    send_sse(client, sse.str());
                }
                else if (type == "done") {
                    std::string tests = extract_json_value(line, "tests");
                    std::string passed = extract_json_value(line, "passed");
                    std::string failed = extract_json_value(line, "failed");
                    std::string rate = extract_json_value(line, "rate");
                    std::string rating = extract_json_value(line, "rating");
                    std::string duration = extract_json_value(line, "duration");
                    std::string avg_ber = extract_json_value(line, "avg_ber");
                    
                    // Final summary
                    std::ostringstream sse;
                    sse << "{\"output\":\"\\n=== TEST COMPLETE ===\\n"
                        << "Tests: " << tests << "\\n"
                        << "Passed: " << passed << "\\n"
                        << "Failed: " << failed << "\\n"
                        << "Rate: " << rate << "%\\n"
                        << "Rating: " << rating << "\""
                        << ",\"done\":true"
                        << ",\"tests\":" << tests
                        << ",\"passed\":" << passed
                        << ",\"failed\":" << failed
                        << ",\"rate\":" << rate
                        << ",\"rating\":\"" << rating << "\""
                        << ",\"avgBer\":" << avg_ber
                        << ",\"progress\":100"
                        << "}";
                    send_sse(client, sse.str());
                    
                    // Store for export
                    last_results_.total_tests = std::stoi(tests);
                    last_results_.total_passed = std::stoi(passed);
                    last_results_.rating = rating;
                }
                else if (type == "info") {
                    std::string message = extract_json_value(line, "message");
                    send_sse(client, "{\"output\":\"" + json_escape(message) + "\",\"type\":\"info\"}");
                }
                else if (type == "error") {
                    std::string message = extract_json_value(line, "message");
                    send_sse(client, "{\"output\":\"ERROR: " + json_escape(message) + "\",\"type\":\"error\"}");
                }
                else if (type == "start") {
                    std::string backend = extract_json_value(line, "backend");
                    std::string version = extract_json_value(line, "version");
                    std::string build = extract_json_value(line, "build");
                    std::string commit = extract_json_value(line, "commit");
                    std::string branch = extract_json_value(line, "branch");
                    
                    // Build version header string
                    std::ostringstream ver_header;
                    ver_header << "M110A Modem v" << version;
                    if (!branch.empty()) ver_header << " (" << branch << " branch";
                    if (!build.empty()) ver_header << ", build " << build;
                    if (!commit.empty()) ver_header << ", commit " << commit;
                    if (!branch.empty()) ver_header << ")";
                    
                    send_sse(client, "{\"output\":\"" + json_escape(ver_header.str()) + "\",\"type\":\"version\"}");
                    send_sse(client, "{\"output\":\"Backend: " + json_escape(backend) + "\",\"type\":\"header\"}");
                }
            }
        }
        
        int result = _pclose(pipe);
        
        if (stop_test_) {
            send_sse(client, "{\"output\":\"\\n=== TEST STOPPED BY USER ===\",\"type\":\"warning\",\"done\":true}");
        }
#else
        send_sse(client, "{\"output\":\"Linux not supported yet\",\"done\":true}");
#endif
    }
    
    // Simple JSON value extractor (no external dependency)
    std::string extract_json_value(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos += search.length();
        
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        if (pos >= json.size()) return "";
        
        if (json[pos] == '"') {
            // String value
            pos++;
            size_t end = json.find('"', pos);
            if (end == std::string::npos) return "";
            return json.substr(pos, end - pos);
        } else {
            // Number or other value
            size_t end = pos;
            while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') {
                end++;
            }
            return json.substr(pos, end - pos);
        }
    }
    
    // ================================================================
    // Run interop test - Uses JSON output from interop_test.exe --json
    // ================================================================
    void handle_run_interop(SOCKET client) {
        send_sse_headers(client);
        stop_test_ = false;
        
        // Find interop_test.exe - all executables are in release/bin/ together
        std::string test_exe = exe_dir_ + "interop_test.exe";
        
        if (!fs::exists(test_exe)) {
            send_sse(client, "{\"output\":\"ERROR: interop_test.exe not found\",\"done\":true}");
            return;
        }
        
        std::ostringstream cmd;
        cmd << "\"" << test_exe << "\" --json 2>NUL";
        
        send_sse(client, "{\"output\":\"Executing: " + json_escape(cmd.str()) + "\",\"type\":\"header\"}");
        
#ifdef _WIN32
        FILE* pipe = _popen(cmd.str().c_str(), "r");
        if (!pipe) {
            send_sse(client, "{\"output\":\"ERROR: Could not start interop_test.exe\",\"done\":true}");
            return;
        }
        
        int test_num = 0;
        int total_tests = 36;
        
        char buffer[2048];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr && !stop_test_) {
            std::string line(buffer);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            if (line.empty() || line[0] != '{') continue;
            
            std::string event = extract_json_value(line, "event");
            
            if (event == "start") {
                std::string total = extract_json_value(line, "total_tests");
                std::string msg_size = extract_json_value(line, "message_size");
                if (!total.empty()) total_tests = std::stoi(total);
                send_sse(client, "{\"output\":\"Starting " + total + " interop tests (" + msg_size + " byte message)\",\"type\":\"header\",\"total\":" + total + "}");
            }
            else if (event == "result") {
                test_num++;
                std::string mode = extract_json_value(line, "mode");
                std::string pn_brain = extract_json_value(line, "pn_brain");
                std::string brain_pn = extract_json_value(line, "brain_pn");
                std::string auto_detect = extract_json_value(line, "auto");
                std::string detected_pn = extract_json_value(line, "detected_pn");
                std::string detected_brain = extract_json_value(line, "detected_brain");
                std::string ber_pn_brain = extract_json_value(line, "ber_pn_brain");
                std::string ber_brain_pn_set = extract_json_value(line, "ber_brain_pn_set");
                std::string ber_brain_pn_auto = extract_json_value(line, "ber_brain_pn_auto");
                std::string decoded_brain_pn_set = extract_json_value(line, "decoded_brain_pn_set");
                std::string expected = extract_json_value(line, "expected");
                std::string error_pn_brain = extract_json_value(line, "error_pn_brain");
                std::string error_brain_pn_set = extract_json_value(line, "error_brain_pn_set");
                
                int progress = (test_num * 100) / total_tests;
                
                // Build detailed output
                std::ostringstream detail;
                detail << mode << ": ";
                detail << "PN→Brain=" << (pn_brain == "true" ? "PASS" : "FAIL");
                if (!detected_brain.empty() && detected_brain != "---") {
                    detail << "(" << detected_brain << ")";
                }
                detail << " Brain→PN=" << (brain_pn == "true" ? "PASS" : "FAIL");
                if (brain_pn != "true" && !decoded_brain_pn_set.empty()) {
                    detail << "(" << decoded_brain_pn_set << "/" << expected << "B)";
                }
                detail << " Auto=" << (auto_detect == "true" ? "PASS" : "FAIL");
                if (!detected_pn.empty() && detected_pn != "---") {
                    detail << "(" << detected_pn << ")";
                }
                
                std::ostringstream sse;
                sse << "{\"output\":\"" << json_escape(detail.str()) << "\""
                    << ",\"type\":\"interop_result\""
                    << ",\"mode\":\"" << mode << "\""
                    << ",\"pn_brain\":" << pn_brain
                    << ",\"brain_pn\":" << brain_pn
                    << ",\"auto\":" << auto_detect
                    << ",\"detected_pn\":\"" << detected_pn << "\""
                    << ",\"detected_brain\":\"" << detected_brain << "\""
                    << ",\"progress\":" << progress
                    << ",\"currentTest\":\"" << mode << "\""
                    << "}";
                send_sse(client, sse.str());
            }
            else if (event == "complete") {
                std::string passed = extract_json_value(line, "passed");
                std::string total = extract_json_value(line, "total");
                std::string elapsed = extract_json_value(line, "elapsed");
                
                std::ostringstream sse;
                sse << "{\"output\":\"\\n=== INTEROP TEST COMPLETE ===\\n"
                    << "Passed: " << passed << "/" << total << "\\n"
                    << "Time: " << elapsed << "s\""
                    << ",\"done\":true"
                    << ",\"passed\":" << passed
                    << ",\"total\":" << total
                    << ",\"elapsed\":" << elapsed
                    << ",\"progress\":100"
                    << "}";
                send_sse(client, sse.str());
            }
        }
        
        _pclose(pipe);
        
        if (stop_test_) {
            send_sse(client, "{\"output\":\"\\n=== TEST STOPPED BY USER ===\",\"type\":\"warning\",\"done\":true}");
        }
#else
        send_sse(client, "{\"output\":\"Linux not supported yet\",\"done\":true}");
#endif
    }
    
    // Reports
    void handle_list_reports(SOCKET client) {
        std::vector<std::string> report_dirs = {
            exe_dir_ + "reports",
            exe_dir_ + "/../reports",
            exe_dir_ + "/../test/reports"
        };
        
        std::ostringstream json;
        json << "{\"reports\":[";
        
        bool first = true;
        for (const auto& dir : report_dirs) {
            if (!fs::exists(dir)) continue;
            
            try {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.path().extension() == ".md") {
                        if (!first) json << ",";
                        first = false;
                        
                        auto ftime = fs::last_write_time(entry);
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                        
                        char date_buf[64];
                        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                        
                        auto size = fs::file_size(entry);
                        std::string size_str = size < 1024 ? std::to_string(size) + " B" :
                                               size < 1024*1024 ? std::to_string(size/1024) + " KB" :
                                               std::to_string(size/(1024*1024)) + " MB";
                        
                        json << "{\"name\":\"" << entry.path().filename().string() << "\","
                             << "\"date\":\"" << date_buf << "\","
                             << "\"size\":\"" << size_str << "\"}";
                    }
                }
            } catch (...) {}
        }
        
        json << "]}";
        send_json(client, json.str());
    }
    
    void handle_view_report(SOCKET client, const std::string& path) {
        std::string filename = url_decode(path.substr(8));
        
        std::vector<std::string> report_dirs = {
            exe_dir_ + "reports",
            exe_dir_ + "/../reports",
            exe_dir_ + "/../test/reports"
        };
        
        for (const auto& dir : report_dirs) {
            fs::path report_path = fs::path(dir) / filename;
            if (fs::exists(report_path)) {
                std::ifstream file(report_path);
                if (file) {
                    std::string content((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                    send_response(client, "text/markdown; charset=utf-8", content);
                    return;
                }
            }
        }
        
        send_404(client);
    }
    
    void handle_export(SOCKET client, const std::string& path) {
        if (path == "/export-report") {
            std::ostringstream md;
            md << "# M110A Test Report\n\n";
            md << "## Results\n\n";
            md << "- Total Tests: " << last_results_.total_tests << "\n";
            md << "- Passed: " << last_results_.total_passed << "\n";
            md << "- Failed: " << (last_results_.total_tests - last_results_.total_passed) << "\n";
            md << "- Rating: " << last_results_.rating << "\n";
            send_response(client, "text/markdown", md.str());
        }
        else if (path == "/export-csv") {
            std::ostringstream csv;
            csv << "Category,Passed,Failed,Total,Rate\n";
            csv << "Total," << last_results_.total_passed << "," 
                << (last_results_.total_tests - last_results_.total_passed) << ","
                << last_results_.total_tests << ","
                << (last_results_.total_tests > 0 ? 100.0 * last_results_.total_passed / last_results_.total_tests : 0) << "\n";
            send_response(client, "text/csv", csv.str());
        }
        else {
            std::ostringstream json;
            json << "{\"total\":" << last_results_.total_tests
                 << ",\"passed\":" << last_results_.total_passed
                 << ",\"failed\":" << (last_results_.total_tests - last_results_.total_passed)
                 << ",\"rating\":\"" << last_results_.rating << "\"}";
            send_json(client, json.str());
        }
    }
    
    int port_;
    std::string exe_dir_;
    SOCKET server_sock_;
    bool running_;
    std::atomic<bool> stop_test_{false};
    
    BrainClient brain_client_;
    PnClient pn_client_;
    TestResults last_results_;
};

} // namespace test_gui
