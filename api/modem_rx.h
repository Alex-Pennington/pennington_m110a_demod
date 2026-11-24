// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file modem_rx.h
 * @brief M110A Modem Receiver API
 * 
 * Thread-safe receiver with synchronous API.
 * Uses PIMPL pattern to hide implementation details.
 */

#ifndef M110A_API_MODEM_RX_H
#define M110A_API_MODEM_RX_H

#include "modem_types.h"
#include "modem_config.h"
#include <memory>

namespace m110a {
namespace api {

// Forward declaration of implementation
class ModemRXImpl;

/**
 * Decode result containing all information from a decode operation
 */
struct DecodeResult {
    /// Whether decode was successful
    bool success = false;
    
    /// Detected/used mode
    Mode mode = Mode::AUTO;
    
    /// Decoded data bytes
    std::vector<uint8_t> data;
    
    /// Whether EOM (End of Message) was detected: the EOM 4B65A5B2, or the older
    /// ModemTX ending of zero frames read from trailing zero bytes
    bool eom_detected = false;

    /// True when the EOM 4B65A5B2 itself was found; data stops before it
    bool eom_sequence = false;
    
    /// Estimated SNR in dB
    float snr_db = 0.0f;
    
    /// Estimated bit error rate
    float ber_estimate = 0.0f;
    
    /// Frequency offset detected (Hz)
    float freq_offset_hz = 0.0f;
    
    /// Decode quality: SNR in dB of the known probe symbols as the codec sees
    /// them (the DFE's output with Equalizer::DFE, otherwise the symbols handed
    /// to the codec), over the first 1440 data symbols. Every 110A transmission
    /// carries at least that many, so the span never reaches past the signal.
    float quality_db = 0.0f;
    
    /// Minimum quality_db for the decoded mode. success is true only when the
    /// decode produced bytes, quality_db >= quality_threshold_db and
    /// block_quality_min_db >= block_quality_threshold_db; otherwise data still
    /// holds the bytes but they are not trusted.
    ///
    /// 110A has no CRC, so success is a confidence threshold, not a proof. It
    /// rejects garbage (an untrained equalizer, a decoder off the signal). It
    /// does not catch a short Viterbi error event near the threshold: on held-
    /// out AWGN decodes of the MS-DMT reference recordings, 4 of 56 successes
    /// at the knee had 1-2 wrong bytes, and none had more.
    float quality_threshold_db = 0.0f;

    /// Decode quality of each interleaver block the message occupies, in order,
    /// in dB: the flat-channel SNR with the same expected coded-bit error rate as
    /// the block. Each frame's probe SNR (as quality_db reads it) gives the error
    /// rate of its symbols for the mode's modulation and repetition; the block's
    /// mean of that is what the Viterbi decoder gets after deinterleaving. On a
    /// flat channel it is about quality_db; a fade counts by the errors it causes.
    /// The blocks are those up to the one holding the EOM, or without an EOM the
    /// whole blocks the signal covers. Empty when the mode has no probes or no
    /// block was decoded.
    std::vector<float> block_quality_db;

    /// Lowest of block_quality_db, or quality_db when it is empty.
    float block_quality_min_db = 0.0f;

    /// Minimum block_quality_min_db for the decoded mode. success also needs
    /// block_quality_min_db >= block_quality_threshold_db: quality_db reads only
    /// the first 1440 data symbols, and on a fading channel a later block can be
    /// lost while the first scores well (docs/m110a-quality-gate.md). Like
    /// quality_db it is a confidence threshold: a decode a few bytes wrong at the
    /// knee can still clear it, and a decode that started a whole probe period
    /// off (its probes still line up) scores as well as a right one.
    float block_quality_threshold_db = 0.0f;

    /// Preamble as read in AUTO mode (-1 when not read): D1, D2 (0-7) and
    /// the countdown of the frame the receiver locked onto. countdown_raw is
    /// the three count digits as read, even when ambiguous or out of range.
    int d1 = -1;
    int d2 = -1;
    int countdown = -1;
    int countdown_raw = -1;

    /// Error information (if !success)
    std::optional<Error> error;
    
