/**
 * @file dsp.c
 * @brief DSP utilities implementation
 */

#include "m110a/dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* FIR Filter Implementation */

fir_filter_t* fir_create(const float* coeffs, size_t num_taps)
{
    if (!coeffs || num_taps == 0) {
        return NULL;
    }
    
    fir_filter_t* filter = (fir_filter_t*)malloc(sizeof(fir_filter_t));
    if (!filter) {
        return NULL;
    }
    
    filter->coeffs = (float*)malloc(num_taps * sizeof(float));
    filter->delay_line = (float*)calloc(num_taps, sizeof(float));
    
    if (!filter->coeffs || !filter->delay_line) {
        fir_free(filter);
        return NULL;
    }
    
    memcpy(filter->coeffs, coeffs, num_taps * sizeof(float));
    filter->num_taps = num_taps;
    filter->delay_index = 0;
    
    return filter;
}

void fir_free(fir_filter_t* filter)
{
    if (!filter) {
        return;
    }
    
    if (filter->coeffs) {
        free(filter->coeffs);
    }
    if (filter->delay_line) {
        free(filter->delay_line);
    }
    free(filter);
}

float fir_process_sample(fir_filter_t* filter, float sample)
{
    if (!filter) {
        return 0.0f;
    }
    
    /* Insert new sample into delay line */
    filter->delay_line[filter->delay_index] = sample;
    
    /* Compute output */
    float output = 0.0f;
    size_t idx = filter->delay_index;
    
    for (size_t i = 0; i < filter->num_taps; i++) {
        output += filter->coeffs[i] * filter->delay_line[idx];
        if (idx == 0) {
            idx = filter->num_taps - 1;
        } else {
            idx--;
        }
    }
    
    /* Update delay index */
    filter->delay_index++;
    if (filter->delay_index >= filter->num_taps) {
        filter->delay_index = 0;
    }
    
    return output;
}

void fir_process_block(fir_filter_t* filter, const float* input, float* output, size_t len)
{
    if (!filter || !input || !output) {
        return;
    }
    
    for (size_t i = 0; i < len; i++) {
        output[i] = fir_process_sample(filter, input[i]);
    }
}

/* NCO Implementation */

void nco_init(nco_t* nco, float frequency, float sample_rate)
{
    if (!nco) {
        return;
    }
    
    nco->phase = 0.0f;
    nco->phase_inc = 2.0f * M_PI * frequency / sample_rate;
}

complex_f32_t nco_step(nco_t* nco)
{
    complex_f32_t output = {0.0f, 0.0f};
    
    if (!nco) {
        return output;
    }
    
    output.re = cosf(nco->phase);
    output.im = sinf(nco->phase);
    
    nco->phase += nco->phase_inc;
    normalize_phase(&nco->phase);
    
    return output;
}

void nco_mix(nco_t* nco, const complex_f32_t* input, complex_f32_t* output, size_t len)
{
    if (!nco || !input || !output) {
        return;
    }
    
    for (size_t i = 0; i < len; i++) {
        complex_f32_t lo = nco_step(nco);
        
        /* Complex multiply: (a + jb) * (c + jd) = (ac - bd) + j(ad + bc) */
        output[i].re = input[i].re * lo.re - input[i].im * lo.im;
        output[i].im = input[i].re * lo.im + input[i].im * lo.re;
    }
}

/* Utility Functions */

float compute_magnitude(complex_f32_t sample)
{
    return sqrtf(sample.re * sample.re + sample.im * sample.im);
}

float compute_phase(complex_f32_t sample)
{
    return atan2f(sample.im, sample.re);
}

void normalize_phase(float* phase)
{
    if (!phase) {
        return;
    }
    
    while (*phase >= M_PI) {
        *phase -= 2.0f * M_PI;
    }
    while (*phase < -M_PI) {
        *phase += 2.0f * M_PI;
    }
}
