// src/io/pcm_file.h
#ifndef M110A_PCM_FILE_H
#define M110A_PCM_FILE_H

#include "../common/types.h"
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

namespace m110a {

class PcmFileReader {
public:
    explicit PcmFileReader(const std::string& filename);
    
    // Read all samples
    std::vector<sample_t> read_all();
    
    // Read N samples, returns actual count read
    size_t read(sample_t* buffer, size_t count);
    
    // Check if more data available
    bool eof() const;
    
    size_t total_samples() const { return total_samples_; }
    size_t samples_read() const { return samples_read_; }
    
private:
    std::ifstream file_;
    size_t total_samples_;
    size_t samples_read_;
};

class PcmFileWriter {
public:
    explicit PcmFileWriter(const std::string& filename);
    ~PcmFileWriter();
    
    void write(const sample_t* buffer, size_t count);
    void write(const std::vector<sample_t>& samples);
    
    size_t samples_written() const { return samples_written_; }
    
private:
    std::ofstream file_;
    size_t samples_written_;
};

} // namespace m110a

#endif
