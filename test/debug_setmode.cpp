#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <mutex>

std::mutex g_rx_mutex;
std::vector<uint8_t> g_decoded;

void rx_callback_static(uint8_t byte) {
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    g_decoded.push_back(byte);
}

int main() {
    std::cerr << "Step 1: Create Cm110s" << std::endl;
    Cm110s* modem = new Cm110s();
    
    std::cerr << "Step 2: register_receive_octet_callback_function" << std::endl;
    modem->register_receive_octet_callback_function(rx_callback_static);
    
    std::cerr << "Step 3: tx_set_soundblock_size(1024)" << std::endl;
    modem->tx_set_soundblock_size(1024);
    
    std::cerr << "Step 4: tx_set_mode(M600S)" << std::endl;
    modem->tx_set_mode(M600S);  // This is what wrapper does!
    
    std::cerr << "Step 5: rx_enable" << std::endl;
    modem->rx_enable();
    
    std::cerr << "Step 6: tx_enable" << std::endl;
    modem->tx_enable();
    
    std::cerr << "Step 7: Process block" << std::endl;
    std::vector<signed short> samples(512, 0);
    modem->rx_process_block(samples.data(), 512);
    
    std::cerr << "Step 8: Done" << std::endl;
    delete modem;
    return 0;
}
