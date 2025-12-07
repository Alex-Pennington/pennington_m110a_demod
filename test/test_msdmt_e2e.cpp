/**
 * MS-DMT Compatible End-to-End Test
 * 
 * This test verifies our implementation matches MS-DMT by:
 * 1. Encoding known data exactly as MS-DMT would
 * 2. Adding symbol scrambling
 * 3. Generating complex baseband samples
 * 4. Decoding and verifying
 */

#include "m110a/mode_config.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include "modem/scrambler.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <complex>
#include <cmath>

using namespace m110a;

// Gray code tables from MS-DMT
const int mgd3[8] = {0, 1, 3, 2, 7, 6, 4, 5};  // tribit -> position
const int mgd3_inv[8] = {0, 1, 3, 2, 6, 7, 5, 4};  // position -> tribit

// 8-PSK constellation (MS-DMT con_symbol)
const std::complex<float> con_symbol[8] = {
    { 1.000f,  0.000f},  // 0°
    { 0.707f,  0.707f},  // 45°
    { 0.000f,  1.000f},  // 90°
    {-0.707f,  0.707f},  // 135°
    {-1.000f,  0.000f},  // 180°
    {-0.707f, -0.707f},  // 225°
    { 0.000f, -1.000f},  // 270°
    { 0.707f, -0.707f}   // 315°
};

class MSDMTEncoder {
public:
    MSDMTEncoder() : scrambler_offset_(0) {}
    
    void reset() { scrambler_offset_ = 0; scr_.reset(); }
    
    // Encode data symbols for M2400S
    std::vector<std::complex<float>> encode_m2400s(const std::vector<uint8_t>& tribits) {
        std::vector<std::complex<float>> symbols;
        
        for (uint8_t tribit : tribits) {
            // Apply Gray code: tribit -> position
            int position = mgd3[tribit & 7];
            
            // Add scrambler
            uint8_t scr_val = scr_.next_tribit();
            position = (position + scr_val) % 8;
            
            // Output constellation symbol
            symbols.push_back(con_symbol[position]);
        }
        
        return symbols;
    }
    
private:
    RefScrambler scr_;
    int scrambler_offset_;
};

class MSDMTDecoder {
public:
    MSDMTDecoder() {}
    
    void reset() { scr_.reset(); }
    
    // Decode symbols to tribits
    std::vector<uint8_t> decode_m2400s(const std::vector<std::complex<float>>& symbols) {
        std::vector<uint8_t> tribits;
        
        for (const auto& sym : symbols) {
            // Get scrambler value
            uint8_t scr_val = scr_.next_tribit();
            
            // Descramble: multiply by conjugate of scrambler constellation
            std::complex<float> descrambled = sym * std::conj(con_symbol[scr_val]);
            
            // Find nearest constellation point
            float max_corr = -1e9;
            int best_pos = 0;
            for (int i = 0; i < 8; i++) {
                float corr = descrambled.real() * con_symbol[i].real() +
                            descrambled.imag() * con_symbol[i].imag();
                if (corr > max_corr) {
                    max_corr = corr;
                    best_pos = i;
                }
            }
            
            // Apply inverse Gray code: position -> tribit
            tribits.push_back(mgd3_inv[best_pos]);
        }
        
        return tribits;
    }
    
    // Decode with soft bits (for Viterbi)
    std::vector<soft_bit_t> decode_soft_m2400s(const std::vector<std::complex<float>>& symbols,
                                                bool debug = false) {
        std::vector<soft_bit_t> soft_bits;
        
        for (size_t si = 0; si < symbols.size(); si++) {
            const auto& sym = symbols[si];
            uint8_t scr_val = scr_.next_tribit();
            std::complex<float> descrambled = sym * std::conj(con_symbol[scr_val]);
            
            // Find nearest and confidence
            float max_corr = -1e9;
            int best_pos = 0;
            for (int i = 0; i < 8; i++) {
                float corr = descrambled.real() * con_symbol[i].real() +
                            descrambled.imag() * con_symbol[i].imag();
                if (corr > max_corr) {
                    max_corr = corr;
                    best_pos = i;
                }
            }
            
            int tribit = mgd3_inv[best_pos];
            float conf = std::min(max_corr * 100.0f, 127.0f);
            
            if (debug && si < 4) {
                std::cout << "  sym " << si << ": scr=" << (int)scr_val
                          << " pos=" << best_pos << " tri=" << tribit
                          << " conf=" << conf << std::endl;
            }
            
            // MS-DMT convention: +soft = bit 0, -soft = bit 1
            // (See v110a.cpp line 117-122)
            soft_bits.push_back((tribit & 4) ? -conf : conf);  // MSB
            soft_bits.push_back((tribit & 2) ? -conf : conf);
            soft_bits.push_back((tribit & 1) ? -conf : conf);  // LSB
        }
        
        return soft_bits;
    }
    
private:
    RefScrambler scr_;
};

