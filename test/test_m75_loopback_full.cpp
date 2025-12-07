/**
 * Full M75 Loopback Test - generate signal and decode
 */
#include "m110a/walsh_75_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>

using namespace m110a;

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            if (bits[i + b]) byte |= (1 << (7 - b));
        }
        bytes.push_back(byte);
    }
    return bytes;
}

int main() {
    std::cout << "=== Full M75 Loopback Test ===\n\n";
    
    // Test data
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    std::cout << "Input: Hello\n\n";
    
    // Step 1: Convert to bits
    std::vector<uint8_t> data_bits;
    for (uint8_t byte : data) {
        for (int b = 7; b >= 0; b--) {
            data_bits.push_back((byte >> b) & 1);
        }
    }
    std::cout << "Data bits: " << data_bits.size() << "\n";
    
    // Step 2: FEC encode
    ConvEncoder encoder;
    std::vector<uint8_t> coded_bits;
    encoder.encode(data_bits, coded_bits, true);
    std::cout << "FEC coded: " << coded_bits.size() << " bits\n";
    
    // Step 3: Interleave (need exactly 90 bits per block)
    // Pad coded_bits to 90
    std::vector<soft_bit_t> to_interleave(90, 0);
    for (size_t i = 0; i < coded_bits.size() && i < 90; i++) {
        to_interleave[i] = coded_bits[i] ? 127 : -127;
    }
    
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver interleaver(params);
    auto interleaved = interleaver.interleave(to_interleave);
    std::cout << "Interleaved: " << interleaved.size() << " bits\n";
    
    // Step 4: Gray encode and Walsh modulate
    static const int mgd2[4] = {0, 1, 3, 2};
    
    // Scrambler
    int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
    std::vector<int> scrambler(160);
    for (int i = 0; i < 160; i++) {
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10]; sreg[10] = sreg[9]; sreg[9] = sreg[8];
            sreg[8] = sreg[7]; sreg[7] = sreg[6]; sreg[6] = sreg[5] ^ carry;
            sreg[5] = sreg[4]; sreg[4] = sreg[3] ^ carry; sreg[3] = sreg[2];
            sreg[2] = sreg[1]; sreg[1] = sreg[0] ^ carry; sreg[0] = carry;
        }
        scrambler[i] = (sreg[2] << 2) | (sreg[1] << 1) | sreg[0];
    }
    
    static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
    static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    
    std::vector<complex_t> signal;
    int scr_count = 0;
    int block_count = 0;
    
    // 45 Walsh symbols from 90 interleaved bits (2 bits per Walsh)
    for (int w = 0; w < 45; w++) {
        // Get 2 bits
        int bit0 = interleaved[w * 2] > 0 ? 1 : 0;
        int bit1 = interleaved[w * 2 + 1] > 0 ? 1 : 0;
        int dibit = (bit0 << 1) | bit1;
        int walsh_idx = mgd2[dibit];
        
        // MES check
        block_count++;
        bool is_mes = (block_count == 45);
        if (is_mes) block_count = 0;
        
        const int (*walsh)[32] = is_mes ? Walsh75Decoder::MES : Walsh75Decoder::MNS;
        
        // Generate 32 Walsh symbols
        for (int k = 0; k < 32; k++) {
            int sym = (walsh[walsh_idx][k] + scrambler[(k + scr_count) % 160]) % 8;
            complex_t c(psk8_i[sym], psk8_q[sym]);
            signal.push_back(c);
            signal.push_back(c);  // Duplicate for 4800 Hz
        }
        
        scr_count = (scr_count + 32) % 160;
    }
    
    std::cout << "Signal: " << signal.size() << " samples (4800 Hz)\n";
    std::cout << "Walsh symbols: " << signal.size() / 64 << "\n\n";
    
    // Decode
    std::cout << "--- Decoding ---\n";
    Walsh75Decoder decoder(45);
    std::vector<int8_t> soft_bits;
    
    for (int w = 0; w < 45; w++) {
        auto res = decoder.decode(&signal[w * 64]);
        Walsh75Decoder::gray_decode(res.data, res.soft, soft_bits);
        
        if (w < 5 || w == 44) {
            std::cout << "  Walsh " << std::setw(2) << w << ": data=" << res.data 
                      << " mag=" << std::fixed << std::setprecision(0) << res.magnitude << "\n";
        } else if (w == 5) {
            std::cout << "  ...\n";
        }
    }
    
    std::cout << "Soft bits decoded: " << soft_bits.size() << "\n";
    
    // Deinterleave
    MultiModeInterleaver deinterleaver(params);
    std::vector<soft_bit_t> sb(soft_bits.begin(), soft_bits.end());
    auto deinterleaved = deinterleaver.deinterleave(sb);
    std::cout << "Deinterleaved: " << deinterleaved.size() << " bits\n";
    
    // Viterbi decode
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deinterleaved, decoded_bits, true);
    std::cout << "Viterbi output: " << decoded_bits.size() << " bits\n";
    
    auto bytes = bits_to_bytes(decoded_bits);
    
    std::cout << "\nResult: ";
    for (auto b : bytes) {
        if (b >= 32 && b < 127) std::cout << (char)b;
        else std::cout << ".";
    }
    std::cout << " (";
    for (auto b : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    std::cout << ")\n" << std::dec;
    
    // Check for Hello
    bool found = (bytes.size() >= 5 && 
                  bytes[0] == 'H' && bytes[1] == 'e' && 
                  bytes[2] == 'l' && bytes[3] == 'l' && bytes[4] == 'o');
    
    std::cout << "\n" << (found ? "*** SUCCESS ***" : "*** FAILED ***") << "\n";
    return found ? 0 : 1;
}
