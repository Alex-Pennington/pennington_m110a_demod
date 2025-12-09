#include <iostream>
#include <fstream>
#include <vector>
#include "api/modem_rx.h"
#include "common/types.h"

using namespace m110a;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pcm_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    
    // Read PCM file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<int16_t> pcm_samples(file_size / sizeof(int16_t));
    file.read(reinterpret_cast<char*>(pcm_samples.data()), file_size);
    file.close();

    std::cout << "File: " << filename << std::endl;
    std::cout << "Samples: " << pcm_samples.size() << std::endl;

    // Configure receiver for auto-detect
    ModemRx::Config cfg;
    cfg.mode = ModeId::AUTO;
    cfg.equalizer = EqualizerType::DFE;
    cfg.sample_rate = 8000;

    ModemRx rx(cfg);
    auto result = rx.decode(pcm_samples);

    std::cout << "Detected Mode: ";
    switch (result.detected_mode) {
        case ModeId::M75S: std::cout << "M75S"; break;
        case ModeId::M75L: std::cout << "M75L"; break;
        case ModeId::M150S: std::cout << "M150S"; break;
        case ModeId::M150L: std::cout << "M150L"; break;
        case ModeId::M300S: std::cout << "M300S"; break;
        case ModeId::M300L: std::cout << "M300L"; break;
        case ModeId::M600S: std::cout << "M600S"; break;
        case ModeId::M600L: std::cout << "M600L"; break;
        case ModeId::M1200S: std::cout << "M1200S"; break;
        case ModeId::M1200L: std::cout << "M1200L"; break;
        case ModeId::M2400S: std::cout << "M2400S"; break;
        case ModeId::M2400L: std::cout << "M2400L"; break;
        case ModeId::M4800S: std::cout << "M4800S"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << std::endl;

    std::cout << "Decode Success: " << (result.success ? "YES" : "NO") << std::endl;
    std::cout << "BER: " << result.ber << std::endl;
    std::cout << "Decoded bytes: " << result.data.size() << std::endl;

    return result.success ? 0 : 1;
}
