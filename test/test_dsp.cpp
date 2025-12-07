// test/test_dsp.cpp
#include "../src/dsp/nco.h"
#include "../src/dsp/fir_filter.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <numeric>

using namespace m110a;

// Helper to compare floats
bool approx_equal(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) < tol;
}

bool approx_equal(complex_t a, complex_t b, float tol = 1e-4f) {
    return approx_equal(a.real(), b.real(), tol) && 
           approx_equal(a.imag(), b.imag(), tol);
}

// ============================================================================
// NCO Tests
// ============================================================================

void test_nco_frequency() {
    std::cout << "  Testing NCO frequency generation... ";
    
    // 1000 Hz at 8000 Hz sample rate = 8 samples per cycle
    NCO nco(8000.0f, 1000.0f);
    
    // After 8 samples, should be back to start (2π phase)
    complex_t start = nco.value();
    nco.step(8);
    complex_t end = nco.value();
    
    assert(approx_equal(start, end, 1e-3f));
    std::cout << "PASS\n";
}

void test_nco_carrier_frequency() {
    std::cout << "  Testing NCO at M110A carrier (1800 Hz)... ";
    
    NCO nco(SAMPLE_RATE, CARRIER_FREQ);
    
    // At 8000 Hz sample rate, 1800 Hz = 8000/1800 ≈ 4.44 samples/cycle
    int samples_per_symbol = static_cast<int>(SAMPLES_PER_SYMBOL + 0.5f);
    
    nco.step(samples_per_symbol);
    // Just verify it runs without issue
    (void)nco.phase();
    
    std::cout << "PASS\n";
}

void test_nco_mixing() {
    std::cout << "  Testing NCO mixing (downconversion)... ";
    
    // Create a 1800 Hz signal and mix with -1800 Hz NCO
    // Result should be DC (0 Hz)
    
    NCO carrier(SAMPLE_RATE, CARRIER_FREQ);
    NCO mixer(SAMPLE_RATE, -CARRIER_FREQ);  // Negative for downconversion
    
    for (int i = 0; i < 8; i++) {
        sample_t carrier_sample = carrier.next().real();
        complex_t mixed = mixer.mix(carrier_sample);
        (void)mixed;  // Just verify it runs
    }
    
    std::cout << "PASS\n";
}

void test_nco_phase_adjust() {
    std::cout << "  Testing NCO phase/frequency adjustment... ";
    
    NCO nco(8000.0f, 1000.0f);
    
    // Adjust phase by 90 degrees
    nco.adjust_phase(PI / 2.0f);
    assert(approx_equal(nco.phase(), static_cast<float>(PI / 2.0f), 1e-4f));
    
    // Adjust frequency
    nco.adjust_frequency(100.0f);
    assert(approx_equal(nco.frequency(), 1100.0f, 1e-4f));
    
    std::cout << "PASS\n";
}

// ============================================================================
// FIR Filter Tests
// ============================================================================

void test_fir_impulse_response() {
    std::cout << "  Testing FIR impulse response... ";
    
    // Simple 5-tap filter: [0.1, 0.2, 0.4, 0.2, 0.1]
    std::vector<float> taps = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    RealFirFilter filter(taps);
    
    // Feed impulse and verify first output equals first tap
    sample_t output = filter.process(1.0f);
    assert(approx_equal(output, 0.1f, 1e-4f));
    
    std::cout << "PASS\n";
}

void test_fir_step_response() {
    std::cout << "  Testing FIR step response... ";
    
    std::vector<float> taps = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    RealFirFilter filter(taps);
    
    // Sum of taps = 1.0, so step response should converge to 1.0
    float tap_sum = std::accumulate(taps.begin(), taps.end(), 0.0f);
    
    sample_t output = 0.0f;
    for (int i = 0; i < 10; i++) {
        output = filter.process(1.0f);
    }
    
    // After filter fills, output should equal sum of taps
    assert(approx_equal(output, tap_sum, 1e-4f));
    
    std::cout << "PASS\n";
}

