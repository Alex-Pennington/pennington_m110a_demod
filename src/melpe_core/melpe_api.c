/* ================================================================== */
/*                                                                    */
/*    MELPe Real-Time Streaming API Implementation                    */
/*    Wrapper for STANAG 4591 MELPe codec                             */
/*                                                                    */
/* ================================================================== */

#include "melpe_api.h"
#include "sc1200.h"
#include "sc600.h"
#include "cst600.h"
#include "global.h"
#include "melp_sub.h"
#include "npp.h"

#include <stdlib.h>
#include <string.h>

/* External globals for 600 bps mode */
extern char chbuf600[];

/* ================================================================== */
/*                    Internal Structures                             */
/* ================================================================== */

struct melpe_encoder {
    int rate;
    int frame_size;
    int output_size;
    bool npp_enabled;
    
    /* Input buffer for accumulating samples */
    int16_t *input_buffer;
    int input_count;
    
    /* Callback */
    melpe_bitstream_callback_t callback;
    void *user_data;
    
    /* State flag */
    bool initialized;
};

struct melpe_decoder {
    int rate;
    int frame_size;
    int input_size;
    bool postfilter_enabled;
    
    /* Input buffer for accumulating bits */
    uint8_t *input_buffer;
    int input_count;
    
    /* Previous parameters for error concealment */
    struct melp_param prev_par;
    
    /* Callback */
    melpe_audio_callback_t callback;
    void *user_data;
    
    /* State flag */
    bool initialized;
};

/* ================================================================== */
/*                    Encoder Implementation                          */
/* ================================================================== */

melpe_encoder_t* melpe_encoder_create(int rate_val, bool enable_npp)
{
    melpe_encoder_t *enc;
    
    /* Validate rate */
    if (rate_val != MELPE_RATE_2400 && 
        rate_val != MELPE_RATE_1200 && 
        rate_val != MELPE_RATE_600) {
        return NULL;
    }
    
    enc = (melpe_encoder_t*)calloc(1, sizeof(melpe_encoder_t));
    if (!enc) return NULL;
    
    enc->rate = rate_val;
    enc->npp_enabled = enable_npp;
    
    /* Set frame sizes based on rate */
    switch (rate_val) {
        case MELPE_RATE_2400:
            enc->frame_size = MELPE_FRAME_2400;
            enc->output_size = MELPE_BITS_2400;
            break;
        case MELPE_RATE_1200:
            enc->frame_size = MELPE_FRAME_1200;
            enc->output_size = MELPE_BITS_1200;
            break;
        case MELPE_RATE_600:
            enc->frame_size = MELPE_FRAME_600;
            enc->output_size = MELPE_BITS_600;
            break;
    }
    
    /* Allocate input buffer */
    enc->input_buffer = (int16_t*)calloc(enc->frame_size, sizeof(int16_t));
    if (!enc->input_buffer) {
        free(enc);
        return NULL;
    }
    
    /* Initialize global codec state */
    rate = (short)rate_val;
    chwordsize = 8;  /* 8-bit packed output */
    
    /* Set global frameSize and bitNum - required by analysis/synthesis */
    /* These are critical! melp_chn_write() uses bitNum24 to know how many bits to pack */
    bitNum12 = 81;   /* 1200 bps: 81 bits per frame */
    bitNum24 = 54;   /* 2400 bps: 54 bits per frame */
    
    switch (rate_val) {
        case MELPE_RATE_2400:
            frameSize = FRAME;
            break;
        case MELPE_RATE_1200:
            frameSize = BLOCK;
            break;
        case MELPE_RATE_600:
            frameSize = BLOCK600;
            break;
    }
    
    /* Initialize encoder */
    melp_ana_init();
    
    /* NPP self-initializes on first call, no separate init needed */
    
    enc->initialized = true;
    
    return enc;
}

void melpe_encoder_destroy(melpe_encoder_t *enc)
{
    if (enc) {
        if (enc->input_buffer) {
            free(enc->input_buffer);
        }
        free(enc);
    }
}

