#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>

// Simple callback
std::vector<uint8_t> g_decoded;
void rx_callback(unsigned char byte) {
    g_decoded.push_back(byte);
}

int main() {
    std::cerr << "Step 1: Creating Cm110s on heap" << std::endl;
    Cm110s* modem = new Cm110s();
    
    std::cerr << "Step 2: Registering callback" << std::endl;
    modem->register_receive_octet_callback_function(rx_callback);
    
    std::cerr << "Step 3: Enabling RX" << std::endl;
    modem->rx_enable();
    
    std::cerr << "Step 4: Creating test samples (silence)" << std::endl;
    std::vector<signed short> samples(512, 0);
    
    std::cerr << "Step 5: Calling rx_process_block with 512 samples" << std::endl;
    modem->rx_process_block(samples.data(), 512);
    
    std::cerr << "Step 6: rx_process_block returned!" << std::endl;
    std::cerr << "Decoded bytes: " << g_decoded.size() << std::endl;
    
    delete modem;
    std::cerr << "SUCCESS" << std::endl;
    return 0;
}
