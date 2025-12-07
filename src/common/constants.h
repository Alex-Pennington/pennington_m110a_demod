#ifndef M110A_CONSTANTS_H
#define M110A_CONSTANTS_H

#include <cmath>

namespace m110a {

// Symbol rate is fixed per MIL-STD-188-110A
constexpr float SYMBOL_RATE = 2400.0f;
constexpr float CARRIER_FREQ = 1800.0f;

// Sample rates that give INTEGER samples-per-symbol (critical for timing!)
// SPS = sample_rate / symbol_rate must be an integer to avoid timing drift
constexpr float SAMPLE_RATE_48K = 48000.0f;  // SPS = 20 (hardware native)
constexpr float SAMPLE_RATE_9600 = 9600.0f;  // SPS = 4  (efficient processing)

// Default sample rate - use 48000 for hardware compatibility
constexpr float SAMPLE_RATE = SAMPLE_RATE_48K;
constexpr float SAMPLES_PER_SYMBOL = SAMPLE_RATE / SYMBOL_RATE;  // 20

// Legacy 8000 Hz rate (fractional SPS - NOT RECOMMENDED)
constexpr float SAMPLE_RATE_8K = 8000.0f;    // SPS = 3.333... (timing drift!)

// Helper to get integer SPS for a given sample rate
constexpr int get_sps(float sample_rate) {
    return static_cast<int>(sample_rate / SYMBOL_RATE);
}

// Check if a sample rate gives integer SPS
constexpr bool is_integer_sps(float sample_rate) {
    float sps = sample_rate / SYMBOL_RATE;
    return sps == static_cast<int>(sps);
}

// Filter parameters - scale span with SPS for consistent bandwidth
constexpr float SRRC_ALPHA = 0.35f;
constexpr int SRRC_SPAN_SYMBOLS = 6;

// Frame structure
constexpr int DATA_SYMBOLS_PER_FRAME = 32;
constexpr int PROBE_SYMBOLS_PER_FRAME = 16;
constexpr int FRAME_SYMBOLS = DATA_SYMBOLS_PER_FRAME + PROBE_SYMBOLS_PER_FRAME;  // 48

// Preamble durations
constexpr float PREAMBLE_DURATION_SHORT = 0.6f;   // seconds (ZERO/SHORT interleave)
constexpr float PREAMBLE_DURATION_LONG = 4.8f;    // seconds (LONG interleave)
constexpr int PREAMBLE_SEGMENT_SYMBOLS = 480;     // 0.2 seconds worth
constexpr int PREAMBLE_SYMBOLS_SHORT = 1440;      // 3 segments
constexpr int PREAMBLE_SYMBOLS_LONG = 11520;      // 24 segments

// Scrambler: polynomial 1 + x^-6 + x^-7
// Implemented as shift register with taps at positions 6 and 7
constexpr uint8_t SCRAMBLER_INIT_PREAMBLE = 0b1111111;  // All ones for preamble
constexpr uint8_t SCRAMBLER_INIT_DATA = 0b1111111;      // Reset for data phase

// 8-PSK phase increments (differential encoding)
// tribit value -> phase increment in radians
constexpr float PI = 3.14159265358979323846f;
constexpr float PSK8_PHASE_INCREMENT[8] = {
    0.0f,               // 000 -> 0°
    PI / 4.0f,          // 001 -> 45°
    PI / 2.0f,          // 010 -> 90°
    3.0f * PI / 4.0f,   // 011 -> 135°
    PI,                 // 100 -> 180°
    5.0f * PI / 4.0f,   // 101 -> 225°
    3.0f * PI / 2.0f,   // 110 -> 270°
    7.0f * PI / 4.0f    // 111 -> 315°
};

// Equalizer defaults
constexpr int DFE_FF_TAPS = 20;
constexpr int DFE_FB_TAPS = 20;
constexpr float RLS_LAMBDA = 0.995f;
constexpr float RLS_DELTA = 0.01f;

// Viterbi decoder (K=7, rate 1/2)
constexpr int VITERBI_K = 7;
constexpr int VITERBI_STATES = 64;  // 2^(K-1)
constexpr uint8_t VITERBI_G1 = 0x5B;  // Octal 133 = 1011011 - taps 0,1,3,4,6 (bit1)
constexpr uint8_t VITERBI_G2 = 0x79;  // Octal 171 = 1111001 - taps 0,3,4,5,6 (bit2)

// Interleave modes
enum class InterleaveMode {
    ZERO,    // No interleaving
    SHORT,   // 0.6s block
    LONG     // 4.8s block
};

// Data rates (bps)
constexpr int DATA_RATES[] = {75, 150, 300, 600, 1200, 2400, 4800};
constexpr int NUM_DATA_RATES = 7;

} // namespace m110a

#endif // M110A_CONSTANTS_H
