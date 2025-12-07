#ifndef M110A_AUDIO_INTERFACE_H
#define M110A_AUDIO_INTERFACE_H

#include "common/types.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

#ifdef __linux__
#include <alsa/asoundlib.h>
#define AUDIO_BACKEND_ALSA 1
#else
#define AUDIO_BACKEND_FILE 1
#endif

namespace m110a {

/**
 * Audio Device Information
 */
struct AudioDeviceInfo {
    std::string name;
    std::string description;
    int max_input_channels;
    int max_output_channels;
    std::vector<int> supported_sample_rates;
};

/**
 * Audio Stream Configuration
 */
struct AudioConfig {
    std::string device_name;      // Device identifier
    int sample_rate;              // Sample rate (default 8000)
    int channels;                 // Number of channels (1=mono)
    int frames_per_buffer;        // Buffer size in frames
    
    AudioConfig()
        : device_name("default")
        , sample_rate(8000)
        , channels(1)
        , frames_per_buffer(256) {}
};

/**
 * Thread-safe sample queue for audio I/O
 */
template<typename T>
class SampleQueue {
public:
    explicit SampleQueue(size_t max_size = 65536)
        : max_size_(max_size) {}
    
    void push(const std::vector<T>& samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : samples) {
            if (queue_.size() < max_size_) {
                queue_.push(s);
            }
        }
        cv_.notify_one();
    }
    
    void push(T sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() < max_size_) {
            queue_.push(sample);
        }
        cv_.notify_one();
    }
    
    std::vector<T> pop(size_t count, bool block = true) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (block) {
            cv_.wait(lock, [this, count] { 
                return queue_.size() >= count || !running_; 
            });
        }
        
        std::vector<T> result;
        result.reserve(count);
        
        while (!queue_.empty() && result.size() < count) {
            result.push_back(queue_.front());
            queue_.pop();
        }
        
        return result;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
    }
    
    void stop() {
        running_ = false;
        cv_.notify_all();
    }
    
    void start() {
        running_ = true;
    }

private:
    std::queue<T> queue_;
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
};

/**
 * Audio Interface Base Class
 */
class AudioInterface {
public:
    using RxCallback = std::function<void(const std::vector<sample_t>&)>;
    
    virtual ~AudioInterface() = default;
    
    // Device enumeration
    virtual std::vector<AudioDeviceInfo> list_devices() = 0;
    
    // Stream control
    virtual bool open(const AudioConfig& config) = 0;
    virtual void close() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    
    // Status
    virtual bool is_running() const = 0;
    virtual std::string last_error() const = 0;
    
    // I/O
    virtual void set_rx_callback(RxCallback callback) = 0;
    virtual void transmit(const std::vector<sample_t>& samples) = 0;
    
    // PTT control (for radio interfacing)
    virtual void set_ptt(bool on) { ptt_state_ = on; }
    virtual bool get_ptt() const { return ptt_state_; }

protected:
    std::atomic<bool> ptt_state_{false};
};

#ifdef AUDIO_BACKEND_ALSA

/**
 * ALSA Audio Interface (Linux)
 */
class AlsaAudioInterface : public AudioInterface {
public:
    AlsaAudioInterface() 
        : capture_handle_(nullptr)
        , playback_handle_(nullptr)
        , running_(false) {}
    
    ~AlsaAudioInterface() override {
        close();
    }
    
    std::vector<AudioDeviceInfo> list_devices() override {
        std::vector<AudioDeviceInfo> devices;
        
        // Add default device
        AudioDeviceInfo def;
        def.name = "default";
        def.description = "Default ALSA device";
        def.max_input_channels = 2;
        def.max_output_channels = 2;
        def.supported_sample_rates = {8000, 16000, 44100, 48000};
        devices.push_back(def);
        
        // Enumerate hardware devices
        int card = -1;
        while (snd_card_next(&card) == 0 && card >= 0) {
            char* name = nullptr;
            if (snd_card_get_name(card, &name) == 0 && name) {
                AudioDeviceInfo info;
                info.name = "hw:" + std::to_string(card);
                info.description = name;
                info.max_input_channels = 2;
                info.max_output_channels = 2;
                info.supported_sample_rates = {8000, 16000, 44100, 48000};
                devices.push_back(info);
                free(name);
            }
        }
        
        // Add plughw devices (with automatic conversion)
        for (int i = 0; i < 4; i++) {
            AudioDeviceInfo info;
            info.name = "plughw:" + std::to_string(i);
            info.description = "Plug device " + std::to_string(i);
            info.max_input_channels = 2;
            info.max_output_channels = 2;
            info.supported_sample_rates = {8000, 16000, 44100, 48000};
            devices.push_back(info);
        }
        
        return devices;
    }
    
