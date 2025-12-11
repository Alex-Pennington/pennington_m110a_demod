// Minimal test - create modem via wrapper (heap allocated)
#include "extern/brain_wrapper.h"

#include <iostream>

int main() {
    std::cerr << "Step 1: main() reached" << std::endl;
    
    std::cerr << "Step 2: Creating brain::Modem (heap allocated)..." << std::endl;
    brain::Modem modem;
    std::cerr << "Step 3: Modem created!" << std::endl;
    
    return 0;
}