void test_fir_complex() {
    std::cout << "  Testing complex FIR filter... ";
    
    std::vector<float> taps = {0.25f, 0.5f, 0.25f};
    ComplexFirFilter filter(taps);
    
    // Test with complex sinusoid
    NCO nco(8000.0f, 500.0f);
    
    for (int i = 0; i < 10; i++) {
        complex_t input = nco.next();
        complex_t output = filter.process(input);
        (void)output;  // Just verify it compiles and runs
    }
    
    std::cout << "PASS\n";
}

void test_srrc_generation() {
    std::cout << "  Testing SRRC filter generation... ";
    
    // Generate SRRC for M110A parameters
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SAMPLES_PER_SYMBOL);
    
    // Verify non-empty
    assert(taps.size() > 0);
    
    // Find peak (should be at center)
    auto max_it = std::max_element(taps.begin(), taps.end());
    size_t peak_idx = static_cast<size_t>(std::distance(taps.begin(), max_it));
    assert(peak_idx == taps.size() / 2);
    
    // Verify symmetry
    for (size_t i = 0; i < taps.size() / 2; i++) {
        assert(approx_equal(taps[i], taps[taps.size() - 1 - i], 1e-5f));
    }
    
    // Verify energy normalization
    float energy = 0.0f;
    for (auto t : taps) energy += t * t;
    assert(approx_equal(energy, 1.0f, 0.01f));
    
    std::cout << "PASS\n";
}

void test_srrc_zero_isi() {
    std::cout << "  Testing SRRC zero-ISI property... ";
    
    // Generate matched filter pair (TX SRRC convolved with RX SRRC = raised cosine)
    // At symbol sampling points, should have zero ISI
    
    auto taps = generate_srrc_taps(SRRC_ALPHA, SRRC_SPAN_SYMBOLS, SAMPLES_PER_SYMBOL);
    
    // Convolve SRRC with itself to get raised cosine
    size_t rc_len = 2 * taps.size() - 1;
    std::vector<float> rc(rc_len, 0.0f);
    
    for (size_t i = 0; i < taps.size(); i++) {
        for (size_t j = 0; j < taps.size(); j++) {
            rc[i + j] += taps[i] * taps[j];
        }
    }
    
    // Find center of raised cosine
    int center = static_cast<int>(rc_len / 2);
    int sps = static_cast<int>(SAMPLES_PER_SYMBOL + 0.5f);
    
    // At center (k=0), should be maximum
    // At other symbol times (k≠0), should be near zero
    // Note: With non-integer samples/symbol (3.333), zero-ISI is approximate
    for (int k = -3; k <= 3; k++) {
        int idx = center + k * sps;
        if (idx >= 0 && idx < static_cast<int>(rc_len)) {
            if (k == 0) {
                // Main lobe - should be large
                assert(rc[idx] > 0.5f);
            } else {
                // Side lobes at symbol times - should be small (zero ISI)
                // Relaxed tolerance for non-integer samples/symbol
                assert(std::abs(rc[idx]) < 0.15f);
            }
        }
    }
    
    std::cout << "PASS\n";
}

void test_lowpass_generation() {
    std::cout << "  Testing lowpass filter generation... ";
    
    // 1500 Hz cutoff at 8000 Hz sample rate
    float cutoff = 1500.0f / 8000.0f;  // Normalized
    auto taps = generate_lowpass_taps(cutoff, 31);
    
    // Verify DC gain is 1.0
    float dc_gain = std::accumulate(taps.begin(), taps.end(), 0.0f);
    assert(approx_equal(dc_gain, 1.0f, 0.01f));
    
    std::cout << "PASS\n";
}

int main() {
    std::cout << "[DSP Tests]\n";
    
    try {
        // NCO tests
        test_nco_frequency();
        test_nco_carrier_frequency();
        test_nco_mixing();
        test_nco_phase_adjust();
        
        // FIR tests
        test_fir_impulse_response();
        test_fir_step_response();
        test_fir_complex();
        
        // SRRC tests
        test_srrc_generation();
        test_srrc_zero_isi();
        
        // Lowpass test
        test_lowpass_generation();
        
        std::cout << "\nAll DSP tests PASSED!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
