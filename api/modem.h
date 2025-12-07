/**
 * @file modem.h
 * @brief M110A Modem API - Main Include
 * 
 * MIL-STD-188-110A HF Modem Implementation
 * 
 * This is the main header for the M110A modem API. Include this file
 * to get access to all modem functionality.
 * 
 * Features:
 * - All standard modes: 75, 150, 300, 600, 1200, 2400, 4800 bps
 * - Short and Long interleave options
 * - Automatic mode detection
 * - DFE and MLSE equalization
 * - Thread-safe API
 * 
 * Quick Start:
 * @code
 *   #include <m110a/api/modem.h>
 *   using namespace m110a::api;
 *   
 *   // Encode
 *   auto audio = encode("Hello, World!", Mode::M2400_SHORT);
 *   if (audio.ok()) {
 *       save_pcm("output.pcm", audio.value());
 *   }
 *   
 *   // Decode
 *   auto samples = load_pcm("input.pcm");
 *   auto result = decode(samples);
 *   if (result.success) {
 *       std::cout << result.as_string() << std::endl;
 *   }
 * @endcode
 * 
 * @author Phoenix Nest LLC
 * @version 1.0
 */

#ifndef M110A_API_MODEM_H
#define M110A_API_MODEM_H

// Core types
#include "modem_types.h"

// Configuration
#include "modem_config.h"

// TX/RX classes
#include "modem_tx.h"
#include "modem_rx.h"

// Standard library
#include <string>
#include <fstream>

namespace m110a {
namespace api {

// ============================================================
// Version Information
// ============================================================

/// API version
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

/// Get version string
inline std::string version() {
    return std::to_string(VERSION_MAJOR) + "." +
           std::to_string(VERSION_MINOR) + "." +
           std::to_string(VERSION_PATCH);
}

// ============================================================
// Convenience Functions - Encoding
// ============================================================

/**
 * Encode data to audio samples (convenience function)
 * 
 * @param data Data bytes to encode
 * @param mode Operating mode
 * @param sample_rate Output sample rate (default: 48000)
 * @return Audio samples or error
 */
inline Result<Samples> encode(const std::vector<uint8_t>& data,
                               Mode mode = Mode::M2400_SHORT,
                               float sample_rate = SAMPLE_RATE_DEFAULT) {
    TxConfig config;
    config.mode = mode;
    config.sample_rate = sample_rate;
    
    ModemTX tx(config);
    return tx.encode(data);
}

/**
 * Encode string to audio samples (convenience function)
 * 
 * @param text Text to encode
 * @param mode Operating mode
 * @param sample_rate Output sample rate (default: 48000)
 * @return Audio samples or error
 */
inline Result<Samples> encode(const std::string& text,
                               Mode mode = Mode::M2400_SHORT,
                               float sample_rate = SAMPLE_RATE_DEFAULT) {
    std::vector<uint8_t> data(text.begin(), text.end());
    return encode(data, mode, sample_rate);
}

// ============================================================
// Convenience Functions - Decoding
// ============================================================

/**
 * Decode audio samples (convenience function)
 * 
 * @param samples Audio samples to decode
 * @param sample_rate Input sample rate (default: 48000)
 * @return Decode result
 */
inline DecodeResult decode(const Samples& samples,
                           float sample_rate = SAMPLE_RATE_DEFAULT) {
    RxConfig config;
    config.sample_rate = sample_rate;
    config.mode = Mode::AUTO;
    
    ModemRX rx(config);
    return rx.decode(samples);
}

/**
 * Decode with specific mode (no auto-detect)
 * 
 * @param samples Audio samples to decode
 * @param mode Expected mode
 * @param sample_rate Input sample rate
 * @return Decode result
 */
inline DecodeResult decode(const Samples& samples,
                           Mode mode,
                           float sample_rate = SAMPLE_RATE_DEFAULT) {
    RxConfig config;
    config.sample_rate = sample_rate;
    config.mode = mode;
    
    ModemRX rx(config);
    return rx.decode(samples);
}

/**
 * Decode with full configuration
 * 
 * @param samples Audio samples to decode
 * @param config RX configuration (includes equalizer, sample_rate, mode)
 * @return Decode result
 */
inline DecodeResult decode(const Samples& samples, const RxConfig& config) {
    ModemRX rx(config);
    return rx.decode(samples);
}

// ============================================================
// File I/O Helpers
// ============================================================

/**
 * Load PCM file (16-bit signed, mono)
 * 
 * @param filename Path to PCM file
 * @return Audio samples or error
 */
inline Result<Samples> load_pcm(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return Error(ErrorCode::FILE_NOT_FOUND, "Cannot open: " + filename);
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    size_t num_samples = size / 2;
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    
    if (!file) {
        return Error(ErrorCode::FILE_READ_ERROR, "Error reading: " + filename);
    }
    
    Samples samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    
    return samples;
}

/**
 * Save PCM file (16-bit signed, mono)
 * 
 * @param filename Path to PCM file
 * @param samples Audio samples to save
 * @return Success or error
 */
inline Result<void> save_pcm(const std::string& filename,
                              const Samples& samples) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return Error(ErrorCode::FILE_WRITE_ERROR, "Cannot create: " + filename);
    }
    
