// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file phoenix_tcp_server.h
 * @brief Phoenix Nest M110A TCP server using robust tcp_server_base
 * 
 * This server wraps the Phoenix Nest modem API (m110a::api) with:
 * - TCP control port (4999) for commands
 * - TCP data port (4998) for TX/RX data
 * - PCM file I/O for testing
 * 
 * Uses tcp_server_base for robust socket handling, fixing persistent
 * connection issues in the original tcp_server.cpp implementation.
 */

#ifndef PHOENIX_TCP_SERVER_H
#define PHOENIX_TCP_SERVER_H

#include "tcp_server_base.h"
#include "api/modem.h"
#include "api/version.h"

#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace phoenix_server {

// Default ports (different from Brain Core)
constexpr uint16_t DEFAULT_CONTROL_PORT = 4999;
constexpr uint16_t DEFAULT_DATA_PORT = 4998;

/**
 * @brief Phoenix Nest M110A TCP Server
 * 
 * Implements the same command protocol as the original m110a_server:
 * - CMD:DATA RATE:<mode> - Set TX/RX mode
 * - CMD:SENDBUFFER - Transmit buffered data
 * - CMD:RXAUDIOINJECT:<path> - Inject RX PCM file
 * - CMD:SET EQUALIZER:<type> - Set equalizer
 * - CMD:RECORD TX:ON/OFF - Enable/disable PCM recording
 * - CMD:RECORD PREFIX:<prefix> - Set PCM filename prefix
 * - CMD:KILL TX - Cancel transmission
 */
class PhoenixServer : public tcp_base::ServerBase {
public:
    static constexpr int SAMPLE_RATE = 48000;
    
    PhoenixServer()
        : tx_buffer_()
        , pcm_output_dir_("./tx_pcm_out/")
        , pcm_prefix_()
        , record_tx_(true)
        , current_mode_(m110a::api::Mode::M600_SHORT)
        , current_equalizer_(m110a::api::Equalizer::DFE)
    {
        set_ports(DEFAULT_CONTROL_PORT, DEFAULT_DATA_PORT);
    }
    
    void set_pcm_output_dir(const std::string& dir) { pcm_output_dir_ = dir; }
    void set_control_port(uint16_t port) { set_ports(port, DEFAULT_DATA_PORT); }
    void set_data_port(uint16_t port) { set_ports(DEFAULT_CONTROL_PORT, port); }
    void configure_ports(uint16_t control, uint16_t data) { set_ports(control, data); }
    
    void set_quiet(bool q) { quiet_ = q; }
    
protected:
    std::string get_ready_message() override {
        return std::string("MODEM READY");
    }
    
    void on_control_connected() override {
        if (!quiet_) {
            std::cout << "[CTRL] Client connected\n";
        }
    }
    
    void on_data_connected() override {
        if (!quiet_) {
            std::cout << "[DATA] Client connected\n";
        }
    }
    
    void on_control_disconnected() override {
        if (!quiet_) {
            std::cout << "[CTRL] Client disconnected\n";
        }
    }
    
    void on_data_disconnected() override {
        if (!quiet_) {
            std::cout << "[DATA] Client disconnected\n";
        }
    }
    
