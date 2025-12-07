/**
 * Verify descrambling matches MS-DMT
 */
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <complex>
#include <cstdint>

using complex_t = std::complex<float>;

// MS-DMT scrambler seed
const uint16_t SEED = 0xBAD;

// 8PSK constellation (same as MS-DMT con_symbol)
static const complex_t CON_SYMBOL[8] = {
    {1.0f, 0.0f},           // 0: 0°
    {0.707f, 0.707f},       // 1: 45°
    {0.0f, 1.0f},           // 2: 90°
    {-0.707f, 0.707f},      // 3: 135°
    {-1.0f, 0.0f},          // 4: 180°
    {-0.707f, -0.707f},     // 5: 225°
    {0.0f, -1.0f},          // 6: 270°
    {0.707f, -0.707f}       // 7: 315°
};

// Generate scrambler output - same as MS-DMT
int generate_scrambler(uint16_t& lfsr) {
    int result = 0;
    for (int i = 0; i < 8; i++) {
        int feedback = ((lfsr >> 11) ^ (lfsr >> 10) ^ (lfsr >> 7) ^ (lfsr >> 4)) & 1;
        lfsr = ((lfsr << 1) | feedback) & 0xFFF;
        if (i == 7) {
            result = ((lfsr >> 9) & 0x7);
        }
    }
    return result;
}

int main() {
    std::cout << "=== Descramble Verification ===" << std::endl;
    
    uint16_t tx_lfsr = SEED;
    uint16_t rx_lfsr = SEED;
    
    std::cout << "\nFirst 10 scrambler outputs:" << std::endl;
    for (int i = 0; i < 10; i++) {
        int scr = generate_scrambler(tx_lfsr);
        std::cout << "  [" << i << "] scrambler tribit = " << scr << std::endl;
    }
    
    tx_lfsr = SEED;
    rx_lfsr = SEED;
    
    std::cout << "\nTX/RX verification:" << std::endl;
    std::vector<int> tx_data = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1};
    
    for (int i = 0; i < 10; i++) {
        int data_tribit = tx_data[i];
        int tx_scr = generate_scrambler(tx_lfsr);
        int tx_tribit = (data_tribit + tx_scr) % 8;
        
        complex_t tx_sym = CON_SYMBOL[tx_tribit];
        complex_t rx_sym = tx_sym;
        
        int rx_scr = generate_scrambler(rx_lfsr);
        complex_t scr_conj = std::conj(CON_SYMBOL[rx_scr]);
        complex_t descrambled = rx_sym * scr_conj;
        
        float best_corr = -1e9f;
        int best_tribit = 0;
        for (int t = 0; t < 8; t++) {
            float corr = descrambled.real() * CON_SYMBOL[t].real() + 
                        descrambled.imag() * CON_SYMBOL[t].imag();
            if (corr > best_corr) {
                best_corr = corr;
                best_tribit = t;
            }
        }
        
        std::cout << "  [" << i << "] data=" << data_tribit 
                  << " scr=" << tx_scr 
                  << " tx=" << tx_tribit
                  << " -> decoded=" << best_tribit
                  << (best_tribit == data_tribit ? " OK" : " FAIL")
                  << std::endl;
    }
    
    return 0;
}
