/**
 * Generate expected Walsh sequence for "Hello" and compare with received
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>
#include <iomanip>

using namespace m110a;

std::vector<float> read_pcm(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(0);
    std::vector<int16_t> raw(size / 2);
    f.read(reinterpret_cast<char*>(raw.data()), size);
    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    std::cout << "=== Expected Walsh Sequence for 'Hello' ===\n\n";
    
    // Step 1: Data to bits
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    std::vector<uint8_t> data_bits;
    for (uint8_t byte : data) {
        for (int b = 7; b >= 0; b--) {
            data_bits.push_back((byte >> b) & 1);
        }
    }
    std::cout << "Data bits (40): ";
    for (int i = 0; i < 40; i++) std::cout << (int)data_bits[i];
    std::cout << "\n";
    
    // Step 2: FEC encode
    ConvEncoder encoder;
    std::vector<uint8_t> coded_bits;
    encoder.encode(data_bits, coded_bits, true);
    
    std::cout << "Coded bits (" << coded_bits.size() << "): ";
    for (size_t i = 0; i < std::min(coded_bits.size(), (size_t)20); i++) 
        std::cout << (int)coded_bits[i];
    std::cout << "...\n";
    
    // Step 3: Interleave (need exactly 90 bits)
    std::vector<soft_bit_t> soft_coded(90, -127);  // Pad with 0s
    for (size_t i = 0; i < coded_bits.size() && i < 90; i++) {
        soft_coded[i] = coded_bits[i] ? 127 : -127;
    }
    
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver interleaver(params);
    auto interleaved = interleaver.interleave(soft_coded);
    
    std::cout << "Interleaved bits: ";
    for (int i = 0; i < 20; i++) std::cout << (interleaved[i] > 0 ? "1" : "0");
    std::cout << "...\n\n";
    
    // Step 4: Convert to Walsh indices using mgd2
    static const int mgd2[4] = {0, 1, 3, 2};
    
    std::cout << "Expected Walsh sequence (first 20):\n";
    std::vector<int> expected_walsh;
    for (int i = 0; i < 45; i++) {
        int bit0 = interleaved[i * 2] > 0 ? 1 : 0;
        int bit1 = interleaved[i * 2 + 1] > 0 ? 1 : 0;
        int dibit = (bit0 << 1) | bit1;
        int walsh_idx = mgd2[dibit];
        expected_walsh.push_back(walsh_idx);
        if (i < 20) {
            std::cout << "  W" << std::setw(2) << i << ": bits=" << bit0 << bit1 
                      << " dibit=" << dibit << " walsh=" << walsh_idx << "\n";
        }
    }
    
    // Now load real signal and decode
    std::cout << "\n=== Searching for Match in Received Signal ===\n";
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) return 1;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    // Scrambler
    static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
    static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    
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
    
    int best_matches = 0;
    int best_offset = 0;
    
    for (int offset = 0; offset <= 500; offset++) {
        std::vector<int> received;
        int scr_off = 0;
        bool valid = true;
        
        for (int w = 0; w < 45 && valid; w++) {
            int sym_off = offset + w * 32;
            if (sym_off + 32 > (int)result.data_symbols.size()) {
                valid = false;
                break;
            }
            
            float mags[4];
            for (int p = 0; p < 4; p++) {
                complex_t sum(0, 0);
                for (int i = 0; i < 32; i++) {
                    int tribit = (Walsh75Decoder::MNS[p][i] + scrambler[(i + scr_off) % 160]) % 8;
                    complex_t pattern(psk8_i[tribit], psk8_q[tribit]);
                    auto s = result.data_symbols[sym_off + i];
                    sum += complex_t(
                        s.real() * pattern.real() + s.imag() * pattern.imag(),
                        s.imag() * pattern.real() - s.real() * pattern.imag()
                    );
                }
                mags[p] = std::norm(sum);
            }
            
            int best = 0;
            for (int p = 1; p < 4; p++) {
                if (mags[p] > mags[best]) best = p;
            }
            received.push_back(best);
            scr_off = (scr_off + 32) % 160;
        }
        
        if (!valid) continue;
        
        int matches = 0;
        for (int i = 0; i < 45; i++) {
            if (received[i] == expected_walsh[i]) matches++;
        }
        
        if (matches > best_matches) {
            best_matches = matches;
            best_offset = offset;
        }
        
        if (matches >= 35) {
            std::cout << "\nOffset " << offset << ": " << matches << "/45 matches!\n";
            std::cout << "Expected:  ";
            for (int i = 0; i < 25; i++) std::cout << expected_walsh[i];
            std::cout << "\n";
            std::cout << "Received:  ";
            for (int i = 0; i < 25; i++) std::cout << received[i];
            std::cout << "\n";
        }
    }
    
    std::cout << "\nBest match: " << best_matches << "/45 at offset " << best_offset << "\n";
    
    // Show received at offset 0
    std::cout << "\nReceived at offset 0:\n";
    std::vector<int> recv0;
    int scr_off = 0;
    for (int w = 0; w < 20; w++) {
        int sym_off = w * 32;
        float mags[4];
        for (int p = 0; p < 4; p++) {
            complex_t sum(0, 0);
            for (int i = 0; i < 32; i++) {
                int tribit = (Walsh75Decoder::MNS[p][i] + scrambler[(i + scr_off) % 160]) % 8;
                complex_t pattern(psk8_i[tribit], psk8_q[tribit]);
                auto s = result.data_symbols[sym_off + i];
                sum += complex_t(
                    s.real() * pattern.real() + s.imag() * pattern.imag(),
                    s.imag() * pattern.real() - s.real() * pattern.imag()
                );
            }
            mags[p] = std::norm(sum);
        }
        int best = 0;
        for (int p = 1; p < 4; p++) {
            if (mags[p] > mags[best]) best = p;
        }
        recv0.push_back(best);
        scr_off = (scr_off + 32) % 160;
    }
    std::cout << "  ";
    for (int i = 0; i < 20; i++) std::cout << recv0[i];
    std::cout << "\n";
    
    return 0;
}
