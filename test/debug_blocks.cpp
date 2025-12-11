#include "brain_core/m188110a/Cm110s.h"

#include <iostream>
#include <vector>
#include <cstdint>

void status_callback(ModemStatus status, void* data) {
    std::cerr << "[STATUS: " << (int)status << "]" << std::flush;
}

int main() {
    std::cerr << "Create and setup (with status callback)" << std::endl;
    Cm110s* modem = new Cm110s();
    modem->tx_set_soundblock_size(1024);
    modem->register_status(status_callback);  // Register status callback!
    modem->rx_enable();
    modem->tx_enable();
    
    std::vector<signed short> block(64, 0);
    int total = 0;
    
    for (int i = 0; i < 100; i++) {
        total += 64;
        std::cerr << total << " " << std::flush;
        modem->rx_process_block(block.data(), 64);
    }
    
    std::cerr << "\nDone!" << std::endl;
    delete modem;
    return 0;
}
