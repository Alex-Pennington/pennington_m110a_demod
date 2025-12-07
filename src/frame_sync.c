/**
 * @file frame_sync.c
 * @brief Frame synchronization implementation
 */

#include "m110a/frame_sync.h"
#include <stdlib.h>
#include <string.h>

struct frame_sync_ctx {
    frame_sync_config_t config;
    frame_sync_state_t state;
    
    /* Sync pattern storage */
    uint8_t* sync_pattern;
    
    /* Bit buffer for pattern matching */
    uint8_t* bit_buffer;
    size_t bit_buffer_size;
    size_t bit_count;
    
    /* Frame position tracking */
    size_t frame_position;
    size_t frames_since_sync;
    
    /* Statistics */
    uint32_t sync_hits;
    uint32_t sync_misses;
};

/* Count differing bits between two byte arrays */
static uint32_t count_bit_errors(const uint8_t* a, const uint8_t* b, size_t len)
{
    uint32_t errors = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t diff = a[i] ^ b[i];
        while (diff) {
            errors += diff & 1;
            diff >>= 1;
        }
    }
    return errors;
}

frame_sync_ctx_t* frame_sync_create(const frame_sync_config_t* config)
{
    if (!config || !config->sync_pattern || config->sync_pattern_len == 0) {
        return NULL;
    }
    
    frame_sync_ctx_t* ctx = (frame_sync_ctx_t*)calloc(1, sizeof(frame_sync_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    memcpy(&ctx->config, config, sizeof(frame_sync_config_t));
    
    /* Allocate and copy sync pattern */
    ctx->sync_pattern = (uint8_t*)malloc(config->sync_pattern_len);
    if (!ctx->sync_pattern) {
        frame_sync_free(ctx);
        return NULL;
    }
    memcpy(ctx->sync_pattern, config->sync_pattern, config->sync_pattern_len);
    
    /* Allocate bit buffer */
    ctx->bit_buffer_size = config->frame_length * 2;
    ctx->bit_buffer = (uint8_t*)calloc(ctx->bit_buffer_size, 1);
    if (!ctx->bit_buffer) {
        frame_sync_free(ctx);
        return NULL;
    }
    
    ctx->state = FRAME_SYNC_SEARCHING;
    
    return ctx;
}

void frame_sync_free(frame_sync_ctx_t* ctx)
{
    if (!ctx) {
        return;
    }
    
    if (ctx->sync_pattern) {
        free(ctx->sync_pattern);
    }
    if (ctx->bit_buffer) {
        free(ctx->bit_buffer);
    }
    
    free(ctx);
}

int32_t frame_sync_process(frame_sync_ctx_t* ctx,
                           const uint8_t* bits,
                           size_t num_bits,
                           uint8_t* frame_data,
                           size_t max_frame_size)
{
    if (!ctx || !bits || !frame_data) {
        return -1;
    }
    
    /* TODO: Implement frame synchronization */
    /* 1. Correlate against sync pattern */
    /* 2. Track frame boundaries */
    /* 3. Extract frame data */
    
    (void)num_bits;
    (void)max_frame_size;
    (void)count_bit_errors;  /* Silence unused warning for now */
    
    return 0;
}

frame_sync_state_t frame_sync_get_state(const frame_sync_ctx_t* ctx)
{
    if (!ctx) {
        return FRAME_SYNC_LOST;
    }
    return ctx->state;
}

void frame_sync_reset(frame_sync_ctx_t* ctx)
{
    if (!ctx) {
        return;
    }
    
    ctx->state = FRAME_SYNC_SEARCHING;
    ctx->bit_count = 0;
    ctx->frame_position = 0;
    ctx->frames_since_sync = 0;
    ctx->sync_hits = 0;
    ctx->sync_misses = 0;
    
    if (ctx->bit_buffer) {
        memset(ctx->bit_buffer, 0, ctx->bit_buffer_size);
    }
}
