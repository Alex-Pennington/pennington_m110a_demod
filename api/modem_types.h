/**
 * @file modem_types.h
 * @brief Core types for M110A Modem API
 * 
 * Provides Result<T> pattern for error handling, error codes,
 * and common type definitions.
 */

#ifndef M110A_API_MODEM_TYPES_H
#define M110A_API_MODEM_TYPES_H

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>

namespace m110a {
namespace api {

// ============================================================
// Error Handling
// ============================================================

/**
 * Error codes for modem operations
 */
enum class ErrorCode {
    // Success (not typically used with Result pattern)
    OK = 0,
    
    // Configuration errors (100-199)
    INVALID_MODE = 100,
    INVALID_SAMPLE_RATE = 101,
    INVALID_CARRIER_FREQ = 102,
    INVALID_CONFIG = 103,
    
    // TX errors (200-299)
    TX_DATA_TOO_LARGE = 200,
    TX_DATA_EMPTY = 201,
    TX_ENCODE_FAILED = 202,
    TX_NOT_STARTED = 203,
    
    // RX errors (300-399)
    RX_NO_SIGNAL = 300,
    RX_SYNC_FAILED = 301,
    RX_MODE_DETECT_FAILED = 302,
    RX_DECODE_FAILED = 303,
    RX_CRC_ERROR = 304,
    RX_TIMEOUT = 305,
    RX_NOT_STARTED = 306,
    
    // I/O errors (400-499)
    FILE_NOT_FOUND = 400,
    FILE_READ_ERROR = 401,
    FILE_WRITE_ERROR = 402,
    INVALID_FILE_FORMAT = 403,
    
    // Internal errors (500-599)
    INTERNAL_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    OUT_OF_MEMORY = 502,
};

/**
 * Error information with code and message
 */
struct Error {
    ErrorCode code;
    std::string message;
    
    Error(ErrorCode c, const std::string& msg = "") 
        : code(c), message(msg) {
        if (message.empty()) {
            message = default_message(c);
        }
    }
    
    static std::string default_message(ErrorCode code) {
        switch (code) {
            case ErrorCode::OK: return "Success";
            case ErrorCode::INVALID_MODE: return "Invalid mode specified";
            case ErrorCode::INVALID_SAMPLE_RATE: return "Invalid sample rate";
            case ErrorCode::INVALID_CARRIER_FREQ: return "Invalid carrier frequency";
            case ErrorCode::INVALID_CONFIG: return "Invalid configuration";
            case ErrorCode::TX_DATA_TOO_LARGE: return "TX data too large for mode";
            case ErrorCode::TX_DATA_EMPTY: return "TX data is empty";
            case ErrorCode::TX_ENCODE_FAILED: return "TX encoding failed";
            case ErrorCode::TX_NOT_STARTED: return "TX not started";
            case ErrorCode::RX_NO_SIGNAL: return "No signal detected";
            case ErrorCode::RX_SYNC_FAILED: return "Synchronization failed";
            case ErrorCode::RX_MODE_DETECT_FAILED: return "Mode detection failed";
            case ErrorCode::RX_DECODE_FAILED: return "Decoding failed";
            case ErrorCode::RX_CRC_ERROR: return "CRC check failed";
            case ErrorCode::RX_TIMEOUT: return "Operation timed out";
            case ErrorCode::RX_NOT_STARTED: return "RX not started";
            case ErrorCode::FILE_NOT_FOUND: return "File not found";
            case ErrorCode::FILE_READ_ERROR: return "File read error";
            case ErrorCode::FILE_WRITE_ERROR: return "File write error";
            case ErrorCode::INVALID_FILE_FORMAT: return "Invalid file format";
            case ErrorCode::INTERNAL_ERROR: return "Internal error";
            case ErrorCode::NOT_IMPLEMENTED: return "Not implemented";
            case ErrorCode::OUT_OF_MEMORY: return "Out of memory";
            default: return "Unknown error";
        }
    }
    
    bool operator==(ErrorCode c) const { return code == c; }
    bool operator!=(ErrorCode c) const { return code != c; }
};

/**
 * Result type for operations that can fail
 * 
 * Usage:
 *   Result<std::vector<uint8_t>> result = modem.decode(samples);
 *   if (result.ok()) {
 *       auto data = result.value();
 *   } else {
 *       std::cerr << result.error().message << std::endl;
 *   }
 */
template<typename T>
class Result {
public:
    // Construct success result
    Result(const T& value) : data_(value) {}
    Result(T&& value) : data_(std::move(value)) {}
    
    // Construct error result
    Result(const Error& error) : data_(error) {}
    Result(ErrorCode code, const std::string& msg = "") 
        : data_(Error(code, msg)) {}
    
    // Check status
    bool ok() const { return std::holds_alternative<T>(data_); }
    bool is_error() const { return std::holds_alternative<Error>(data_); }
    explicit operator bool() const { return ok(); }
    
    // Access value (call only if ok())
    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    
    // Access value with default
    T value_or(const T& default_value) const {
        return ok() ? value() : default_value;
    }
    
    // Access error (call only if is_error())
    const Error& error() const { return std::get<Error>(data_); }
    