int main() {
    std::cout << "=== MS-DMT Compatible End-to-End Test ===" << std::endl;
    
    // Test message
    std::string message = "HELLO WORLD FROM MS-DMT TEST!";
    std::cout << "\nOriginal: \"" << message << "\" (" << message.size() << " bytes)" << std::endl;
    
    // Convert to bits
    std::vector<uint8_t> input_bits;
    for (char c : message) {
        for (int b = 7; b >= 0; b--) {
            input_bits.push_back((c >> b) & 1);
        }
    }
    
    // ========== TX PATH ==========
    std::cout << "\n=== TX Path ===" << std::endl;
    
    // 1. Viterbi encode
    ConvEncoder encoder;
    std::vector<uint8_t> encoded;
    encoder.encode(input_bits, encoded, true);
    std::cout << "[1] Viterbi: " << input_bits.size() << " -> " << encoded.size() << " bits" << std::endl;
    
    // 2. Interleave
    ModeId mode = ModeId::M2400S;
    MultiModeInterleaver interleaver(mode);
    int block_size = interleaver.block_size();
    
    // Pad to block size
    while (encoded.size() % block_size != 0) {
        encoded.push_back(0);
    }
    
    std::vector<soft_bit_t> soft_enc(encoded.begin(), encoded.end());
    std::vector<soft_bit_t> interleaved;
    for (size_t i = 0; i < soft_enc.size(); i += block_size) {
        std::vector<soft_bit_t> blk(soft_enc.begin() + i, soft_enc.begin() + i + block_size);
        auto ilvd = interleaver.interleave(blk);
        interleaved.insert(interleaved.end(), ilvd.begin(), ilvd.end());
    }
    std::cout << "[2] Interleave: " << soft_enc.size() << " -> " << interleaved.size() << " bits" << std::endl;
    
    // 3. Pack into tribits
    std::vector<uint8_t> tribits;
    for (size_t i = 0; i + 2 < interleaved.size(); i += 3) {
        uint8_t t = ((interleaved[i] > 0 ? 1 : 0) << 2) |
                   ((interleaved[i+1] > 0 ? 1 : 0) << 1) |
                   (interleaved[i+2] > 0 ? 1 : 0);
        tribits.push_back(t);
    }
    std::cout << "[3] Pack tribits: " << interleaved.size() << " -> " << tribits.size() << " tribits" << std::endl;
    
    // 4. Scramble + modulate
    MSDMTEncoder tx;
    auto tx_symbols = tx.encode_m2400s(tribits);
    std::cout << "[4] Modulate: " << tribits.size() << " -> " << tx_symbols.size() << " symbols" << std::endl;
    
    // ========== RX PATH ==========
    std::cout << "\n=== RX Path ===" << std::endl;
    
    // 5. Demodulate + descramble
    MSDMTDecoder rx;
    auto rx_tribits = rx.decode_m2400s(tx_symbols);
    std::cout << "[5] Demodulate: " << tx_symbols.size() << " -> " << rx_tribits.size() << " tribits" << std::endl;
    
    // Verify tribits match
    int tribit_errors = 0;
    for (size_t i = 0; i < std::min(tribits.size(), rx_tribits.size()); i++) {
        if (tribits[i] != rx_tribits[i]) tribit_errors++;
    }
    std::cout << "    Tribit errors: " << tribit_errors << std::endl;
    
    // 6. Unpack to bits and deinterleave
    std::vector<soft_bit_t> rx_bits;
    for (uint8_t t : rx_tribits) {
        // MS-DMT convention: +soft = bit 0, -soft = bit 1
        rx_bits.push_back((t & 4) ? -1 : 1);
        rx_bits.push_back((t & 2) ? -1 : 1);
        rx_bits.push_back((t & 1) ? -1 : 1);
    }
    
    // Scale to soft bit range
    for (auto& b : rx_bits) b *= 100;
    
    std::vector<soft_bit_t> deinterleaved;
    for (size_t i = 0; i + block_size <= rx_bits.size(); i += block_size) {
        std::vector<soft_bit_t> blk(rx_bits.begin() + i, rx_bits.begin() + i + block_size);
        auto dilvd = interleaver.deinterleave(blk);
        deinterleaved.insert(deinterleaved.end(), dilvd.begin(), dilvd.end());
    }
    std::cout << "[6] Deinterleave: " << rx_bits.size() << " -> " << deinterleaved.size() << " bits" << std::endl;
    
    // 7. Viterbi decode
    ViterbiDecoder decoder;
    std::vector<uint8_t> decoded_bits;
    decoder.decode_block(deinterleaved, decoded_bits, true);
    std::cout << "[7] Viterbi: " << deinterleaved.size() << " -> " << decoded_bits.size() << " bits" << std::endl;
    
    // 8. Pack to bytes
    std::vector<uint8_t> decoded_bytes;
    uint8_t cur = 0; int bc = 0;
    for (uint8_t b : decoded_bits) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) { decoded_bytes.push_back(cur); cur = 0; bc = 0; }
    }
    
    // ========== VERIFY ==========
    std::cout << "\n=== Verification ===" << std::endl;
    
    std::string decoded_msg;
    for (size_t i = 0; i < std::min(message.size(), decoded_bytes.size()); i++) {
        char c = decoded_bytes[i];
        if (c >= 32 && c < 127) decoded_msg += c;
        else decoded_msg += '.';
    }
    std::cout << "Decoded: \"" << decoded_msg << "\"" << std::endl;
    
    int bit_errors = 0;
    for (size_t i = 0; i < std::min(input_bits.size(), decoded_bits.size()); i++) {
        if (input_bits[i] != decoded_bits[i]) bit_errors++;
    }
    
    int byte_errors = 0;
    for (size_t i = 0; i < std::min(message.size(), decoded_bytes.size()); i++) {
        if ((uint8_t)message[i] != decoded_bytes[i]) byte_errors++;
    }
    
    std::cout << "Bit errors: " << bit_errors << " / " << input_bits.size() << std::endl;
    std::cout << "Byte errors: " << byte_errors << " / " << message.size() << std::endl;
    
    bool success = (bit_errors == 0) && (decoded_msg == message);
    std::cout << "\n" << (success ? "✓ TEST PASSED" : "✗ TEST FAILED") << std::endl;
    
    // ========== Test with soft decisions ==========
    std::cout << "\n=== Test with Soft Decision Path ===" << std::endl;
    
    // Debug: print first few TX tribits and symbols
    std::cout << "First 4 TX tribits: ";
    for (int i = 0; i < 4 && i < (int)tribits.size(); i++) {
        std::cout << (int)tribits[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << "First 4 TX symbols: ";
    for (int i = 0; i < 4 && i < (int)tx_symbols.size(); i++) {
        std::cout << "(" << tx_symbols[i].real() << "," << tx_symbols[i].imag() << ") ";
    }
    std::cout << std::endl;
    
    std::cout << "First 4 RX tribits (hard): ";
    for (int i = 0; i < 4 && i < (int)rx_tribits.size(); i++) {
        std::cout << (int)rx_tribits[i] << " ";
    }
    std::cout << std::endl;
    
    MSDMTDecoder rx2;
    std::cout << "Soft decode debug:" << std::endl;
    auto soft_bits = rx2.decode_soft_m2400s(tx_symbols, true);
    std::cout << "Soft bits: " << soft_bits.size() << std::endl;
    
    // Debug: print first few soft bits
    std::cout << "First 12 soft bits: ";
    for (int i = 0; i < 12 && i < (int)soft_bits.size(); i++) {
        std::cout << (int)soft_bits[i] << " ";
    }
    std::cout << std::endl;
    
    // Debug: compare with hard path bits
    std::cout << "First 12 hard bits (scaled): ";
    for (int i = 0; i < 12 && i < (int)rx_bits.size(); i++) {
        std::cout << (int)rx_bits[i] << " ";
    }
    std::cout << std::endl;
    
    // Deinterleave
    std::vector<soft_bit_t> deint2;
    for (size_t i = 0; i + block_size <= soft_bits.size(); i += block_size) {
        std::vector<soft_bit_t> blk(soft_bits.begin() + i, soft_bits.begin() + i + block_size);
        auto d = interleaver.deinterleave(blk);
        deint2.insert(deint2.end(), d.begin(), d.end());
    }
    
    // Viterbi
    ViterbiDecoder dec2;
    std::vector<uint8_t> out2;
    dec2.decode_block(deint2, out2, true);
    
    // Pack bytes
    std::vector<uint8_t> bytes2;
    cur = 0; bc = 0;
    for (uint8_t b : out2) {
        cur = (cur << 1) | (b & 1);
        if (++bc == 8) { bytes2.push_back(cur); cur = 0; bc = 0; }
    }
    
    std::string msg2;
    for (size_t i = 0; i < std::min(message.size(), bytes2.size()); i++) {
        char c = bytes2[i];
        if (c >= 32 && c < 127) msg2 += c;
        else msg2 += '.';
    }
    std::cout << "Soft decoded: \"" << msg2 << "\"" << std::endl;
    
    success = (msg2 == message);
    std::cout << (success ? "✓ SOFT PATH PASSED" : "✗ SOFT PATH FAILED") << std::endl;
    
    return success ? 0 : 1;
}
