// Simpler test - bypass wrapper, test callback directly
#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>

std::vector<uint8_t> g_decoded;

void my_callback(unsigned char byte) {
    std::cerr << "  [callback] got byte: " << (int)byte << std::endl;
    g_decoded.push_back(byte);
}

int main() {
    std::cerr << "Step 1: Creating Cm110s" << std::endl;
    Cm110s* modem = new Cm110s();
    
    std::cerr << "Step 2: Register callback" << std::endl;
    modem->register_receive_octet_callback_function(my_callback);
    
    std::cerr << "Step 3: tx_set_soundblock_size" << std::endl;
    modem->tx_set_soundblock_size(1024);
    
    std::cerr << "Step 4: rx_enable" << std::endl;
    modem->rx_enable();
    
    std::cerr << "Step 5: tx_enable" << std::endl;
    modem->tx_enable();
    
    std::cerr << "Step 6: Process silence (512 samples)" << std::endl;
    std::vector<signed short> samples(512, 0);
    modem->rx_process_block(samples.data(), 512);
    
    std::cerr << "Step 7: Process more silence" << std::endl;
    modem->rx_process_block(samples.data(), 512);
    
    std::cerr << "Step 8: Done! Decoded " << g_decoded.size() << " bytes" << std::endl;
    
    delete modem;
    return 0;
}
