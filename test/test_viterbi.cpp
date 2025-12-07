#include "modem/viterbi.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <random>

using namespace m110a;

void test_encoder() {
    std::cout << "=== Test: Convolutional Encoder ===\n";
    
    ConvEncoder enc;
    
    // Test single bits
    std::cout << "Encoding single bits from state 0:\n";
    
    // Input: 1
    auto [g1, g2] = enc.encode_bit(1);
    std::cout << "  Input 1: output (" << (int)g1 << "," << (int)g2 << ")\n";
    std::cout << "  State: " << (int)enc.state() << "\n";
    
    // The outputs should follow the generator polynomials
    // G1 = 0x6D = 1101101, G2 = 0x4F = 1001111
    // For state=1: G1 parity of (1 & 0x6D) = parity(1) = 1
    //              G2 parity of (1 & 0x4F) = parity(1) = 1
    assert(g1 == 1 && g2 == 1);
    
    enc.reset();
    
    // Encode a sequence
    std::vector<uint8_t> input = {1, 0, 1, 1, 0};
    std::vector<uint8_t> output;
    
    enc.encode(input, output, false);  // No flush
    
    std::cout << "Input:  ";
    for (auto b : input) std::cout << (int)b;
    std::cout << "\nOutput: ";
    for (auto b : output) std::cout << (int)b;
    std::cout << " (" << output.size() << " bits)\n";
    
    assert(output.size() == input.size() * 2);
    
    // Test with flush
    enc.reset();
    std::vector<uint8_t> output_flush;
    enc.encode(input, output_flush, true);
    
    std::cout << "With flush: " << output_flush.size() << " bits\n";
    assert(output_flush.size() == input.size() * 2 + (VITERBI_K - 1) * 2);
    
    std::cout << "PASSED\n\n";
}

void test_decoder_basic() {
    std::cout << "=== Test: Viterbi Decoder Basic ===\n";
    
    // Encode a simple message
    ConvEncoder enc;
    std::vector<uint8_t> message = {1, 0, 1, 1, 0, 0, 1, 0};
    std::vector<uint8_t> encoded;
    
    enc.encode(message, encoded, true);
    
    std::cout << "Message:  ";
    for (auto b : message) std::cout << (int)b;
    std::cout << " (" << message.size() << " bits)\n";
    
    std::cout << "Encoded:  ";
    for (auto b : encoded) std::cout << (int)b;
    std::cout << " (" << encoded.size() << " bits)\n";
    
    // Decode
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    
    dec.decode_block_hard(encoded, decoded, true);
    
    std::cout << "Decoded:  ";
    for (auto b : decoded) std::cout << (int)b;
    std::cout << " (" << decoded.size() << " bits)\n";
    
    // Compare (decoded may have extra flush bits at end)
    bool match = true;
    for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
        if (message[i] != decoded[i]) {
            match = false;
            std::cout << "Mismatch at bit " << i << "\n";
        }
    }
    
    std::cout << "Match: " << (match ? "YES" : "NO") << "\n";
    assert(match);
    
    std::cout << "PASSED\n\n";
}

void test_decoder_errors() {
    std::cout << "=== Test: Viterbi Decoder with Errors ===\n";
    
    // Encode
    ConvEncoder enc;
    std::vector<uint8_t> message = {1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0};
    std::vector<uint8_t> encoded;
    
    enc.encode(message, encoded, true);
    
    std::cout << "Message length: " << message.size() << " bits\n";
    std::cout << "Encoded length: " << encoded.size() << " bits\n";
    
    // Introduce bit errors
    std::vector<uint8_t> corrupted = encoded;
    int num_errors = 3;
    
    // Flip some bits (spread out)
    corrupted[5] ^= 1;
    corrupted[15] ^= 1;
    corrupted[25] ^= 1;
    
    std::cout << "Introduced " << num_errors << " bit errors\n";
    
    // Decode corrupted
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    
    dec.decode_block_hard(corrupted, decoded, true);
    
    // Count errors in decoded output
    int decode_errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
        if (message[i] != decoded[i]) {
            decode_errors++;
        }
    }
    
    std::cout << "Decoded errors: " << decode_errors << "\n";
    
    // Viterbi should correct most errors
    assert(decode_errors < num_errors);
    
    std::cout << "PASSED\n\n";
}

