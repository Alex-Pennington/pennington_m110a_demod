/**
 * Scan all possible data start offsets to find where message might be
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* EXPECTED = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
const int inv_gray[8] = {0, 1, 3, 2, 7, 6, 4, 5};

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    vector<int16_t> raw(size / 2);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(size / 2);
    for (size_t i = 0; i < size / 2; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int try_decode(vector<complex<float>>& all_syms, int start_offset, int data_len, int probe_len) {
    // Descramble starting at offset
    RefScrambler scr;
    
    // Pre-advance scrambler
    for (int i = 0; i < start_offset; i++) scr.next_tribit();
    
    vector<int> tribits;
    int sym_idx = 0;
    int frame_len = data_len + probe_len;
    
    while (sym_idx + frame_len <= (int)all_syms.size() && tribits.size() < 480) {
        // Data symbols
        for (int i = 0; i < data_len && sym_idx + i < (int)all_syms.size(); i++) {
            auto sym = all_syms[sym_idx + i];
            float phase = atan2(sym.imag(), sym.real());
            if (phase < 0) phase += 2 * M_PI;
            int rcv = (int)round(phase * 4 / M_PI) % 8;
            uint8_t s = scr.next_tribit();
            int desc = (rcv - s + 8) % 8;
            tribits.push_back(inv_gray[desc]);
        }
        // Probe symbols
        for (int i = 0; i < probe_len; i++) scr.next_tribit();
        sym_idx += frame_len;
    }
    
    if (tribits.size() < 480) return 0;
    
    // Tribits to bits
    vector<uint8_t> bits;
    for (int t : tribits) {
        bits.push_back((t >> 2) & 1);
        bits.push_back((t >> 1) & 1);
        bits.push_back(t & 1);
    }
    
    // Deinterleave
    int rows = 40, cols = 36;
    vector<uint8_t> deint(1440);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int read_idx = col * rows + row;
            int write_idx = row * cols + col;
            if (read_idx < (int)bits.size()) deint[write_idx] = bits[read_idx];
        }
    }
    
    // Viterbi decode
    vector<int8_t> soft;
    for (uint8_t b : deint) soft.push_back(b ? -127 : 127);
    
    ViterbiDecoder viterbi;
    vector<uint8_t> decoded;
    viterbi.decode_block(soft, decoded, true);
    
    // Convert to bytes
    vector<uint8_t> bytes;
    for (size_t i = 0; i + 8 <= decoded.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) byte = (byte << 1) | (decoded[i + j] & 1);
        bytes.push_back(byte);
    }
    
    // Count matches
    int matches = 0;
    for (size_t i = 0; i < bytes.size() && i < 54; i++) {
        if (bytes[i] == (uint8_t)EXPECTED[i]) matches++;
    }
    
    return matches;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    MSDMTDecoderConfig cfg;
    MSDMTDecoder decoder(cfg);
    auto samples = read_pcm(filename);
    auto result = decoder.decode(samples);
    
    cout << "Total data_symbols: " << result.data_symbols.size() << endl;
    cout << "\nScanning offsets 0-200 with frame structure 20+20:" << endl;
    
    int best_matches = 0;
    int best_offset = 0;
    
    for (int off = 0; off <= 200; off++) {
        int m = try_decode(result.data_symbols, off, 20, 20);
        if (m > best_matches) {
            best_matches = m;
            best_offset = off;
            cout << "Offset " << off << ": " << m << " matches" << endl;
        }
    }
    
    cout << "\nBest: offset=" << best_offset << " with " << best_matches << "/54 matches" << endl;
    
    // Try different frame structures
    cout << "\nTrying different frame structures at offset 0:" << endl;
    struct { int data; int probe; } frames[] = {
        {20, 20}, {32, 8}, {16, 16}, {40, 0}, {20, 0}
    };
    
    for (auto f : frames) {
        int m = try_decode(result.data_symbols, 0, f.data, f.probe);
        cout << "Frame " << f.data << "+" << f.probe << ": " << m << " matches" << endl;
    }
    
    return 0;
}
