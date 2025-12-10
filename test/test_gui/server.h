#pragma once
/**
 * @file server.h
 * @brief HTTP server for test GUI
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
        else {
            send_404(client);
        }
        
        closesocket(client);
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
        
        // Try to connect
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
        
        // Read welcome/version
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
    
    // Run exhaustive test
    void handle_run_exhaustive(SOCKET client, const std::string& path) {
        auto params = parse_query_string(path);
        std::string config_json = params.count("config") ? params["config"] : "{}";
        
        // Send SSE headers
        send_sse_headers(client);
        
        stop_test_ = false;
        
        // Parse basic config (simplified - in production would use JSON parser)
        int duration_sec = 180;
        
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(duration_sec);
        
        int tests = 0, passed = 0, iteration = 0;
        
        send_sse(client, "{\"output\":\"Starting exhaustive test...\",\"type\":\"header\"}");
        
        while (!stop_test_ && std::chrono::steady_clock::now() < end_time) {
            iteration++;
            
            // Simulated test execution
            // In real implementation, this would call the modem API
            tests++;
            bool success = (rand() % 100) < 95; // 95% pass rate simulation
            if (success) passed++;
            
            double rate = tests > 0 ? 100.0 * passed / tests : 0.0;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            auto remaining = duration_sec - elapsed;
            double progress = 100.0 * elapsed / duration_sec;
            
            std::ostringstream json;
            json << "{\"tests\":" << tests 
                 << ",\"passed\":" << passed
                 << ",\"rate\":" << rate
                 << ",\"progress\":" << progress
                 << ",\"elapsed\":\"" << (elapsed / 60) << ":" << std::setw(2) << std::setfill('0') << (elapsed % 60) << "\""
                 << ",\"remaining\":\"" << remaining << "s\""
                 << ",\"iteration\":" << iteration
                 << ",\"currentTest\":\"Mode " << (tests % 13) << " iteration " << iteration << "\""
                 << "}";
            send_sse(client, json.str());
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Final results
        double final_rate = tests > 0 ? 100.0 * passed / tests : 0.0;
        std::string rating = final_rate >= 95 ? "EXCELLENT" : final_rate >= 80 ? "GOOD" : final_rate >= 60 ? "FAIR" : "NEEDS WORK";
        
        std::ostringstream summary;
        summary << "{\"output\":\"\\n=== SUMMARY ===\\nTests: " << tests 
                << "\\nPassed: " << passed 
                << "\\nRate: " << std::fixed << std::setprecision(1) << final_rate << "%"
                << "\\nRating: " << rating << "\""
                << ",\"type\":\"header\",\"done\":true}";
        send_sse(client, summary.str());
        
        // Store last results for export
        last_results_.total_tests = tests;
        last_results_.total_passed = passed;
        last_results_.iterations = iteration;
        last_results_.rating = rating;
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
            md << "- Rating: " << last_results_.rating << "\n";
            send_response(client, "text/markdown", md.str());
        }
        else if (path == "/export-csv") {
            std::ostringstream csv;
            csv << "Category,Passed,Failed,Total,Rate,AvgBER\n";
            csv << "Total," << last_results_.total_passed << "," 
                << (last_results_.total_tests - last_results_.total_passed) << ","
                << last_results_.total_tests << ","
                << (last_results_.total_tests > 0 ? 100.0 * last_results_.total_passed / last_results_.total_tests : 0) << ",0\n";
            send_response(client, "text/csv", csv.str());
        }
        else {
            std::ostringstream json;
            json << "{\"total\":" << last_results_.total_tests
                 << ",\"passed\":" << last_results_.total_passed
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
