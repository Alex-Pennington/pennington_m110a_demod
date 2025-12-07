/**
 * Verify if data starts at position 1420 in our data_symbols array
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int EXPECTED_LEN = 54;

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

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    pos = ((pos % 8) + 8) % 8;
    return pos;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    // Assume true data starts at position 1420
    // Wait, position 1440 had probe[0:20], so:
    // - 1440 = probe start
    // - 1420 = data start (20 symbols before probe)
    // But probe is AFTER data, so data is at 1420-1439, probe at 1440-1459
    
    // Actually no. In the frame:
    // - Symbols 0-19: UNKNOWN (data) 
    // - Symbols 20-39: KNOWN (probe)
    // So if position 1440 has probe[0:20], that's the FIRST probe.
    // That means frame 0 data is at 1420-1439, and this position 1440 corresponds to symbol 20 of frame 0.
    // Wait no, probe[0:20] is scrambler output 0-19, which corresponds to frame positions 0-19.
    
    // Let me think more carefully:
    // - probe[0:20] = scrambler output at symbol positions 0-19
    // - In a frame, probes are at positions 20-39
    // - But the scrambler runs continuously through both data and probe
    // 
    // So if received at position 1440 matches probe[0:20] = scrambler[0:20],
    // then position 1440 is where the scrambler is at state 0.
    // That means data starts at 1440, probes at 1460.
    
    // BUT WAIT - the probe pattern I generated was just scrambler output,
    // which is what gets added to data symbols OR used directly for probes.
    // For probe symbols, the transmitted value = scrambler_output (since known=0)
    
    int data_start_offset = 1440;  // In the data_symbols array
    
    cout << "Testing with data start offset = " << data_start_offset << endl;
    
    // Extract and descramble
    RefScrambler scr;
    vector<int> positions;
    int idx = data_start_offset;
    
    // Need enough for 1 interleave block (1440 bits / 3 = 480 symbols data only)
    // With 20 data + 20 probe per frame, that's 480/20 = 24 frames
    // = 24 * 40 = 960 total symbols
    
    while (idx + 40 <= (int)result.data_symbols.size() && positions.size() < 480) {
        // 20 data symbols
        for (int i = 0; i < 20 && positions.size() < 480; i++) {
            complex<float> sym = result.data_symbols[idx + i];
            uint8_t scr_val = scr.next_tribit();
            
            float scr_phase = -scr_val * (M_PI / 4.0f);
            sym *= polar(1.0f, scr_phase);
            
            positions.push_back(decode_8psk_position(sym));
        }
        
        // 20 probe symbols - just advance scrambler
        for (int i = 0; i < 20; i++) {
            scr.next_tribit();
        }
        
        idx += 40;
    }
    
    cout << "Extracted " << positions.size() << " data symbols" << endl;
    
    // Gray decode
    const int gray_map[8] = {0, 1, 3, 2, 6, 7, 5, 4};
    vector<int> bits;
    for (int pos : positions) {
        int tribit = gray_map[pos];
        bits.push_back((tribit >> 2) & 1);
        bits.push_back((tribit >> 1) & 1);
        bits.push_back(tribit & 1);
    }
    
    // Deinterleave
    int rows = 40, cols = 36, block_size = 1440;
    vector<int> deinterleaved;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = col * rows + row;
            if (in_idx < (int)bits.size()) {
                deinterleaved.push_back(bits[in_idx]);
            }
        }
    }
    
    // Viterbi
    vector<int8_t> soft;
    for (int b : deinterleaved) {
        soft.push_back(b ? -127 : 127);
    }
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            byte = (byte << 1) | (decoded[i + j] & 1);
        }
        bytes.push_back(byte);
    }
    
    // Show result
    cout << "\nDecoded " << bytes.size() << " bytes:" << endl;
    cout << "ASCII: ";
    for (size_t i = 0; i < min(bytes.size(), (size_t)60); i++) {
        char c = bytes[i];
        if (c >= 32 && c < 127) cout << c;
        else cout << '.';
    }
    cout << endl;
    
    int matches = 0;
    for (size_t i = 0; i < min(bytes.size(), (size_t)EXPECTED_LEN); i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    cout << "Match: " << matches << "/" << EXPECTED_LEN << endl;
    
    return 0;
}
