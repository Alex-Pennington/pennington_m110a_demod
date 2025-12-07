/**
 * Verify scrambler matches reference exactly
 */
#include <iostream>
#include <vector>
#include <cstdint>

using namespace std;

// Reference implementation from t110a.cpp
class RefDataScrambler {
public:
    RefDataScrambler() {
        sreg[0]  = 1;
        sreg[1]  = 0;
        sreg[2]  = 1;
        sreg[3]  = 1;
        sreg[4]  = 0;
        sreg[5]  = 1;
        sreg[6]  = 0;
        sreg[7]  = 1;
        sreg[8]  = 1;
        sreg[9]  = 1;
        sreg[10] = 0;
        sreg[11] = 1;
        offset = 0;
    }
    
    // Generate full 160-symbol sequence
    void generate_sequence(int* seq) {
        RefDataScrambler tmp;
        for (int i = 0; i < 160; i++) {
            seq[i] = tmp.next();
        }
    }
    
    int next() {
        // Clock 8 times before outputting
        for (int j = 0; j < 8; j++) {
            int carry = sreg[11];
            sreg[11] = sreg[10];
            sreg[10] = sreg[9];
            sreg[9]  = sreg[8];
            sreg[8]  = sreg[7];
            sreg[7]  = sreg[6];
            sreg[6]  = sreg[5] ^ carry;
            sreg[5]  = sreg[4];
            sreg[4]  = sreg[3] ^ carry;
            sreg[3]  = sreg[2];
            sreg[2]  = sreg[1];
            sreg[1]  = sreg[0] ^ carry;
            sreg[0]  = carry;
        }
        int tribit = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
        offset = (offset + 1) % 160;
        return tribit;
    }
    
private:
    int sreg[12];
    int offset;
};

int main() {
    RefDataScrambler scr;
    
    cout << "Reference data scrambler sequence (first 80):" << endl;
    int seq[160];
    scr.generate_sequence(seq);
    
    for (int i = 0; i < 80; i++) {
        cout << seq[i];
        if ((i + 1) % 40 == 0) cout << endl;
    }
    
    // According to earlier analysis, position 1440 in received symbols 
    // matches seq[0:39] exactly. Let's verify the scrambler is correct.
    
    // Expected from earlier test: position 1440 = "02433645767055435437"
    cout << "\nExpected first 20: 02433645767055435437" << endl;
    cout << "Got first 20:      ";
    for (int i = 0; i < 20; i++) cout << seq[i];
    cout << endl;
    
    // Verify mgd3 mapping
    int mgd3[8] = {0,1,3,2,7,6,4,5};  // tribit -> gray position
    
    cout << "\nmgd3 mapping (tribit -> gray):" << endl;
    for (int i = 0; i < 8; i++) {
        cout << "  " << i << " -> " << mgd3[i] << endl;
    }
    
    // Inverse: gray position -> tribit
    int inv_mgd3[8];
    for (int i = 0; i < 8; i++) {
        inv_mgd3[mgd3[i]] = i;
    }
    
    cout << "\nInverse mgd3 (gray -> tribit):" << endl;
    for (int i = 0; i < 8; i++) {
        cout << "  " << i << " -> " << inv_mgd3[i] << endl;
    }
    
    return 0;
}