int melpe_encoder_process(
    melpe_encoder_t *enc,
    const int16_t *samples,
    int num_samples,
    uint8_t *output,
    int output_size)
{
    int samples_consumed = 0;
    int bytes_written = 0;
    
    if (!enc || !enc->initialized || !samples) {
        return -1;
    }
    
    while (samples_consumed < num_samples) {
        /* Fill input buffer */
        int space = enc->frame_size - enc->input_count;
        int to_copy = num_samples - samples_consumed;
        if (to_copy > space) to_copy = space;
        
        memcpy(&enc->input_buffer[enc->input_count],
               &samples[samples_consumed],
               to_copy * sizeof(int16_t));
        
        enc->input_count += to_copy;
        samples_consumed += to_copy;
        
        /* Process complete frame */
        if (enc->input_count >= enc->frame_size) {
            /* Check output space */
            if (output && (bytes_written + enc->output_size > output_size)) {
                break;  /* Output buffer full */
            }
            
            /* Set global rate before analysis - needed for correct codec path */
            rate = (short)enc->rate;
            switch (enc->rate) {
                case MELPE_RATE_2400: frameSize = FRAME; break;
                case MELPE_RATE_1200: frameSize = BLOCK; break;
                case MELPE_RATE_600:  frameSize = BLOCK600; break;
            }
            
            /* Apply noise pre-processor if enabled */
            if (enc->npp_enabled) {
                npp((Shortword*)enc->input_buffer, (Shortword*)enc->input_buffer);
            }
            
            /* Encode frame - analysis() handles quantization and channel 
             * writing internally based on rate. For RATE2400 it calls 
             * melp_chn_write(), for RATE600 it calls WRS_build_stream() */
            analysis((Shortword*)enc->input_buffer, melp_par);
            
            /* Copy to output - use correct buffer based on rate */
            if (output) {
                if (enc->rate == MELPE_RATE_600) {
                    memcpy(&output[bytes_written], chbuf600, enc->output_size);
                } else {
                    memcpy(&output[bytes_written], chbuf, enc->output_size);
                }
                bytes_written += enc->output_size;
            }
            
            /* Invoke callback if set */
            if (enc->callback) {
                if (enc->rate == MELPE_RATE_600) {
                    enc->callback(chbuf600, enc->output_size, enc->user_data);
                } else {
                    enc->callback(chbuf, enc->output_size, enc->user_data);
                }
            }
            
            enc->input_count = 0;
        }
    }
    
    return bytes_written;
}

void melpe_encoder_set_callback(
    melpe_encoder_t *enc,
    melpe_bitstream_callback_t callback,
    void *user_data)
{
    if (enc) {
        enc->callback = callback;
        enc->user_data = user_data;
    }
}

int melpe_encoder_frame_size(melpe_encoder_t *enc)
{
    return enc ? enc->frame_size : 0;
}

int melpe_encoder_output_size(melpe_encoder_t *enc)
{
    return enc ? enc->output_size : 0;
}

/* ================================================================== */
/*                    Decoder Implementation                          */
/* ================================================================== */

melpe_decoder_t* melpe_decoder_create(int rate_val, bool enable_postfilter)
{
    melpe_decoder_t *dec;
    
    /* Validate rate */
    if (rate_val != MELPE_RATE_2400 && 
        rate_val != MELPE_RATE_1200 && 
        rate_val != MELPE_RATE_600) {
        return NULL;
    }
    
    dec = (melpe_decoder_t*)calloc(1, sizeof(melpe_decoder_t));
    if (!dec) return NULL;
    
    dec->rate = rate_val;
    dec->postfilter_enabled = enable_postfilter;
    
    /* Set sizes based on rate */
    switch (rate_val) {
        case MELPE_RATE_2400:
            dec->frame_size = MELPE_FRAME_2400;
            dec->input_size = MELPE_BITS_2400;
            break;
        case MELPE_RATE_1200:
            dec->frame_size = MELPE_FRAME_1200;
            dec->input_size = MELPE_BITS_1200;
            break;
        case MELPE_RATE_600:
            dec->frame_size = MELPE_FRAME_600;
            dec->input_size = MELPE_BITS_600;
            break;
    }
    
    /* Allocate input buffer */
    dec->input_buffer = (uint8_t*)calloc(dec->input_size, sizeof(uint8_t));
    if (!dec->input_buffer) {
        free(dec);
        return NULL;
    }
    
    /* Initialize global codec state */
    rate = (short)rate_val;
    chwordsize = 8;
    
    /* Set global frameSize - required by analysis/synthesis */
    switch (rate_val) {
        case MELPE_RATE_2400:
            frameSize = FRAME;
            break;
        case MELPE_RATE_1200:
            frameSize = BLOCK;
            break;
        case MELPE_RATE_600:
            frameSize = BLOCK600;
            break;
    }
    
    /* Initialize decoder */
    melp_syn_init();
    
    dec->initialized = true;
    
    return dec;
}

void melpe_decoder_destroy(melpe_decoder_t *dec)
{
    if (dec) {
        if (dec->input_buffer) {
            free(dec->input_buffer);
        }
        free(dec);
    }
}

