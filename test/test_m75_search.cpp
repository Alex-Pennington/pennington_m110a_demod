/**
 * Search for correct M75 decode parameters
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

// Scrambler
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

bool try_decode(const std::vector<complex_t>& symbols, int sym_offset, int scr_start, bool invert_bits) {
    std::vector<int8_t> soft_bits;
    int scr_offset = scr_start;
    
    for (int w = 0; w < 45; w++) {
        int pos = sym_offset + w * 32;
        if (pos + 32 > (int)symbols.size()) return false;
        
        int data = walsh_decode(&symbols[pos], scr_offset);
        
        // Gray decode with optional inversion
        int b0, b1;
        switch (data) {
            case 0: b0 = 127;  b1 = 127;  break;
            case 1: b0 = 127;  b1 = -127; break;
            case 2: b0 = -127; b1 = -127; break;
            case 3: b0 = -127; b1 = 127;  break;
        }
        if (invert_bits) { b0 = -b0; b1 = -b1; }
        soft_bits.push_back(b0);
        soft_bits.push_back(b1);
        
        scr_offset = (scr_offset + 32) % 160;
    }
    
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
    std::string expected = "Hello";
    for (size_t i = 0; i + expected.size() <= bytes.size(); i++) {
        if (memcmp(&bytes[i], expected.data(), expected.size()) == 0) {
            return true;
        }
    }
    return false;
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
    
    std::cout << "Searching for Hello...\n";
    std::cout << "Symbols: " << result.data_symbols.size() << "\n\n";
    
    // Search over symbol offset, scrambler start, and bit inversion
    for (int sym_offset = 0; sym_offset < 500; sym_offset++) {
        for (int scr_start = 0; scr_start < 160; scr_start += 32) {
            for (int inv = 0; inv <= 1; inv++) {
                if (try_decode(result.data_symbols, sym_offset, scr_start, inv)) {
                    std::cout << "*** FOUND at sym_offset=" << sym_offset 
                              << ", scr_start=" << scr_start
                              << ", invert=" << inv << " ***\n";
                    return 0;
                }
            }
        }
    }
    
    std::cout << "'Hello' not found in search range.\n";
    
    // Show what we get at offset 0
    std::cout << "\nShowing result at offset 0, scr_start 0:\n";
    std::vector<int8_t> soft_bits;
    int scr_offset = 0;
    
    for (int w = 0; w < 45; w++) {
        int pos = w * 32;
        int data = walsh_decode(&result.data_symbols[pos], scr_offset);
        
        int b0, b1;
        switch (data) {
            case 0: b0 = 127;  b1 = 127;  break;
            case 1: b0 = 127;  b1 = -127; break;
            case 2: b0 = -127; b1 = -127; break;
            case 3: b0 = -127; b1 = 127;  break;
        }
        soft_bits.push_back(b0);
        soft_bits.push_back(b1);
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
    std::cout << "Result: ";
    for (auto b : bytes) {
        if (b >= 32 && b < 127) std::cout << (char)b;
        else std::cout << ".";
    }
    std::cout << " (";
    for (auto b : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    std::cout << ")\n";
    
    return 1;
}
