// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Phoenix Nest LLC
/**
 * @file brain_tcp_server.h
 * @brief Brain Core TCP server using robust tcp_server_base
 * 
 * This server wraps the Brain Modem (m188110a) core with:
 * - TCP control port (3999) for commands
 * - TCP data port (3998) for TX/RX data
 * - PCM file I/O for testing
 * 
 * Uses the tcp_server_base for robust socket handling.
 */

#ifndef BRAIN_TCP_SERVER_H
#define BRAIN_TCP_SERVER_H

#include "tcp_server_base.h"
#include "../extern/brain_wrapper.h"

#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace brain_server {

// Default ports
constexpr uint16_t DEFAULT_CONTROL_PORT = 3999;
constexpr uint16_t DEFAULT_DATA_PORT = 3998;

/**
 * @brief Brain Core TCP Server
 * 
 * Implements the same command protocol as the original brain_core server:
 * - CMD:DATA RATE:<mode> - Set TX mode
 * - CMD:SENDBUFFER - Transmit buffered data
 * - CMD:RESET MDM - Reset modem
 * - CMD:RXAUDIOINJECT:<path> - Inject RX PCM file
 * - CMD:QUERY:* - Query status, modes, version, etc.
 */
class BrainServer : public tcp_base::ServerBase {
public:
    static constexpr int SAMPLE_RATE_NATIVE = 9600;
    static constexpr int SAMPLE_RATE_COMPAT = 48000;
    static constexpr int RESAMPLE_RATIO = 5;  // 48000 / 9600
    
    BrainServer()
        : modem_()
        , tx_buffer_()
        , pcm_output_dir_("./tx_pcm_out/")
        , pcm_prefix_()
        , record_tx_(true)
        , current_mode_(brain::Mode::M600S)
    {
        set_ports(DEFAULT_CONTROL_PORT, DEFAULT_DATA_PORT);
    }
    
    void set_pcm_output_dir(const std::string& dir) { pcm_output_dir_ = dir; }
    
protected:
    std::string get_ready_message() override {
        return "READY:Paul Brain Core (tcp_base)";
    }
    
    void on_command(const std::string& cmd) override {
        std::cout << "[CMD] " << cmd << std::endl;
        
        // Trim whitespace
        std::string c = cmd;
        while (!c.empty() && (c.back() == '\r' || c.back() == '\n' || c.back() == ' '))
            c.pop_back();
        
        // Parse command
        if (starts_with(c, "CMD:DATA RATE:")) {
            std::string rate = c.substr(14);
            current_mode_ = string_to_mode(rate);
            send_control(std::string("OK:DATA RATE:") + mode_string(current_mode_));
        }
        else if (c == "CMD:SENDBUFFER") {
            do_transmit();
        }
        else if (c == "CMD:RESET MDM") {
            modem_.reset_rx();
            tx_buffer_.clear();
            send_control("OK:RESET");
        }
        else if (c == "CMD:KILL TX") {
            tx_buffer_.clear();
            send_control("OK:TX KILLED");
        }
        else if (c == "CMD:RECORD TX:ON") {
            record_tx_ = true;
            send_control("OK:RECORD TX:ON");
        }
        else if (c == "CMD:RECORD TX:OFF") {
            record_tx_ = false;
            send_control("OK:RECORD TX:OFF");
        }
        else if (starts_with(c, "CMD:RECORD PREFIX:")) {
            pcm_prefix_ = c.substr(18);
            send_control("OK:PREFIX:" + pcm_prefix_);
        }
        else if (starts_with(c, "CMD:RXAUDIOINJECT:")) {
            std::string path = c.substr(18);
            do_rx_inject(path);
        }
        else if (c == "CMD:QUERY:PCM OUTPUT") {
            send_control("PCM OUTPUT:" + pcm_output_dir_);
        }
        else if (c == "CMD:QUERY:STATUS") {
            std::string status = "STATUS:IDLE";
            status += " TX_MODE:" + std::string(brain::mode_to_string(current_mode_));
            status += " TX_BUF:" + std::to_string(tx_buffer_.size());
            send_control(status);
        }
        else if (c == "CMD:QUERY:MODES") {
            send_control("MODES:75S,75L,150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L");
        }
        else if (c == "CMD:QUERY:HELP") {
            send_control("COMMANDS:DATA RATE,SENDBUFFER,RESET MDM,KILL TX,"
                "RECORD TX:ON/OFF,RECORD PREFIX,RXAUDIOINJECT,QUERY:*");
        }
        else if (c == "CMD:QUERY:VERSION") {
            send_control("VERSION:v1.1.0-tcp_base");
        }
        else {
            send_control("ERROR:UNKNOWN COMMAND");
        }
    }
    
    void on_data_received(const std::vector<uint8_t>& data) override {
        // Append to TX buffer
        tx_buffer_.insert(tx_buffer_.end(), data.begin(), data.end());
        std::cout << "[DATA] Received " << data.size() << " bytes, buffer now "
                  << tx_buffer_.size() << " bytes" << std::endl;
    }
    
private:
    brain::Modem modem_;
    std::vector<uint8_t> tx_buffer_;
    std::string pcm_output_dir_;
    std::string pcm_prefix_;
    bool record_tx_;
    brain::Mode current_mode_;
    
