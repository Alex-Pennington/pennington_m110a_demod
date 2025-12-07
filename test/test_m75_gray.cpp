/**
 * Test different Gray decode mappings
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstring>

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

std::vector<int> scrambler;
void init_scrambler() {
    int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
    scrambler.resize(160);
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
}

static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};

int walsh_decode(const complex_t* sym, int scr_offset) {
    float mags[4];
    for (int p = 0; p < 4; p++) {
        complex_t sum(0, 0);
        for (int i = 0; i < 32; i++) {
            int tribit = (Walsh75Decoder::MNS[p][i] + scrambler[(i + scr_offset) % 160]) % 8;
            complex_t pattern(psk8_i[tribit], psk8_q[tribit]);
            sum += complex_t(
                sym[i].real() * pattern.real() + sym[i].imag() * pattern.imag(),
                sym[i].imag() * pattern.real() - sym[i].real() * pattern.imag()
            );
        }
        mags[p] = std::norm(sum);
    }
    int best = 0;
    for (int p = 1; p < 4; p++) if (mags[p] > mags[best]) best = p;
    return best;
}

// Gray decode mappings to try
// Walsh -> (b0, b1) where bits are +127/-127
void gray_decode(int data, int mapping, int& b0, int& b1) {
    // Standard Gray: 0->00, 1->01, 2->11, 3->10
    // Inverse Gray:  0->00, 1->10, 2->11, 3->01
    static const int maps[8][4][2] = {
        {{1,1}, {1,-1}, {-1,-1}, {-1,1}},   // 0: standard
        {{1,1}, {-1,1}, {-1,-1}, {1,-1}},   // 1: swap bits
        {{-1,-1}, {-1,1}, {1,1}, {1,-1}},   // 2: invert all
        {{-1,-1}, {1,-1}, {1,1}, {-1,1}},   // 3: invert + swap
        {{1,1}, {1,-1}, {-1,1}, {-1,-1}},   // 4: alternate mapping
        {{1,1}, {-1,-1}, {1,-1}, {-1,1}},   // 5: different order
        {{-1,1}, {1,1}, {1,-1}, {-1,-1}},   // 6: another variation
        {{1,-1}, {-1,-1}, {-1,1}, {1,1}},   // 7: yet another
    };
    b0 = maps[mapping][data][0] * 127;
    b1 = maps[mapping][data][1] * 127;
}

bool try_decode(const std::vector<complex_t>& symbols, int sym_offset, int gray_map, bool swap_order) {
    std::vector<int8_t> soft_bits;
    int scr_offset = 0;
    
    for (int w = 0; w < 45; w++) {
        int pos = sym_offset + w * 32;
        if (pos + 32 > (int)symbols.size()) return false;
        
        int data = walsh_decode(&symbols[pos], scr_offset);
        
        int b0, b1;
        gray_decode(data, gray_map, b0, b1);
        
        if (swap_order) {
            soft_bits.push_back(b1);
            soft_bits.push_back(b0);
        } else {
            soft_bits.push_back(b0);
            soft_bits.push_back(b1);
        }
        
        scr_offset = (scr_offset + 32) % 160;
    }
    
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver deinterleaver(params);
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
    auto deint = deinterleaver.deinterleave(block);
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deint, decoded_bits, true);
    
    auto bytes = bits_to_bytes(decoded_bits);
    
    std::string expected = "Hello";
    for (size_t i = 0; i + expected.size() <= bytes.size(); i++) {
        if (memcmp(&bytes[i], expected.data(), expected.size()) == 0) {
            return true;
        }
    }
    return false;
}

std::string decode_to_string(const std::vector<complex_t>& symbols, int sym_offset, int gray_map, bool swap_order) {
    std::vector<int8_t> soft_bits;
    int scr_offset = 0;
    
    for (int w = 0; w < 45; w++) {
        int pos = sym_offset + w * 32;
        if (pos + 32 > (int)symbols.size()) return "ERR";
        
        int data = walsh_decode(&symbols[pos], scr_offset);
        
        int b0, b1;
        gray_decode(data, gray_map, b0, b1);
        
        if (swap_order) {
            soft_bits.push_back(b1);
            soft_bits.push_back(b0);
        } else {
            soft_bits.push_back(b0);
            soft_bits.push_back(b1);
        }
        
        scr_offset = (scr_offset + 32) % 160;
    }
    
    InterleaverParams params{10, 9, 7, 2, 45};
    MultiModeInterleaver deinterleaver(params);
    std::vector<soft_bit_t> block(soft_bits.begin(), soft_bits.end());
    auto deint = deinterleaver.deinterleave(block);
    
    ViterbiDecoder viterbi;
    std::vector<uint8_t> decoded_bits;
    viterbi.decode_block(deint, decoded_bits, true);
    
    auto bytes = bits_to_bytes(decoded_bits);
    
    std::string result;
    for (auto b : bytes) {
        if (b >= 32 && b < 127) result += (char)b;
        else result += ".";
    }
    return result;
}

int main() {
    init_scrambler();
    
    auto samples = read_pcm("/home/claude/tx_75S_20251206_202410_888.pcm");
    if (samples.empty()) return 1;
    
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    cfg.preamble_symbols = 1440;
    
    MSDMTDecoder msdmt(cfg);
    auto result = msdmt.decode(samples);
    
    std::cout << "Testing Gray decode mappings at offset 0:\n\n";
    
    for (int gray_map = 0; gray_map < 8; gray_map++) {
        for (int swap = 0; swap <= 1; swap++) {
            std::string s = decode_to_string(result.data_symbols, 0, gray_map, swap);
            std::cout << "Gray " << gray_map << " swap=" << swap << ": " << s << "\n";
        }
    }
    
    std::cout << "\nSearching all combinations...\n";
    
    for (int sym_offset = 0; sym_offset < 200; sym_offset++) {
        for (int gray_map = 0; gray_map < 8; gray_map++) {
            for (int swap = 0; swap <= 1; swap++) {
                if (try_decode(result.data_symbols, sym_offset, gray_map, swap)) {
                    std::cout << "*** FOUND at offset=" << sym_offset 
                              << ", gray=" << gray_map 
                              << ", swap=" << swap << " ***\n";
                    return 0;
                }
            }
        }
    }
    
    std::cout << "Not found in search range.\n";
    return 1;
}
