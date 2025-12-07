// src/common/types.h
#ifndef M110A_TYPES_H
#define M110A_TYPES_H

#include <complex>
#include <cstdint>
#include <vector>

namespace m110a {

// Sample types
using sample_t = float;
using complex_t = std::complex<float>;

// PCM types
using pcm_sample_t = int16_t;

// Soft bits for Viterbi (-127 to +127 typical)
using soft_bit_t = int8_t;

// Convert PCM to normalized float
inline sample_t pcm_to_float(pcm_sample_t s) {
    return static_cast<sample_t>(s) / 32768.0f;
}

inline pcm_sample_t float_to_pcm(sample_t s) {
    s = std::max(-1.0f, std::min(1.0f, s));
    return static_cast<pcm_sample_t>(s * 32767.0f);
}

} // namespace m110a

#endif