    static bool starts_with(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() &&
               str.compare(0, prefix.size(), prefix) == 0;
    }
    
    static brain::Mode string_to_mode(const std::string& s) {
        if (s == "75S" || s == "75 BPS SHORT") return brain::Mode::M75S;
        if (s == "75L" || s == "75 BPS LONG") return brain::Mode::M75L;
        if (s == "150S" || s == "150 BPS SHORT") return brain::Mode::M150S;
        if (s == "150L" || s == "150 BPS LONG") return brain::Mode::M150L;
        if (s == "300S" || s == "300 BPS SHORT") return brain::Mode::M300S;
        if (s == "300L" || s == "300 BPS LONG") return brain::Mode::M300L;
        if (s == "600S" || s == "600 BPS SHORT") return brain::Mode::M600S;
        if (s == "600L" || s == "600 BPS LONG") return brain::Mode::M600L;
        if (s == "1200S" || s == "1200 BPS SHORT") return brain::Mode::M1200S;
        if (s == "1200L" || s == "1200 BPS LONG") return brain::Mode::M1200L;
        if (s == "2400S" || s == "2400 BPS SHORT") return brain::Mode::M2400S;
        if (s == "2400L" || s == "2400 BPS LONG") return brain::Mode::M2400L;
        return brain::Mode::M600S;  // Default
    }
    
    static const char* mode_string(brain::Mode m) {
        switch (m) {
            case brain::Mode::M75S: return "75 BPS SHORT";
            case brain::Mode::M75L: return "75 BPS LONG";
            case brain::Mode::M150S: return "150 BPS SHORT";
            case brain::Mode::M150L: return "150 BPS LONG";
            case brain::Mode::M300S: return "300 BPS SHORT";
            case brain::Mode::M300L: return "300 BPS LONG";
            case brain::Mode::M600S: return "600 BPS SHORT";
            case brain::Mode::M600L: return "600 BPS LONG";
            case brain::Mode::M1200S: return "1200 BPS SHORT";
            case brain::Mode::M1200L: return "1200 BPS LONG";
            case brain::Mode::M2400S: return "2400 BPS SHORT";
            case brain::Mode::M2400L: return "2400 BPS LONG";
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
            send_control("ERROR:NO DATA");
            return;
        }
        
        std::cout << "[TX] Starting transmit of " << tx_buffer_.size()
                  << " bytes in mode " << brain::mode_to_string(current_mode_)
                  << std::endl;
        
        // Encode using brain wrapper (handles threading internally)
        auto pcm_48k = modem_.encode_48k(tx_buffer_, current_mode_);
        
        std::cout << "[TX] Generated " << pcm_48k.size() << " samples at 48kHz"
                  << std::endl;
        
        // Write PCM file if recording
        if (record_tx_ && !pcm_48k.empty()) {
            std::string filename = generate_pcm_filename();
            if (write_pcm_file(filename, pcm_48k)) {
                send_control("TX:PCM:" + filename);
            }
        }
        
        send_control("TX:COMPLETE:" + std::to_string(tx_buffer_.size()));
        tx_buffer_.clear();
    }
    
    void do_rx_inject(const std::string& filename) {
        std::vector<int16_t> samples;
        if (!read_pcm_file(filename, samples)) {
            send_control("ERROR:CANNOT READ:" + filename);
            return;
        }
        
        send_control("RX:INJECTING:" + filename);
        std::cout << "[RX] Injecting " << samples.size() << " samples from "
                  << filename << std::endl;
        
        // Decode using brain wrapper (handles decimation)
        auto decoded = modem_.decode_48k(samples);
        
        std::cout << "[RX] Decoded " << decoded.size() << " bytes, mode: "
                  << modem_.get_detected_mode_name() << std::endl;
        
        // Send decoded data on data port
        if (!decoded.empty()) {
            send_data(decoded);
        }
        
        send_control("RX:COMPLETE:" + std::to_string(decoded.size()) +
                     ":MODE:" + modem_.get_detected_mode_name());
        
        modem_.reset_rx();
    }
    
    static bool write_pcm_file(const std::string& filename,
                               const std::vector<int16_t>& samples) {
        std::ofstream f(filename, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(samples.data()), samples.size() * 2);
        return f.good();
    }
    
    static bool read_pcm_file(const std::string& filename,
                              std::vector<int16_t>& samples) {
        std::ifstream f(filename, std::ios::binary | std::ios::ate);
        if (!f) return false;
        
        size_t size = f.tellg();
        f.seekg(0, std::ios::beg);
        
        samples.resize(size / 2);
        f.read(reinterpret_cast<char*>(samples.data()), size);
        return f.good();
    }
};

} // namespace brain_server

#endif // BRAIN_TCP_SERVER_H
