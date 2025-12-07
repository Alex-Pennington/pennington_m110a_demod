#ifndef M110A_AWGN_H
#define M110A_AWGN_H

/**
 * AWGN Channel Model
 * 
 * Adds calibrated Additive White Gaussian Noise to signals.
 * Supports specification by SNR, Es/N0, or Eb/N0.
 */

#include "common/types.h"
#include <random>
#include <cmath>
#include <vector>

namespace m110a {

class AWGNChannel {
public:
    explicit AWGNChannel(unsigned int seed = 42) : rng_(seed) {}
    
    /**
     * Add AWGN with specified SNR (signal-to-noise ratio)
     * @param signal Input/output signal (modified in place)
     * @param snr_db SNR in dB
     */
    void add_noise_snr(std::vector<float>& signal, float snr_db) {
        float signal_power = calculate_power(signal);
        float noise_power = signal_power / std::pow(10.0f, snr_db / 10.0f);
        add_gaussian_noise(signal, noise_power);
    }
    
    /**
     * Add AWGN with specified Es/N0 (symbol energy to noise spectral density)
     * @param signal Input/output signal
     * @param es_n0_db Es/N0 in dB
     * @param sps Samples per symbol
     */
    void add_noise_es_n0(std::vector<float>& signal, float es_n0_db, float sps) {
        float signal_power = calculate_power(signal);
        float es_n0_linear = std::pow(10.0f, es_n0_db / 10.0f);
        // Noise power per sample = Signal power / (Es/N0)
        // (sps factor already in signal power due to pulse shaping)
        float noise_power = signal_power / es_n0_linear;
        add_gaussian_noise(signal, noise_power);
    }
    
    /**
     * Add AWGN with specified Eb/N0 (bit energy to noise spectral density)
     * @param signal Input/output signal
     * @param eb_n0_db Eb/N0 in dB  
     * @param bits_per_symbol Bits per symbol (3 for 8-PSK)
     * @param code_rate FEC code rate (0.5 for rate-1/2)
     * @param sps Samples per symbol
     */
    void add_noise_eb_n0(std::vector<float>& signal, float eb_n0_db,
                         float bits_per_symbol, float code_rate, float sps) {
        // Es/N0 = Eb/N0 * bits_per_symbol * code_rate
        float es_n0_db = eb_n0_db + 10.0f * std::log10(bits_per_symbol * code_rate);
        add_noise_es_n0(signal, es_n0_db, sps);
    }
    
    /**
     * Calculate actual SNR of a noisy signal (requires clean reference)
     */
    static float measure_snr(const std::vector<float>& clean,
                            const std::vector<float>& noisy) {
        if (clean.size() != noisy.size()) return -100.0f;
        
        float signal_power = 0.0f;
        float noise_power = 0.0f;
        
        for (size_t i = 0; i < clean.size(); i++) {
            signal_power += clean[i] * clean[i];
            float noise = noisy[i] - clean[i];
            noise_power += noise * noise;
        }
        
        signal_power /= clean.size();
        noise_power /= clean.size();
        
        if (noise_power < 1e-20f) return 100.0f;
        return 10.0f * std::log10(signal_power / noise_power);
    }
    
    /**
     * Reseed the random number generator
     */
    void seed(unsigned int s) { rng_.seed(s); }

private:
    std::mt19937 rng_;
    
    float calculate_power(const std::vector<float>& signal) {
        float power = 0.0f;
        for (float s : signal) power += s * s;
        return power / signal.size();
    }
    
    void add_gaussian_noise(std::vector<float>& signal, float noise_power) {
        float noise_std = std::sqrt(noise_power);
        std::normal_distribution<float> dist(0.0f, noise_std);
        for (float& s : signal) s += dist(rng_);
    }
};

} // namespace m110a

#endif // M110A_AWGN_H
