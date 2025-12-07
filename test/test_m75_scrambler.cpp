/**
 * Search for correct scrambler offset
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
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) return 1;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    // Generate full scrambler sequence
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
    
    std::cout << "Searching all symbol offsets (0-200) and scrambler starts (0-160)...\n\n";
    
    for (int sym_start = 0; sym_start <= 200; sym_start += 1) {
        for (int scr_start = 0; scr_start < 160; scr_start += 32) {
            // Decode 45 Walsh symbols
            std::vector<int8_t> soft_bits;
            int scr_off = scr_start;
            bool valid = true;
            
            for (int w = 0; w < 45 && valid; w++) {
                int sym_off = sym_start + w * 32;
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
                float total = 0;
                for (int p = 0; p < 4; p++) {
                    total += mags[p];
                    if (mags[p] > mags[best]) best = p;
                }
                
                float soft = (total > 0) ? std::sqrt(mags[best] / total) : 0;
                int s = static_cast<int>(soft * 127);
                
                // Gray decode
                switch (best) {
                    case 0: soft_bits.push_back(s);  soft_bits.push_back(s);  break;
                    case 1: soft_bits.push_back(s);  soft_bits.push_back(-s); break;
                    case 2: soft_bits.push_back(-s); soft_bits.push_back(-s); break;
                    case 3: soft_bits.push_back(-s); soft_bits.push_back(s);  break;
                }
                
                scr_off = (scr_off + 32) % 160;
            }
            
            if (!valid) continue;
            
            // Deinterleave
            InterleaverParams params{10, 9, 7, 2, 45};
            MultiModeInterleaver deinterleaver(params);
            std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
            auto deint = deinterleaver.deinterleave(block);
            
            // Viterbi decode
            ViterbiDecoder viterbi;
            std::vector<uint8_t> decoded_bits;
            viterbi.decode_block(deint, decoded_bits, true);
            
            auto bytes = bits_to_bytes(decoded_bits);
            
            // Check for Hello
            if (bytes.size() >= 5 && 
                bytes[0] == 'H' && bytes[1] == 'e' && 
                bytes[2] == 'l' && bytes[3] == 'l' && bytes[4] == 'o') {
                std::cout << "*** FOUND at sym_start=" << sym_start 
                          << ", scr_start=" << scr_start << " ***\n";
                std::cout << "Output: ";
                for (auto b : bytes) {
                    if (b >= 32 && b < 127) std::cout << (char)b;
                    else std::cout << ".";
                }
                std::cout << "\n";
                return 0;
            }
        }
    }
    
    std::cout << "'Hello' not found.\n";
    
    // Show what we get at a few positions
    std::cout << "\nResults at various positions:\n";
    for (int sym_start : {0, 32, 64, 96}) {
        for (int scr_start : {0, 32, 64, 96, 128}) {
            std::vector<int8_t> soft_bits;
            int scr_off = scr_start;
            
            for (int w = 0; w < 45; w++) {
                int sym_off = sym_start + w * 32;
                if (sym_off + 32 > (int)result.data_symbols.size()) break;
                
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
                float total = 0;
                for (int p = 0; p < 4; p++) {
                    total += mags[p];
                    if (mags[p] > mags[best]) best = p;
                }
                
                float soft = (total > 0) ? std::sqrt(mags[best] / total) : 0;
                int s = static_cast<int>(soft * 127);
                switch (best) {
                    case 0: soft_bits.push_back(s);  soft_bits.push_back(s);  break;
                    case 1: soft_bits.push_back(s);  soft_bits.push_back(-s); break;
                    case 2: soft_bits.push_back(-s); soft_bits.push_back(-s); break;
                    case 3: soft_bits.push_back(-s); soft_bits.push_back(s);  break;
                }
                scr_off = (scr_off + 32) % 160;
            }
            
            InterleaverParams params{10, 9, 7, 2, 45};
            MultiModeInterleaver deinterleaver(params);
            std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
            auto deint = deinterleaver.deinterleave(block);
            
            ViterbiDecoder viterbi;
            std::vector<uint8_t> decoded_bits;
            viterbi.decode_block(deint, decoded_bits, true);
            
            auto bytes = bits_to_bytes(decoded_bits);
            
            std::cout << "sym=" << std::setw(3) << sym_start 
                      << " scr=" << std::setw(3) << scr_start << ": ";
            for (auto b : bytes) {
                if (b >= 32 && b < 127) std::cout << (char)b;
                else std::cout << ".";
            }
            std::cout << "\n";
        }
    }
    
    return 1;
}
