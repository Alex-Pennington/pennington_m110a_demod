/**
 * @file msdmt_server.h
 * @brief MS-DMT Compatible Network Interface Server
 * 
 * Implements the MS-DMT network protocol for MIL-STD-188-110A modem.
 * 
 * Network Architecture:
 *   - Data Port (TCP 4998):    Raw binary data in/out
 *   - Control Port (TCP 4999): ASCII commands and status messages
 *   - Discovery Port (UDP 5000): "helo" broadcasts for auto-discovery
 * 
 * This provides a drop-in replacement interface compatible with MS-DMT
 * clients, allowing seamless integration with existing applications.
 */

#ifndef MSDMT_SERVER_H
#define MSDMT_SERVER_H

#include "api/modem_types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <cstdint>

namespace m110a {
namespace server {

// ============================================================
// Configuration
// ============================================================

/// Default port numbers (MS-DMT compatible)
constexpr uint16_t DEFAULT_DATA_PORT = 4998;
constexpr uint16_t DEFAULT_CONTROL_PORT = 4999;
constexpr uint16_t DEFAULT_DISCOVERY_PORT = 5000;

/// Server configuration
struct ServerConfig {
    uint16_t data_port = DEFAULT_DATA_PORT;
    uint16_t control_port = DEFAULT_CONTROL_PORT;
    uint16_t discovery_port = DEFAULT_DISCOVERY_PORT;
    
    bool enable_discovery = true;           ///< Enable UDP discovery broadcasts
    int discovery_interval_ms = 5000;       ///< Discovery broadcast interval
    
    std::string pcm_output_dir = "./tx_pcm_out/";   ///< TX recording directory
    std::string pcm_input_dir = "./rx_pcm_in/";     ///< RX input directory
    
    bool log_commands = true;               ///< Log all commands to console
    bool log_status = true;                 ///< Log all status messages
};

// ============================================================
// Data Rate Modes
// ============================================================

/// Supported data rates and interleave modes
enum class DataRateMode {
    M75_SHORT,
    M75_LONG,
    M150_SHORT,
    M150_LONG,
    M300_SHORT,
    M300_LONG,
    M600_SHORT,
    M600_LONG,
    M1200_SHORT,
    M1200_LONG,
    M2400_SHORT,
    M2400_LONG,
    UNKNOWN
};

/// Convert mode string to enum
DataRateMode parse_data_rate_mode(const std::string& mode_str);

/// Convert enum to mode string
std::string data_rate_mode_to_string(DataRateMode mode);

/// Convert enum to human-readable status string (e.g., "600 BPS SHORT")
std::string data_rate_mode_to_status_string(DataRateMode mode);

// ============================================================
// Modem State
// ============================================================

/// Current modem operating state
enum class ModemState {
    IDLE,           ///< Ready for commands
    TRANSMITTING,   ///< TX in progress
    RECEIVING,      ///< RX active (DCD)
    INJECTING       ///< Processing RX audio inject
};

// ============================================================
// Command Types
// ============================================================

/// Parsed command from control port
struct Command {
    std::string type;           ///< Command type (e.g., "DATA RATE", "SENDBUFFER")
    std::string parameter;      ///< Command parameter (e.g., "600S")
    std::string raw;            ///< Raw command string
    
    bool valid = false;         ///< True if parsing succeeded
};

/// Parse a command string from control port
Command parse_command(const std::string& cmd_str);

// ============================================================
// Status Messages
// ============================================================

/// Status message categories
enum class StatusCategory {
    TX,             ///< Transmission status
    RX,             ///< Reception status
    MODEM,          ///< General modem status
    ERR             ///< Error condition
};

/// Format a status message for control port
std::string format_status(StatusCategory category, const std::string& details);

/// Format an OK response
std::string format_ok(const std::string& command, const std::string& details = "");

/// Format an ERROR response
std::string format_error(const std::string& command, const std::string& details);

// ============================================================
// Client Connection
// ============================================================

/// Represents a connected client
struct ClientConnection {
    int socket_fd = -1;             ///< Socket file descriptor
    std::string address;            ///< Client IP address
    uint16_t port = 0;              ///< Client port
    bool connected = false;         ///< Connection state
    
