/**
 * @file demodulator.c
 * @brief M110A Demodulator core implementation
 */

#include "m110a/demodulator.h"
#include "m110a/dsp.h"
#include "m110a/symbol_sync.h"
#include "m110a/frame_sync.h"
#include <stdlib.h>
#include <string.h>

/* Internal demodulator context structure */
struct m110a_demod_ctx {
    m110a_config_t config;
    m110a_status_t status;
    
    /* DSP components */
    nco_t nco;
    fir_filter_t* matched_filter_i;
    fir_filter_t* matched_filter_q;
    
    /* Synchronization */
    symbol_sync_ctx_t* symbol_sync;
    frame_sync_ctx_t* frame_sync;
    
    /* Internal buffers */
    complex_f32_t* sample_buffer;
    size_t buffer_size;
};

m110a_demod_ctx_t* m110a_init(const m110a_config_t* config)
{
    if (!config) {
        return NULL;
    }
    
    m110a_demod_ctx_t* ctx = (m110a_demod_ctx_t*)calloc(1, sizeof(m110a_demod_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    /* Copy configuration */
    memcpy(&ctx->config, config, sizeof(m110a_config_t));
    
    /* Initialize NCO for carrier recovery */
    nco_init(&ctx->nco, config->center_frequency, (float)config->sample_rate);
    
    /* TODO: Initialize matched filters */
    /* TODO: Initialize symbol synchronizer */
    /* TODO: Initialize frame synchronizer */
    
    ctx->status = M110A_OK;
    return ctx;
}

int32_t m110a_process(m110a_demod_ctx_t* ctx, 
                      const float* samples, 
                      size_t num_samples,
                      uint8_t* output,
                      size_t output_size)
{
    if (!ctx || !samples || !output) {
        return -M110A_ERROR_INVALID_PARAM;
    }
    
    if (ctx->status != M110A_OK) {
        return -ctx->status;
    }
    
    /* TODO: Implement demodulation pipeline */
    /* 1. Mix to baseband */
    /* 2. Apply matched filter */
    /* 3. Symbol timing recovery */
    /* 4. Decision/slicing */
    /* 5. Frame synchronization */
    /* 6. Data extraction */
    
    (void)num_samples;
    (void)output_size;
    
    return 0;
}

m110a_status_t m110a_reset(m110a_demod_ctx_t* ctx)
{
    if (!ctx) {
        return M110A_ERROR_INVALID_PARAM;
    }
    
    /* Reset NCO */
    nco_init(&ctx->nco, ctx->config.center_frequency, (float)ctx->config.sample_rate);
    
    /* Reset synchronizers */
    if (ctx->symbol_sync) {
        symbol_sync_reset(ctx->symbol_sync);
    }
    if (ctx->frame_sync) {
        frame_sync_reset(ctx->frame_sync);
    }
    
    ctx->status = M110A_OK;
    return M110A_OK;
}

void m110a_free(m110a_demod_ctx_t* ctx)
{
    if (!ctx) {
        return;
    }
    
    if (ctx->matched_filter_i) {
        fir_free(ctx->matched_filter_i);
    }
    if (ctx->matched_filter_q) {
        fir_free(ctx->matched_filter_q);
    }
    if (ctx->symbol_sync) {
        symbol_sync_free(ctx->symbol_sync);
    }
    if (ctx->frame_sync) {
        frame_sync_free(ctx->frame_sync);
    }
    if (ctx->sample_buffer) {
        free(ctx->sample_buffer);
    }
    
    free(ctx);
}

m110a_status_t m110a_get_status(const m110a_demod_ctx_t* ctx)
{
    if (!ctx) {
        return M110A_ERROR_NOT_INITIALIZED;
    }
    return ctx->status;
}
