#include "extern/brain_wrapper.h"

#ifdef ERROR
#undef ERROR
#endif

#include <iostream>
#include <vector>
#include "api/modem_tx.h"
#include "api/modem_config.h"

int main() {
    std::cerr << "Step 1: Creating test data" << std::endl;
    std::vector<uint8_t> data = {'H','E','L','L','O'};
    
    std::cerr << "Step 2: Creating PhoenixNest TX" << std::endl;
    auto cfg = m110a::api::TxConfig::for_mode(m110a::api::Mode::M600_SHORT);
    cfg.sample_rate = 48000.0f;
    m110a::api::ModemTX tx(cfg);
    
    std::cerr << "Step 3: Encoding with PhoenixNest" << std::endl;
    auto res = tx.encode(data);
    if (!res.ok()) {
        std::cerr << "TX encode failed!" << std::endl;
        return 1;
    }
    auto pcm_float = res.value();
    std::cerr << "Step 4: Got " << pcm_float.size() << " samples at 48kHz" << std::endl;
    
    // Convert to int16
    std::cerr << "Step 5: Converting to int16" << std::endl;
    std::vector<int16_t> pcm16(pcm_float.size());
    for (size_t i = 0; i < pcm_float.size(); i++) {
        pcm16[i] = (int16_t)(pcm_float[i] * 32767.0f);
    }
    
    // Manually do what decode_48k does
    std::cerr << "Step 6: Resampling 48k -> 9600 (5:1 decimation)" << std::endl;
    std::vector<int16_t> pcm_9600;
    pcm_9600.reserve(pcm16.size() / 5);
    for (size_t i = 0; i < pcm16.size(); i += 5) {
        pcm_9600.push_back(pcm16[i]);
    }
    std::cerr << "Step 7: Resampled to " << pcm_9600.size() << " samples at 9600 Hz" << std::endl;
    
    std::cerr << "Step 8: Creating Brain modem" << std::endl;
    brain::Modem brain;
    
    std::cerr << "Step 9: Calling decode() with 9600 Hz samples" << std::endl;
    std::cerr << "  (this calls rx_process_block internally)" << std::endl;
    
    // Call decode directly with 9600 Hz data
    auto decoded = brain.decode(pcm_9600);
    
    std::cerr << "Step 10: Decoded " << decoded.size() << " bytes" << std::endl;
    std::cerr << "SUCCESS" << std::endl;
    return 0;
}
