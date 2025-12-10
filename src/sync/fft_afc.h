/**
 * FFT-based Coarse AFC for MIL-STD-188-110A
 * 
 * Implements two-stage AFC:
 * 1. Coarse: Delay-multiply frequency estimation (±10 Hz range, ~1-2 Hz accuracy)
 * 2. Fine: Preamble correlation search (±2 Hz around coarse estimate)
 * 
 * Theory (Delay-Multiply Method):
 * - For PSK signal: y[n] = A * exp(j*(2πfΔt*n + φ))
 * - Multiply signal by delayed conjugate: y[n] * conj(y[n-D])
 * - Result: exp(j*2πfΔt*D) - phase rotation proportional to frequency
 * - Frequency estimate: f = angle(sum) / (2π*D*Δt)
 * - Works for PSK because constellation rotation doesn't affect phase slope
 */

#ifndef M110A_FFT_AFC_H
#define M110A_FFT_AFC_H

#include "common/types.h"
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>

namespace m110a {

/**
 * Delay-multiply frequency estimator for PSK signals
 * More robust than FFT-correlation for unknown start position
 */
class CoarseAFC {
public:
    struct Config {
        float sample_rate;         // Input sample rate
        float baud_rate;           // Symbol rate
        float search_range_hz;     // ±12 Hz search range (for validation)
        int delay_samples;         // Delay for multiply (in symbols)
        int integration_symbols;   // Symbols to integrate over
        float min_power_db;        // Minimum signal power (dB)
        
        Config() 
            : sample_rate(48000.0f)
            , baud_rate(2400.0f)
            , search_range_hz(12.0f)
            , delay_samples(10)          // 10 symbols = 4.17 ms @ 2400 baud
            , integration_symbols(200)   // 200 symbols = 83 ms integration
            , min_power_db(-20.0f)
        {}
    };
    
    CoarseAFC(const Config& cfg) : config_(cfg) {}
    
    /**
     * Estimate frequency offset using delay-multiply method
     * 
     * @param samples Complex baseband samples (after RRC filtering, at symbol rate or higher)
     * @param start_idx Starting sample index
     * @return Estimated frequency offset in Hz (0.0 if failed)
     */
    float estimate_frequency_offset(
        const std::vector<std::complex<float>>& samples,
        size_t start_idx = 0)
    {
        const int sps = static_cast<int>(config_.sample_rate / config_.baud_rate + 0.5f);
        const int delay = config_.delay_samples * sps;
        const int needed_samples = (config_.integration_symbols + config_.delay_samples) * sps;
        
        if (samples.size() < start_idx + needed_samples) {
            return 0.0f;  // Not enough samples
        }
        
        // Delay-multiply: y[n] * conj(y[n-delay])
        std::complex<float> accum(0.0f, 0.0f);
        float power = 0.0f;
        int count = 0;
        
        for (int i = 0; i < config_.integration_symbols; ++i) {
            size_t idx = start_idx + i * sps;
            size_t idx_delay = idx + delay;
            
            if (idx_delay >= samples.size()) break;
            
            std::complex<float> product = samples[idx_delay] * std::conj(samples[idx]);
            accum += product;
            power += std::norm(samples[idx]);
            count++;
        }
        
        if (count < config_.integration_symbols / 2) {
            return 0.0f;  // Not enough valid samples
        }
        
        // Check minimum power
        float avg_power = power / count;
        float power_db = 10.0f * std::log10(avg_power + 1e-10f);
        if (power_db < config_.min_power_db) {
            return 0.0f;  // Signal too weak
        }
        
        // Extract frequency from phase
        // freq = phase / (2π * delay * Δt)
        // where Δt = 1/sample_rate, delay in samples
        float phase = std::arg(accum);
        float delta_t = 1.0f / config_.sample_rate;
        float freq_offset = phase / (2.0f * M_PI * delay * delta_t);
        
        // Clamp to search range
        if (std::abs(freq_offset) > config_.search_range_hz) {
            // Phase wrapping issue - try unwrapping
            // If frequency is actually > search_range, phase wraps around
            // Adjust by ±2π and recompute
            if (freq_offset > config_.search_range_hz) {
                phase -= 2.0f * M_PI;
            } else {
                phase += 2.0f * M_PI;
            }
            freq_offset = phase / (2.0f * M_PI * delay * delta_t);
            
            // Still out of range?
            if (std::abs(freq_offset) > config_.search_range_hz) {
                return 0.0f;  // Probably noise
            }
        }
        
        return freq_offset;
    }
    
    /**
     * Overload for preamble compatibility (ignores preamble_symbols)
     */
    float estimate_frequency_offset(
        const std::vector<std::complex<float>>& samples,
        const std::vector<std::complex<float>>& preamble_symbols,
        size_t start_idx = 0)
    {
        (void)preamble_symbols;  // Unused - delay-multiply doesn't need known pattern
        return estimate_frequency_offset(samples, start_idx);
    }
    
private:
    Config config_;
};

} // namespace m110a

#endif // M110A_FFT_AFC_H
