// src/io/pcm_file.cpp
#include "pcm_file.h"

namespace m110a {

// ============================================================================
// PcmFileReader Implementation
// ============================================================================

PcmFileReader::PcmFileReader(const std::string& filename)
    : file_(filename, std::ios::binary), samples_read_(0) {
    
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    file_.seekg(0, std::ios::end);
    total_samples_ = static_cast<size_t>(file_.tellg()) / sizeof(pcm_sample_t);
    file_.seekg(0, std::ios::beg);
}

std::vector<sample_t> PcmFileReader::read_all() {
    std::vector<pcm_sample_t> pcm_buffer(total_samples_);
    file_.read(reinterpret_cast<char*>(pcm_buffer.data()), 
               total_samples_ * sizeof(pcm_sample_t));
    
    std::vector<sample_t> samples(total_samples_);
    for (size_t i = 0; i < total_samples_; i++) {
        samples[i] = pcm_to_float(pcm_buffer[i]);
    }
    
    samples_read_ = total_samples_;
    return samples;
}

size_t PcmFileReader::read(sample_t* buffer, size_t count) {
    size_t remaining = total_samples_ - samples_read_;
    size_t to_read = std::min(count, remaining);
    
    if (to_read == 0) return 0;
    
    std::vector<pcm_sample_t> pcm_buffer(to_read);
    file_.read(reinterpret_cast<char*>(pcm_buffer.data()),
               to_read * sizeof(pcm_sample_t));
    
    size_t actually_read = static_cast<size_t>(file_.gcount()) / sizeof(pcm_sample_t);
    
    for (size_t i = 0; i < actually_read; i++) {
        buffer[i] = pcm_to_float(pcm_buffer[i]);
    }
    
    samples_read_ += actually_read;
    return actually_read;
}

bool PcmFileReader::eof() const {
    return samples_read_ >= total_samples_;
}

// ============================================================================
// PcmFileWriter Implementation
// ============================================================================

PcmFileWriter::PcmFileWriter(const std::string& filename)
    : file_(filename, std::ios::binary), samples_written_(0) {
    
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot create file: " + filename);
    }
}

PcmFileWriter::~PcmFileWriter() {
    file_.close();
}

void PcmFileWriter::write(const sample_t* buffer, size_t count) {
    std::vector<pcm_sample_t> pcm_buffer(count);
    for (size_t i = 0; i < count; i++) {
        pcm_buffer[i] = float_to_pcm(buffer[i]);
    }
    file_.write(reinterpret_cast<const char*>(pcm_buffer.data()),
                count * sizeof(pcm_sample_t));
    samples_written_ += count;
}

void PcmFileWriter::write(const std::vector<sample_t>& samples) {
    write(samples.data(), samples.size());
}

} // namespace m110a
