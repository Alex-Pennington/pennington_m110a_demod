#include "extern/brain_wrapper.h"

#include <iostream>
#include <vector>
#include <cstdint>

int main() {
    std::cerr << "Step 1: Test static mutex" << std::endl;
    {
        std::lock_guard<std::mutex> lock(brain::Modem::rx_mutex_);
        std::cerr << "  Acquired lock OK" << std::endl;
    }
    std::cerr << "  Released lock OK" << std::endl;
    
    std::cerr << "Step 2: Creating brain::Modem" << std::endl;
    brain::Modem* modem = new brain::Modem();
    std::cerr << "  Created OK" << std::endl;
    
    std::cerr << "Step 3: Small decode" << std::endl;
    std::vector<int16_t> samples(512, 0);
    
    std::cerr << "Step 4: Calling decode..." << std::endl;
    std::cerr.flush();
    auto result = modem->decode(samples);
    
    std::cerr << "Step 5: Done" << std::endl;
    delete modem;
    return 0;
}
