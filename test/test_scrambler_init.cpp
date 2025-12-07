/**
 * Test scrambler initialization for data
 */
#include <iostream>
#include <vector>
#include "modem/scrambler.h"

using namespace m110a;
using namespace std;

int main() {
    cout << "=== Scrambler Initialization Test ===" << endl;
    
    // Preamble is 1440 symbols for short interleave
    int preamble_symbols = 1440;
    
    cout << "\n--- Fresh scrambler (seed 0xBAD) ---" << endl;
    RefScrambler scr1;
    cout << "First 20: ";
    for (int i = 0; i < 20; i++) {
        cout << (int)scr1.next_tribit();
    }
    cout << endl;
    
    cout << "\n--- Scrambler after " << preamble_symbols << " advances ---" << endl;
    RefScrambler scr2;
    for (int i = 0; i < preamble_symbols; i++) {
        scr2.next_tribit();
    }
    cout << "Next 20: ";
    for (int i = 0; i < 20; i++) {
        cout << (int)scr2.next_tribit();
    }
    cout << endl;
    
    // Also check if scrambler resets for data
    cout << "\n--- After advancing preamble ---" << endl;
    
    return 0;
}
