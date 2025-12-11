#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <mutex>

// Replicate wrapper's static pattern
std::mutex g_rx_mutex;
std::vector<uint8_t> g_decoded;

void rx_callback_static(uint8_t byte) {
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    std::cerr << "  [cb] byte " << (int)byte << std::endl;
    g_decoded.push_back(byte);
}

int main() {
    std::cerr << "Step 1: Create Cm110s" << std::endl;
    Cm110s* modem = new Cm110s();
    
    std::cerr << "Step 2: Setup" << std::endl;
    modem->register_receive_octet_callback_function(rx_callback_static);
    modem->tx_set_soundblock_size(1024);
    modem->rx_enable();
    modem->tx_enable();
    
    std::cerr << "Step 3: Acquire lock in main" << std::endl;
    {
        std::lock_guard<std::mutex> lock(g_rx_mutex);
        g_decoded.clear();
        std::cerr << "  lock acquired, cleared" << std::endl;
    }
    std::cerr << "  lock released" << std::endl;
    
    std::cerr << "Step 4: Process block" << std::endl;
    std::vector<signed short> samples(512, 0);
    modem->rx_process_block(samples.data(), 512);
    
    std::cerr << "Step 5: Done, decoded " << g_decoded.size() << " bytes" << std::endl;
    delete modem;
    return 0;
}
