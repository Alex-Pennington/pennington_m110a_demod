/**
 * Full decode test for reference PCM files
 * Test message: "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890" (54 bytes)
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iomanip>
#include "m110a/msdmt_decoder.h"
#include "m110a/msdmt_preamble.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

// Mode parameters struct
struct ModeParams {
    string name;
    int data_rate;
    int bits_per_symbol;
    int repetition;
    int unknown_syms;
    int known_syms;
    int interleave_rows;
    int interleave_cols;
};

// Mode lookup by D1/D2
ModeParams get_mode(int d1, int d2) {
    // From MS-DMT and our mode_config.h
    if (d1 == 6 && d2 == 4) return {"M2400S", 2400, 3, 1, 20, 20, 40, 36};
    if (d1 == 4 && d2 == 4) return {"M2400L", 2400, 3, 1, 20, 20, 40, 288};
    if (d1 == 6 && d2 == 5) return {"M1200S", 1200, 3, 3, 20, 20, 40, 36};
    if (d1 == 4 && d2 == 5) return {"M1200L", 1200, 3, 3, 20, 20, 40, 288};
    if (d1 == 6 && d2 == 6) return {"M600S", 600, 2, 3, 20, 20, 40, 36};  // QPSK
    if (d1 == 4 && d2 == 6) return {"M600L", 600, 2, 3, 20, 20, 40, 288}; // QPSK
    if (d1 == 6 && d2 == 7) return {"M300S", 300, 2, 6, 20, 20, 40, 36};  // QPSK
    if (d1 == 4 && d2 == 7) return {"M300L", 300, 2, 6, 20, 20, 40, 288}; // QPSK
    if (d1 == 7 && d2 == 4) return {"M150S", 150, 2, 12, 20, 20, 40, 18}; // QPSK
    if (d1 == 5 && d2 == 4) return {"M150L", 150, 2, 12, 20, 20, 40, 144};
    if (d1 == 7 && d2 == 7) return {"M75S", 75, 2, 24, 20, 20, 40, 18};
    if (d1 == 5 && d2 == 7) return {"M75L", 75, 2, 24, 20, 20, 40, 144};
    return {"UNKNOWN", 0, 0, 0, 0, 0, 0, 0};
}

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    size_t num_samples = size / 2;
    vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

// Decode 8-PSK symbol to position (0-7)
int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    pos = ((pos % 8) + 8) % 8;
    return pos;
}

int main(int argc, char** argv) {
    string filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    }
    
    cout << "=== Full Decode Test ===" << endl;
    cout << "File: " << filename << endl;
    cout << "Expected: " << EXPECTED << endl << endl;
    
    auto samples = read_pcm(filename);
    if (samples.empty()) {
        cerr << "Cannot read file" << endl;
        return 1;
    }
    cout << "Samples: " << samples.size() << " (" << samples.size()/48000.0 << " sec)" << endl;
    
    // Step 1: Mode detection and symbol extraction
    MSDMTDecoderConfig cfg;
    cfg.sample_rate = 48000.0f;
    cfg.carrier_freq = 1800.0f;
    cfg.baud_rate = 2400.0f;
    MSDMTDecoder decoder(cfg);
    
    auto result = decoder.decode(samples);
    
    cout << "\nMode: " << result.mode_name << " (D1=" << result.d1 << ", D2=" << result.d2 << ")" << endl;
    cout << "Preamble at sample " << result.start_sample << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    if (result.data_symbols.empty()) {
        cerr << "No data symbols extracted" << endl;
        return 1;
    }
    
    // Get mode parameters
    auto mode = get_mode(result.d1, result.d2);
    if (mode.data_rate == 0) {
        cerr << "Unknown mode D1=" << result.d1 << " D2=" << result.d2 << endl;
        return 1;
    }
    
    cout << "\nMode parameters:" << endl;
    cout << "  Name: " << mode.name << endl;
    cout << "  Data rate: " << mode.data_rate << " bps" << endl;
    cout << "  Bits/symbol: " << mode.bits_per_symbol << endl;
    cout << "  Repetition: " << mode.repetition << endl;
    cout << "  Frame: " << mode.unknown_syms << " data + " << mode.known_syms << " probe" << endl;
    cout << "  Interleave: " << mode.interleave_rows << "x" << mode.interleave_cols << endl;
    
    // Step 2: Descramble data symbols
    cout << "\n--- Descrambling ---" << endl;
    RefScrambler scr;
    
    int unknown_len = mode.unknown_syms;
    int known_len = mode.known_syms;
    int pattern_len = unknown_len + known_len;
    
    vector<int> data_positions;  // Descrambled symbol positions (0-7)
    int sym_idx = 0;
    int frame = 0;
    
    while (sym_idx + pattern_len <= (int)result.data_symbols.size()) {
        // Process data symbols
        for (int i = 0; i < unknown_len; i++) {
            complex<float> sym = result.data_symbols[sym_idx + i];
            uint8_t scr_val = scr.next_tribit();
            
            // Descramble: rotate by -scr_val * 45°
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            
            // Get position
            int pos = decode_8psk_position(sym);
            data_positions.push_back(pos);
        }
        
        // Skip probe symbols
        for (int i = 0; i < known_len; i++) {
            scr.next_tribit();
        }
        
        sym_idx += pattern_len;
        frame++;
    }
    
    cout << "Descrambled " << data_positions.size() << " symbols from " << frame << " frames" << endl;
    
    // Show first few
    cout << "First 20 positions: ";
    for (int i = 0; i < min(20, (int)data_positions.size()); i++) {
        cout << data_positions[i];
    }
    cout << endl;
    
    // Step 3: Apply Gray code mapping (position to tribit)
    // MS-DMT 8-PSK Gray code: pos 0=000, 1=001, 2=011, 3=010, 4=110, 5=111, 6=101, 7=100
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    
    vector<int> bits;
    for (int pos : data_positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    cout << "\nTotal bits: " << bits.size() << endl;
    
    // Step 4: Deinterleave
    int rows = mode.interleave_rows;
    int cols = mode.interleave_cols;
    int block_size = rows * cols;
    
    cout << "\n--- Deinterleaving (" << rows << "x" << cols << " = " << block_size << ") ---" << endl;
    
    vector<int> deinterleaved;
    int num_blocks = bits.size() / block_size;
    cout << "Blocks: " << num_blocks << endl;
    
    for (int block = 0; block < num_blocks; block++) {
        size_t block_start = block * block_size;
        // Interleaver writes rows, reads columns
        // Deinterleaver: read columns, output rows
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                int in_idx = col * rows + row;  // Column-major read
                if (block_start + in_idx < bits.size()) {
                    deinterleaved.push_back(bits[block_start + in_idx]);
                }
            }
        }
    }
    
    cout << "Deinterleaved bits: " << deinterleaved.size() << endl;
    
    // Step 5: Handle repetition (if any)
    vector<int> combined = deinterleaved;
    if (mode.repetition > 1) {
        cout << "\n--- Combining " << mode.repetition << "x repetition ---" << endl;
        int combined_len = deinterleaved.size() / mode.repetition;
        combined.resize(combined_len);
        for (int i = 0; i < combined_len; i++) {
            int sum = 0;
            for (int r = 0; r < mode.repetition; r++) {
                sum += deinterleaved[i + r * combined_len];
            }
            combined[i] = (sum > mode.repetition / 2) ? 1 : 0;
        }
        cout << "Combined bits: " << combined.size() << endl;
    }
    
    // Step 6: Viterbi decode
    cout << "\n--- Viterbi Decoding ---" << endl;
    
    // Convert to soft bits
    vector<int8_t> soft_bits;
    for (int b : combined) {
        // MS-DMT convention: bit 0 -> +127, bit 1 -> -127
        soft_bits.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    
    vector<uint8_t> decoded;
    viterbi.decode_block(soft_bits, decoded, true);
    
    cout << "Decoded " << decoded.size() << " bits -> " << decoded.size()/8 << " bytes" << endl;
    
    // Convert bits to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        bytes.push_back(byte);
    }
    
    // Step 7: Display results
    cout << "\n=== DECODED DATA ===" << endl;
    cout << "Hex: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        printf("%02x ", bytes[i]);
    }
    cout << endl;
    
    cout << "ASCII: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    // Compare to expected
    cout << "\nExpected: " << EXPECTED << endl;
    
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)EXPECTED_LEN); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    cout << "Match: " << matches << "/" << EXPECTED_LEN << " characters";
    if (matches == EXPECTED_LEN) cout << " ✓ PERFECT!";
    else if (matches > 0) cout << " (partial)";
    else cout << " ✗";
    cout << endl;
    
    return 0;
}
