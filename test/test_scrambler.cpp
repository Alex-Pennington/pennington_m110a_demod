// test/test_scrambler.cpp
#include "../src/modem/scrambler.h"
#include <iostream>
#include <set>
#include <cassert>

using namespace m110a;

void test_scrambler_initial_state() {
    std::cout << "  Testing initial state... ";
    
    Scrambler scr(0x7F);  // All ones
    assert(scr.state() == 0x7F);
    
    Scrambler scr2(0xFF);  // Should mask to 7 bits
    assert(scr2.state() == 0x7F);
    
    Scrambler scr3(0x00);
    assert(scr3.state() == 0x00);
    
    std::cout << "PASS" << std::endl;
}

void test_scrambler_period() {
    std::cout << "  Testing LFSR period (should be 127)... ";
    
    Scrambler scr(0x7F);
    
    // The LFSR should have period 2^7 - 1 = 127 bits
    // After 127 bits, the state should return to initial
    (void)scr.state();
    
    // Clock 127 bits
    for (int i = 0; i < 127; i++) {
        scr.next_tribit();  // Clock single bits
    }
    
    // Actually, let's test differently - generate bit sequence and check period
    Scrambler scr2(0x7F);
    std::vector<uint8_t> bits;
    
    // Generate enough bits to see the period
    // Since each next_tribits() clocks 3 bits, we need ~43 calls for 127 bits
    // But period is in single bits, so let's track states instead
    
    std::set<uint8_t> states_seen;
    Scrambler scr3(0x7F);
    
    // Manually clock one bit at a time by calling next_tribits and tracking state
    // Actually, the state cycles through all 127 non-zero states
    
    Scrambler scr4(0x7F);
    states_seen.insert(scr4.state());
    
    for (int i = 0; i < 200; i++) {
        scr4.next_tribit();  // Advances state by 1 bit
        if (scr4.state() == 0x7F && i > 0) {
            // Returned to initial state
            break;
        }
        states_seen.insert(scr4.state());
    }
    
    // With proper taps (6,0), we should see all 127 non-zero states
    // But since we advance 3 bits at a time, we see 127/gcd(127,3) = 127 unique states
    // (since gcd(127,3) = 1)
    
    assert(states_seen.size() == 127);
    
    std::cout << "PASS (saw " << states_seen.size() << " unique states)" << std::endl;
}

void test_scrambler_deterministic() {
    std::cout << "  Testing deterministic output... ";
    
    // Same initial state should produce same sequence
    Scrambler scr1(0x7F);
    Scrambler scr2(0x7F);
    
    for (int i = 0; i < 100; i++) {
        assert(scr1.next_tribit() == scr2.next_tribit());
    }
    
    std::cout << "PASS" << std::endl;
}

void test_scrambler_reset() {
    std::cout << "  Testing reset... ";
    
    Scrambler scr(0x7F);
    
    // Generate some output
    std::vector<uint8_t> first_run;
    for (int i = 0; i < 50; i++) {
        first_run.push_back(scr.next_tribit());
    }
    
    // Reset and regenerate
    scr.reset(0x7F);
    for (int i = 0; i < 50; i++) {
        assert(scr.next_tribit() == first_run[i]);
    }
    
    std::cout << "PASS" << std::endl;
}

void test_scrambler_generate() {
    std::cout << "  Testing bulk generate (tribits)... ";
    
    Scrambler scr1(0x7F);
    Scrambler scr2(0x7F);
    
    auto bulk = scr1.generate_tribits(100);
    assert(bulk.size() == 100);
    
    for (size_t i = 0; i < 100; i++) {
        assert(bulk[i] == scr2.next_tribit());
    }
    
    std::cout << "PASS" << std::endl;
}

void test_scrambler_descramble() {
    std::cout << "  Testing descramble... ";
    
    Scrambler tx_scr(0x7F);
    Scrambler rx_scr(0x7F);
    
    // Scramble some data (tribits)
    std::vector<uint8_t> original_data = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3};
    std::vector<uint8_t> scrambled;
    
    for (uint8_t d : original_data) {
        scrambled.push_back(d ^ tx_scr.next_tribit());
    }
    
    // Descramble
    for (size_t i = 0; i < original_data.size(); i++) {
        uint8_t recovered = rx_scr.descramble_tribit(scrambled[i]);
        assert(recovered == original_data[i]);
    }
    
    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "[Scrambler Tests]" << std::endl;
    
    try {
        test_scrambler_initial_state();
        test_scrambler_period();
        test_scrambler_deterministic();
        test_scrambler_reset();
        test_scrambler_generate();
        test_scrambler_descramble();
        
        std::cout << "\nAll scrambler tests PASSED!" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
