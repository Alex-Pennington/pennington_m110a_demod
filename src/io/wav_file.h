#ifndef M110A_WAV_FILE_H
#define M110A_WAV_FILE_H

#include "common/types.h"
#include "common/constants.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>

namespace m110a {

/**
 * Simple WAV file reader/writer
 * 
 * Supports:
 *   - 8-bit unsigned PCM
 *   - 16-bit signed PCM
 *   - Mono and stereo
 *   - Common sample rates (8000, 16000, 44100, 48000)
 */

#pragma pack(push, 1)
struct WavHeader {
    // RIFF chunk
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    
    // Format chunk
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // 16 for PCM
    uint16_t audio_format;  // 1 for PCM
    uint16_t num_channels;  // 1=mono, 2=stereo
    uint32_t sample_rate;   // e.g., 8000
    uint32_t byte_rate;     // sample_rate * channels * bits/8
    uint16_t block_align;   // channels * bits/8
    uint16_t bits_per_sample; // 8 or 16
    
    // Data chunk
    char data[4];           // "data"
    uint32_t data_size;     // Number of bytes of audio data
};
#pragma pack(pop)

/**
 * Read WAV file to float samples
 */
inline bool read_wav_file(const std::string& filename, 
                          std::vector<sample_t>& samples,
                          int& sample_rate,
                          int& channels) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    
    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        return false;  // Not a WAV file
    }
    
    sample_rate = header.sample_rate;
    channels = header.num_channels;
    
    size_t num_samples = header.data_size / (header.bits_per_sample / 8) / channels;
    samples.resize(num_samples);
    
    if (header.bits_per_sample == 16) {
        std::vector<int16_t> pcm(num_samples * channels);
        file.read(reinterpret_cast<char*>(pcm.data()), header.data_size);
        
        for (size_t i = 0; i < num_samples; i++) {
            // Mix to mono if stereo
            if (channels == 2) {
                samples[i] = (pcm_to_float(pcm[i*2]) + pcm_to_float(pcm[i*2+1])) / 2.0f;
            } else {
                samples[i] = pcm_to_float(pcm[i]);
            }
        }
    } else if (header.bits_per_sample == 8) {
        std::vector<uint8_t> pcm(num_samples * channels);
        file.read(reinterpret_cast<char*>(pcm.data()), header.data_size);
        
        for (size_t i = 0; i < num_samples; i++) {
            if (channels == 2) {
                samples[i] = ((pcm[i*2] - 128) / 128.0f + (pcm[i*2+1] - 128) / 128.0f) / 2.0f;
            } else {
                samples[i] = (pcm[i] - 128) / 128.0f;
            }
        }
    } else {
        return false;  // Unsupported format
    }
    
    return true;
}

/**
 * Write float samples to WAV file
 */
inline bool write_wav_file(const std::string& filename,
                           const std::vector<sample_t>& samples,
                           int sample_rate = 8000,
                           int channels = 1,
                           int bits = 16) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    
    uint32_t data_size = static_cast<uint32_t>(samples.size() * channels * (bits / 8));
    
    WavHeader header;
    std::memcpy(header.riff, "RIFF", 4);
    header.file_size = sizeof(WavHeader) - 8 + data_size;
    std::memcpy(header.wave, "WAVE", 4);
    std::memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;  // PCM
    header.num_channels = static_cast<uint16_t>(channels);
    header.sample_rate = static_cast<uint32_t>(sample_rate);
    header.byte_rate = static_cast<uint32_t>(sample_rate * channels * (bits / 8));
    header.block_align = static_cast<uint16_t>(channels * (bits / 8));
    header.bits_per_sample = static_cast<uint16_t>(bits);
    std::memcpy(header.data, "data", 4);
    header.data_size = data_size;
    
    file.write(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (bits == 16) {
        for (size_t i = 0; i < samples.size(); i++) {
            int16_t pcm = float_to_pcm(samples[i]);
            for (int ch = 0; ch < channels; ch++) {
                file.write(reinterpret_cast<char*>(&pcm), sizeof(pcm));
            }
        }
    } else if (bits == 8) {
        for (size_t i = 0; i < samples.size(); i++) {
            uint8_t pcm = static_cast<uint8_t>(samples[i] * 127.0f + 128.0f);
            for (int ch = 0; ch < channels; ch++) {
                file.write(reinterpret_cast<char*>(&pcm), sizeof(pcm));
            }
        }
    }
    
    return true;
}

/**
 * Quick save for TX output
 */
inline bool save_tx_wav(const std::string& filename, 
                        const std::vector<sample_t>& samples) {
    return write_wav_file(filename, samples, SAMPLE_RATE, 1, 16);
}

/**
 * Quick load for RX input
 */
inline bool load_rx_wav(const std::string& filename,
                        std::vector<sample_t>& samples) {
    int rate, channels;
    if (!read_wav_file(filename, samples, rate, channels)) {
        return false;
    }
    
    // Resample if needed
    if (rate != SAMPLE_RATE) {
        // Simple linear resampling
        float ratio = (float)SAMPLE_RATE / rate;
        std::vector<sample_t> resampled;
        resampled.reserve(static_cast<size_t>(samples.size() * ratio));
        
        for (float i = 0; i < samples.size() - 1; i += 1.0f / ratio) {
            size_t idx = static_cast<size_t>(i);
            float frac = i - idx;
            resampled.push_back(samples[idx] * (1.0f - frac) + samples[idx + 1] * frac);
        }
        
        samples = std::move(resampled);
    }
    
    return true;
}

} // namespace m110a

#endif // M110A_WAV_FILE_H