    /// Send data to client (thread-safe)
    bool send(const std::vector<uint8_t>& data);
    bool send(const std::string& text);
    
    /// Receive data from client
    std::vector<uint8_t> receive(size_t max_bytes = 4096);
    std::string receive_line();
    
    /// Close connection
    void close();
};

// ============================================================
// Event Callbacks
// ============================================================

/// Callback function types for server events
using OnDataReceivedCallback = std::function<void(const std::vector<uint8_t>& data)>;
using OnCommandReceivedCallback = std::function<void(const Command& cmd)>;
using OnClientConnectedCallback = std::function<void(const std::string& address, uint16_t port)>;
using OnClientDisconnectedCallback = std::function<void(const std::string& address, uint16_t port)>;

// ============================================================
// MS-DMT Server Interface
// ============================================================

/**
 * @class MSDMTServer
 * @brief Main server class implementing MS-DMT compatible network interface
 * 
 * This server provides a network interface compatible with MS-DMT clients.
 * It wraps the m110a::api modem implementation and exposes it via TCP.
 * 
 * Usage:
 * @code
 *   MSDMTServer server;
 *   server.configure(config);
 *   server.start();
 *   // Server runs in background threads
 *   // ... application logic ...
 *   server.stop();
 * @endcode
 */
class MSDMTServer {
public:
    MSDMTServer();
    ~MSDMTServer();
    
    // Non-copyable
    MSDMTServer(const MSDMTServer&) = delete;
    MSDMTServer& operator=(const MSDMTServer&) = delete;
    
    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------
    
    /// Configure server settings (call before start())
    void configure(const ServerConfig& config);
    
    /// Get current configuration
    const ServerConfig& config() const { return config_; }
    
    // --------------------------------------------------------
    // Server Lifecycle
    // --------------------------------------------------------
    
    /// Start the server (opens ports, begins accepting connections)
    bool start();
    
    /// Stop the server (closes all connections, stops threads)
    void stop();
    
    /// Check if server is running
    bool is_running() const { return running_.load(); }
    
    // --------------------------------------------------------
    // State Access
    // --------------------------------------------------------
    
    /// Get current modem state
    ModemState state() const { return state_.load(); }
    
    /// Get current data rate mode
    DataRateMode current_mode() const { return current_mode_; }
    
    /// Check if TX recording is enabled
    bool is_recording() const { return recording_enabled_; }
    
    /// Get recording prefix
    const std::string& recording_prefix() const { return recording_prefix_; }
    
    // --------------------------------------------------------
    // TX Buffer Management
    // --------------------------------------------------------
    
    /// Get current TX buffer size (bytes queued for transmission)
    size_t tx_buffer_size() const;
    
    /// Clear TX buffer
    void clear_tx_buffer();
    
    // --------------------------------------------------------
    // Status Notifications
    // --------------------------------------------------------
    
    /// Send status message to all connected control clients
    void broadcast_status(StatusCategory category, const std::string& details);
    
    /// Send data to all connected data clients
    void broadcast_data(const std::vector<uint8_t>& data);
    
    // --------------------------------------------------------
    // Event Callbacks
    // --------------------------------------------------------
    
    /// Set callback for data received on data port
    void on_data_received(OnDataReceivedCallback callback);
    
    /// Set callback for command received on control port
    void on_command_received(OnCommandReceivedCallback callback);
    
    /// Set callback for client connection
    void on_client_connected(OnClientConnectedCallback callback);
    
    /// Set callback for client disconnection
    void on_client_disconnected(OnClientDisconnectedCallback callback);
    
private:
    // Configuration
    ServerConfig config_;
    
    // State
    std::atomic<bool> running_{false};
    std::atomic<ModemState> state_{ModemState::IDLE};
    DataRateMode current_mode_ = DataRateMode::M2400_SHORT;
    
    // Recording
    bool recording_enabled_ = false;
    std::string recording_prefix_;
    
