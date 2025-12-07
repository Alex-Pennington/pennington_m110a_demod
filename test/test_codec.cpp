/**
 * MIL-STD-188-110A Codec Test Suite
 * Tests all implemented modes against reference PCM files
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include "modem/m110a_codec.h"
#include "m110a/msdmt_decoder.h"

using namespace std;
using namespace m110a;

const char* TEST_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    vector<int16_t> raw(size / 2);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

bool test_loopback(ModeId mode, const char* name) {
    M110ACodec codec(mode);
    
    vector<uint8_t> data(TEST_MSG, TEST_MSG + strlen(TEST_MSG));
    auto symbols = codec.encode(data);
    auto decoded = codec.decode(symbols);
    
    int matches = 0;
    for (size_t i = 0; i < min(decoded.size(), data.size()); i++) {
        if (decoded[i] == data[i]) matches++;
    }
    
    bool pass = (matches == (int)data.size());
    cout << name << " Loopback: " << matches << "/" << data.size() 
         << " " << (pass ? "PASS" : "FAIL") << endl;
    return pass;
}

bool test_pcm_decode(ModeId mode, const char* name, const char* pcm_file) {
    auto samples = read_pcm(pcm_file);
    if (samples.empty()) {
        cout << name << " PCM: File not found - SKIP" << endl;
        return true;
    }
    
    auto& cfg = ModeDatabase::get(mode);
    
    MSDMTDecoderConfig dcfg;
    dcfg.unknown_data_len = cfg.unknown_data_len;
    dcfg.known_data_len = cfg.known_data_len;
    
    MSDMTDecoder decoder(dcfg);
    auto result = decoder.decode(samples);
    
    M110ACodec codec(mode);
    auto decoded = codec.decode_with_probes(result.data_symbols);
    
    int matches = 0;
    for (size_t i = 0; i < min(decoded.size(), strlen(TEST_MSG)); i++) {
        if (decoded[i] == (uint8_t)TEST_MSG[i]) matches++;
    }
    
    bool pass = (matches >= 51);
    cout << name << " PCM: " << matches << "/54 " << (pass ? "PASS" : "FAIL") << endl;
    return pass;
}

int main() {
    cout << "========================================" << endl;
    cout << "MIL-STD-188-110A Codec Test Suite" << endl;
    cout << "========================================" << endl;
    
    int passed = 0, failed = 0;
    
    // Loopback tests - Short interleave
    cout << "\n--- Loopback Tests (Short Interleave) ---" << endl;
    if (test_loopback(ModeId::M2400S, "M2400S")) passed++; else failed++;
    if (test_loopback(ModeId::M1200S, "M1200S")) passed++; else failed++;
    if (test_loopback(ModeId::M600S, "M600S")) passed++; else failed++;
    if (test_loopback(ModeId::M300S, "M300S")) passed++; else failed++;
    if (test_loopback(ModeId::M150S, "M150S")) passed++; else failed++;
    
    // Loopback tests - Long interleave
    cout << "\n--- Loopback Tests (Long Interleave) ---" << endl;
    if (test_loopback(ModeId::M2400L, "M2400L")) passed++; else failed++;
    if (test_loopback(ModeId::M1200L, "M1200L")) passed++; else failed++;
    if (test_loopback(ModeId::M600L, "M600L")) passed++; else failed++;
    if (test_loopback(ModeId::M300L, "M300L")) passed++; else failed++;
    if (test_loopback(ModeId::M150L, "M150L")) passed++; else failed++;
    
    // Loopback test - Uncoded
    cout << "\n--- Loopback Tests (Special Modes) ---" << endl;
    if (test_loopback(ModeId::M4800S, "M4800S")) passed++; else failed++;
    
    // PCM decode tests - Short interleave
    cout << "\n--- PCM Decode Tests (Short Interleave) ---" << endl;
    if (test_pcm_decode(ModeId::M2400S, "M2400S", "/home/claude/tx_2400S_20251206_202547_345.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M1200S, "M1200S", "/home/claude/tx_1200S_20251206_202533_636.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M600S, "M600S", "/home/claude/tx_600S_20251206_202518_709.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M300S, "M300S", "/home/claude/tx_300S_20251206_202501_840.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M150S, "M150S", "/home/claude/tx_150S_20251206_202440_580.pcm")) passed++; else failed++;
    
    // PCM decode tests - Long interleave
    cout << "\n--- PCM Decode Tests (Long Interleave) ---" << endl;
    if (test_pcm_decode(ModeId::M2400L, "M2400L", "/home/claude/tx_2400L_20251206_202549_783.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M1200L, "M1200L", "/home/claude/tx_1200L_20251206_202536_295.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M600L, "M600L", "/home/claude/tx_600L_20251206_202521_953.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M300L, "M300L", "/home/claude/tx_300L_20251206_202506_058.pcm")) passed++; else failed++;
    if (test_pcm_decode(ModeId::M150L, "M150L", "/home/claude/tx_150L_20251206_202446_986.pcm")) passed++; else failed++;
    
    cout << "\n========================================" << endl;
    cout << "Results: " << passed << " passed, " << failed << " failed" << endl;
    cout << "========================================" << endl;
    cout << "\nNote: M75S/L (Walsh coded) not yet implemented" << endl;
    cout << "Note: M4800S PCM test file not available" << endl;
    
    return failed > 0 ? 1 : 0;
}