void test_soft_decoding() {
    std::cout << "=== Test: Soft Decision Decoding ===\n";
    
    // Encode
    ConvEncoder enc;
    std::vector<uint8_t> message = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
    std::vector<uint8_t> encoded;
    
    enc.encode(message, encoded, true);
    
    // Convert to soft decisions with some noise
    std::mt19937 rng(12345);
    std::normal_distribution<float> noise(0.0f, 0.3f);  // Noisy
    
    std::vector<soft_bit_t> soft_bits;
    for (uint8_t bit : encoded) {
        // Ideal: bit=0 -> -1.0, bit=1 -> +1.0
        float ideal = bit ? 1.0f : -1.0f;
        float noisy = ideal + noise(rng);
        
        // Convert to soft bit range
        soft_bit_t soft = static_cast<soft_bit_t>(
            std::clamp(noisy * 100.0f, -127.0f, 127.0f));
        soft_bits.push_back(soft);
    }
    
    // Decode with soft decisions
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    
    dec.decode_block(soft_bits, decoded, true);
    
    // Compare
    int errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
        if (message[i] != decoded[i]) {
            errors++;
        }
    }
    
    std::cout << "Message: " << message.size() << " bits\n";
    std::cout << "Decoded: " << decoded.size() << " bits\n";
    std::cout << "Errors: " << errors << "\n";
    
    assert(errors <= 1);  // Should decode correctly with soft decisions
    
    std::cout << "PASSED\n\n";
}

void test_soft_vs_hard() {
    std::cout << "=== Test: Soft vs Hard Decision Comparison ===\n";
    
    // Generate longer message
    std::vector<uint8_t> message;
    std::mt19937 rng(42);
    
    for (int i = 0; i < 100; i++) {
        message.push_back(rng() & 1);
    }
    
    // Encode
    ConvEncoder enc;
    std::vector<uint8_t> encoded;
    enc.encode(message, encoded, true);
    
    std::cout << "Message: " << message.size() << " bits\n";
    std::cout << "Encoded: " << encoded.size() << " bits\n";
    
    // Add noise at different SNR levels
    std::normal_distribution<float> noise(0.0f, 0.5f);  // Moderate noise
    
    // Create noisy channel outputs
    std::vector<float> channel_out;
    for (uint8_t bit : encoded) {
        float ideal = bit ? 1.0f : -1.0f;
        channel_out.push_back(ideal + noise(rng));
    }
    
    // Hard decision decode
    std::vector<uint8_t> hard_bits;
    for (float s : channel_out) {
        hard_bits.push_back(s > 0 ? 1 : 0);
    }
    
    ViterbiDecoder dec_hard;
    std::vector<uint8_t> decoded_hard;
    dec_hard.decode_block_hard(hard_bits, decoded_hard, true);
    
    int hard_errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded_hard.size(); i++) {
        if (message[i] != decoded_hard[i]) hard_errors++;
    }
    
    // Soft decision decode
    std::vector<soft_bit_t> soft_bits;
    for (float s : channel_out) {
        soft_bit_t soft = static_cast<soft_bit_t>(
            std::clamp(s * 80.0f, -127.0f, 127.0f));
        soft_bits.push_back(soft);
    }
    
    ViterbiDecoder dec_soft;
    std::vector<uint8_t> decoded_soft;
    dec_soft.decode_block(soft_bits, decoded_soft, true);
    
    int soft_errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded_soft.size(); i++) {
        if (message[i] != decoded_soft[i]) soft_errors++;
    }
    
    std::cout << "Hard decision errors: " << hard_errors << "\n";
    std::cout << "Soft decision errors: " << soft_errors << "\n";
    
    // Soft should be equal or better than hard
    assert(soft_errors <= hard_errors);
    
    std::cout << "PASSED\n\n";
}

void test_burst_errors() {
    std::cout << "=== Test: Burst Error Correction ===\n";
    
    // Longer message
    std::vector<uint8_t> message;
    for (int i = 0; i < 50; i++) {
        message.push_back((i * 7 + 3) & 1);  // Pseudo-random
    }
    
    ConvEncoder enc;
    std::vector<uint8_t> encoded;
    enc.encode(message, encoded, true);
    
    // Introduce burst error (5 consecutive bits)
    std::vector<uint8_t> corrupted = encoded;
    int burst_start = 40;
    int burst_len = 5;
    
    for (int i = 0; i < burst_len; i++) {
        corrupted[burst_start + i] ^= 1;
    }
    
    std::cout << "Burst error: " << burst_len << " bits at position " 
              << burst_start << "\n";
    
    // Decode
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    dec.decode_block_hard(corrupted, decoded, true);
    
    int errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
        if (message[i] != decoded[i]) errors++;
    }
    
    std::cout << "Decoded errors: " << errors << "\n";
    
    // Should handle some burst errors
    assert(errors < burst_len);
    
    std::cout << "PASSED\n\n";
}

