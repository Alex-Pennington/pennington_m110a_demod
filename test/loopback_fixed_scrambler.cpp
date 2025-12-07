/**
 * Loopback test with fixed MS-DMT scrambler
 */
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include "modem/viterbi.h"
#include "modem/multimode_interleaver.h"

using namespace m110a;
using complex_t = std::complex<float>;

// Fixed MS-DMT scrambler
class MSDMTScrambler {
    uint16_t lfsr_;
public:
    MSDMTScrambler() : lfsr_(0xBAD) {}
    void reset() { lfsr_ = 0xBAD; }
    
    int next() {
        for (int j = 0; j < 8; j++) {
            int carry = (lfsr_ >> 11) & 1;
            uint16_t new_lfsr = ((lfsr_ << 1) | carry) & 0xFFF;
            if (carry) new_lfsr ^= (1 << 6) | (1 << 4) | (1 << 1);
            lfsr_ = new_lfsr;
        }
        return lfsr_ & 7;
    }
};

int main() {
    std::cout << "=== Loopback Test with Fixed Scrambler ===" << std::endl;
    
    std::string message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    std::cout << "Message: \"" << message << "\" (" << message.size() << " bytes)" << std::endl;
    
    // Convert to bits
    std::vector<uint8_t> bits;
    for (char c : message) {
        for (int i = 7; i >= 0; i--) {
            bits.push_back((c >> i) & 1);
        }
    }
    std::cout << "Input bits: " << bits.size() << std::endl;
    
    // Viterbi encode
    ViterbiEncoder enc;
    std::vector<uint8_t> encoded;
    for (uint8_t b : bits) {
        auto pair = enc.encode_bit(b);
        encoded.push_back(pair.first);
        encoded.push_back(pair.second);
    }
    enc.flush(encoded);
    std::cout << "Encoded bits: " << encoded.size() << std::endl;
    
    // Pad to interleaver block (1440 for M1200S)
    while (encoded.size() < 1440) encoded.push_back(0);
    
    // Interleave
    MultiModeInterleaver interleaver(ModeId::M1200S);
    std::vector<soft_bit_t> soft_encoded;
    for (uint8_t b : encoded) soft_encoded.push_back(b ? -127 : 127);
    auto interleaved = interleaver.interleave(soft_encoded);
    std::cout << "Interleaved bits: " << interleaved.size() << std::endl;
    
    // Map to QPSK symbols (2 bits per symbol)
    std::vector<complex_t> symbols;
    const complex_t con[8] = {
        {1,0}, {0.707f,0.707f}, {0,1}, {-0.707f,0.707f},
        {-1,0}, {-0.707f,-0.707f}, {0,-1}, {0.707f,-0.707f}
    };
    
    // QPSK mapping: dibit 00->0, 01->2, 10->6, 11->4
    for (size_t i = 0; i + 1 < interleaved.size(); i += 2) {
        int b0 = interleaved[i] < 0 ? 1 : 0;    // First bit (MSB of dibit)
        int b1 = interleaved[i+1] < 0 ? 1 : 0;  // Second bit (LSB of dibit)
        int dibit = (b0 << 1) | b1;
        int pos = (dibit == 0) ? 0 : (dibit == 1) ? 2 : (dibit == 2) ? 6 : 4;
        symbols.push_back(con[pos]);
    }
    std::cout << "QPSK symbols: " << symbols.size() << std::endl;
    
    // Scramble
    MSDMTScrambler scr;
    for (auto& sym : symbols) {
        int s = scr.next();
        sym = sym * con[s];  // Rotate by scrambler
    }
    
    // === RX Path ===
    std::cout << "\n=== RX Path ===" << std::endl;
    
    // Descramble
    scr.reset();
    std::vector<soft_bit_t> rx_soft;
    
    for (const auto& sym : symbols) {
        int s = scr.next();
        complex_t desc = sym * std::conj(con[s]);
        
        // Find best QPSK point
        int best = 0;
        float best_corr = -1e9f;
        for (int q = 0; q < 8; q += 2) {
            float corr = desc.real() * con[q].real() + desc.imag() * con[q].imag();
            if (corr > best_corr) { best_corr = corr; best = q; }
        }
        
        float soft = std::min(best_corr * 50.0f, 127.0f);
        
        // MS-DMT QPSK mapping
        float sd0, sd1;
        switch (best) {
            case 0: sd0 = soft; sd1 = soft; break;
            case 2: sd0 = soft; sd1 = -soft; break;
            case 4: sd0 = -soft; sd1 = -soft; break;
            case 6: sd0 = -soft; sd1 = soft; break;
            default: sd0 = sd1 = 0;
        }
        rx_soft.push_back(sd0);
        rx_soft.push_back(sd1);
    }
    std::cout << "RX soft bits: " << rx_soft.size() << std::endl;
    
    // Deinterleave
    auto deinterleaved = interleaver.deinterleave(rx_soft);
    std::cout << "Deinterleaved: " << deinterleaved.size() << std::endl;
    
    // Viterbi decode
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    dec.decode_block(deinterleaved, decoded, true);
    std::cout << "Decoded bits: " << decoded.size() << std::endl;
    
    // Pack to bytes
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    int bc = 0;
    for (uint8_t b : decoded) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) {
            bytes.push_back(cur);
            cur = 0;
            bc = 0;
        }
    }
    
    std::cout << "\nDecoded: \"";
    for (size_t i = 0; i < message.size() && i < bytes.size(); i++) {
        char c = bytes[i];
        std::cout << (c >= 32 && c < 127 ? c : '.');
    }
    std::cout << "\"" << std::endl;
    
    int matches = 0;
    for (size_t i = 0; i < message.size() && i < bytes.size(); i++) {
        if (bytes[i] == (uint8_t)message[i]) matches++;
    }
    std::cout << "Matches: " << matches << "/" << message.size() << std::endl;
    
    return matches == message.size() ? 0 : 1;
}
