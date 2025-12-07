/**
 * Verify encoder matches reference
 */
#include <iostream>
#include <vector>
#include "modem/viterbi.h"

using namespace m110a;
using namespace std;

// Reference encoder using exact same method as reference modem
class RefEncoder {
public:
    RefEncoder() : state(0) {}
    void reset() { state = 0; }
    
    pair<int,int> encode(int in) {
        state = state >> 1;
        if (in) state |= 0x40;
        
        // G1 = 0x5B: bits 0,1,3,4,6
        int count1 = 0;
        if (state & 0x01) count1++;
        if (state & 0x02) count1++;
        if (state & 0x08) count1++;
        if (state & 0x10) count1++;
        if (state & 0x40) count1++;
        int bit1 = count1 & 1;
        
        // G2 = 0x79: bits 0,3,4,5,6
        int count2 = 0;
        if (state & 0x01) count2++;
        if (state & 0x08) count2++;
        if (state & 0x10) count2++;
        if (state & 0x20) count2++;
        if (state & 0x40) count2++;
        int bit2 = count2 & 1;
        
        return {bit1, bit2};
    }
    
private:
    int state;
};

int main() {
    const char* MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
    
    // Convert to bits LSB first
    vector<int> bits;
    for (const char* p = MSG; *p; p++) {
        uint8_t c = *p;
        for (int i = 0; i < 8; i++) {
            bits.push_back((c >> i) & 1);
        }
    }
    
    cout << "First 24 message bits (LSB first): ";
    for (int i = 0; i < 24; i++) cout << bits[i];
    cout << endl;
    
    // Encode with reference encoder
    RefEncoder ref;
    vector<int> ref_out;
    for (int bit : bits) {
        auto [b1, b2] = ref.encode(bit);
        ref_out.push_back(b1);
        ref_out.push_back(b2);
    }
    
    cout << "First 48 reference encoded: ";
    for (int i = 0; i < 48; i++) cout << ref_out[i];
    cout << endl;
    
    // Encode with my encoder
    ConvEncoder my_enc;
    vector<uint8_t> my_out;
    my_enc.encode(vector<uint8_t>(bits.begin(), bits.end()), my_out, false);
    
    cout << "First 48 my encoded:        ";
    for (int i = 0; i < 48; i++) cout << (int)my_out[i];
    cout << endl;
    
    // Compare
    int matches = 0;
    for (size_t i = 0; i < min(ref_out.size(), my_out.size()); i++) {
        if (ref_out[i] == my_out[i]) matches++;
    }
    cout << "\nMatches: " << matches << "/" << min(ref_out.size(), my_out.size()) << endl;
    
    // Also check with bit1/bit2 swapped
    ref.reset();
    vector<int> ref_swapped;
    for (int bit : bits) {
        auto [b1, b2] = ref.encode(bit);
        ref_swapped.push_back(b2);  // Swap!
        ref_swapped.push_back(b1);
    }
    
    int matches_swapped = 0;
    for (size_t i = 0; i < min(ref_swapped.size(), my_out.size()); i++) {
        if (ref_swapped[i] == my_out[i]) matches_swapped++;
    }
    cout << "Matches (swapped): " << matches_swapped << "/" << min(ref_swapped.size(), my_out.size()) << endl;
    
    return 0;
}
