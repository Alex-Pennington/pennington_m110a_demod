/**
 * Trace M75 decode step by step
 */
#include "m110a/walsh_75_decoder.h"
#include "m110a/msdmt_decoder.h"
#include "modem/multimode_interleaver.h"
#include "modem/viterbi.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

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

// Direct Walsh correlation on 2400 Hz symbols (no i*2 indexing, no sync_mask)
struct WalshDecodeResult {
    int data;
    float magnitude;
    float soft;
};

WalshDecodeResult walsh_decode_direct(const complex_t* sym, int scr_offset) {
    static const float psk8_i[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
    static const float psk8_q[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    
    // Scrambler
    static std::vector<int> scrambler;
    if (scrambler.empty()) {
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
    
    float mags[4];
    float total = 0;
    
    for (int p = 0; p < 4; p++) {
        complex_t sum(0, 0);
        for (int i = 0; i < 32; i++) {
            int tribit = (Walsh75Decoder::MNS[p][i] + scrambler[(i + scr_offset) % 160]) % 8;
            complex_t pattern(psk8_i[tribit], psk8_q[tribit]);
            // Conjugate correlation
            sum += complex_t(
                sym[i].real() * pattern.real() + sym[i].imag() * pattern.imag(),
                sym[i].imag() * pattern.real() - sym[i].real() * pattern.imag()
            );
        }
        mags[p] = std::norm(sum);
        total += mags[p];
    }
    
    int best = 0;
    for (int p = 1; p < 4; p++) {
        if (mags[p] > mags[best]) best = p;
    }
    
    float soft = (total > 0) ? std::sqrt(mags[best] / total) : 0;
    return {best, mags[best], soft};
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
    
    std::cout << "Testing direct decode (32 symbols per Walsh, 2400 Hz):\n\n";
    
    // Decode 45 Walsh symbols starting at offset 0
    int sym_offset = 0;
    int scr_offset = 0;
    std::vector<int8_t> soft_bits;
    
    std::cout << "W#  Data  Mag    Soft   Bits\n";
    for (int w = 0; w < 45; w++) {
        if (sym_offset + 32 > (int)result.data_symbols.size()) break;
        
        auto res = walsh_decode_direct(&result.data_symbols[sym_offset], scr_offset);
        
        // Gray decode
        int s = static_cast<int>(res.soft * 127);
        int b0, b1;
        switch (res.data) {
            case 0: b0 = s;  b1 = s;  break;  // 00
            case 1: b0 = s;  b1 = -s; break;  // 01
            case 2: b0 = -s; b1 = -s; break;  // 11
            case 3: b0 = -s; b1 = s;  break;  // 10
        }
        soft_bits.push_back(b0);
        soft_bits.push_back(b1);
        
        if (w < 20) {
            std::cout << std::setw(2) << w << "  " << res.data 
                      << "    " << std::fixed << std::setprecision(1) << std::setw(6) << res.magnitude
                      << "  " << std::setprecision(2) << res.soft
                      << "    " << (b0 > 0 ? "+" : "-") << (b1 > 0 ? "+" : "-") << "\n";
        }
        
        sym_offset += 32;
        scr_offset = (scr_offset + 32) % 160;
    }
    
    std::cout << "...\n\n";
    std::cout << "Soft bits: " << soft_bits.size() << "\n";
    
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
    
    std::cout << "\nResult: ";
    for (auto b : bytes) {
        if (b >= 32 && b < 127) std::cout << (char)b;
        else std::cout << ".";
    }
    std::cout << " (";
    for (auto b : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    std::cout << ")\n" << std::dec;
    
    // Compare with expected
    std::cout << "\nExpected: Hello (48 65 6c 6c 6f)\n";
    
    // Check what bits we expect for 'H'
    std::cout << "\nFor 'H' (0x48 = 01001000):\n";
    std::cout << "  Bits 01 -> mgd2[1]=1 -> Walsh 1 -> Gray decode (+,-)  = 01\n";
    std::cout << "  Bits 00 -> mgd2[0]=0 -> Walsh 0 -> Gray decode (+,+)  = 00\n";
    std::cout << "  Bits 10 -> mgd2[2]=3 -> Walsh 3 -> Gray decode (-,+)  = 10\n";
    std::cout << "  Bits 00 -> mgd2[0]=0 -> Walsh 0 -> Gray decode (+,+)  = 00\n";
    
    return 0;
}
