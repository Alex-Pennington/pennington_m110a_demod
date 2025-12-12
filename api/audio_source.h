// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file audio_source.h
 * @brief Audio input source - wraps existing 48kHz real sample path
 * 
 * This source takes real-valued audio samples (from audio device or PCM file)
 * and converts them to complex baseband using a Hilbert transform.
 * 
 * Note: The actual Hilbert transform is not performed here - the existing
 * BrainDecoder already handles downconversion from real samples to complex
 * baseband. This class serves as an adapter to the SampleSource interface.
 */

#ifndef M110A_API_AUDIO_SOURCE_H
#define M110A_API_AUDIO_SOURCE_H

#include "sample_source.h"
#include <vector>
#include <cmath>

namespace m110a {

/**
 * Audio input source - wraps existing 48kHz real sample path.
 * 
 * The existing modem path (BrainDecoder) takes real samples and internally
 * performs downconversion to complex baseband. This class provides the
 * SampleSource interface for real audio samples.
 * 
 * For direct complex output (I/Q), use IQSource instead.
 */
class AudioSource : public SampleSource {
public:
    /**
     * Construct from pre-loaded audio samples.
     * 
     * @param samples Audio samples (float, normalized -1.0 to +1.0)
     * @param sample_rate Sample rate in Hz (default 48000)
     */
    explicit AudioSource(const std::vector<float>& samples, double sample_rate = 48000.0)
        : samples_(samples)
        , sample_rate_(sample_rate)
        , read_pos_(0)
        , carrier_freq_(1800.0)
        , phase_(0.0f) {
    }
    
    /**
     * Construct from raw PCM data.
     * 
     * @param pcm_data PCM samples (int16_t)
     * @param count Number of samples
     * @param sample_rate Sample rate in Hz (default 48000)
     */
    AudioSource(const int16_t* pcm_data, size_t count, double sample_rate = 48000.0)
        : sample_rate_(sample_rate)
        , read_pos_(0)
        , carrier_freq_(1800.0)
        , phase_(0.0f) {
        samples_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            samples_.push_back(static_cast<float>(pcm_data[i]) / 32768.0f);
        }
    }
    
    /**
     * Default constructor for streaming use.
     */
    AudioSource()
        : sample_rate_(48000.0)
        , read_pos_(0)
        , carrier_freq_(1800.0)
        , phase_(0.0f) {
    }
    
    /**
     * Push new audio samples into the source (streaming mode).
     * 
     * @param samples Audio samples to add
     */
    void push_samples(const std::vector<float>& samples) {
        samples_.insert(samples_.end(), samples.begin(), samples.end());
    }
    
    /**
     * Push PCM samples into the source (streaming mode).
     * 
     * @param pcm_data PCM samples (int16_t)
     * @param count Number of samples
     */
    void push_samples_pcm(const int16_t* pcm_data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            samples_.push_back(static_cast<float>(pcm_data[i]) / 32768.0f);
        }
    }
    
    /**
     * Set carrier frequency for downconversion.
     * 
     * @param freq Carrier frequency in Hz (default 1800)
     */
    void set_carrier_freq(double freq) {
        carrier_freq_ = freq;
    }
    
    /**
     * Read complex baseband samples.
     * 
     * Performs downconversion from real audio to complex baseband.
     * This is the same operation done by BrainDecoder::downconvert_and_filter()
     * but without the RRC matched filter (which is applied later).
     * 
     * @param out Buffer to receive complex samples
     * @param count Maximum samples to read
     * @return Actual samples read
     */
    size_t read(std::complex<float>* out, size_t count) override {
        size_t available = samples_.size() - read_pos_;
        size_t to_read = std::min(count, available);
        
        if (to_read == 0) return 0;
        
        // Downconvert real samples to complex baseband
        const float phase_inc = static_cast<float>(2.0 * M_PI * carrier_freq_ / sample_rate_);
        
        for (size_t i = 0; i < to_read; ++i) {
            float sample = samples_[read_pos_ + i];
            
            // Multiply by complex exponential to shift to baseband
            out[i] = std::complex<float>(
                sample * std::cos(phase_),
                -sample * std::sin(phase_)
            );
            
            phase_ += phase_inc;
            if (phase_ > 2.0f * static_cast<float>(M_PI)) {
                phase_ -= 2.0f * static_cast<float>(M_PI);
            }
        }
        
        read_pos_ += to_read;
        return to_read;
    }
    
    /**
     * Get the raw real samples (for backward compatibility).
     * 
     * @return Reference to the internal sample buffer
     */
    const std::vector<float>& raw_samples() const {
        return samples_;
    }
    
    double sample_rate() const override {
        return sample_rate_;
    }
    
    bool has_data() const override {
        return read_pos_ < samples_.size();
    }
    
    const char* source_type() const override {
        return "audio";
    }
    
    void reset() override {
        read_pos_ = 0;
        phase_ = 0.0f;
    }
    
    /**
     * Get number of samples remaining.
     */
    size_t samples_remaining() const {
        return samples_.size() - read_pos_;
    }
    
    /**
     * Get total number of samples.
     */
    size_t total_samples() const {
        return samples_.size();
    }

private:
    std::vector<float> samples_;
    double sample_rate_;
    size_t read_pos_;
    double carrier_freq_;
    float phase_;
};

} // namespace m110a

#endif // M110A_API_AUDIO_SOURCE_H
