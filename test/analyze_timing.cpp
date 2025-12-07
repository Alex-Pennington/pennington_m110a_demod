/**
 * Analyze timing of the PCM file to determine actual symbol rate
 */
#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

int main() {
    string filename = "/home/claude/tx_2400S_20251206_202547_345.pcm";
    
    ifstream file(filename, ios::binary);
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    size_t num_samples = size / 2;
    
    float sample_rate = 48000.0f;
    float duration = num_samples / sample_rate;
    
    cout << "File analysis:" << endl;
    cout << "  Samples: " << num_samples << endl;
    cout << "  Duration: " << duration << " seconds" << endl;
    cout << "  Sample rate: " << sample_rate << " Hz" << endl;
    
    // According to metadata: 54 bytes of data
    // FEC rate 1/2: 54*8*2 = 864 coded bits
    // With tail: ~876 coded bits
    // Interleave block: 40*72 = 2880 bits (for M2400S)
    // That's about 1 block + padding
    
    // Preamble: 3 frames * 480 = 1440 symbols (SHORT interleave)
    // Data: depends on structure
    
    // If 2400 baud (sps=20), preamble = 1440*20 = 28800 samples = 0.6 sec
    // Data portion: (67200 - 28800) / 20 = 1920 symbols
    
    // If 800 baud (sps=60), preamble = 1440*60 = 86400 samples > file size!
    
    cout << "\nIf 2400 baud (sps=20):" << endl;
    cout << "  Preamble samples: " << (1440 * 20) << endl;
    cout << "  Data samples: " << (num_samples - 1440*20) << endl;
    cout << "  Data symbols: " << ((num_samples - 1440*20) / 20) << endl;
    
    cout << "\nIf 800 baud (sps=60):" << endl;
    cout << "  Preamble samples: " << (1440 * 60) << " (exceeds file!)" << endl;
    
    // Actually for M2400S, data uses a different pattern
    // 10 mini-frames of (32 unknown + 16 known) = 480 symbols per super-frame
    // That's 480 * 3 = 1440 bits per super-frame
    
    // At 2400 baud, super-frame = 480 symbols = 0.2 seconds
    // Data: 1920 symbols / 480 = 4 super-frames = 5760 coded bits
    
    cout << "\nExpected transmission:" << endl;
    cout << "  Message: 54 bytes = 432 bits" << endl;
    cout << "  Encoded (rate 1/2): ~876 bits" << endl;
    cout << "  One interleave block: 2880 bits" << endl;
    cout << "  Data symbols needed: " << (2880/3) << endl;
    
    return 0;
}
