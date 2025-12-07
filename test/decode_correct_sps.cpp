/**
 * Decode with correct samples per symbol (60 for 8PSK at 2400 bps)
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
    for (size_t i = 0; i < num_samples; i++) samples[i] = raw[i] / 32768.0f;
    return samples;
}

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    cout << "Samples: " << samples.size() << endl;
    
    // Use correct baud rate: 2400 bps / 3 bits = 800 symbols/sec
    // But wait, preamble is at 2400 baud (BPSK/QPSK in preamble)
    
    // Actually, the preamble uses 2400 baud, but data uses different rate?
    // No, the symbol rate should be consistent. Let me check MS-DMT.
    
    // According to modes.json: symbol_rate = 800 for M2400S
    // But the reference code uses M1_SAMPLE_RATE = 9600 and processes at 2 samples/symbol
    // That would be 4800 symbols/sec... but that's M4800S territory
    
    // Let me try with sps=60 (48000/800)
    
    MSDMTDecoderConfig cfg;
    cfg.baud_rate = 800.0f;  // 2400 bps / 3 bits per 8PSK symbol
    MSDMTDecoder decoder(cfg);
    auto result = decoder.decode(samples);
    
    cout << "Mode: " << result.mode_name << endl;
    cout << "Correlation: " << result.correlation << endl;
    cout << "Accuracy: " << result.accuracy << "%" << endl;
    cout << "Preamble start: " << result.start_sample << endl;
    cout << "Data symbols: " << result.data_symbols.size() << endl;
    
    return 0;
}
