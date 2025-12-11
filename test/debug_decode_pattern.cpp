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
    
    std::cerr << "Step 2: Setup decode (like wrapper)" << std::endl;
    {
        std::lock_guard<std::mutex> lock(g_rx_mutex);
        g_decoded.clear();
    }
    
    std::cerr << "Step 3: Create 1920 samples" << std::endl;
    std::vector<signed short> pcm_copy(1920, 0);
    
    std::cerr << "Step 4: Process in blocks of 512" << std::endl;
    const int BLOCK_SIZE = 512;
    for (size_t i = 0; i < pcm_copy.size(); i += BLOCK_SIZE) {
        int len = std::min(BLOCK_SIZE, static_cast<int>(pcm_copy.size() - i));
        std::cerr << "  block at " << i << ", len=" << len << std::endl;
        modem->rx_process_block(pcm_copy.data() + i, len);
    }
    
    std::cerr << "Step 5: Flush with silence (1920*3)" << std::endl;
    std::vector<signed short> flush(1920 * 3, 0);
    for (size_t i = 0; i < flush.size(); i += BLOCK_SIZE) {
        int len = std::min(BLOCK_SIZE, static_cast<int>(flush.size() - i));
        std::cerr << "  flush at " << i << ", len=" << len << std::endl;
        modem->rx_process_block(flush.data() + i, len);
    }
    
    std::cerr << "Step 6: Done, decoded " << g_decoded.size() << " bytes" << std::endl;
    delete modem;
    return 0;
}
