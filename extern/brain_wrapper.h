#pragma once
/**
 * @file brain_wrapper.h
 * @brief Clean C++ wrapper for Brain Modem (m188110a) core
 * 
 * Provides simple encode/decode functions for interoperability testing
 * between PhoenixNest and Brain Modem implementations.
 */

#ifndef BRAIN_WRAPPER_H
#define BRAIN_WRAPPER_H

// IMPORTANT: Include Brain/Windows headers BEFORE C++ stdlib to avoid std::byte conflict
#include "brain_core/m188110a/Cm110s.h"

// Now safe to include C++ standard library
#include <vector>
#include <cstdint>
#include <string>
#include <functional>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>

namespace brain {

// Brain modem mode enum - wraps the global Mode from Cm110s.h
enum class Mode {
    M75S   = ::M75NS,
    M75L   = ::M75NL,
    M150S  = ::M150S,
    M150L  = ::M150L,
    M300S  = ::M300S,
    M300L  = ::M300L,
    M600S  = ::M600S,
    M600L  = ::M600L,
    M1200S = ::M1200S,
    M1200L = ::M1200L,
    M2400S = ::M2400S,
    M2400L = ::M2400L,
    M4800S = ::M4800S
};

inline const char* mode_to_string(Mode m) {
    switch (m) {
        case Mode::M75S:   return "75S";
        case Mode::M75L:   return "75L";
        case Mode::M150S:  return "150S";
        case Mode::M150L:  return "150L";
        case Mode::M300S:  return "300S";
        case Mode::M300L:  return "300L";
        case Mode::M600S:  return "600S";
        case Mode::M600L:  return "600L";
        case Mode::M1200S: return "1200S";
        case Mode::M1200L: return "1200L";
        case Mode::M2400S: return "2400S";
        case Mode::M2400L: return "2400L";
        case Mode::M4800S: return "4800S";
        default:           return "UNKNOWN";
    }
}

inline ::Mode to_brain_mode(Mode m) {
    return static_cast<::Mode>(static_cast<int>(m));
}

/**
 * @brief Brain Modem wrapper class
 * 
 * NOTE: Cm110s is HUGE (~2MB+ due to tx_bit_array[400000] and other arrays)
 * Must be heap-allocated to avoid stack overflow!
 */
class Modem {
public:
    static constexpr int SOUNDBLOCK_SIZE = 1024;  // Must match brain_core server
    
    Modem() : modem_(new Cm110s()) {
        // Initialize like brain_core server does
        modem_->register_receive_octet_callback_function(rx_callback_static);
        modem_->register_status(status_callback_static);
        modem_->tx_set_soundblock_size(SOUNDBLOCK_SIZE);
        modem_->tx_set_mode(::M600S);  // Default
        modem_->rx_enable();
        modem_->tx_enable();
    }
    
    ~Modem() {
        delete modem_;
    }
    
    // No copy
    Modem(const Modem&) = delete;
    Modem& operator=(const Modem&) = delete;
    
