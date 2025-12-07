/**
 * Decode MIL-STD-188-110A reference WAV files
 * 
 * Tests our implementation against reference signals
 */

#include "m110a/multimode_rx.h"
#include "m110a/multimode_tx.h"
#include "io/wav_file.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>

using namespace m110a;

void print_hex(const std::vector<uint8_t>& data, int max_bytes = 64) {
    int count = std::min(static_cast<int>(data.size()), max_bytes);
    for (int i = 0; i < count; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n";
        else if ((i + 1) % 8 == 0) std::cout << "  ";
        else std::cout << " ";
    }
    if (count % 16 != 0) std::cout << "\n";
    if (data.size() > static_cast<size_t>(max_bytes)) {
        std::cout << "... (" << data.size() << " bytes total)\n";
    }
    std::cout << std::dec;
}

void print_ascii(const std::vector<uint8_t>& data, int max_bytes = 128) {
    int count = std::min(static_cast<int>(data.size()), max_bytes);
    std::cout << "\"";
    for (int i = 0; i < count; i++) {
        char c = data[i];
        if (c >= 32 && c < 127) {
            std::cout << c;
        } else if (c == '\n') {
            std::cout << "\\n";
        } else if (c == '\r') {
            std::cout << "\\r";
        } else {
            std::cout << ".";
        }
    }
    std::cout << "\"";
    if (data.size() > static_cast<size_t>(max_bytes)) {
        std::cout << " ... (" << data.size() << " bytes)";
    }
    std::cout << "\n";
}

bool decode_file(const std::string& filename, bool verbose = false) {
    std::cout << "\n=== " << filename << " ===\n";
    
    // Load WAV file
    std::vector<float> samples;
    int sample_rate, channels;
    
    if (!read_wav_file(filename, samples, sample_rate, channels)) {
        std::cout << "  ERROR: Failed to read WAV file\n";
        return false;
    }
    
    std::cout << "  Samples: " << samples.size() 
              << " (" << std::fixed << std::setprecision(2) 
              << samples.size() / static_cast<float>(sample_rate) << "s)\n";
    std::cout << "  Sample rate: " << sample_rate << " Hz\n";
    
    // Configure receiver with auto-detection
    MultiModeRx::Config cfg;
    cfg.sample_rate = static_cast<float>(sample_rate);
    cfg.carrier_freq = 1800.0f;
    cfg.auto_detect = true;
    cfg.verbose = verbose;
    cfg.enable_dfe = true;  // Enable DFE for multipath
    
    MultiModeRx rx(cfg);
    
    // Try to decode
    auto result = rx.decode(samples);
    
    if (result.mode_detected) {
        const auto& mode = ModeDatabase::get(result.detected_mode);
        std::cout << "  Mode detected: " << mode.name << "\n";
        std::cout << "  D1/D2 confidence: " << result.d1_confidence << "/" 
                  << result.d2_confidence << "\n";
    } else {
        std::cout << "  Mode: NOT DETECTED\n";
    }
    
    std::cout << "  Freq offset: " << std::setprecision(1) 
              << result.freq_offset_hz << " Hz\n";
    std::cout << "  Symbols decoded: " << result.symbols_decoded << "\n";
    std::cout << "  Frames decoded: " << result.frames_decoded << "\n";
    
    if (result.success && !result.data.empty()) {
        std::cout << "  Data bytes: " << result.data.size() << "\n";
        std::cout << "  Hex:\n";
        print_hex(result.data);
        std::cout << "  ASCII: ";
        print_ascii(result.data);
        return true;
    } else {
        std::cout << "  DECODE FAILED\n";
        return false;
    }
}

// Try multiple carrier frequencies
bool decode_file_multifreq(const std::string& filename) {
    std::vector<float> freqs = {1800.0f, 1500.0f, 1650.0f};
    
    std::cout << "\n=== " << filename << " ===\n";
    
    // Load WAV file
    std::vector<float> samples;
    int sample_rate, channels;
    
    if (!read_wav_file(filename, samples, sample_rate, channels)) {
        std::cout << "  ERROR: Failed to read WAV file\n";
        return false;
    }
    
    std::cout << "  Samples: " << samples.size() 
              << " (" << std::fixed << std::setprecision(2) 
              << samples.size() / static_cast<float>(sample_rate) << "s)\n";
    
    for (float freq : freqs) {
        std::cout << "  Trying " << freq << " Hz carrier...\n";
        
        MultiModeRx::Config cfg;
        cfg.sample_rate = static_cast<float>(sample_rate);
        cfg.carrier_freq = freq;
        cfg.auto_detect = true;
        cfg.verbose = false;
        cfg.enable_dfe = true;
        
        MultiModeRx rx(cfg);
        auto result = rx.decode(samples);
        
        if (result.mode_detected && result.success && !result.data.empty()) {
            const auto& mode = ModeDatabase::get(result.detected_mode);
            std::cout << "  SUCCESS at " << freq << " Hz!\n";
            std::cout << "  Mode: " << mode.name << "\n";
            std::cout << "  Data bytes: " << result.data.size() << "\n";
            std::cout << "  ASCII: ";
            print_ascii(result.data);
            return true;
        }
    }
    
    std::cout << "  DECODE FAILED at all frequencies\n";
    return false;
}

int main(int argc, char* argv[]) {
    std::cout << "MIL-STD-188-110A Reference File Decoder\n";
    std::cout << "========================================\n";
    
    std::vector<std::string> files;
    bool verbose = false;
    
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (std::string(argv[i]) == "-v") {
                verbose = true;
            } else {
                files.push_back(argv[i]);
            }
        }
    } else {
        // Default: test all uploaded files
        files = {
            "/mnt/user-data/uploads/MIL-STD-188-110A_2400bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_2400bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_1200bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_1200bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_600bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_600bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_300bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_300bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_150bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_150bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_75bps_Short.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_75bps_Long.wav",
            "/mnt/user-data/uploads/MIL-STD-188-110A_4800bps_Short.wav",
        };
    }
    
    int success = 0;
    int total = 0;
    
    for (const auto& file : files) {
        total++;
        if (decode_file(file, verbose)) {
            success++;
        }
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << success << "/" << total << " decoded\n";
    
    return (success > 0) ? 0 : 1;
}
