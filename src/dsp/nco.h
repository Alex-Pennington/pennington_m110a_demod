#ifndef M110A_NCO_H
#define M110A_NCO_H

#include "../common/types.h"
#include "../common/constants.h"
#include <cmath>

namespace m110a {

/**
 * Numerically Controlled Oscillator
 * 
 * Generates complex sinusoid for:
 * - Carrier generation (TX upconversion)
 * - Carrier removal (RX downconversion)
 * - Carrier recovery (phase/frequency tracking)
 * 
 * Uses direct computation (cos/sin). For higher performance,
 * could use lookup table or CORDIC.
 */
class NCO {
public:
    /**
     * Create NCO
     * @param sample_rate System sample rate (Hz)
     * @param frequency Initial frequency (Hz)
     */
    NCO(float sample_rate, float frequency = 0.0f);
    
    /**
     * Get current complex value without advancing
     */
    complex_t value() const;
    
    /**
     * Get current value and advance phase by one sample
     */
    complex_t next();
    
    /**
     * Advance phase without returning value
     */
    void step();
    
    /**
     * Step N times
     */
    void step(int n);
    
    /**
     * Mix (multiply) input sample with NCO output
     * Advances NCO by one sample
     * Note: Uses e^(+jωt), suitable for upconversion or complex mixing
     */
    complex_t mix(complex_t input);
    
    /**
     * Mix real sample with NCO, producing complex baseband
     * Uses e^(+jωt) - advances NCO by one sample
     * Note: For proper downconversion, use mix_down() instead
     */
    complex_t mix(sample_t input);
    
    /**
     * Downconvert real sample to complex baseband
     * Uses e^(-jωt) (conjugate) - advances NCO by one sample
     * This is the correct function for receiver mixing
     */
    complex_t mix_down(sample_t input);
    
    // Frequency control
    void set_frequency(float freq_hz);
    void adjust_frequency(float delta_hz);
    float frequency() const { return frequency_; }
    
    // Phase control
    void set_phase(float phase_rad);
    void adjust_phase(float delta_rad);
    float phase() const { return phase_; }
    
    // Reset to initial state
    void reset();
    
private:
    float sample_rate_;
    float frequency_;
    float phase_;
    float phase_increment_;
    
    void update_increment();
    void wrap_phase();
};

// ============================================================================
// Implementation
// ============================================================================

inline NCO::NCO(float sample_rate, float frequency)
    : sample_rate_(sample_rate)
    , frequency_(frequency)
    , phase_(0.0f) {
    update_increment();
}

inline void NCO::update_increment() {
    phase_increment_ = 2.0f * PI * frequency_ / sample_rate_;
}

inline void NCO::wrap_phase() {
    // Keep phase in [-π, π) for numerical stability
    while (phase_ >= PI) phase_ -= 2.0f * PI;
    while (phase_ < -PI) phase_ += 2.0f * PI;
}

inline complex_t NCO::value() const {
    return complex_t(std::cos(phase_), std::sin(phase_));
}

inline complex_t NCO::next() {
    complex_t v = value();
    step();
    return v;
}

inline void NCO::step() {
    phase_ += phase_increment_;
    wrap_phase();
}

inline void NCO::step(int n) {
    phase_ += phase_increment_ * n;
    wrap_phase();
}

inline complex_t NCO::mix(complex_t input) {
    complex_t v = value();
    step();
    return input * v;
}

inline complex_t NCO::mix(sample_t input) {
    complex_t v = value();
    step();
    return complex_t(input * v.real(), input * v.imag());
}

inline complex_t NCO::mix_down(sample_t input) {
    complex_t v = value();
    step();
    // Use conjugate: e^(-jωt) = cos(ωt) - j*sin(ωt)
    return complex_t(input * v.real(), -input * v.imag());
}

inline void NCO::set_frequency(float freq_hz) {
    frequency_ = freq_hz;
    update_increment();
}

inline void NCO::adjust_frequency(float delta_hz) {
    frequency_ += delta_hz;
    update_increment();
}

inline void NCO::set_phase(float phase_rad) {
    phase_ = phase_rad;
    wrap_phase();
}

inline void NCO::adjust_phase(float delta_rad) {
    phase_ += delta_rad;
    wrap_phase();
}

inline void NCO::reset() {
    phase_ = 0.0f;
}

} // namespace m110a

#endif // M110A_NCO_H