int melpe_decoder_process(
    melpe_decoder_t *dec,
    const uint8_t *bits,
    int num_bytes,
    int16_t *output,
    int output_size)
{
    int bytes_consumed = 0;
    int samples_written = 0;
    int16_t frame_output[MELPE_FRAME_600];  /* Max frame size */
    
    if (!dec || !dec->initialized || !bits) {
        return -1;
    }
    
    while (bytes_consumed < num_bytes) {
        /* Fill input buffer */
        int space = dec->input_size - dec->input_count;
        int to_copy = num_bytes - bytes_consumed;
        if (to_copy > space) to_copy = space;
        
        memcpy(&dec->input_buffer[dec->input_count],
               &bits[bytes_consumed],
               to_copy);
        
        dec->input_count += to_copy;
        bytes_consumed += to_copy;
        
        /* Process complete frame */
        if (dec->input_count >= dec->input_size) {
            /* Check output space */
            if (output && (samples_written + dec->frame_size > output_size)) {
                break;  /* Output buffer full */
            }
            
            /* Set global rate before synthesis - needed for correct codec path */
            rate = (short)dec->rate;
            switch (dec->rate) {
                case MELPE_RATE_2400: frameSize = FRAME; break;
                case MELPE_RATE_1200: frameSize = BLOCK; break;
                case MELPE_RATE_600:  frameSize = BLOCK600; break;
            }
            
            /* Copy input bits to global buffer - synthesis() reads from there.
             * synthesis() handles channel read internally based on rate.
             * For RATE2400/1200 it uses chbuf, for RATE600 it uses chbuf600 */
            if (dec->rate == MELPE_RATE_600) {
                memcpy(chbuf600, dec->input_buffer, dec->input_size);
            } else {
                memcpy(chbuf, dec->input_buffer, dec->input_size);
            }
            synthesis(&melp_par[0], (Shortword*)frame_output, 
                     dec->postfilter_enabled ? 0 : 1);
            
            /* Save for error concealment */
            memcpy(&dec->prev_par, &melp_par[0], sizeof(struct melp_param));
            
            /* Copy to output */
            if (output) {
                memcpy(&output[samples_written], frame_output, 
                       dec->frame_size * sizeof(int16_t));
                samples_written += dec->frame_size;
            }
            
            /* Invoke callback if set */
            if (dec->callback) {
                dec->callback(frame_output, dec->frame_size, dec->user_data);
            }
            
            dec->input_count = 0;
        }
    }
    
    return samples_written;
}

void melpe_decoder_set_callback(
    melpe_decoder_t *dec,
    melpe_audio_callback_t callback,
    void *user_data)
{
    if (dec) {
        dec->callback = callback;
        dec->user_data = user_data;
    }
}

int melpe_decoder_frame_size(melpe_decoder_t *dec)
{
    return dec ? dec->frame_size : 0;
}

int melpe_decoder_input_size(melpe_decoder_t *dec)
{
    return dec ? dec->input_size : 0;
}

int melpe_decoder_frame_erasure(
    melpe_decoder_t *dec,
    int16_t *output,
    int output_size)
{
    if (!dec || !dec->initialized || !output) {
        return -1;
    }
    
    if (output_size < dec->frame_size) {
        return -1;
    }
    
    /* Use previous parameters with erasure flag for concealment */
    /* The codec has built-in FEC and error concealment */
    synthesis(&dec->prev_par, (Shortword*)output, 
             dec->postfilter_enabled ? 0 : 1);
    
    return dec->frame_size;
}

/* ================================================================== */
/*                    Utility Functions                               */
/* ================================================================== */

const char* melpe_version(void)
{
    return "MELPe 1.0.0 (STANAG 4591)";
}

float melpe_frame_duration_ms(int rate_val)
{
    switch (rate_val) {
        case MELPE_RATE_2400: return 22.5f;
        case MELPE_RATE_1200: return 67.5f;
        case MELPE_RATE_600:  return 90.0f;
        default: return 0.0f;
    }
}

int melpe_buffer_size(int rate_val, float duration_ms, bool for_samples)
{
    float frames = duration_ms / melpe_frame_duration_ms(rate_val);
    int num_frames = (int)(frames + 0.999f);  /* Round up */
    
    if (for_samples) {
        switch (rate_val) {
            case MELPE_RATE_2400: return num_frames * MELPE_FRAME_2400;
            case MELPE_RATE_1200: return num_frames * MELPE_FRAME_1200;
            case MELPE_RATE_600:  return num_frames * MELPE_FRAME_600;
        }
    } else {
        switch (rate_val) {
            case MELPE_RATE_2400: return num_frames * MELPE_BITS_2400;
            case MELPE_RATE_1200: return num_frames * MELPE_BITS_1200;
            case MELPE_RATE_600:  return num_frames * MELPE_BITS_600;
        }
    }
    
    return 0;
}