    // TX buffer
    std::vector<uint8_t> tx_buffer_;
    mutable std::mutex tx_buffer_mutex_;
    
    // RX decoded data queue
    std::queue<std::vector<uint8_t>> rx_data_queue_;
    std::mutex rx_data_mutex_;
    
    // Sockets (platform-specific implementation)
    struct SocketImpl;
    std::unique_ptr<SocketImpl> sockets_;
    
    // Server threads
    std::thread data_accept_thread_;
    std::thread control_accept_thread_;
    std::thread discovery_thread_;
    std::thread processing_thread_;
    
    // Client connections
    std::vector<std::shared_ptr<ClientConnection>> data_clients_;
    std::vector<std::shared_ptr<ClientConnection>> control_clients_;
    std::mutex clients_mutex_;
    
    // Callbacks
    OnDataReceivedCallback on_data_received_;
    OnCommandReceivedCallback on_command_received_;
    OnClientConnectedCallback on_client_connected_;
    OnClientDisconnectedCallback on_client_disconnected_;
    
    // Internal methods
    void run_data_accept_loop();
    void run_control_accept_loop();
    void run_discovery_loop();
    void run_processing_loop();
    
    void handle_data_client(std::shared_ptr<ClientConnection> client);
    void handle_control_client(std::shared_ptr<ClientConnection> client);
    
    void process_command(std::shared_ptr<ClientConnection> client, const Command& cmd);
    
    // Command handlers
    void cmd_data_rate(std::shared_ptr<ClientConnection> client, const std::string& param);
    void cmd_send_buffer(std::shared_ptr<ClientConnection> client);
    void cmd_record_tx(std::shared_ptr<ClientConnection> client, bool enable);
    void cmd_record_prefix(std::shared_ptr<ClientConnection> client, const std::string& prefix);
    void cmd_rx_audio_inject(std::shared_ptr<ClientConnection> client, const std::string& filepath);
    void cmd_kill_tx(std::shared_ptr<ClientConnection> client);
    
    // Channel simulation command handlers
    void cmd_channel_config(std::shared_ptr<ClientConnection> client, const std::string& param);
    void cmd_channel_preset(std::shared_ptr<ClientConnection> client, const std::string& preset);
    void cmd_channel_awgn(std::shared_ptr<ClientConnection> client, const std::string& snr_db);
    void cmd_channel_multipath(std::shared_ptr<ClientConnection> client, const std::string& params);
    void cmd_channel_freq_offset(std::shared_ptr<ClientConnection> client, const std::string& offset_hz);
    void cmd_channel_off(std::shared_ptr<ClientConnection> client);
    void cmd_run_ber_test(std::shared_ptr<ClientConnection> client, const std::string& params);
    void cmd_set_equalizer(std::shared_ptr<ClientConnection> client, const std::string& param);
    
    // Channel simulation state
    bool channel_sim_enabled_ = false;
    float channel_snr_db_ = 30.0f;
    bool channel_awgn_enabled_ = false;
    bool channel_multipath_enabled_ = false;
    int channel_multipath_delay_ = 48;
    float channel_multipath_gain_ = 0.5f;
    bool channel_freq_offset_enabled_ = false;
    float channel_freq_offset_hz_ = 0.0f;
    
    // RX equalizer setting
    m110a::api::Equalizer current_equalizer_ = m110a::api::Equalizer::DFE;
};

// ============================================================
// Utility Functions
// ============================================================

/// Read raw PCM file (16-bit mono, 48kHz, little-endian)
std::vector<int16_t> read_pcm_file(const std::string& filepath);

/// Write raw PCM file
bool write_pcm_file(const std::string& filepath, const std::vector<int16_t>& samples);

/// Convert int16 samples to float samples (normalized to [-1, 1])
std::vector<float> samples_to_float(const std::vector<int16_t>& samples);

/// Convert float samples to int16
std::vector<int16_t> samples_to_int16(const std::vector<float>& samples);

/// Generate timestamped filename
std::string generate_pcm_filename(const std::string& prefix, const std::string& output_dir);

} // namespace server
} // namespace m110a

#endif // MSDMT_SERVER_H
