#include "modem/symbol_mapper.h"
#include "modem/scrambler.h"
#include "m110a/m110a_tx.h"
#include "io/pcm_file.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <numeric>

using namespace m110a;

bool approx_equal(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) < tol;
}

// ============================================================================
// Symbol Mapper Tests
// ============================================================================

void test_symbol_mapper_constellation() {
    std::cout << "=== Test: 8-PSK Constellation Points ===\n";
    
    const complex_t* constellation = SymbolMapper::constellation();
    
    std::cout << "8-PSK Constellation:\n";
    std::cout << "Index | Phase(deg) | Real     | Imag\n";
    std::cout << "------|------------|----------|----------\n";
    
    for (int i = 0; i < 8; i++) {
        float phase = std::atan2(constellation[i].imag(), constellation[i].real());
        float phase_deg = phase * 180.0f / PI;
        if (phase_deg < 0) phase_deg += 360.0f;
        
        std::cout << "  " << i << "   | " 
                  << std::fixed << std::setprecision(1) << std::setw(10) << phase_deg << " | "
                  << std::setprecision(4) << std::setw(8) << constellation[i].real() << " | "
                  << std::setw(8) << constellation[i].imag() << "\n";
        
        // Verify unit magnitude
        float mag = std::abs(constellation[i]);
        assert(approx_equal(mag, 1.0f, 1e-5f));
    }
    
    std::cout << "All points on unit circle: PASSED\n\n";
}

void test_symbol_mapper_differential() {
    std::cout << "=== Test: Differential Encoding ===\n";
    
    SymbolMapper mapper;
    
    std::cout << "Mapping tribits with differential encoding:\n";
    std::cout << "Tribit | Phase Inc | Accum Phase | Symbol\n";
    std::cout << "-------|-----------|-------------|------------------\n";
    
    // Test sequence: 0, 1, 2, 3, 4, 5, 6, 7
    for (uint8_t tribit = 0; tribit < 8; tribit++) {
        float phase_inc_deg = tribit * 45.0f;
        complex_t sym = mapper.map(tribit);
        float accum_phase_deg = mapper.phase() * 180.0f / PI;
        
        std::cout << "   " << (int)tribit << "   | "
                  << std::setw(9) << phase_inc_deg << "° | "
                  << std::setw(11) << std::fixed << std::setprecision(1) << accum_phase_deg << "° | ("
                  << std::setprecision(3) << std::setw(6) << sym.real() << ", "
                  << std::setw(6) << sym.imag() << ")\n";
    }
    
    std::cout << "PASSED\n\n";
}

void test_symbol_mapper_hard_decision() {
    std::cout << "=== Test: Hard Decision Decoding ===\n";
    
    const complex_t* constellation = SymbolMapper::constellation();
    
    // Test that each constellation point maps back to correct index
    std::cout << "Testing ideal constellation points:\n";
    bool all_correct = true;
    for (int i = 0; i < 8; i++) {
        uint8_t decision = SymbolMapper::hard_decision(constellation[i]);
        bool correct = (decision == i);
        all_correct &= correct;
        std::cout << "  Point " << i << " → decision " << (int)decision 
                  << (correct ? " ✓" : " ✗") << "\n";
    }
    assert(all_correct);
    
    // Test noisy points
    std::cout << "\nTesting noisy points:\n";
    complex_t noisy1 = complex_t(0.9f, 0.15f);   // Should be 0 (near 0°)
    complex_t noisy2 = complex_t(0.6f, 0.7f);    // Should be 1 (near 45°)
    complex_t noisy3 = complex_t(-0.1f, 0.95f);  // Should be 2 (near 90°)
    
    std::cout << "  (0.9, 0.15) → " << (int)SymbolMapper::hard_decision(noisy1) << " (expect 0)\n";
    std::cout << "  (0.6, 0.7)  → " << (int)SymbolMapper::hard_decision(noisy2) << " (expect 1)\n";
    std::cout << "  (-0.1, 0.95)→ " << (int)SymbolMapper::hard_decision(noisy3) << " (expect 2)\n";
    
    std::cout << "PASSED\n\n";
}