    void on_command(const std::string& cmd) override {
        if (!quiet_) {
            std::cout << "[CMD] " << cmd << std::endl;
        }
        
        // Trim whitespace
        std::string c = cmd;
        while (!c.empty() && (c.back() == '\r' || c.back() == '\n' || c.back() == ' '))
            c.pop_back();
        
        // Parse command - must start with "CMD:"
        if (!starts_with(c, "CMD:")) {
            send_control("ERROR:INVALID:Must start with CMD:\n");
            return;
        }
        
        std::string cmd_body = c.substr(4);  // Remove "CMD:" prefix
        
        // Handle various commands
        if (starts_with(cmd_body, "DATA RATE:")) {
            std::string rate = cmd_body.substr(10);
            current_mode_ = string_to_mode(rate);
            send_control("OK:DATA RATE:" + mode_string(current_mode_));
        }
        else if (cmd_body == "SENDBUFFER") {
            do_transmit();
        }
        else if (cmd_body == "KILL TX") {
            tx_buffer_.clear();
            send_control("OK:KILL TX");
        }
        else if (cmd_body == "RESET MDM") {
            tx_buffer_.clear();
            send_control("OK:RESET");
        }
        else if (cmd_body == "RECORD TX:ON" || cmd_body == "RECORD TX: ON") {
            record_tx_ = true;
            send_control("OK:RECORD TX:ON");
        }
        else if (cmd_body == "RECORD TX:OFF" || cmd_body == "RECORD TX: OFF") {
            record_tx_ = false;
            send_control("OK:RECORD TX:OFF");
        }
        else if (starts_with(cmd_body, "RECORD PREFIX:")) {
            pcm_prefix_ = cmd_body.substr(14);
            send_control("OK:RECORD PREFIX:" + pcm_prefix_);
        }
        else if (starts_with(cmd_body, "RXAUDIOINJECT:")) {
            std::string path = cmd_body.substr(14);
            do_rx_inject(path);
        }
        else if (starts_with(cmd_body, "SET EQUALIZER:") || starts_with(cmd_body, "EQUALIZER:")) {
            std::string eq_str = starts_with(cmd_body, "SET EQUALIZER:") 
                ? cmd_body.substr(14) 
                : cmd_body.substr(10);
            do_set_equalizer(eq_str);
        }
        else if (cmd_body == "QUERY:STATUS") {
            std::string status = "STATUS:IDLE TX_MODE:" + mode_string(current_mode_);
            status += " TX_BUF:" + std::to_string(tx_buffer_.size());
            send_control(status);
        }
        else if (cmd_body == "QUERY:MODES") {
            send_control("MODES:75S,75L,150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L");
        }
        else if (cmd_body == "QUERY:VERSION") {
            send_control("VERSION:" + std::string(m110a::api::version()));
        }
        else if (cmd_body == "QUERY:HELP") {
            send_control("COMMANDS:DATA RATE,SENDBUFFER,KILL TX,RESET MDM,RECORD TX:ON/OFF,"
                "RECORD PREFIX,RXAUDIOINJECT,SET EQUALIZER,QUERY:*");
        }
        else {
            send_control("ERROR:" + cmd_body + ":UNKNOWN COMMAND");
        }
    }
    
    void on_data_received(const std::vector<uint8_t>& data) override {
        // Append to TX buffer
        tx_buffer_.insert(tx_buffer_.end(), data.begin(), data.end());
        if (!quiet_) {
            std::cout << "[DATA] Received " << data.size() << " bytes, buffer now "
                      << tx_buffer_.size() << " bytes" << std::endl;
        }
    }
    
private:
    std::vector<uint8_t> tx_buffer_;
    std::string pcm_output_dir_;
    std::string pcm_prefix_;
    bool record_tx_;
    bool quiet_ = false;
    m110a::api::Mode current_mode_;
    m110a::api::Equalizer current_equalizer_;
    
    static bool starts_with(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() &&
               str.compare(0, prefix.size(), prefix) == 0;
    }
    
    static m110a::api::Mode string_to_mode(const std::string& s) {
        // Uppercase for comparison
        std::string upper = s;
        for (char& c : upper) c = std::toupper(static_cast<unsigned char>(c));
        
        if (upper == "75S" || upper == "75 BPS SHORT") return m110a::api::Mode::M75_SHORT;
        if (upper == "75L" || upper == "75 BPS LONG") return m110a::api::Mode::M75_LONG;
        if (upper == "150S" || upper == "150 BPS SHORT") return m110a::api::Mode::M150_SHORT;
        if (upper == "150L" || upper == "150 BPS LONG") return m110a::api::Mode::M150_LONG;
        if (upper == "300S" || upper == "300 BPS SHORT") return m110a::api::Mode::M300_SHORT;
        if (upper == "300L" || upper == "300 BPS LONG") return m110a::api::Mode::M300_LONG;
        if (upper == "600S" || upper == "600 BPS SHORT") return m110a::api::Mode::M600_SHORT;
        if (upper == "600L" || upper == "600 BPS LONG") return m110a::api::Mode::M600_LONG;
        if (upper == "1200S" || upper == "1200 BPS SHORT") return m110a::api::Mode::M1200_SHORT;
        if (upper == "1200L" || upper == "1200 BPS LONG") return m110a::api::Mode::M1200_LONG;
        if (upper == "2400S" || upper == "2400 BPS SHORT") return m110a::api::Mode::M2400_SHORT;
        if (upper == "2400L" || upper == "2400 BPS LONG") return m110a::api::Mode::M2400_LONG;
        return m110a::api::Mode::M600_SHORT;  // Default
    }
    
