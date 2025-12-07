/**
 * Verify actual scrambler period
 */
#include <iostream>
#include <vector>
using namespace std;

class MyScrambler {
public:
    MyScrambler() { reset(); }
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
    
    // Get current state as 12-bit value
    int get_state() const {
        int state = 0;
        for (int i = 0; i < 12; i++) state |= (sreg[i] << i);
        return state;
    }
    
private:
    int sreg[12];
};

int main() {
    MyScrambler scr;
    
    // Generate first 500 symbols
    vector<int> seq;
    vector<int> states;
    
    int initial_state = scr.get_state();
    cout << "Initial state: 0x" << hex << initial_state << dec << endl;
    
    for (int i = 0; i < 500; i++) {
        seq.push_back(scr.next());
        states.push_back(scr.get_state());
    }
    
    // Check if sequence repeats at various points
    cout << "\nChecking for period at various lengths:" << endl;
    
    for (int period : {40, 48, 80, 96, 120, 160, 192, 240, 320, 480}) {
        bool repeats = true;
        for (int i = 0; i < 160 && i + period < 500; i++) {
            if (seq[i] != seq[i + period]) {
                repeats = false;
                break;
            }
        }
        cout << "Period " << period << ": " << (repeats ? "YES" : "NO") << endl;
    }
    
    // Check state periodicity (when does state return to initial?)
    cout << "\nLooking for state repeat..." << endl;
    for (int i = 0; i < (int)states.size(); i++) {
        if (states[i] == initial_state) {
            cout << "State repeats at position " << (i + 1) << endl;
            break;
        }
    }
    
    // Show scrambler output around position 160
    cout << "\nScrambler output around position 160:" << endl;
    cout << "Pos 155-165: ";
    for (int i = 155; i < 165 && i < (int)seq.size(); i++) {
        cout << seq[i];
    }
    cout << endl;
    
    // Reset and compare position 0 vs 160
    scr.reset();
    vector<int> first160, next160;
    for (int i = 0; i < 160; i++) first160.push_back(scr.next());
    for (int i = 0; i < 160; i++) next160.push_back(scr.next());
    
    int match = 0;
    for (int i = 0; i < 160; i++) {
        if (first160[i] == next160[i]) match++;
    }
    cout << "\nFirst 160 vs Next 160: " << match << "/160 matches" << endl;
    
    cout << "\nFirst 20:  ";
    for (int i = 0; i < 20; i++) cout << first160[i];
    cout << endl;
    cout << "Next 20:   ";
    for (int i = 0; i < 20; i++) cout << next160[i];
    cout << endl;
    
    return 0;
}
