/**
 * @file exhaustive_server_test.cpp
 * @brief Exhaustive Modem Test via Server Interface
 * 
 * Runs comprehensive tests across all modes, SNR levels, and channel conditions
 * through the MS-DMT server interface. This validates the complete TX → PCM → RX
 * path under various simulated HF channel conditions.
 * 
 * Usage:
 *   exhaustive_server_test.exe [options]
 * 
 * Options:
 *   --duration N    Test duration in minutes (default: 3)
 *   --host IP       Server IP address (default: 127.0.0.1)
 *   --port N        Control port (default: 4999)
 *   --report FILE   Output report file (default: auto-generated)
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <random>

// Version info
#include "../api/version.h"

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

using namespace std::chrono;

// ============================================================
// Test Statistics
// ============================================================

struct TestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
    double total_ber = 0.0;
    int ber_tests = 0;
    
    void record(bool success, double ber = -1.0) {
        total++;
        if (success) passed++;
        else failed++;
        if (ber >= 0) {
            total_ber += ber;
            ber_tests++;
        }
    }
    
    double avg_ber() const { return ber_tests > 0 ? total_ber / ber_tests : 0.0; }
    double pass_rate() const { return total > 0 ? 100.0 * passed / total : 0.0; }
};

// Stats grouped by channel condition
std::map<std::string, TestStats> channel_stats;

// Stats grouped by mode
std::map<std::string, TestStats> mode_stats;

// Stats for mode × channel combinations
std::map<std::string, std::map<std::string, TestStats>> mode_channel_stats;

// ============================================================
// Socket Utilities
// ============================================================

class ServerConnection {
public:
    socket_t control_sock = INVALID_SOCK;
    socket_t data_sock = INVALID_SOCK;
    std::string host;
    int control_port;
    int data_port;
    
    std::string last_pcm_file;
    
    ServerConnection(const std::string& h = "127.0.0.1", int cp = 4999, int dp = 4998)
        : host(h), control_port(cp), data_port(dp) {}
    
    ~ServerConnection() {
        disconnect();
    }
    
    bool connect_to_server() {
        control_sock = connect_socket(control_port);
        if (control_sock == INVALID_SOCK) return false;
        
        data_sock = connect_socket(data_port);
        if (data_sock == INVALID_SOCK) {
            close_socket(control_sock);
            control_sock = INVALID_SOCK;
            return false;
        }
        
        // Wait for MODEM READY
        std::string ready = receive_line(control_sock, 2000);
        return ready.find("MODEM READY") != std::string::npos;
    }
    
    void disconnect() {
        if (control_sock != INVALID_SOCK) {
            close_socket(control_sock);
            control_sock = INVALID_SOCK;
        }
        if (data_sock != INVALID_SOCK) {
            close_socket(data_sock);
            data_sock = INVALID_SOCK;
        }
    }
    
    std::string send_command(const std::string& cmd, int timeout_ms = 1000) {
        std::string full_cmd = cmd + "\n";
        send(control_sock, full_cmd.c_str(), (int)full_cmd.length(), 0);
        
        // Collect all responses until we get OK or ERROR
        std::string response;
        auto start = steady_clock::now();
        
        while (duration_cast<milliseconds>(steady_clock::now() - start).count() < timeout_ms) {
            std::string line = receive_line(control_sock, 200);
            if (!line.empty()) {
                response += line + "\n";
                
                // Check for terminal responses
                if (line.find("OK:") == 0 || line.find("ERROR:") == 0) {
                    // Check if we got a PCM filename (look for FILE: prefix)
                    size_t file_pos = line.find("FILE:");
                    if (file_pos != std::string::npos) {
                        // Extract filename after "FILE:"
                        last_pcm_file = line.substr(file_pos + 5);
                        // Trim any trailing whitespace/newlines
                        while (!last_pcm_file.empty() && 
                               (last_pcm_file.back() == ' ' || last_pcm_file.back() == '\n' || 
                                last_pcm_file.back() == '\r')) {
                            last_pcm_file.pop_back();
                        }
                    }
                    break;
                }
            }
        }
        
        return response;
    }
    
    // Wait for control port response containing specific text
    bool wait_for_response(const std::string& text, int timeout_ms = 5000) {
        auto start = steady_clock::now();
        while (duration_cast<milliseconds>(steady_clock::now() - start).count() < timeout_ms) {
            std::string line = receive_line(control_sock, 500);
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    
    bool send_data(const std::vector<uint8_t>& data) {
        int sent = send(data_sock, reinterpret_cast<const char*>(data.data()), (int)data.size(), 0);
        return sent == (int)data.size();
    }
    
    std::vector<uint8_t> receive_data(int timeout_ms = 2000) {
        // Set receive timeout
#ifdef _WIN32
        DWORD tv = timeout_ms;
        setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        
        std::vector<uint8_t> result(4096);
        int received = recv(data_sock, reinterpret_cast<char*>(result.data()), 4096, 0);
        if (received > 0) {
            result.resize(received);
            return result;
        }
        return {};
    }
    
    // Check if connection is healthy
    bool is_connected() {
        if (control_sock == INVALID_SOCK || data_sock == INVALID_SOCK) {
            return false;
        }
        // Try a simple query
        std::string resp = send_command("CMD:GET MODE", 500);
        return resp.find("OK:") != std::string::npos || resp.find("MODE:") != std::string::npos;
    }
    
    // Reconnect if needed
    bool ensure_connected() {
        if (is_connected()) return true;
        
        std::cout << "\n[RECONNECT] Connection lost, reconnecting..." << std::flush;
        disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        if (connect_to_server()) {
            std::cout << " OK\n" << std::flush;
            return true;
        }
        std::cout << " FAILED\n" << std::flush;
        return false;
    }
    
private:
    socket_t connect_socket(int port) {
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCK) return INVALID_SOCK;
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close_socket(sock);
            return INVALID_SOCK;
        }
        
        return sock;
    }
    
    std::string receive_line(socket_t sock, int timeout_ms) {
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
};

// ============================================================
// BER Calculation
// ============================================================

double calculate_ber(const std::vector<uint8_t>& tx, const std::vector<uint8_t>& rx) {
    if (tx.empty() || rx.empty()) return 1.0;
    
    size_t min_len = std::min(tx.size(), rx.size());
    int bit_errors = 0;
    int total_bits = 0;
    
    for (size_t i = 0; i < min_len; i++) {
        uint8_t diff = tx[i] ^ rx[i];
        for (int b = 0; b < 8; b++) {
            if (diff & (1 << b)) bit_errors++;
        }
        total_bits += 8;
    }
    
    // Count length mismatch as errors
    size_t len_diff = std::max(tx.size(), rx.size()) - min_len;
    bit_errors += (int)(len_diff * 8);
    total_bits += (int)(len_diff * 8);
    
    return total_bits > 0 ? (double)bit_errors / total_bits : 1.0;
}

// ============================================================
// Test Modes and Conditions
// ============================================================

struct ModeInfo {
    std::string cmd;    // Command string
    std::string name;   // Display name
    int tx_time_ms;     // Approximate TX time for 54 bytes
};

std::vector<ModeInfo> get_modes() {
    // TX times based on interleaver block sizes and data rates
    // SHORT: 0.6s blocks, LONG: 4.8s blocks (8x longer)
    // Time = (interleaver_block_bits / data_rate_bps) + preamble overhead
    return {
        {"75S",   "75S",   10000},   // Very slow data rate
        {"75L",   "75L",   80000},   // 8x longer interleaver
        {"150S",  "150S",  5000},    // 720 bits / 150 bps + overhead
        {"150L",  "150L",  40000},   // 5760 bits / 150 bps + overhead
        {"300S",  "300S",  3000},    // 720 bits / 300 bps + overhead
        {"300L",  "300L",  20000},   // 5760 bits / 300 bps + overhead
        {"600S",  "600S",  2000},    // 720 bits / 600 bps + overhead
        {"600L",  "600L",  10000},   // 5760 bits / 600 bps + overhead
        {"1200S", "1200S", 2000},    // 1440 bits / 1200 bps + overhead
        {"1200L", "1200L", 10000},   // 11520 bits / 1200 bps + overhead
        {"2400S", "2400S", 2000},    // 2880 bits / 2400 bps + overhead
        {"2400L", "2400L", 10000},   // 23040 bits / 2400 bps + overhead
    };
}

struct ChannelCondition {
    std::string name;
    std::string setup_cmd;  // Empty for clean, otherwise the command
    float expected_ber_threshold;
};

std::vector<ChannelCondition> get_channel_conditions() {
    return {
        {"clean",           "",                          0.0},
        {"awgn_30db",       "CMD:CHANNEL AWGN:30",       0.001},
        {"awgn_25db",       "CMD:CHANNEL AWGN:25",       0.005},
        {"awgn_20db",       "CMD:CHANNEL AWGN:20",       0.01},
        {"awgn_15db",       "CMD:CHANNEL AWGN:15",       0.05},
        {"mp_24samp",       "CMD:CHANNEL MULTIPATH:24",  0.05},
        {"mp_48samp",       "CMD:CHANNEL MULTIPATH:48",  0.05},
        {"foff_1hz",        "CMD:CHANNEL FREQOFFSET:1",  0.05},
        {"foff_5hz",        "CMD:CHANNEL FREQOFFSET:5",  0.10},
        {"moderate_hf",     "CMD:CHANNEL PRESET:MODERATE", 0.05},
        {"poor_hf",         "CMD:CHANNEL PRESET:POOR",   0.10},
    };
}

// ============================================================
// Single Test Execution
// ============================================================

bool run_single_test(ServerConnection& conn, 
                     const ModeInfo& mode,
                     const ChannelCondition& channel,
                     const std::vector<uint8_t>& test_data,
                     double& ber_out) {
    ber_out = 1.0;
    
    // 1. Set mode
    std::string resp = conn.send_command("CMD:DATA RATE:" + mode.cmd);
    if (resp.find("OK:") == std::string::npos) {
        return false;
    }
    
    // 2. Enable recording
    conn.send_command("CMD:RECORD TX:ON");
    conn.send_command("CMD:RECORD PREFIX:" + mode.name + "_" + channel.name);
    
    // 3. Send test data
    conn.send_data(test_data);
    
    // 4. Trigger TX
    conn.last_pcm_file.clear();
    resp = conn.send_command("CMD:SENDBUFFER", mode.tx_time_ms + 2000);
    if (resp.find("OK:") == std::string::npos) {
        return false;
    }
    
    // Wait a bit for file to be written
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 5. Get PCM filename
    std::string pcm_file = conn.last_pcm_file;
    if (pcm_file.empty()) {
        return false;
    }
    
    // 6. Configure channel
    conn.send_command("CMD:CHANNEL OFF");
    if (!channel.setup_cmd.empty()) {
        resp = conn.send_command(channel.setup_cmd);
        if (resp.find("OK:") == std::string::npos) {
            return false;
        }
    }
    
    // 7. Inject PCM for decode
    {
        std::string full_cmd = "CMD:RXAUDIOINJECT:" + pcm_file + "\n";
        send(conn.control_sock, full_cmd.c_str(), (int)full_cmd.length(), 0);
        
        bool got_complete = false;
        auto start = steady_clock::now();
        int timeout_ms = mode.tx_time_ms + 5000;
        
        while (duration_cast<milliseconds>(steady_clock::now() - start).count() < timeout_ms) {
#ifdef _WIN32
            DWORD tv = 500;
            setsockopt(conn.control_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
            std::string line;
            char c;
            while (recv(conn.control_sock, &c, 1, 0) == 1) {
                if (c == '\n') break;
                if (c != '\r') line += c;
            }
            
            if (!line.empty()) {
                if (line.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    got_complete = true;
                    break;
                }
                if (line.find("ERROR:") != std::string::npos) {
                    return false;
                }
            }
        }
        
        if (!got_complete) {
            return false;
        }
    }
    
    // 8. Receive decoded data
    std::vector<uint8_t> rx_data = conn.receive_data(2000);
    
    // 9. Calculate BER
    ber_out = calculate_ber(test_data, rx_data);
    
    // 10. Cleanup
    conn.send_command("CMD:CHANNEL OFF");
    
    // Keep last 2 PCM files, delete older ones
    static std::string prev_pcm_file;
    static std::string prev_prev_pcm_file;
    
    if (!prev_prev_pcm_file.empty()) {
        std::remove(prev_prev_pcm_file.c_str());
    }
    prev_prev_pcm_file = prev_pcm_file;
    prev_pcm_file = pcm_file;
    
    return ber_out <= channel.expected_ber_threshold;
}

// ============================================================
// Report Generation
// ============================================================

void generate_report(const std::string& filename, 
                     int duration_sec,
                     int iterations,
                     int total_tests) {
    std::ofstream report(filename);
    if (!report.is_open()) {
        std::cerr << "Cannot create report: " << filename << std::endl;
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&now_time);
    
    // Calculate overall stats
    int grand_total = 0, grand_passed = 0;
    for (const auto& [key, stats] : channel_stats) {
        grand_total += stats.total;
        grand_passed += stats.passed;
    }
    double grand_rate = grand_total > 0 ? 100.0 * grand_passed / grand_total : 0.0;
    
    std::string rating;
    if (grand_rate >= 95.0) rating = "EXCELLENT";
    else if (grand_rate >= 80.0) rating = "GOOD";
    else if (grand_rate >= 60.0) rating = "FAIR";
    else rating = "NEEDS WORK";
    
    report << "# M110A Modem Exhaustive Test Report (Server-Based)\n\n";
    report << "## Test Information\n";
    report << "| Field | Value |\n";
    report << "|-------|-------|\n";
    report << "| **Version** | " << m110a::version_full() << " |\n";
    report << "| **Build** | " << m110a::build_info() << " |\n";
    report << "| **Date** | " << std::put_time(tm, "%B %d, %Y %H:%M") << " |\n";
    report << "| **Duration** | " << duration_sec << " seconds |\n";
    report << "| **Iterations** | " << iterations << " |\n";
    report << "| **Total Tests** | " << total_tests << " |\n";
    report << "| **Rating** | " << rating << " |\n\n";
    
    report << "---\n\n";
    report << "## Summary\n\n";
    report << "| Metric | Value |\n";
    report << "|--------|-------|\n";
    report << "| **Overall Pass Rate** | " << std::fixed << std::setprecision(1) << grand_rate << "% |\n";
    report << "| **Total Passed** | " << grand_passed << " |\n";
    report << "| **Total Failed** | " << (grand_total - grand_passed) << " |\n\n";
    
    report << "---\n\n";
    report << "## Results by Mode\n\n";
    report << "| Mode | Passed | Failed | Total | Pass Rate | Avg BER |\n";
    report << "|------|--------|--------|-------|-----------|--------|\n";
    
    for (const auto& [key, stats] : mode_stats) {
        report << "| " << key
               << " | " << stats.passed
               << " | " << stats.failed
               << " | " << stats.total
               << " | " << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
               << " | " << std::scientific << std::setprecision(2) << stats.avg_ber()
               << " |\n";
    }
    
    report << "\n---\n\n";
    report << "## Results by Channel Condition\n\n";
    report << "| Channel | Passed | Failed | Total | Pass Rate | Avg BER |\n";
    report << "|---------|--------|--------|-------|-----------|--------|\n";
    
    for (const auto& [key, stats] : channel_stats) {
        report << "| " << key
               << " | " << stats.passed
               << " | " << stats.failed
               << " | " << stats.total
               << " | " << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
               << " | " << std::scientific << std::setprecision(2) << stats.avg_ber()
               << " |\n";
    }
    
    report << "\n---\n\n";
    report << "## Mode × Channel Matrix (Pass Rates)\n\n";
    
    // Get all channel names
    std::vector<std::string> channel_names;
    for (const auto& [ch, _] : channel_stats) {
        channel_names.push_back(ch);
    }
    
    // Header row
    report << "| Mode |";
    for (const auto& ch : channel_names) {
        report << " " << ch << " |";
    }
    report << " **Total** |\n";
    
    // Separator
    report << "|------|";
    for (size_t i = 0; i < channel_names.size(); i++) {
        report << ":------:|";
    }
    report << ":------:|\n";
    
    // Data rows
    for (const auto& [mode, ch_map] : mode_channel_stats) {
        report << "| **" << mode << "** |";
        for (const auto& ch : channel_names) {
            auto it = ch_map.find(ch);
            if (it != ch_map.end() && it->second.total > 0) {
                report << " " << std::fixed << std::setprecision(0) << it->second.pass_rate() << "% |";
            } else {
                report << " - |";
            }
        }
        // Mode total
        auto mode_it = mode_stats.find(mode);
        if (mode_it != mode_stats.end()) {
            report << " **" << std::fixed << std::setprecision(0) << mode_it->second.pass_rate() << "%** |";
        } else {
            report << " - |";
        }
        report << "\n";
    }
    
    // Channel totals row
    report << "| **Total** |";
    for (const auto& ch : channel_names) {
        auto it = channel_stats.find(ch);
        if (it != channel_stats.end() && it->second.total > 0) {
            report << " **" << std::fixed << std::setprecision(0) << it->second.pass_rate() << "%** |";
        } else {
            report << " - |";
        }
    }
    report << " **" << std::fixed << std::setprecision(0) << grand_rate << "%** |\n";
    
    report << "\n---\n\n";
    report << "## Test Configuration\n\n";
    report << "### Modes Tested\n";
    report << "75S/L, 150S/L, 300S/L, 600S/L, 1200S/L, 2400S/L\n\n";
    
    report << "### Channel Conditions\n";
    report << "- **Clean**: No impairments\n";
    report << "- **AWGN**: 30, 25, 20, 15 dB SNR\n";
    report << "- **Multipath**: 24, 48 samples delay\n";
    report << "- **Frequency Offset**: 1 Hz, 5 Hz\n";
    report << "- **Presets**: MODERATE_HF, POOR_HF\n\n";
    
    report << "---\n\n";
    report << "*Generated by exhaustive_server_test via MS-DMT interface*\n";
    
    report.close();
    std::cout << "\nReport saved to: " << filename << std::endl;
}

// ============================================================
// Main Test Loop
// ============================================================

int main(int argc, char* argv[]) {
    // Parse arguments
    int duration_minutes = 3;
    std::string host = "127.0.0.1";
    int control_port = 4999;
    std::string report_file;
    std::string mode_filter;  // Empty = all modes, otherwise filter by mode name
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) {
            duration_minutes = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            control_port = std::stoi(argv[++i]);
        } else if (arg == "--report" && i + 1 < argc) {
            report_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_filter = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --duration N    Test duration in minutes (default: 3)\n";
            std::cout << "  --host IP       Server IP (default: 127.0.0.1)\n";
            std::cout << "  --port N        Control port (default: 4999)\n";
            std::cout << "  --report FILE   Output report file\n";
            std::cout << "  --mode MODE     Test only specific mode (e.g., 600S, 1200L, 75S)\n";
            std::cout << "                  Use 'SHORT' for all short modes, 'LONG' for all long modes\n";
            return 0;
        }
    }
    
    // Auto-generate report filename if not specified
    if (report_file.empty()) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&now_time);
        std::ostringstream ss;
        ss << "../docs/test_reports/server_exhaustive_" << std::put_time(tm, "%Y%m%d_%H%M%S") << ".md";
        report_file = ss.str();
    }
    
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    std::cout << "==============================================\n";
    std::cout << "M110A Exhaustive Test (Server-Based)\n";
    std::cout << "==============================================\n";
    std::cout << "Duration: " << duration_minutes << " minutes\n";
    std::cout << "Server: " << host << ":" << control_port << "\n";
    if (!mode_filter.empty()) {
        std::cout << "Mode Filter: " << mode_filter << "\n";
    }
    std::cout << "\n";
    
    // Connect to server
    ServerConnection conn(host, control_port, control_port - 1);
    if (!conn.connect_to_server()) {
        std::cerr << "ERROR: Cannot connect to server at " << host << ":" << control_port << std::endl;
        std::cerr << "Make sure the server is running: m110a_server.exe --testdevices\n";
        return 1;
    }
    
    std::cout << "Connected to server.\n\n";
    
    // Test data
    std::string test_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::vector<uint8_t> test_data(test_msg.begin(), test_msg.end());
    
    // Get test configurations
    auto all_modes = get_modes();
    auto channels = get_channel_conditions();
    
    // Filter modes if specified
    std::vector<ModeInfo> modes;
    for (const auto& m : all_modes) {
        if (mode_filter.empty()) {
            modes.push_back(m);
        } else if (mode_filter == "SHORT" && m.cmd.back() == 'S') {
            modes.push_back(m);
        } else if (mode_filter == "LONG" && m.cmd.back() == 'L') {
            modes.push_back(m);
        } else if (m.cmd == mode_filter || m.name == mode_filter) {
            modes.push_back(m);
        }
    }
    
    if (modes.empty()) {
        std::cerr << "ERROR: No modes match filter '" << mode_filter << "'\n";
        std::cerr << "Valid modes: 75S, 75L, 150S, 150L, 300S, 300L, 600S, 600L, 1200S, 1200L, 2400S, 2400L\n";
        std::cerr << "Special: SHORT (all short), LONG (all long)\n";
        return 1;
    }
    
    // Timing
    auto start_time = steady_clock::now();
    auto end_time = start_time + minutes(duration_minutes);
    
    int iteration = 0;
    int total_tests = 0;
    
    // Main test loop
    while (steady_clock::now() < end_time) {
        iteration++;
        
        // Check connection health at start of each iteration
        if (!conn.ensure_connected()) {
            std::cerr << "\n[ERROR] Cannot reconnect to server, aborting.\n";
            // Generate partial report before exit
            generate_report(report_file, 
                duration_cast<seconds>(steady_clock::now() - start_time).count(),
                iteration, total_tests);
            std::exit(1);
        }
        
        auto now = steady_clock::now();
        auto elapsed = duration_cast<seconds>(now - start_time).count();
        auto remaining = duration_cast<seconds>(end_time - now).count();
        
        std::cout << "\r[" << std::setw(3) << elapsed << "s] Iteration " << iteration
                  << " | Tests: " << total_tests
                  << " | Remaining: " << remaining << "s   " << std::flush;
        
        // Cycle through modes (skip slow modes on some iterations)
        for (const auto& mode : modes) {
            // Skip very slow modes more often
            if ((mode.cmd == "75S" || mode.cmd == "75L") && iteration % 5 != 0) continue;
            if ((mode.cmd == "150L" || mode.cmd == "300L") && iteration % 3 != 0) continue;
            
            // Cycle through channel conditions
            for (const auto& channel : channels) {
                // Skip some channel conditions to save time
                if (iteration % 2 != 0 && 
                    (channel.name == "foff_5hz" || channel.name == "poor_hf")) continue;
                
                // Progress update before each test
                auto now = steady_clock::now();
                auto elapsed = duration_cast<seconds>(now - start_time).count();
                auto remaining = duration_cast<seconds>(end_time - now).count();
                
                // Calculate pass rate so far
                int total_passed = 0, total_run = 0;
                for (const auto& [k, s] : mode_stats) {
                    total_passed += s.passed;
                    total_run += s.total;
                }
                double rate = total_run > 0 ? 100.0 * total_passed / total_run : 0.0;
                
                std::cout << "\r[" << std::setw(3) << elapsed << "s] "
                          << std::setw(6) << mode.name << " + " << std::setw(10) << channel.name
                          << " | Tests: " << std::setw(4) << total_tests
                          << " | Pass: " << std::fixed << std::setprecision(1) << rate << "%"
                          << " | " << remaining << "s left   " << std::flush;
                
                double ber;
                bool passed = run_single_test(conn, mode, channel, test_data, ber);
                
                // Track consecutive failures to detect connection issues
                static int consecutive_failures = 0;
                static int reconnect_attempts = 0;
                if (passed) {
                    consecutive_failures = 0;
                    reconnect_attempts = 0;
                } else {
                    consecutive_failures++;
                    if (consecutive_failures >= 10) {
                        reconnect_attempts++;
                        std::cout << "\n[WARNING] 10 consecutive failures (attempt " << reconnect_attempts << "/3)\n";
                        
                        if (reconnect_attempts >= 3) {
                            std::cerr << "[ERROR] Too many consecutive failures, aborting.\n";
                            // Generate partial report before exit
                            generate_report(report_file, 
                                duration_cast<seconds>(steady_clock::now() - start_time).count(),
                                iteration, total_tests);
                            std::exit(1);
                        }
                        
                        // Try reconnecting
                        conn.disconnect();
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        if (!conn.connect_to_server()) {
                            std::cerr << "[ERROR] Cannot reconnect, aborting.\n";
                            generate_report(report_file, 
                                duration_cast<seconds>(steady_clock::now() - start_time).count(),
                                iteration, total_tests);
                            std::exit(1);
                        }
                        consecutive_failures = 0;
                    }
                }
                
                // Record stats - by channel, by mode, and by combination
                channel_stats[channel.name].record(passed, ber);
                mode_stats[mode.name].record(passed, ber);
                mode_channel_stats[mode.name][channel.name].record(passed, ber);
                total_tests++;
                
                // Check time
                if (steady_clock::now() >= end_time) break;
            }
            
            if (steady_clock::now() >= end_time) break;
        }
    }
    
    auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
    
    // Print results
    std::cout << "\n\n";
    std::cout << "==============================================\n";
    std::cout << "EXHAUSTIVE TEST RESULTS (Server-Based)\n";
    std::cout << "==============================================\n";
    std::cout << "Duration: " << total_elapsed << " seconds\n";
    std::cout << "Iterations: " << iteration << "\n";
    std::cout << "Total Tests: " << total_tests << "\n\n";
    
    // Results by Mode
    std::cout << "--- BY MODE ---\n";
    std::cout << std::left << std::setw(12) << "Mode"
              << std::right << std::setw(8) << "Passed"
              << std::setw(8) << "Failed"
              << std::setw(8) << "Total"
              << std::setw(10) << "Rate"
              << std::setw(12) << "Avg BER"
              << "\n";
    std::cout << std::string(58, '-') << "\n";
    
    for (const auto& [key, stats] : mode_stats) {
        std::cout << std::left << std::setw(12) << key
                  << std::right << std::setw(8) << stats.passed
                  << std::setw(8) << stats.failed
                  << std::setw(8) << stats.total
                  << std::setw(9) << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << stats.avg_ber()
                  << "\n";
    }
    
    // Results by Channel
    std::cout << "\n--- BY CHANNEL ---\n";
    std::cout << std::left << std::setw(20) << "Channel"
              << std::right << std::setw(8) << "Passed"
              << std::setw(8) << "Failed"
              << std::setw(8) << "Total"
              << std::setw(10) << "Rate"
              << std::setw(12) << "Avg BER"
              << "\n";
    std::cout << std::string(66, '-') << "\n";
    
    int grand_total = 0, grand_passed = 0;
    
    for (const auto& [key, stats] : channel_stats) {
        std::cout << std::left << std::setw(20) << key
                  << std::right << std::setw(8) << stats.passed
                  << std::setw(8) << stats.failed
                  << std::setw(8) << stats.total
                  << std::setw(9) << std::fixed << std::setprecision(1) << stats.pass_rate() << "%"
                  << std::setw(12) << std::scientific << std::setprecision(2) << stats.avg_ber()
                  << "\n";
        
        grand_total += stats.total;
        grand_passed += stats.passed;
    }
    
    // Mode × Channel Matrix
    std::cout << "\n--- MODE × CHANNEL MATRIX (Pass Rates) ---\n\n";
    
    // Get all channel names that were tested
    std::vector<std::string> channel_names;
    for (const auto& [ch, _] : channel_stats) {
        channel_names.push_back(ch);
    }
    
    // Header row - abbreviated channel names
    std::cout << std::left << std::setw(8) << "Mode";
    for (const auto& ch : channel_names) {
        std::string abbrev = ch.length() > 8 ? ch.substr(0, 8) : ch;
        std::cout << std::right << std::setw(9) << abbrev;
    }
    std::cout << std::right << std::setw(9) << "TOTAL" << "\n";
    std::cout << std::string(8 + 9 * (channel_names.size() + 1), '-') << "\n";
    
    // Data rows
    for (const auto& [mode, ch_map] : mode_channel_stats) {
        std::cout << std::left << std::setw(8) << mode;
        for (const auto& ch : channel_names) {
            auto it = ch_map.find(ch);
            if (it != ch_map.end() && it->second.total > 0) {
                std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) 
                          << it->second.pass_rate() << "%";
            } else {
                std::cout << std::right << std::setw(9) << "-";
            }
        }
        // Mode total
        auto mode_it = mode_stats.find(mode);
        if (mode_it != mode_stats.end()) {
            std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) 
                      << mode_it->second.pass_rate() << "%";
        }
        std::cout << "\n";
    }
    
    // Channel totals row
    std::cout << std::left << std::setw(8) << "TOTAL";
    for (const auto& ch : channel_names) {
        auto it = channel_stats.find(ch);
        if (it != channel_stats.end() && it->second.total > 0) {
            std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0)
                      << it->second.pass_rate() << "%";
        } else {
            std::cout << std::right << std::setw(9) << "-";
        }
    }
    double grand_rate = grand_total > 0 ? 100.0 * grand_passed / grand_total : 0.0;
    std::cout << std::right << std::setw(8) << std::fixed << std::setprecision(0) << grand_rate << "%\n";

    std::cout << "\n";
    std::cout << std::string(66, '-') << "\n";
    std::cout << std::left << std::setw(20) << "OVERALL"
              << std::right << std::setw(8) << grand_passed
              << std::setw(8) << (grand_total - grand_passed)
              << std::setw(8) << grand_total
              << std::setw(9) << std::fixed << std::setprecision(1) << grand_rate << "%"
              << "\n";
    
    std::cout << "\n";
    if (grand_rate >= 95.0) {
        std::cout << "*** EXCELLENT: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 80.0) {
        std::cout << "*** GOOD: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else if (grand_rate >= 60.0) {
        std::cout << "*** FAIR: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    } else {
        std::cout << "*** NEEDS WORK: " << std::fixed << std::setprecision(1) << grand_rate << "% pass rate ***\n";
    }
    
    // Generate report
    generate_report(report_file, (int)total_elapsed, iteration, total_tests);
    
    // Cleanup
    conn.disconnect();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    return grand_rate >= 80.0 ? 0 : 1;
}
