// Copyright (C) 2025 Phoenix Nest LLC
// Phoenix Nest Modem - MIL-STD-188-110A HF Data Modem
// Licensed under Phoenix Nest EULA - see phoenixnestmodem_eula.md
/**
 * @file sample_source.h
 * @brief Abstract sample source interface for demodulator input
 * 
 * Provides a unified interface for different sample sources:
 * - Audio input (48kHz real samples → Hilbert → complex baseband)
 * - I/Q input (SDR complex samples → decimate → complex baseband)
 * 
 * All implementations deliver std::complex<float> samples at 48kHz.
 * The demodulator doesn't know or care about the upstream source.
 */

#ifndef M110A_API_SAMPLE_SOURCE_H
#define M110A_API_SAMPLE_SOURCE_H

#include <complex>
#include <cstddef>
#include <cstdint>

namespace m110a {

/**
 * Abstract sample source for demodulator input.
 * 
 * All implementations deliver complex float samples at 48kHz.
 * The demodulator doesn't know or care about the upstream source.
 */
class SampleSource {
public:
    virtual ~SampleSource() = default;
    
    /**
     * Read complex baseband samples.
     * 
     * @param out     Buffer to receive samples
     * @param count   Maximum samples to read
     * @return        Actual samples read (0 = EOF or no data available)
     */
    virtual size_t read(std::complex<float>* out, size_t count) = 0;
    
    /**
     * Get output sample rate (always 48000 for this modem).
     */
    virtual double sample_rate() const { return 48000.0; }
    
    /**
     * Check if source has more data available.
     */
    virtual bool has_data() const = 0;
    
    /**
     * Get source type for logging/debugging.
     */
    virtual const char* source_type() const = 0;
    
    /**
     * Reset the source state.
     * Clears any internal buffers and resets to initial state.
     */
    virtual void reset() = 0;
};

} // namespace m110a

#endif // M110A_API_SAMPLE_SOURCE_H
