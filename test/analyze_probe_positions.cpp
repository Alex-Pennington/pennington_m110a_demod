/**
 * Analyze where probe pattern appears - determine correct data start
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_decoder.h"
#include "modem/scrambler.h"

using namespace m110a;
using namespace std;

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
    
    vector<int> received;
    for (auto& sym : result.data_symbols) {
        received.push_back(decode_8psk_position(sym));
    }
    
    // Generate scrambler output at various offsets
    cout << "=== Analysis ===" << endl;
    cout << "Data symbols: " << received.size() << endl;
    cout << "Preamble start sample: " << result.start_sample << endl;
    
    // Current data start is preamble_start + 1440*20 (for short interleave)
    // 1440 symbols = 3 preamble frames
    
    // But what if only 480 preamble symbols? (1 frame)
    // Then data would start 480*20 = 9600 samples after preamble_start
    // Which is 960 symbols earlier in our current extraction
    
    cout << "\n--- If preamble is only 480 symbols (1 frame) ---" << endl;
    int offset_if_1_frame = 1440 - 480;  // = 960 symbols earlier
    
    // Check if probes align at this offset
    RefScrambler scr;
    vector<int> probe;
    for (int i = 0; i < 100; i++) {
        probe.push_back(scr.next_tribit());
    }
    
    // If data really starts at position 960 in our received array...
    int assumed_data_start = 960;
    cout << "Checking probes starting at position " << assumed_data_start << endl;
    
    for (int frame = 0; frame < 5; frame++) {
        int probe_pos = assumed_data_start + frame * 40 + 20;
        
        if (probe_pos + 20 > (int)received.size()) break;
        
        int matches = 0;
        cout << "Frame " << frame << " probe (pos " << probe_pos << "): ";
        for (int i = 0; i < 20; i++) {
            cout << received[probe_pos + i];
            if (received[probe_pos + i] == probe[frame * 20 + i]) matches++;
        }
        cout << " (" << matches << "/20)" << endl;
        
        cout << "Expected:                        ";
        for (int i = 0; i < 20; i++) {
            cout << probe[frame * 20 + i];
        }
        cout << endl;
    }
    
    // Also check if perfect match at 1440 means probes start there
    cout << "\n--- Checking if true data starts at position 1440 ---" << endl;
    int true_start = 1440;
    
    for (int frame = 0; frame < 3; frame++) {
        int probe_pos = true_start + frame * 40 + 20;
        
        if (probe_pos + 20 > (int)received.size()) break;
        
        scr = RefScrambler();  // Reset for each test
        
        int matches = 0;
        cout << "Frame " << frame << " probe (pos " << probe_pos << "): ";
        for (int i = 0; i < 20; i++) {
            cout << received[probe_pos + i];
        }
        
        // Expected: scrambler output starting at frame * 20
        cout << "  vs  ";
        for (int i = 0; i < 20; i++) {
            cout << probe[frame * 20 + i];
            if (received[probe_pos + i] == probe[frame * 20 + i]) matches++;
        }
        cout << " (" << matches << "/20)" << endl;
    }
    
    return 0;
}