    /**
     * @brief Encode data to PCM at native 9600 Hz
     * 
     * Brain TX requires concurrent draining - tx_sync_frame_eom blocks
     * while a separate thread pulls audio via tx_get_soundblock.
     */
    std::vector<float> encode(const std::vector<uint8_t>& data, Mode mode) {
        std::vector<float> pcm;
        std::atomic<bool> tx_done{false};
        
        // Set mode
        modem_->tx_set_mode(to_brain_mode(mode));
        
        // Brain API needs non-const pointer - make a copy
        std::vector<uint8_t> tx_data = data;
        
        // Drain thread - pulls audio blocks while TX runs
        std::thread drain_thread([&]() {
            while (!tx_done.load()) {
                float* block = modem_->tx_get_soundblock();
                if (block != nullptr) {
                    pcm.insert(pcm.end(), block, block + SOUNDBLOCK_SIZE);
                    modem_->tx_release_soundblock(block);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            // Drain remaining blocks after TX completes
            float* block;
            while ((block = modem_->tx_get_soundblock()) != nullptr) {
                pcm.insert(pcm.end(), block, block + SOUNDBLOCK_SIZE);
                modem_->tx_release_soundblock(block);
            }
        });
        
        // This blocks until complete (drain thread empties queue)
        modem_->tx_sync_frame_eom(tx_data.data(), static_cast<int>(tx_data.size()));
        
        tx_done.store(true);
        drain_thread.join();
        
        return pcm;
    }
    
    /**
     * @brief Encode data to 16-bit PCM at 48kHz (resampled from 9600)
     */
    std::vector<int16_t> encode_48k(const std::vector<uint8_t>& data, Mode mode) {
        auto pcm_9600 = encode(data, mode);
        
        // Resample 9600 -> 48000 (5x interpolation)
        std::vector<int16_t> pcm_48k;
        pcm_48k.reserve(pcm_9600.size() * 5);
        
        for (size_t i = 0; i < pcm_9600.size(); i++) {
            float sample = pcm_9600[i];
            float next = (i + 1 < pcm_9600.size()) ? pcm_9600[i + 1] : sample;
            
            for (int j = 0; j < 5; j++) {
                float t = j / 5.0f;
                float interp = sample * (1.0f - t) + next * t;
                int16_t s16 = static_cast<int16_t>(interp * 32000.0f);
                pcm_48k.push_back(s16);
            }
        }
        
        return pcm_48k;
    }
    
    /**
     * @brief Decode PCM audio at 9600 Hz to data bytes
     */
    std::vector<uint8_t> decode(const std::vector<int16_t>& pcm) {
        // Setup callback
        {
            std::lock_guard<std::mutex> lock(rx_mutex_);
            current_instance_ = this;
            decoded_data_.clear();
        }
        
        // Brain API needs non-const pointer - make a copy
        std::vector<int16_t> pcm_copy = pcm;
        
        // Process in blocks
        const int BLOCK_SIZE = 512;
        for (size_t i = 0; i < pcm_copy.size(); i += BLOCK_SIZE) {
            int len = std::min(BLOCK_SIZE, static_cast<int>(pcm_copy.size() - i));
            modem_->rx_process_block(pcm_copy.data() + i, len);
        }
        
        // Flush with silence (like brain_core server does)
        // Need extra flush to fully drain the decoder pipeline
        std::vector<int16_t> flush(1920 * 10, 0);  // 10 frames of silence
        for (size_t i = 0; i < flush.size(); i += BLOCK_SIZE) {
            int len = std::min(BLOCK_SIZE, static_cast<int>(flush.size() - i));
            modem_->rx_process_block(flush.data() + i, len);
        }
        
        std::lock_guard<std::mutex> lock(rx_mutex_);
        current_instance_ = nullptr;
        return decoded_data_;
    }
    
    /**
     * @brief Decode 48kHz 16-bit PCM (decimates to 9600)
     */
    std::vector<uint8_t> decode_48k(const std::vector<int16_t>& pcm) {
        // Decimate 48000 -> 9600 (take every 5th sample)
        std::vector<int16_t> pcm_9600;
        pcm_9600.reserve(pcm.size() / 5);
        
        for (size_t i = 0; i < pcm.size(); i += 5) {
            pcm_9600.push_back(pcm[i]);
        }
        
        return decode(pcm_9600);
    }
    
    const char* get_rx_mode_string() const {
        return modem_->rx_get_mode_string();
    }
    
    /**
     * @brief Get name of detected mode after decode
     * Note: Brain RX only supports auto-detect - no explicit mode setting
     */
    std::string get_detected_mode_name() const {
        const char* s = modem_->rx_get_mode_string();
        return s ? s : "---";
    }
    
    void reset_rx() {
        modem_->rx_reset();
    }

private:
    Cm110s* modem_;
    std::vector<uint8_t> decoded_data_;
    static std::mutex rx_mutex_;
    static Modem* current_instance_;
    
    static void status_callback_static(ModemStatus status, void* data) {
        // Required by Brain - does nothing but prevents crash
        (void)status;
        (void)data;
    }
    
    static void rx_callback_static(uint8_t byte) {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        if (current_instance_) {
            current_instance_->decoded_data_.push_back(byte);
        }
    }
};

// Static member definitions
inline std::mutex Modem::rx_mutex_;
inline Modem* Modem::current_instance_ = nullptr;

} // namespace brain

#endif // BRAIN_WRAPPER_H