    std::vector<int16_t> raw(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        raw[i] = static_cast<int16_t>(s * 32767.0f);
    }
    
    file.write(reinterpret_cast<const char*>(raw.data()), 
               raw.size() * sizeof(int16_t));
    
    if (!file) {
        return Error(ErrorCode::FILE_WRITE_ERROR, "Error writing: " + filename);
    }
    
    return Result<void>();
}

/**
 * Load WAV file
 * 
 * @param filename Path to WAV file
 * @param sample_rate Output: detected sample rate
 * @return Audio samples or error
 */
inline Result<Samples> load_wav(const std::string& filename,
                                 float* sample_rate = nullptr) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return Error(ErrorCode::FILE_NOT_FOUND, "Cannot open: " + filename);
    }
    
    // Read WAV header
    char riff[4], wave[4], fmt[4], data_marker[4];
    uint32_t file_size, fmt_size, data_size;
    uint16_t audio_format, num_channels, bits_per_sample;
    uint32_t wav_sample_rate, byte_rate;
    uint16_t block_align;
    
    file.read(riff, 4);
    file.read(reinterpret_cast<char*>(&file_size), 4);
    file.read(wave, 4);
    
    if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        return Error(ErrorCode::INVALID_FILE_FORMAT, "Not a WAV file");
    }
    
    file.read(fmt, 4);
    file.read(reinterpret_cast<char*>(&fmt_size), 4);
    file.read(reinterpret_cast<char*>(&audio_format), 2);
    file.read(reinterpret_cast<char*>(&num_channels), 2);
    file.read(reinterpret_cast<char*>(&wav_sample_rate), 4);
    file.read(reinterpret_cast<char*>(&byte_rate), 4);
    file.read(reinterpret_cast<char*>(&block_align), 2);
    file.read(reinterpret_cast<char*>(&bits_per_sample), 2);
    
    // Skip any extra format bytes
    if (fmt_size > 16) {
        file.seekg(fmt_size - 16, std::ios::cur);
    }
    
    // Find data chunk
    while (file) {
        file.read(data_marker, 4);
        file.read(reinterpret_cast<char*>(&data_size), 4);
        if (std::string(data_marker, 4) == "data") break;
        file.seekg(data_size, std::ios::cur);
    }
    
    if (!file) {
        return Error(ErrorCode::INVALID_FILE_FORMAT, "No data chunk in WAV");
    }
    
    if (sample_rate) {
        *sample_rate = static_cast<float>(wav_sample_rate);
    }
    
    // Read samples
    size_t num_samples = data_size / (bits_per_sample / 8) / num_channels;
    Samples samples(num_samples);
    
    if (bits_per_sample == 16) {
        std::vector<int16_t> raw(num_samples * num_channels);
        file.read(reinterpret_cast<char*>(raw.data()), data_size);
        
        for (size_t i = 0; i < num_samples; i++) {
            // Take first channel if stereo
            samples[i] = raw[i * num_channels] / 32768.0f;
        }
    } else if (bits_per_sample == 8) {
        std::vector<uint8_t> raw(num_samples * num_channels);
        file.read(reinterpret_cast<char*>(raw.data()), data_size);
        
        for (size_t i = 0; i < num_samples; i++) {
            samples[i] = (raw[i * num_channels] - 128) / 128.0f;
        }
    } else {
        return Error(ErrorCode::INVALID_FILE_FORMAT, 
                    "Unsupported bits per sample: " + std::to_string(bits_per_sample));
    }
    
    return samples;
}