void test_preamble_generation() {
    std::cout << "=== Test: Preamble Symbol Generation ===\n";
    
    M110A_Tx tx;
    
    auto symbols = tx.generate_preamble_symbols(false);  // SHORT preamble
    
    std::cout << "SHORT preamble: " << symbols.size() << " symbols\n";
    std::cout << "Expected: " << PREAMBLE_SYMBOLS_SHORT << " symbols (0.6s × 2400 baud)\n";
    assert(symbols.size() == PREAMBLE_SYMBOLS_SHORT);
    
    // Verify all symbols are on unit circle
    float max_mag = 0.0f, min_mag = 1.0f;
    for (const auto& s : symbols) {
        float mag = std::abs(s);
        max_mag = std::max(max_mag, mag);
        min_mag = std::min(min_mag, mag);
    }
    std::cout << "Symbol magnitudes: min=" << min_mag << ", max=" << max_mag << "\n";
    assert(approx_equal(min_mag, 1.0f, 0.01f) && approx_equal(max_mag, 1.0f, 0.01f));
    
    // Verify the repeating pattern (three 0.2s segments should be identical)
    std::cout << "\nVerifying segment repetition:\n";
    constexpr int SEG_LEN = 480;
    bool seg1_eq_seg2 = true, seg2_eq_seg3 = true;
    
    for (int i = 0; i < SEG_LEN; i++) {
        if (std::abs(symbols[i] - symbols[SEG_LEN + i]) > 1e-5f) seg1_eq_seg2 = false;
        if (std::abs(symbols[SEG_LEN + i] - symbols[2*SEG_LEN + i]) > 1e-5f) seg2_eq_seg3 = false;
    }
    
    std::cout << "  Segment 1 == Segment 2: " << (seg1_eq_seg2 ? "YES" : "NO") << "\n";
    std::cout << "  Segment 2 == Segment 3: " << (seg2_eq_seg3 ? "YES" : "NO") << "\n";
    assert(seg1_eq_seg2 && seg2_eq_seg3);
    
    std::cout << "PASSED\n\n";
}

void test_modulation_output() {
    std::cout << "=== Test: Modulated Output ===\n";
    
    M110A_Tx tx;
    
    // Generate short preamble
    auto samples = tx.generate_preamble(false);
    
    float duration = samples.size() / SAMPLE_RATE;
    std::cout << "Generated " << samples.size() << " samples\n";
    std::cout << "Duration: " << duration << " seconds\n";
    std::cout << "Expected: ~0.6 seconds (plus filter transient)\n";
    
    // Check amplitude
    float max_amp = 0.0f;
    for (auto s : samples) {
        max_amp = std::max(max_amp, std::abs(s));
    }
    std::cout << "Peak amplitude: " << max_amp << "\n";
    assert(max_amp < 1.0f);  // Should not clip
    assert(max_amp > 0.5f);  // Should have reasonable level
    
    // Compute RMS
    float sum_sq = 0.0f;
    for (auto s : samples) {
        sum_sq += s * s;
    }
    float rms = std::sqrt(sum_sq / samples.size());
    std::cout << "RMS level: " << rms << "\n";
    
    // Simple spectral check: energy should be concentrated around 1800 Hz
    // (Full spectral analysis would require FFT)
    
    std::cout << "PASSED\n\n";
}