    bool open(const AudioConfig& config) override {
        config_ = config;
        
        // Open capture device
        int err = snd_pcm_open(&capture_handle_, config.device_name.c_str(),
                               SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            last_error_ = "Cannot open capture device: " + 
                          std::string(snd_strerror(err));
            return false;
        }
        
        // Open playback device
        err = snd_pcm_open(&playback_handle_, config.device_name.c_str(),
                           SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            last_error_ = "Cannot open playback device: " + 
                          std::string(snd_strerror(err));
            snd_pcm_close(capture_handle_);
            capture_handle_ = nullptr;
            return false;
        }
        
        // Configure devices
        if (!configure_device(capture_handle_, config) ||
            !configure_device(playback_handle_, config)) {
            close();
            return false;
        }
        
        return true;
    }
    
    void close() override {
        stop();
        
        if (capture_handle_) {
            snd_pcm_close(capture_handle_);
            capture_handle_ = nullptr;
        }
        
        if (playback_handle_) {
            snd_pcm_close(playback_handle_);
            playback_handle_ = nullptr;
        }
    }
    
    bool start() override {
        if (!capture_handle_ || !playback_handle_) {
            last_error_ = "Devices not opened";
            return false;
        }
        
        running_ = true;
        tx_queue_.start();
        
        // Start capture thread
        capture_thread_ = std::thread([this]() {
            capture_loop();
        });
        
        // Start playback thread
        playback_thread_ = std::thread([this]() {
            playback_loop();
        });
        
        return true;
    }
    
    void stop() override {
        running_ = false;
        tx_queue_.stop();
        
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        
        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }
    }
    
    bool is_running() const override {
        return running_;
    }
    
    std::string last_error() const override {
        return last_error_;
    }
    
    void set_rx_callback(RxCallback callback) override {
        rx_callback_ = callback;
    }
    
    void transmit(const std::vector<sample_t>& samples) override {
        tx_queue_.push(samples);
    }

