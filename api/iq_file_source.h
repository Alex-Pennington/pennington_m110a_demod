// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file iq_file_source.h
 * @brief I/Q file source - reads .iqr files from phoenix_sdr
 * 
 * Wraps IQSource to read pre-recorded I/Q captures from .iqr files.
 * This enables file-based testing without live SDR hardware.
 * 
 * .iqr file format:
 *   - 64-byte header (magic, sample rate, center freq, etc.)
 *   - Interleaved int16 I/Q data: I0, Q0, I1, Q1, ... (little-endian)
 *   - Sample rate: 2 MSPS (2,000,000 Hz)
 */

#ifndef M110A_API_IQ_FILE_SOURCE_H
#define M110A_API_IQ_FILE_SOURCE_H

#include "iq_source.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <memory>

namespace m110a {

/**
 * .iqr file header structure (64 bytes)
 * Matches phoenix_sdr's iq_recorder.h format
 */
#pragma pack(push, 1)
struct IQRHeader {
    char     magic[4];       // "IQR1"
    uint32_t version;        // Format version (1)
    double   sample_rate;    // Hz (e.g., 2000000.0)
    double   center_freq;    // Hz
    uint32_t bandwidth;      // kHz
    int32_t  gain_reduction; // dB
    uint32_t lna_state;      // 0-8
    int64_t  start_time;     // Unix microseconds
    uint64_t sample_count;   // Total samples
    uint32_t flags;          // Reserved
    uint8_t  reserved[8];    // Padding to 64 bytes
};
#pragma pack(pop)

static_assert(sizeof(IQRHeader) == 64, "IQRHeader must be exactly 64 bytes");

/**
 * I/Q file source - reads .iqr files and feeds to IQSource for decimation.
 * 
 * Usage:
 * @code
 *   m110a::IQFileSource file_source("capture.iqr");
 *   if (!file_source.is_open()) {
 *       fprintf(stderr, "Failed to open: %s\n", file_source.error().c_str());
 *       return;
 *   }
 *   
 *   printf("Center freq: %.0f Hz\n", file_source.center_frequency());
 *   printf("Sample rate: %.0f Hz\n", file_source.input_rate());
 *   
 *   // Load all samples (or call load_chunk() for streaming)
 *   file_source.load_all();
 *   
 *   // Read decimated output (48 kHz complex)
 *   std::complex<float> buffer[1024];
 *   while (file_source.has_data()) {
 *       size_t n = file_source.read(buffer, 1024);
 *       // Feed to demodulator...
 *   }
 * @endcode
 */
class IQFileSource : public SampleSource {
public:
    /// Default read chunk size (samples pairs)
    static constexpr size_t DEFAULT_CHUNK_SIZE = 8192;
    
    /**
     * Open an .iqr file for reading.
     * 
     * @param filename  Path to .iqr file
     * @param output_rate_hz  Target output rate (default 48000)
     */
    explicit IQFileSource(const std::string& filename, double output_rate_hz = 48000.0)
        : filename_(filename)
        , file_(nullptr)
        , iq_source_(nullptr)
        , total_samples_(0)
        , samples_loaded_(0)
        , eof_reached_(false) {
        
        file_ = fopen(filename.c_str(), "rb");
        if (!file_) {
            error_ = "Failed to open file: " + filename;
            return;
        }
        
        // Read and validate header
        IQRHeader header;
        if (fread(&header, sizeof(header), 1, file_) != 1) {
            error_ = "Failed to read header";
            fclose(file_);
            file_ = nullptr;
            return;
        }
        
        // Validate magic
        if (memcmp(header.magic, "IQR1", 4) != 0) {
            error_ = "Invalid magic bytes (expected IQR1)";
            fclose(file_);
            file_ = nullptr;
            return;
        }
        
        // Store header info
        header_ = header;
        total_samples_ = header.sample_count;
        
        // Create IQSource with parameters from header
        double input_rate = header.sample_rate;
        if (input_rate <= 0) {
            input_rate = 2000000.0;  // Default to 2 MSPS
        }
        
        iq_source_ = std::make_unique<IQSource>(
            input_rate, 
            IQSource::Format::INT16_INTERLEAVED, 
            output_rate_hz
        );
        
        // Set metadata from header
        iq_source_->set_metadata(header.center_freq, header.bandwidth * 1000.0);
    }
    
    /**
     * Destructor - close file.
     */
    ~IQFileSource() {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
    }
    
    // Non-copyable
    IQFileSource(const IQFileSource&) = delete;
    IQFileSource& operator=(const IQFileSource&) = delete;
    
    // Movable
    IQFileSource(IQFileSource&& other) noexcept
        : filename_(std::move(other.filename_))
        , file_(other.file_)
        , iq_source_(std::move(other.iq_source_))
        , header_(other.header_)
        , total_samples_(other.total_samples_)
        , samples_loaded_(other.samples_loaded_)
        , eof_reached_(other.eof_reached_)
        , error_(std::move(other.error_)) {
        other.file_ = nullptr;
    }
    