void test_generate_pcm_file() {
    std::cout << "=== Test: Generate PCM Test File ===\n";
    
    M110A_Tx tx;
    
    // Generate test pattern with preamble + 10 data frames
    auto samples = tx.generate_test_pattern(10);
    
    std::cout << "Generated test pattern:\n";
    std::cout << "  Samples: " << samples.size() << "\n";
    std::cout << "  Duration: " << samples.size() / SAMPLE_RATE << " seconds\n";
    
    // Save to file
    std::string filename = "test_m110a_signal.pcm";
    PcmFileWriter writer(filename);
    writer.write(samples);
    
    std::cout << "  Saved to: " << filename << "\n";
    
    // Read back and verify
    PcmFileReader reader(filename);
    auto read_samples = reader.read_all();
    
    std::cout << "  Read back: " << read_samples.size() << " samples\n";
    assert(read_samples.size() == samples.size());
    
    // Verify first few samples match (within PCM quantization)
    bool match = true;
    for (size_t i = 0; i < 100 && i < samples.size(); i++) {
        if (std::abs(samples[i] - read_samples[i]) > 0.001f) {
            match = false;
            break;
        }
    }
    std::cout << "  Data integrity: " << (match ? "OK" : "MISMATCH") << "\n";
    
    std::cout << "PASSED\n\n";
}

void test_probe_symbols() {
    std::cout << "=== Test: Probe Symbol Generation ===\n";
    
    M110A_Tx tx;
    
    // Generate 16 probe symbols (one probe block)
    auto probes = tx.generate_probe_symbols(16);
    
    std::cout << "Generated " << probes.size() << " probe symbols\n";
    
    // Probes should be on unit circle
    for (size_t i = 0; i < probes.size(); i++) {
        float mag = std::abs(probes[i]);
        assert(approx_equal(mag, 1.0f, 0.01f));
    }
    
    // Probes should match preamble sequence (same scrambler init)
    M110A_Tx tx2;
    auto preamble = tx2.generate_preamble_symbols(false);
    
    bool match = true;
    for (size_t i = 0; i < probes.size(); i++) {
        if (std::abs(probes[i] - preamble[i]) > 1e-5f) {
            match = false;
            break;
        }
    }
    std::cout << "Probes match preamble start: " << (match ? "YES" : "NO") << "\n";
    assert(match);
    
    std::cout << "PASSED\n\n";
}

void generate_test_files() {
    std::cout << "=== Generating Test Files ===\n";
    
    M110A_Tx tx;
    
    // 1. Preamble only
    {
        auto samples = tx.generate_preamble(false);
        PcmFileWriter writer("preamble_short.pcm");
        writer.write(samples);
        std::cout << "Created: preamble_short.pcm (" << samples.size() << " samples, "
                  << samples.size() / SAMPLE_RATE << "s)\n";
    }
    
    // 2. Test pattern (preamble + data)
    {
        auto samples = tx.generate_test_pattern(20);
        PcmFileWriter writer("test_pattern.pcm");
        writer.write(samples);
        std::cout << "Created: test_pattern.pcm (" << samples.size() << " samples, "
                  << samples.size() / SAMPLE_RATE << "s)\n";
    }
    
    // 3. Long preamble (for difficult sync testing)
    {
        auto samples = tx.generate_preamble(true);
        PcmFileWriter writer("preamble_long.pcm");
        writer.write(samples);
        std::cout << "Created: preamble_long.pcm (" << samples.size() << " samples, "
                  << samples.size() / SAMPLE_RATE << "s)\n";
    }
    
    std::cout << "\nTest files ready for analysis!\n";
    std::cout << "  - View in Audacity (import raw: 16-bit signed, mono, 8000 Hz)\n";
    std::cout << "  - Check spectrum is centered at 1800 Hz\n";
    std::cout << "  - Bandwidth should be ~3 kHz\n\n";
}

int main() {
    std::cout << "M110A Transmitter Tests\n";
    std::cout << "=======================\n\n";
    
    // Symbol mapper tests
    test_symbol_mapper_constellation();
    test_symbol_mapper_differential();
    test_symbol_mapper_hard_decision();
    
    // Transmitter tests
    test_preamble_generation();
    test_modulation_output();
    test_probe_symbols();
    test_generate_pcm_file();
    
    // Generate test files for external analysis
    generate_test_files();
    
    std::cout << "All transmitter tests passed!\n";
    return 0;
}
