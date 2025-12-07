/**
 * Check what scrambler values are used for probe symbols
 */
#include <iostream>
#include <vector>
using namespace std;

class RefScrambler {
public:
    RefScrambler() { reset(); }
    void reset() {
        sreg[0]=1; sreg[1]=0; sreg[2]=1; sreg[3]=1;
        sreg[4]=0; sreg[5]=1; sreg[6]=0; sreg[7]=1;
        sreg[8]=1; sreg[9]=1; sreg[10]=0; sreg[11]=1;
    }
    int next() {
        for (int j = 0; j < 8; j++) {
            int c = sreg[11];
            for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
            sreg[0] = c;
            sreg[6] ^= c; sreg[4] ^= c; sreg[1] ^= c;
        }
        return (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
    }
private:
    int sreg[12];
};

int main() {
    RefScrambler scr;
    
    cout << "Full scrambler sequence (first 160 values):" << endl;
    for (int i = 0; i < 160; i++) {
        cout << scr.next();
        if ((i + 1) % 48 == 0) cout << " | ";
        else if ((i + 1) % 16 == 0 && i < 47) cout << " ";
    }
    cout << endl;
    
    // Reset and trace through frame structure
    scr.reset();
    
    cout << "\nFrame structure trace:" << endl;
    cout << "Frame 0 (positions 0-47):" << endl;
    cout << "  Data (0-31): ";
    for (int i = 0; i < 32; i++) cout << scr.next();
    cout << endl;
    cout << "  Probe (32-47): ";
    for (int i = 0; i < 16; i++) cout << scr.next();
    cout << endl;
    
    cout << "Frame 1 (positions 48-95):" << endl;
    cout << "  Data (48-79): ";
    for (int i = 0; i < 32; i++) cout << scr.next();
    cout << endl;
    cout << "  Probe (80-95): ";
    for (int i = 0; i < 16; i++) cout << scr.next();
    cout << endl;
    
    return 0;
}