void test_long_message() {
    std::cout << "=== Test: Long Message ===\n";
    
    // 1000 bit message
    std::vector<uint8_t> message;
    std::mt19937 rng(123);
    
    for (int i = 0; i < 1000; i++) {
        message.push_back(rng() & 1);
    }
    
    ConvEncoder enc;
    std::vector<uint8_t> encoded;
    enc.encode(message, encoded, true);
    
    std::cout << "Message: " << message.size() << " bits\n";
    std::cout << "Encoded: " << encoded.size() << " bits\n";
    
    // Clean channel (no errors)
    ViterbiDecoder dec;
    std::vector<uint8_t> decoded;
    dec.decode_block_hard(encoded, decoded, true);
    
    int errors = 0;
    for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
        if (message[i] != decoded[i]) errors++;
    }
    
    std::cout << "Decoded: " << decoded.size() << " bits\n";
    std::cout << "Errors: " << errors << "\n";
    
    assert(errors == 0);
    
    std::cout << "PASSED\n\n";
}

void test_soft_demapper() {
    std::cout << "=== Test: 8-PSK Soft Demapper ===\n";
    
    // Test ideal constellation points
    std::cout << "Testing ideal 8-PSK points:\n";
    
    float noise_var = 0.1f;
    
    for (int i = 0; i < 8; i++) {
        float phase = i * PI / 4.0f;
        complex_t symbol = std::polar(1.0f, phase);
        
        std::array<soft_bit_t, 3> soft_bits;
        SoftDemapper8PSK::demap(symbol, noise_var, soft_bits);
        
        std::cout << "  Point " << i << " (phase=" << (int)(phase * 180 / PI) 
                  << "°): soft=[" << (int)soft_bits[0] << ", " 
                  << (int)soft_bits[1] << ", " << (int)soft_bits[2] << "]\n";
    }
    
    // Test noisy symbol
    complex_t noisy = std::polar(0.8f, 0.1f);  // Near point 0
    std::array<soft_bit_t, 3> soft;
    SoftDemapper8PSK::demap(noisy, noise_var, soft);
    
    std::cout << "\nNoisy symbol near 0°: soft=[" << (int)soft[0] << ", "
              << (int)soft[1] << ", " << (int)soft[2] << "]\n";
    
    std::cout << "PASSED\n\n";
}

void test_coding_gain() {
    std::cout << "=== Test: Coding Gain Measurement ===\n";
    
    std::mt19937 rng(999);
    
    // Test at different noise levels
    std::vector<float> noise_levels = {0.3f, 0.4f, 0.5f, 0.6f};
    
    for (float sigma : noise_levels) {
        // Generate message
        std::vector<uint8_t> message;
        for (int i = 0; i < 500; i++) {
            message.push_back(rng() & 1);
        }
        
        // Encode
        ConvEncoder enc;
        std::vector<uint8_t> encoded;
        enc.encode(message, encoded, true);
        
        // Add noise
        std::normal_distribution<float> noise(0.0f, sigma);
        
        std::vector<soft_bit_t> soft_bits;
        int raw_errors = 0;
        
        for (uint8_t bit : encoded) {
            float ideal = bit ? 1.0f : -1.0f;
            float noisy = ideal + noise(rng);
            
            // Count raw channel errors
            if ((noisy > 0) != (bit == 1)) raw_errors++;
            
            soft_bit_t soft = static_cast<soft_bit_t>(
                std::clamp(noisy * 80.0f, -127.0f, 127.0f));
            soft_bits.push_back(soft);
        }
        
        // Decode
        ViterbiDecoder dec;
        std::vector<uint8_t> decoded;
        dec.decode_block(soft_bits, decoded, true);
        
        int decoded_errors = 0;
        for (size_t i = 0; i < message.size() && i < decoded.size(); i++) {
            if (message[i] != decoded[i]) decoded_errors++;
        }
        
        float raw_ber = (float)raw_errors / encoded.size();
        float decoded_ber = (float)decoded_errors / message.size();
        
        std::cout << "Sigma=" << sigma << ": Raw BER=" << std::fixed 
                  << std::setprecision(4) << raw_ber
                  << " Decoded BER=" << decoded_ber << "\n";
    }
    
    std::cout << "PASSED\n\n";
}

int main() {
    std::cout << "M110A Viterbi Decoder Tests\n";
    std::cout << "===========================\n\n";
    
    test_encoder();
    test_decoder_basic();
    test_decoder_errors();
    test_soft_decoding();
    test_soft_vs_hard();
    test_burst_errors();
    test_long_message();
    test_soft_demapper();
    test_coding_gain();
    
    std::cout << "All Viterbi tests passed!\n";
    return 0;
}