/**
 * Save WAV file
 * 
 * @param filename Path to WAV file
 * @param samples Audio samples to save
 * @param sample_rate Sample rate
 * @return Success or error
 */
inline Result<void> save_wav(const std::string& filename,
                              const Samples& samples,
                              float sample_rate = SAMPLE_RATE_DEFAULT) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return Error(ErrorCode::FILE_WRITE_ERROR, "Cannot create: " + filename);
    }
    
    uint32_t sr = static_cast<uint32_t>(sample_rate);
    uint32_t data_size = samples.size() * 2;
    uint32_t file_size = 36 + data_size;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels = 1;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sr * num_channels * bits_per_sample / 8;
    uint16_t block_align = num_channels * bits_per_sample / 8;
    
    // Write header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&file_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    uint32_t fmt_size = 16;
    file.write(reinterpret_cast<const char*>(&fmt_size), 4);
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    file.write(reinterpret_cast<const char*>(&num_channels), 2);
    file.write(reinterpret_cast<const char*>(&sr), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_size), 4);
    
    // Write samples
    std::vector<int16_t> raw(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        raw[i] = static_cast<int16_t>(s * 32767.0f);
    }
    
    file.write(reinterpret_cast<const char*>(raw.data()), data_size);
    
    if (!file) {
        return Error(ErrorCode::FILE_WRITE_ERROR, "Error writing: " + filename);
    }
    
    return Result<void>();
}

// ============================================================
// High-Level File Operations
// ============================================================

/**
 * Encode data and save to file
 * 
 * @param data Data bytes to encode
 * @param filename Output audio file path
 * @param mode Operating mode
 * @return Success or error
 */
inline Result<void> encode_to_file(const std::vector<uint8_t>& data,
                                    const std::string& filename,
                                    Mode mode = Mode::M2400_SHORT) {
    auto result = encode(data, mode);
    if (!result.ok()) {
        return result.error();
    }
    
    // Determine format from extension
    std::string ext = filename.substr(filename.find_last_of('.') + 1);
    if (ext == "wav" || ext == "WAV") {
        return save_wav(filename, result.value());
    } else {
        return save_pcm(filename, result.value());
    }
}

/**
 * Decode audio file
 * 
 * @param filename Input audio file path
 * @return Decode result
 */
inline DecodeResult decode_file(const std::string& filename) {
    // Determine format from extension
    std::string ext = filename.substr(filename.find_last_of('.') + 1);
    
    float sample_rate = SAMPLE_RATE_DEFAULT;
    Result<Samples> samples_result = (ext == "wav" || ext == "WAV") 
        ? load_wav(filename, &sample_rate)
        : load_pcm(filename);
    
    if (!samples_result.ok()) {
        DecodeResult result;
        result.success = false;
        result.error = samples_result.error();
        return result;
    }
    
    return decode(samples_result.value(), sample_rate);
}

} // namespace api
} // namespace m110a

#endif // M110A_API_MODEM_H