    static std::string mode_string(m110a::api::Mode m) {
        switch (m) {
            case m110a::api::Mode::M75_SHORT: return "75 BPS SHORT";
            case m110a::api::Mode::M75_LONG: return "75 BPS LONG";
            case m110a::api::Mode::M150_SHORT: return "150 BPS SHORT";
            case m110a::api::Mode::M150_LONG: return "150 BPS LONG";
            case m110a::api::Mode::M300_SHORT: return "300 BPS SHORT";
            case m110a::api::Mode::M300_LONG: return "300 BPS LONG";
            case m110a::api::Mode::M600_SHORT: return "600 BPS SHORT";
            case m110a::api::Mode::M600_LONG: return "600 BPS LONG";
            case m110a::api::Mode::M1200_SHORT: return "1200 BPS SHORT";
            case m110a::api::Mode::M1200_LONG: return "1200 BPS LONG";
            case m110a::api::Mode::M2400_SHORT: return "2400 BPS SHORT";
            case m110a::api::Mode::M2400_LONG: return "2400 BPS LONG";
            default: return "UNKNOWN";
        }
    }
    
    std::string generate_pcm_filename() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::tm tm = *std::localtime(&time);
        std::ostringstream oss;
        oss << pcm_output_dir_;
        if (!pcm_prefix_.empty()) oss << pcm_prefix_ << "_";
        oss << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_"
            << std::setfill('0') << std::setw(3) << ms.count() << ".pcm";
        return oss.str();
    }
    
    void do_transmit() {
        if (tx_buffer_.empty()) {
            send_control("OK:SENDBUFFER:EMPTY");
            return;
        }
        
        if (!quiet_) {
            std::cout << "[TX] Starting transmit of " << tx_buffer_.size()
                      << " bytes in mode " << mode_string(current_mode_)
                      << std::endl;
        }
        
        // Encode using Phoenix Nest API
        // NOTE: Disable leading symbols for Brain Core interoperability
        // Brain Core expects standard MIL-STD preamble without extra leading symbols
        m110a::api::TxConfig config;
        config.mode = current_mode_;
        config.sample_rate = static_cast<float>(SAMPLE_RATE);
        config.include_leading_symbols = false;  // Critical for Brain Core interop!
        config.include_preamble = true;
        config.include_eom = true;
        
        m110a::api::ModemTX tx(config);
        auto result = tx.encode(tx_buffer_);
        
        if (!result.ok()) {
            send_control("ERROR:SENDBUFFER:ENCODE FAILED");
            tx_buffer_.clear();
            return;
        }
        
        auto samples = result.value();
        
        if (!quiet_) {
            std::cout << "[TX] Generated " << samples.size() << " samples at "
                      << SAMPLE_RATE << " Hz" << std::endl;
        }
        
        // Write PCM file if recording
        std::string pcm_filename;
        if (record_tx_ && !samples.empty()) {
            pcm_filename = generate_pcm_filename();
            if (write_pcm_file(pcm_filename, samples)) {
                if (!quiet_) {
                    std::cout << "[TX] Saved: " << pcm_filename << std::endl;
                }
            }
        }
        
        size_t bytes_sent = tx_buffer_.size();
        tx_buffer_.clear();
        
        if (!pcm_filename.empty()) {
            send_control("OK:SENDBUFFER:" + std::to_string(bytes_sent) + 
                        " bytes FILE:" + pcm_filename);
        } else {
            send_control("OK:SENDBUFFER:" + std::to_string(bytes_sent) + " bytes");
        }
    }
    
    void do_rx_inject(const std::string& filename) {
        // Check if file exists
        std::ifstream test(filename, std::ios::binary);
        if (!test.good()) {
            send_control("ERROR:RXAUDIOINJECT:FILE NOT FOUND:" + filename);
            return;
        }
        test.close();
        
        send_control("OK:RXAUDIOINJECT:STARTED:" + filename);
        
        // Read PCM file
        std::vector<int16_t> int16_samples;
        if (!read_pcm_file(filename, int16_samples)) {
            send_control("STATUS:RX:NO DCD");
            return;
        }
        
        // Convert to float
        std::vector<float> samples(int16_samples.size());
        for (size_t i = 0; i < int16_samples.size(); i++) {
            samples[i] = static_cast<float>(int16_samples[i]) / 32768.0f;
        }
        
        if (!quiet_) {
            std::cout << "[RX] Injecting " << samples.size() << " samples from "
                      << filename << std::endl;
        }
        
        // Decode using Phoenix Nest API
        m110a::api::RxConfig rx_cfg;
        rx_cfg.sample_rate = SAMPLE_RATE;
        rx_cfg.mode = current_mode_;
        rx_cfg.equalizer = current_equalizer_;
        
        auto result = m110a::api::decode(samples, rx_cfg);
        
        if (result.success && !result.data.empty()) {
            // Strip trailing null bytes (padding)
            auto data = result.data;
            while (!data.empty() && data.back() == 0x00) {
                data.pop_back();
            }
            
            if (!quiet_) {
                std::cout << "[RX] Decoded " << data.size() << " bytes" << std::endl;
            }
            
            // Send decoded data on data port
            if (!data.empty()) {
                send_data(data);
            }
            
            send_control("STATUS:RX:" + mode_string(current_mode_));
        }
        
        send_control("STATUS:RX:NO DCD");
        send_control("OK:RXAUDIOINJECT:COMPLETE:" + 
                     std::to_string(int16_samples.size()) + " samples");
    }
    
    void do_set_equalizer(const std::string& eq_str) {
        // Uppercase for comparison
        std::string upper = eq_str;
        for (char& c : upper) c = std::toupper(static_cast<unsigned char>(c));
        
        m110a::api::Equalizer eq;
        std::string eq_name;
        
        if (upper == "NONE" || upper == "OFF") {
            eq = m110a::api::Equalizer::NONE;
            eq_name = "NONE";
        } else if (upper == "DFE" || upper == "LMS") {
            eq = m110a::api::Equalizer::DFE;
            eq_name = "DFE";
        } else if (upper == "DFE_RLS" || upper == "RLS") {
            eq = m110a::api::Equalizer::DFE_RLS;
            eq_name = "DFE_RLS";
        } else if (upper == "MLSE_L2" || upper == "MLSE2") {
            eq = m110a::api::Equalizer::MLSE_L2;
            eq_name = "MLSE_L2";
        } else if (upper == "MLSE_L3" || upper == "MLSE3" || upper == "MLSE") {
            eq = m110a::api::Equalizer::MLSE_L3;
            eq_name = "MLSE_L3";
        } else if (upper == "MLSE_ADAPTIVE" || upper == "ADAPTIVE") {
            eq = m110a::api::Equalizer::MLSE_ADAPTIVE;
            eq_name = "MLSE_ADAPTIVE";
        } else if (upper == "TURBO") {
            eq = m110a::api::Equalizer::TURBO;
            eq_name = "TURBO";
        } else {
            send_control("ERROR:SET EQUALIZER:UNKNOWN:" + eq_str);
            return;
        }
        
        current_equalizer_ = eq;
        send_control("OK:SET EQUALIZER:" + eq_name);
    }
    
    // File I/O helpers
    bool write_pcm_file(const std::string& filename, const std::vector<float>& samples) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;
        
        // Convert to int16 and write
        for (float sample : samples) {
            int16_t s = static_cast<int16_t>(sample * 32767.0f);
            file.write(reinterpret_cast<const char*>(&s), sizeof(s));
        }
        return true;
    }
    
    bool read_pcm_file(const std::string& filename, std::vector<int16_t>& samples) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        
        auto size = file.tellg();
        file.seekg(0);
        
        samples.resize(size / sizeof(int16_t));
        file.read(reinterpret_cast<char*>(samples.data()), size);
        return true;
    }
};

} // namespace phoenix_server

#endif // PHOENIX_TCP_SERVER_H