    IQFileSource& operator=(IQFileSource&& other) noexcept {
        if (this != &other) {
            if (file_) fclose(file_);
            filename_ = std::move(other.filename_);
            file_ = other.file_;
            iq_source_ = std::move(other.iq_source_);
            header_ = other.header_;
            total_samples_ = other.total_samples_;
            samples_loaded_ = other.samples_loaded_;
            eof_reached_ = other.eof_reached_;
            error_ = std::move(other.error_);
            other.file_ = nullptr;
        }
        return *this;
    }
    
    /**
     * Check if file was opened successfully.
     */
    bool is_open() const { return file_ != nullptr && iq_source_ != nullptr; }
    
    /**
     * Get error message if open failed.
     */
    const std::string& error() const { return error_; }
    
    /**
     * Load all samples from file.
     * Useful for small files or one-shot processing.
     */
    void load_all() {
        if (!is_open()) return;
        
        while (!eof_reached_) {
            load_chunk(DEFAULT_CHUNK_SIZE);
        }
    }
    
    /**
     * Load a chunk of samples from file.
     * Call repeatedly for streaming playback.
     * 
     * @param max_samples  Maximum sample pairs to load
     * @return Number of sample pairs actually loaded
     */
    size_t load_chunk(size_t max_samples = DEFAULT_CHUNK_SIZE) {
        if (!is_open() || eof_reached_) return 0;
        
        // Read interleaved int16 data
        std::vector<int16_t> buffer(max_samples * 2);
        size_t items_read = fread(buffer.data(), sizeof(int16_t), max_samples * 2, file_);
        
        if (items_read == 0) {
            eof_reached_ = true;
            return 0;
        }
        
        // Number of complete sample pairs
        size_t sample_pairs = items_read / 2;
        samples_loaded_ += sample_pairs;
        
        // Push to IQSource for decimation
        iq_source_->push_samples_interleaved(buffer.data(), sample_pairs);
        
        if (feof(file_)) {
            eof_reached_ = true;
        }
        
        return sample_pairs;
    }
    
    // SampleSource interface implementation
    
    /**
     * Read decimated complex samples (48 kHz output).
     */
    size_t read(std::complex<float>* out, size_t count) override {
        return iq_source_ ? iq_source_->read(out, count) : 0;
    }
    
    double sample_rate() const override {
        return iq_source_ ? iq_source_->sample_rate() : 48000.0;
    }
    
    bool has_data() const override {
        return iq_source_ && iq_source_->has_data();
    }
    
    const char* source_type() const override {
        return "iq_file";
    }
    
    void reset() override {
        if (!is_open()) return;
        
        // Seek back to data start (after header)
        fseek(file_, 64, SEEK_SET);
        samples_loaded_ = 0;
        eof_reached_ = false;
        iq_source_->reset();
    }
    
    // Additional accessors
    
    /**
     * Get center frequency from file header.
     */
    double center_frequency() const { 
        return iq_source_ ? iq_source_->center_frequency() : 0.0; 
    }
    
    /**
     * Get bandwidth from file header.
     */
    double bandwidth() const { 
        return iq_source_ ? iq_source_->bandwidth() : 0.0; 
    }
    
    /**
     * Get input sample rate from file header.
     */
    double input_rate() const { 
        return iq_source_ ? iq_source_->input_rate() : 0.0; 
    }
    
    /**
     * Get total samples in file.
     */
    uint64_t total_samples() const { return total_samples_; }
    
    /**
     * Get number of samples loaded so far.
     */
    uint64_t samples_loaded() const { return samples_loaded_; }
    
    /**
     * Check if end of file reached.
     */
    bool eof() const { return eof_reached_; }
    
    /**
     * Get filename.
     */
    const std::string& filename() const { return filename_; }
    
    /**
     * Get raw header for advanced use.
     */
    const IQRHeader& header() const { return header_; }
    
    /**
     * Get underlying IQSource for advanced use.
     */
    IQSource* iq_source() { return iq_source_.get(); }
    const IQSource* iq_source() const { return iq_source_.get(); }
    
    /**
     * Check if more file data can be loaded.
     * True if file not fully read yet.
     */
    bool can_load_more() const { return is_open() && !eof_reached_; }
    
    /**
     * Get progress as percentage (0-100).
     */
    double progress_percent() const {
        if (total_samples_ == 0) return 100.0;
        return 100.0 * static_cast<double>(samples_loaded_) / static_cast<double>(total_samples_);
    }
    
    /**
     * Get duration of file in seconds.
     */
    double duration_seconds() const {
        if (!iq_source_ || iq_source_->input_rate() <= 0) return 0.0;
        return static_cast<double>(total_samples_) / iq_source_->input_rate();
    }

private:
    std::string filename_;
    FILE* file_;
    std::unique_ptr<IQSource> iq_source_;
    IQRHeader header_;
    uint64_t total_samples_;
    uint64_t samples_loaded_;
    bool eof_reached_;
    std::string error_;
};

} // namespace m110a

#endif // M110A_API_IQ_FILE_SOURCE_H
