/**
 * @file modem_tx.h
 * @brief M110A Modem Transmitter API
 * 
 * Thread-safe transmitter with synchronous API.
 * Uses PIMPL pattern to hide implementation details.
 */

#ifndef M110A_API_MODEM_TX_H
#define M110A_API_MODEM_TX_H

#include "modem_types.h"
#include "modem_config.h"
#include <memory>

namespace m110a {
namespace api {

// Forward declaration of implementation
class ModemTXImpl;

/**
 * M110A Modem Transmitter
 * 
 * Encodes data into audio samples for transmission.
 * Thread-safe: can be called from multiple threads.
 * 
 * Usage:
 * @code
 *   ModemTX tx(TxConfig::for_mode(Mode::M2400_SHORT));
 *   auto result = tx.encode({0x48, 0x65, 0x6c, 0x6c, 0x6f}); // "Hello"
 *   if (result.ok()) {
 *       write_audio(result.value());
 *   }
 * @endcode
 */
class ModemTX {
public:
    /**
     * Construct transmitter with configuration
     * @param config TX configuration
     */
    explicit ModemTX(const TxConfig& config = TxConfig());
    
    /**
     * Destructor
     */
    ~ModemTX();
    
    // Non-copyable
    ModemTX(const ModemTX&) = delete;
    ModemTX& operator=(const ModemTX&) = delete;
    
    // Movable
    ModemTX(ModemTX&&) noexcept;
    ModemTX& operator=(ModemTX&&) noexcept;
    
    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------
    
    /**
     * Get current configuration
     */
    const TxConfig& config() const;
    
    /**
     * Update configuration
     * @param config New configuration
     * @return Success or error
     */
    Result<void> set_config(const TxConfig& config);
    
    /**
     * Set operating mode
     * @param mode New mode
     * @return Success or error
     */
    Result<void> set_mode(Mode mode);
    
    // --------------------------------------------------------
    // Encoding
    // --------------------------------------------------------
    
    /**
     * Encode data to audio samples (one-shot)
     * 
     * This is the main encoding function. It takes raw data bytes
     * and produces audio samples ready for transmission.
     * 
     * @param data Data bytes to encode
     * @return Audio samples or error
     */
    Result<Samples> encode(const std::vector<uint8_t>& data);
    
    /**
     * Encode string to audio samples
     * @param text Text to encode
     * @return Audio samples or error
     */
    Result<Samples> encode(const std::string& text);
    
    /**
     * Generate preamble only (no data)
     * @return Preamble audio samples or error
     */
    Result<Samples> generate_preamble();
    
    /**
     * Generate test tone
     * @param duration_seconds Duration in seconds
     * @param frequency_hz Tone frequency (default: carrier)
     * @return Audio samples or error
     */
    Result<Samples> generate_tone(float duration_seconds, 
                                   float frequency_hz = 0.0f);
    
    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------
    
    /**
     * Get transmission statistics
     */
    ModemStats stats() const;
    
    /**
     * Reset statistics counters
     */
    void reset_stats();
    
    // --------------------------------------------------------
    // Utility
    // --------------------------------------------------------
    
    /**
     * Calculate transmission duration for data size
     * @param data_bytes Number of bytes to transmit
     * @return Duration in seconds
     */
    float calculate_duration(size_t data_bytes) const;
    
    /**
     * Calculate maximum data size for mode
     * @return Maximum bytes per transmission (0 = unlimited)
     */
    size_t max_data_size() const;

private:
    std::unique_ptr<ModemTXImpl> impl_;
};

} // namespace api
} // namespace m110a

#endif // M110A_API_MODEM_TX_H
