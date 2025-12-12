/* ================================================================== */
/*                                                                    */
/*    MELPe Real-Time Streaming API                                   */
/*    Wrapper for STANAG 4591 MELPe codec                             */
/*                                                                    */
/* ================================================================== */

#ifndef MELPE_API_H
#define MELPE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ================================================================== */
/*                         Constants                                  */
/* ================================================================== */

/* Supported bit rates */
#define MELPE_RATE_2400     2400
#define MELPE_RATE_1200     1200
#define MELPE_RATE_600      600

/* Frame sizes in samples (at 8000 Hz sample rate) */
#define MELPE_FRAME_2400    180     /* 22.5 ms per frame */
#define MELPE_FRAME_1200    540     /* 67.5 ms (3 frames) */
#define MELPE_FRAME_600     720     /* 90 ms (4 frames) */

/* Bitstream sizes in bytes */
#define MELPE_BITS_2400     7       /* 54 bits packed */
#define MELPE_BITS_1200     11      /* 81 bits packed */
#define MELPE_BITS_600      7       /* 54 bits packed */

/* Audio parameters */
#define MELPE_SAMPLE_RATE   8000    /* 8 kHz */
#define MELPE_SAMPLE_BITS   16      /* 16-bit signed PCM */

/* ================================================================== */
/*                         Data Types                                 */
/* ================================================================== */

typedef struct melpe_encoder melpe_encoder_t;
typedef struct melpe_decoder melpe_decoder_t;

/* Callback for decoded audio output */
typedef void (*melpe_audio_callback_t)(
    const int16_t *samples,     /* Decoded PCM samples */
    int num_samples,            /* Number of samples */
    void *user_data             /* User context */
);

/* Callback for encoded bitstream output */
typedef void (*melpe_bitstream_callback_t)(
    const uint8_t *bits,        /* Encoded bitstream */
    int num_bytes,              /* Number of bytes */
    void *user_data             /* User context */
);

/* ================================================================== */
/*                      Encoder Functions                             */
/* ================================================================== */

/**
 * Create a new MELPe encoder
 * @param rate Bit rate (2400, 1200, or 600)
 * @param enable_npp Enable noise pre-processor (recommended)
 * @return Encoder handle or NULL on error
 */
melpe_encoder_t* melpe_encoder_create(int rate, bool enable_npp);

/**
 * Destroy encoder and free resources
 */
void melpe_encoder_destroy(melpe_encoder_t *enc);

/**
 * Encode PCM audio samples to MELPe bitstream
 * @param enc Encoder handle
 * @param samples Input PCM samples (16-bit signed, 8kHz)
 * @param num_samples Number of input samples
 * @param output Output buffer for encoded bits
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written, or -1 on error
 * 
 * Note: Feed samples continuously. Output is produced when enough
 * samples have been accumulated for a complete frame.
 */
int melpe_encoder_process(
    melpe_encoder_t *enc,
    const int16_t *samples,
    int num_samples,
    uint8_t *output,
    int output_size
);

/**
 * Set callback for encoded output (alternative to polling)
 */
void melpe_encoder_set_callback(
    melpe_encoder_t *enc,
    melpe_bitstream_callback_t callback,
    void *user_data
);

/**
 * Get required frame size in samples for current rate
 */
int melpe_encoder_frame_size(melpe_encoder_t *enc);

/**
 * Get output bitstream size in bytes per frame
 */
int melpe_encoder_output_size(melpe_encoder_t *enc);

/* ================================================================== */
/*                      Decoder Functions                             */
/* ================================================================== */

/**
 * Create a new MELPe decoder
 * @param rate Bit rate (2400, 1200, or 600)
 * @param enable_postfilter Enable post-filter for improved quality
 * @return Decoder handle or NULL on error
 */
melpe_decoder_t* melpe_decoder_create(int rate, bool enable_postfilter);

/**
 * Destroy decoder and free resources
 */
void melpe_decoder_destroy(melpe_decoder_t *dec);

/**
 * Decode MELPe bitstream to PCM audio
 * @param dec Decoder handle
 * @param bits Input bitstream
 * @param num_bytes Number of input bytes
 * @param output Output buffer for PCM samples
 * @param output_size Size of output buffer in samples
 * @return Number of samples written, or -1 on error
 */
int melpe_decoder_process(
    melpe_decoder_t *dec,
    const uint8_t *bits,
    int num_bytes,
    int16_t *output,
    int output_size
);

/**
 * Set callback for decoded audio (alternative to polling)
 */
void melpe_decoder_set_callback(
    melpe_decoder_t *dec,
    melpe_audio_callback_t callback,
    void *user_data
);

/**
 * Get output frame size in samples for current rate
 */
int melpe_decoder_frame_size(melpe_decoder_t *dec);

/**
 * Get required input size in bytes per frame
 */
int melpe_decoder_input_size(melpe_decoder_t *dec);

/**
 * Handle frame erasure (lost/corrupted frame)
 * Performs error concealment
 * @param output Output buffer for concealed audio
 * @param output_size Size of output buffer in samples
 * @return Number of samples written
 */
int melpe_decoder_frame_erasure(
    melpe_decoder_t *dec,
    int16_t *output,
    int output_size
);

/* ================================================================== */
/*                    Transcoding Functions                           */
/* ================================================================== */

/**
 * Transcode bitstream from one rate to another
 * @param input Input bitstream
 * @param input_rate Input bit rate
 * @param output Output buffer
 * @param output_rate Output bit rate
 * @return Number of output bytes, or -1 on error
 */
int melpe_transcode(
    const uint8_t *input,
    int input_rate,
    uint8_t *output,
    int output_rate
);

/* ================================================================== */
/*                      Utility Functions                             */
/* ================================================================== */

/**
 * Get version string
 */
const char* melpe_version(void);

/**
 * Get frame duration in milliseconds for a given rate
 */
float melpe_frame_duration_ms(int rate);

/**
 * Calculate buffer size needed for given duration
 * @param rate Bit rate
 * @param duration_ms Duration in milliseconds
 * @param for_samples true for sample buffer, false for bitstream buffer
 */
int melpe_buffer_size(int rate, float duration_ms, bool for_samples);

#ifdef __cplusplus
}
#endif

#endif /* MELPE_API_H */