    // Convenience accessors
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }
    const T& operator*() const { return value(); }
    T& operator*() { return value(); }

private:
    std::variant<T, Error> data_;
};

/**
 * Result type for void operations
 */
template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(const Error& error) : error_(error) {}
    Result(ErrorCode code, const std::string& msg = "") 
        : error_(Error(code, msg)) {}
    
    bool ok() const { return !error_.has_value(); }
    bool is_error() const { return error_.has_value(); }
    explicit operator bool() const { return ok(); }
    
    const Error& error() const { return error_.value(); }
    
private:
    std::optional<Error> error_;
};

// ============================================================
// Mode Definitions
// ============================================================

/**
 * Modem operating modes
 */
enum class Mode {
    // Auto-detect (RX only)
    AUTO = 0,
    
    // 75 bps Walsh orthogonal coding
    M75_SHORT,
    M75_LONG,
    
    // 150 bps BPSK with 8x repetition
    M150_SHORT,
    M150_LONG,
    
    // 300 bps BPSK with 4x repetition
    M300_SHORT,
    M300_LONG,
    
    // 600 bps BPSK with 2x repetition
    M600_SHORT,
    M600_LONG,
    
    // 1200 bps QPSK
    M1200_SHORT,
    M1200_LONG,
    
    // 2400 bps 8-PSK
    M2400_SHORT,
    M2400_LONG,
    
    // 4800 bps 8-PSK uncoded
    M4800_SHORT,
    M4800_LONG,
};

/**
 * Get human-readable mode name
 */
inline std::string mode_name(Mode mode) {
    switch (mode) {
        case Mode::AUTO: return "AUTO";
        case Mode::M75_SHORT: return "75S";
        case Mode::M75_LONG: return "75L";
        case Mode::M150_SHORT: return "150S";
        case Mode::M150_LONG: return "150L";
        case Mode::M300_SHORT: return "300S";
        case Mode::M300_LONG: return "300L";
        case Mode::M600_SHORT: return "600S";
        case Mode::M600_LONG: return "600L";
        case Mode::M1200_SHORT: return "1200S";
        case Mode::M1200_LONG: return "1200L";
        case Mode::M2400_SHORT: return "2400S";
        case Mode::M2400_LONG: return "2400L";
        case Mode::M4800_SHORT: return "4800S";
        case Mode::M4800_LONG: return "4800L";
        default: return "UNKNOWN";
    }
}

/**
 * Get data rate in bps for mode
 */
inline int mode_bitrate(Mode mode) {
    switch (mode) {
        case Mode::M75_SHORT:
        case Mode::M75_LONG: return 75;
        case Mode::M150_SHORT:
        case Mode::M150_LONG: return 150;
        case Mode::M300_SHORT:
        case Mode::M300_LONG: return 300;
        case Mode::M600_SHORT:
        case Mode::M600_LONG: return 600;
        case Mode::M1200_SHORT:
        case Mode::M1200_LONG: return 1200;
        case Mode::M2400_SHORT:
        case Mode::M2400_LONG: return 2400;
        case Mode::M4800_SHORT:
        case Mode::M4800_LONG: return 4800;
        default: return 0;
    }
}

/**
 * Check if mode uses long interleave
 */
inline bool mode_is_long(Mode mode) {
    switch (mode) {
        case Mode::M75_LONG:
        case Mode::M150_LONG:
        case Mode::M300_LONG:
        case Mode::M600_LONG:
        case Mode::M1200_LONG:
        case Mode::M2400_LONG:
        case Mode::M4800_LONG:
            return true;
        default:
            return false;
    }
}

// ============================================================
// Equalizer Selection
// ============================================================

/**
 * Equalizer algorithms
 */
enum class Equalizer {
    NONE,       // No equalization
    DFE,        // Decision Feedback Equalizer
    MLSE_L2,    // MLSE with L=2 (8 states)
    MLSE_L3,    // MLSE with L=3 (64 states)
};

// ============================================================
// Statistics
// ============================================================

/**
 * Modem statistics
 */
struct ModemStats {
    // Signal quality
    float snr_db = 0.0f;           // Estimated SNR in dB
    float freq_offset_hz = 0.0f;   // Frequency offset in Hz
    float timing_offset = 0.0f;    // Timing offset (fractional symbol)
    
    // Error rates
    float ber_estimate = 0.0f;     // Bit error rate estimate
    float ser_estimate = 0.0f;     // Symbol error rate estimate
    
    // Counters
    uint64_t bytes_transmitted = 0;
    uint64_t bytes_received = 0;
    uint64_t frames_transmitted = 0;
    uint64_t frames_received = 0;
    uint64_t frames_errors = 0;
    
    // Timing
    double last_tx_duration_ms = 0.0;
    double last_rx_duration_ms = 0.0;
};

// ============================================================
// Audio Types
// ============================================================

/**
 * Audio sample format
 */
using Sample = float;  // -1.0 to +1.0 normalized
using Samples = std::vector<Sample>;

/**
 * Common sample rates
 */
constexpr float SAMPLE_RATE_8K = 8000.0f;
constexpr float SAMPLE_RATE_48K = 48000.0f;
constexpr float SAMPLE_RATE_DEFAULT = SAMPLE_RATE_48K;

/**
 * Standard carrier frequency
 */
constexpr float CARRIER_FREQ_DEFAULT = 1800.0f;

} // namespace api
} // namespace m110a

#endif // M110A_API_MODEM_TYPES_H