private:
    snd_pcm_t* capture_handle_;
    snd_pcm_t* playback_handle_;
    AudioConfig config_;
    std::atomic<bool> running_;
    std::string last_error_;
    
    std::thread capture_thread_;
    std::thread playback_thread_;
    
    RxCallback rx_callback_;
    SampleQueue<sample_t> tx_queue_;
    
    bool configure_device(snd_pcm_t* handle, const AudioConfig& config) {
        snd_pcm_hw_params_t* params;
        snd_pcm_hw_params_alloca(&params);
        
        int err = snd_pcm_hw_params_any(handle, params);
        if (err < 0) {
            last_error_ = "Cannot get hardware params: " + 
                          std::string(snd_strerror(err));
            return false;
        }
        
        // Set access type
        err = snd_pcm_hw_params_set_access(handle, params,
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            last_error_ = "Cannot set access type: " + 
                          std::string(snd_strerror(err));
            return false;
        }
        
        // Set sample format (16-bit signed)
        err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
        if (err < 0) {
            last_error_ = "Cannot set format: " + std::string(snd_strerror(err));
            return false;
        }
        
        // Set channels
        err = snd_pcm_hw_params_set_channels(handle, params, config.channels);
        if (err < 0) {
            last_error_ = "Cannot set channels: " + std::string(snd_strerror(err));
            return false;
        }
        
        // Set sample rate
        unsigned int rate = config.sample_rate;
        err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
        if (err < 0) {
            last_error_ = "Cannot set rate: " + std::string(snd_strerror(err));
            return false;
        }
        
        // Set buffer size
        snd_pcm_uframes_t buffer_size = config.frames_per_buffer * 4;
        err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size);
        if (err < 0) {
            last_error_ = "Cannot set buffer size: " + 
                          std::string(snd_strerror(err));
            return false;
        }
        
        // Apply parameters
        err = snd_pcm_hw_params(handle, params);
        if (err < 0) {
            last_error_ = "Cannot apply params: " + std::string(snd_strerror(err));
            return false;
        }
        
        return true;
    }
    
    void capture_loop() {
        std::vector<int16_t> buffer(config_.frames_per_buffer * config_.channels);
        std::vector<sample_t> float_buffer(config_.frames_per_buffer);
        
        while (running_) {
            snd_pcm_sframes_t frames = snd_pcm_readi(
                capture_handle_, buffer.data(), config_.frames_per_buffer);
            
            if (frames < 0) {
                // Handle underrun
                snd_pcm_prepare(capture_handle_);
                continue;
            }
            
            // Convert to float
            for (snd_pcm_sframes_t i = 0; i < frames; i++) {
                float_buffer[i] = pcm_to_float(buffer[i * config_.channels]);
            }
            float_buffer.resize(frames);
            
            // Call callback
            if (rx_callback_) {
                rx_callback_(float_buffer);
            }
        }
    }
    
    void playback_loop() {
        std::vector<int16_t> buffer(config_.frames_per_buffer * config_.channels);
        
        while (running_) {
            // Get samples from queue
            auto samples = tx_queue_.pop(config_.frames_per_buffer, true);
            
            if (samples.empty() && !running_) break;
            
            // Convert to PCM
            for (size_t i = 0; i < samples.size(); i++) {
                int16_t pcm = float_to_pcm(samples[i]);
                for (int ch = 0; ch < config_.channels; ch++) {
                    buffer[i * config_.channels + ch] = pcm;
                }
            }
            
            // Pad with silence if needed
            for (size_t i = samples.size(); i < (size_t)config_.frames_per_buffer; i++) {
                for (int ch = 0; ch < config_.channels; ch++) {
                    buffer[i * config_.channels + ch] = 0;
                }
            }
            
            // Write to device
            snd_pcm_sframes_t frames = snd_pcm_writei(
                playback_handle_, buffer.data(), config_.frames_per_buffer);
            
            if (frames < 0) {
                snd_pcm_prepare(playback_handle_);
            }
        }
    }
};

using PlatformAudioInterface = AlsaAudioInterface;

#else

/**
 * File-based Audio Interface (for testing without audio hardware)
 */
class FileAudioInterface : public AudioInterface {
public:
    FileAudioInterface() : running_(false) {}
    
    ~FileAudioInterface() override {
        close();
    }
    
    std::vector<AudioDeviceInfo> list_devices() override {
        std::vector<AudioDeviceInfo> devices;
        
        AudioDeviceInfo file_dev;
        file_dev.name = "file";
        file_dev.description = "File-based audio (for testing)";
        file_dev.max_input_channels = 1;
        file_dev.max_output_channels = 1;
        file_dev.supported_sample_rates = {8000};
        devices.push_back(file_dev);
        
        return devices;
    }
    
    bool open(const AudioConfig& config) override {
        config_ = config;
        return true;
    }
    
    void close() override {
        stop();
    }
    
    bool start() override {
        running_ = true;
        return true;
    }
    
    void stop() override {
        running_ = false;
    }
    
    bool is_running() const override {
        return running_;
    }
    
    std::string last_error() const override {
        return last_error_;
    }
    
    void set_rx_callback(RxCallback callback) override {
        rx_callback_ = callback;
    }
    
    void transmit(const std::vector<sample_t>& samples) override {
        tx_buffer_.insert(tx_buffer_.end(), samples.begin(), samples.end());
    }
    
    // File I/O for testing
    void load_rx_file(const std::string& filename) {
        (void)filename;
        // Load PCM file and feed to callback
    }
    
    void save_tx_file(const std::string& filename) {
        (void)filename;
        // Save TX buffer to PCM file
    }
    
    const std::vector<sample_t>& get_tx_buffer() const {
        return tx_buffer_;
    }

private:
    AudioConfig config_;
    std::atomic<bool> running_;
    std::string last_error_;
    RxCallback rx_callback_;
    std::vector<sample_t> tx_buffer_;
};

using PlatformAudioInterface = FileAudioInterface;

#endif

/**
 * Factory function to create appropriate audio interface
 */
inline std::unique_ptr<AudioInterface> create_audio_interface() {
    return std::make_unique<PlatformAudioInterface>();
}

} // namespace m110a

#endif // M110A_AUDIO_INTERFACE_H