    /// Get data as string
    std::string as_string() const {
        return std::string(data.begin(), data.end());
    }
};

/**
 * Receiver state
 */
enum class RxState {
    IDLE,           ///< Not processing
    SEARCHING,      ///< Looking for preamble
    SYNCHRONIZING,  ///< Acquiring timing/carrier
    RECEIVING,      ///< Decoding data
    COMPLETE,       ///< Decode finished
    DECODE_ERROR           ///< Error occurred
};

/**
 * M110A Modem Receiver
 * 
 * Decodes audio samples to extract transmitted data.
 * Thread-safe: can be called from multiple threads.
 * 
 * Usage (one-shot):
 * @code
 *   ModemRX rx;
 *   auto samples = read_audio_file("signal.pcm");
 *   auto result = rx.decode(samples);
 *   if (result.success) {
 *       std::cout << "Received: " << result.as_string() << std::endl;
 *   }
 * @endcode
 * 
 * Usage (streaming):
 * @code
 *   ModemRX rx;
 *   rx.start();
 *   while (audio_available()) {
 *       auto chunk = read_audio_chunk(1024);
 *       rx.push_samples(chunk);
 *       if (rx.state() == RxState::COMPLETE) {
 *           auto result = rx.get_result();
 *           process(result);
 *           rx.start();  // Reset for next message
 *       }
 *   }
 *   rx.stop();
 * @endcode
 */
class ModemRX {
public:
    /**
     * Construct receiver with configuration
     * @param config RX configuration
     */
    explicit ModemRX(const RxConfig& config = RxConfig());
    
    /**
     * Destructor
     */
    ~ModemRX();
    
    // Non-copyable
    ModemRX(const ModemRX&) = delete;
    ModemRX& operator=(const ModemRX&) = delete;
    
    // Movable
    ModemRX(ModemRX&&) noexcept;
    ModemRX& operator=(ModemRX&&) noexcept;
    
    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------
    
    /**
     * Get current configuration
     */
    const RxConfig& config() const;
    
    /**
     * Update configuration
     * @param config New configuration
     * @return Success or error
     */
    Result<void> set_config(const RxConfig& config);
    
    /**
     * Set operating mode
     * @param mode Mode to use (AUTO for auto-detect)
     * @return Success or error
     */
    Result<void> set_mode(Mode mode);
    
    /**
     * Set equalizer algorithm
     * @param eq Equalizer to use
     * @return Success or error
     */
    Result<void> set_equalizer(Equalizer eq);
    
    // --------------------------------------------------------
    // One-Shot Decoding
    // --------------------------------------------------------
    
    /**
     * Decode audio samples (one-shot, blocking)
     * 
     * This is the main decoding function. It processes all provided
     * samples and returns the decoded result.
     * 
     * @param samples Audio samples to decode
     * @return Decode result with data or error
     */
    DecodeResult decode(const Samples& samples);
    
    /**
     * Decode from file
     * @param filename Path to audio file (PCM or WAV)
     * @return Decode result with data or error
     */
    DecodeResult decode_file(const std::string& filename);
    
    // --------------------------------------------------------
    // Streaming Decoding
    // --------------------------------------------------------
    
    /**
     * Start streaming decode session
     * Resets state and prepares for sample input.
     */
    void start();
    
    /**
     * Push audio samples for processing
     * @param samples Audio samples to process
     * @return Success or error
     */
    Result<void> push_samples(const Samples& samples);
    
    /**
     * Stop streaming decode session
     */
    void stop();
    
    /**
     * Get current receiver state
     */
    RxState state() const;
    
    /**
     * Check if decode is complete
     */
    bool is_complete() const;
    
    /**
     * Check if an error occurred
     */
    bool has_error() const;
    
    /**
     * Get decode result (call after is_complete() returns true)
     * @return Decode result
     */
    DecodeResult get_result() const;
    
    // --------------------------------------------------------
    // Status & Diagnostics
    // --------------------------------------------------------
    
    /**
     * Get detected mode (valid after sync)
     */
    Mode detected_mode() const;
    
    /**
     * Get current SNR estimate (dB)
     */
    float snr() const;
    
    /**
     * Get frequency offset estimate (Hz)
     */
    float freq_offset() const;
    
    /**
     * Get statistics
     */
    ModemStats stats() const;
    
    /**
     * Reset statistics counters
     */
    void reset_stats();
    
    /**
     * Get number of samples processed
     */
    size_t samples_processed() const;
    
    // --------------------------------------------------------
    // Signal Analysis
    // --------------------------------------------------------
    
    /**
     * Check if signal is present
     * @param samples Audio samples to analyze
     * @param threshold_db Detection threshold in dB
     * @return true if signal detected
     */
    static bool signal_present(const Samples& samples, 
                               float threshold_db = -20.0f);
    
    /**
     * Estimate signal SNR
     * @param samples Audio samples to analyze
     * @return Estimated SNR in dB
     */
    static float estimate_snr(const Samples& samples);

private:
    std::unique_ptr<ModemRXImpl> impl_;
};

} // namespace api
} // namespace m110a

#endif // M110A_API_MODEM_RX_H

