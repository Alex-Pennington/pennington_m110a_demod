/**
 * Compute expected first data symbols for M2400S
 */
#include <iostream>
#include <vector>
#include <cstring>
#include "modem/scrambler.h"
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

int main() {
    cout << "=== Expected Data Symbols for M2400S ===" << endl;
    
    // Step 1: Message to bits
    vector<uint8_t> msg_bits;
    for (const char* p = TEST_MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 7; i >= 0; i--) {
            msg_bits.push_back((c >> i) & 1);
        }
    }
    cout << "Message bits: " << msg_bits.size() << endl;
    
    // Step 2: FEC encode
    ConvEncoder encoder;
    vector<uint8_t> encoded;
    encoder.encode(msg_bits, encoded, true);
    cout << "Encoded bits: " << encoded.size() << endl;
    
    // Step 3: Pad to 1440 (40x36 interleave block)
    while (encoded.size() < 1440) {
        encoded.push_back(0);
    }
    
    // Step 4: Interleave (write rows, read cols)
    int rows = 40, cols = 36;
    vector<uint8_t> interleaved(1440);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int in_idx = row * cols + col;
            int out_idx = col * rows + row;
            interleaved[out_idx] = encoded[in_idx];
        }
    }
    
    // Step 5: Map to tribits and positions (Gray code)
    // Gray: tribit 0=pos0, 1=pos1, 2=pos3, 3=pos2, 4=pos7, 5=pos6, 6=pos4, 7=pos5
    const int tribit_to_pos[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    
    vector<int> positions;
    for (size_t i = 0; i + 3 <= interleaved.size(); i += 3) {
        int tribit = (interleaved[i] << 2) | (interleaved[i+1] << 1) | interleaved[i+2];
        positions.push_back(tribit_to_pos[tribit]);
    }
    cout << "Symbol positions: " << positions.size() << endl;
    
    // Step 6: Scramble
    RefScrambler scr;
    vector<int> scrambled;
    for (int pos : positions) {
        uint8_t scr_val = scr.next_tribit();
        scrambled.push_back((pos + scr_val) % 8);
    }
    
    // Step 7: Insert probe symbols (for 20 data + 20 probe frame structure)
    // Probe symbols are scrambled version of known pattern (usually 0)
    scr = RefScrambler();
    vector<int> with_probes;
    int data_idx = 0;
    
    cout << "\n--- First 2 frames with probes ---" << endl;
    for (int frame = 0; frame < 2; frame++) {
        cout << "Frame " << frame << " data: ";
        for (int i = 0; i < 20 && data_idx < (int)scrambled.size(); i++) {
            // Advance scrambler 
            scr.next_tribit();
            with_probes.push_back(scrambled[data_idx]);
            cout << scrambled[data_idx];
            data_idx++;
        }
        cout << endl;
        
        cout << "Frame " << frame << " probe: ";
        for (int i = 0; i < 20; i++) {
            uint8_t scr_val = scr.next_tribit();
            // Probe is scrambled 0
            int probe = scr_val % 8;
            with_probes.push_back(probe);
            cout << probe;
        }
        cout << endl;
    }
    
    cout << "\n--- Expected first 40 transmitted symbols (1 frame) ---" << endl;
    cout << "Data + Probe: ";
    for (size_t i = 0; i < 40 && i < with_probes.size(); i++) {
        cout << with_probes[i];
    }
    cout << endl;
    
    return 0;
}
