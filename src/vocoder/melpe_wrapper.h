/* ================================================================== */
/*                                                                    */
/*    MELPe Vocoder C++ Wrapper Stub                                  */
/*    Stub for future integration with melpe_core                     */
/*                                                                    */
/*    Copyright (c) 2024-2025 Alex Pennington                         */
/*                                                                    */
/* ================================================================== */

#ifndef MELPE_WRAPPER_H
#define MELPE_WRAPPER_H

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>

namespace vocoder {

/* ================================================================== */
/*                         Constants                                  */
/* ================================================================== */

enum class MelpeRate {
    RATE_600  = 600,
    RATE_1200 = 1200,
    RATE_2400 = 2400
};

/* Frame sizes in samples (at 8000 Hz sample rate) */
constexpr int MELPE_FRAME_SAMPLES_2400 = 180;   // 22.5 ms
constexpr int MELPE_FRAME_SAMPLES_1200 = 540;   // 67.5 ms
constexpr int MELPE_FRAME_SAMPLES_600  = 720;   // 90.0 ms

/* Bitstream sizes in bytes */
constexpr int MELPE_FRAME_BYTES_2400 = 7;       // 54 bits
constexpr int MELPE_FRAME_BYTES_1200 = 11;      // 81 bits
constexpr int MELPE_FRAME_BYTES_600  = 7;       // 54 bits

/* Audio parameters */
constexpr int MELPE_SAMPLE_RATE = 8000;         // 8 kHz
constexpr int MELPE_SAMPLE_BITS = 16;           // 16-bit signed PCM

/* ================================================================== */
/*                      Callback Types                                */
/* ================================================================== */

using AudioCallback = std::function<void(const int16_t*, int)>;
using BitstreamCallback = std::function<void(const uint8_t*, int)>;

/* ================================================================== */
/*                      MelpeEncoder Class                            */
/* ================================================================== */

/**
 * MELPe Encoder wrapper
 * 
 * STUB: This class provides the interface for future integration
 * with the melpe_core C library. Currently returns empty/zero data.
 */
class MelpeEncoder {
public:
    /**
     * Create encoder with specified rate
     * @param rate Bit rate (600, 1200, or 2400 bps)
     * @param enable_npp Enable noise pre-processor
     */
    explicit MelpeEncoder(MelpeRate rate = MelpeRate::RATE_2400, bool enable_npp = true);
    ~MelpeEncoder();
    
    // Non-copyable
    MelpeEncoder(const MelpeEncoder&) = delete;
    MelpeEncoder& operator=(const MelpeEncoder&) = delete;
    
    // Movable
    MelpeEncoder(MelpeEncoder&&) noexcept;
    MelpeEncoder& operator=(MelpeEncoder&&) noexcept;
    
    /**
     * Check if encoder is valid/initialized
     */
    bool isValid() const;
    
    /**
     * Encode PCM audio samples
     * @param samples Input 16-bit signed PCM at 8kHz
     * @param num_samples Number of samples
     * @param output Output buffer for encoded bits
     * @param output_size Size of output buffer
     * @return Number of bytes written, or -1 on error
     */
    int encode(const int16_t* samples, int num_samples,
               uint8_t* output, int output_size);
    
    /**
     * Encode with vector interface
     */
    std::vector<uint8_t> encode(const std::vector<int16_t>& samples);
    
    /**
     * Set callback for encoded output
     */
    void setCallback(BitstreamCallback callback);
    
    /**
     * Get frame size in samples for current rate
     */
    int getFrameSizeSamples() const;
    
    /**
     * Get output size in bytes per frame
     */
    int getFrameSizeBytes() const;
    
    /**
     * Get current rate
     */
    MelpeRate getRate() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/* ================================================================== */
/*                      MelpeDecoder Class                            */
/* ================================================================== */

/**
 * MELPe Decoder wrapper
 * 
 * STUB: This class provides the interface for future integration
 * with the melpe_core C library. Currently returns empty/zero data.
 */
class MelpeDecoder {
public:
    /**
     * Create decoder with specified rate
     * @param rate Bit rate (600, 1200, or 2400 bps)
     * @param enable_postfilter Enable post-filter for improved quality
     */
    explicit MelpeDecoder(MelpeRate rate = MelpeRate::RATE_2400, bool enable_postfilter = true);
    ~MelpeDecoder();
    
    // Non-copyable
    MelpeDecoder(const MelpeDecoder&) = delete;
    MelpeDecoder& operator=(const MelpeDecoder&) = delete;
    
    // Movable
    MelpeDecoder(MelpeDecoder&&) noexcept;
    MelpeDecoder& operator=(MelpeDecoder&&) noexcept;
    
    /**
     * Check if decoder is valid/initialized
     */
    bool isValid() const;
    
    /**
     * Decode MELPe bitstream to PCM
     * @param bits Input bitstream
     * @param num_bytes Number of input bytes
     * @param output Output buffer for PCM samples
     * @param output_size Size of output buffer in samples
     * @return Number of samples written, or -1 on error
     */
    int decode(const uint8_t* bits, int num_bytes,
               int16_t* output, int output_size);
    
    /**
     * Decode with vector interface
     */
    std::vector<int16_t> decode(const std::vector<uint8_t>& bits);
    
    /**
     * Set callback for decoded audio
     */
    void setCallback(AudioCallback callback);
    
    /**
     * Handle frame erasure (error concealment)
     */
    int frameErasure(int16_t* output, int output_size);
    
    /**
     * Get frame size in samples for current rate
     */
    int getFrameSizeSamples() const;
    
    /**
     * Get input size in bytes per frame
     */
    int getFrameSizeBytes() const;
    
    /**
     * Get current rate
     */
    MelpeRate getRate() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/* ================================================================== */
/*                      Utility Functions                             */
/* ================================================================== */

/**
 * Get MELPe wrapper version
 */
const char* melpe_wrapper_version();

/**
 * Check if MELPe core is available (linked)
 * STUB: Returns false until melpe_core is integrated
 */
bool melpe_core_available();

/**
 * Get frame size in samples for a given rate
 */
int melpe_frame_samples(MelpeRate rate);

/**
 * Get frame size in bytes for a given rate
 */
int melpe_frame_bytes(MelpeRate rate);

/**
 * Get frame duration in milliseconds
 */
float melpe_frame_duration_ms(MelpeRate rate);

} // namespace vocoder

#endif /* MELPE_WRAPPER_H */
