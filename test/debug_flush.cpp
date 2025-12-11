#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <mutex>
#include <algorithm>

std::mutex g_rx_mutex;
std::vector<uint8_t> g_decoded;

void rx_callback_static(uint8_t byte) {
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    g_decoded.push_back(byte);
}

int main() {
    std::cerr << "Step 1: Create and setup Cm110s" << std::endl;
    Cm110s* modem = new Cm110s();
    modem->register_receive_octet_callback_function(rx_callback_static);
    modem->tx_set_soundblock_size(1024);
    modem->tx_set_mode(M600S);
    modem->rx_enable();
    modem->tx_enable();
    
    std::cerr << "Step 2: Process 1920 samples" << std::endl;
    std::vector<signed short> pcm(1920, 0);
    const int BLOCK_SIZE = 512;
    for (size_t i = 0; i < pcm.size(); i += BLOCK_SIZE) {
        int len = std::min(BLOCK_SIZE, static_cast<int>(pcm.size() - i));
        modem->rx_process_block(pcm.data() + i, len);
    }
    std::cerr << "  Done with 1920" << std::endl;
    
    std::cerr << "Step 3: Flush block 1 (512)" << std::endl;
    std::vector<signed short> flush(512, 0);
    modem->rx_process_block(flush.data(), 512);
    
    std::cerr << "Step 4: Flush block 2" << std::endl;
    modem->rx_process_block(flush.data(), 512);
    
    std::cerr << "Step 5: Flush block 3" << std::endl;
    modem->rx_process_block(flush.data(), 512);
    
    std::cerr << "Step 6: Flush block 4" << std::endl;
    modem->rx_process_block(flush.data(), 512);
    
    std::cerr << "Step 7: Flush block 5" << std::endl;
    modem->rx_process_block(flush.data(), 512);
    
    std::cerr << "Step 8: Done" << std::endl;
    delete modem;
    return 0;
}
