#ifndef DECODER_H
#define DECODER_H

#include <stddef.h>
#include <limits.h>

/* Include this header in ONE .c/.cpp file: it contains the implementation. */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "media/minimp3.h"

typedef struct {
    mp3dec_t mp3;
    const uint8_t *data;
    size_t size;
    size_t position;
    mp3dec_frame_info_t info;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
} Decoder;

static void decoder_init(Decoder *decoder, const uint8_t *data, size_t size)
{
    mp3dec_init(&decoder->mp3);
    decoder->data = data;
    decoder->size = size;
    decoder->position = 0;
}

/* Returns the number of int16_t values in pcm (ALL channels).
 * 0: end of data / no more decodable frames; -1: invalid input size.
 * pcm and info remain valid until the next call.
 */
static int decoder_next(Decoder *decoder)
{
    while (decoder->position < decoder->size) {
        size_t remaining = decoder->size - decoder->position;
        if (remaining > INT_MAX) {
            return -1;
        }

        int samples = mp3dec_decode_frame(
            &decoder->mp3,
            decoder->data + decoder->position,
            (int)remaining,
            decoder->pcm,
            &decoder->info);

        if (decoder->info.frame_bytes <= 0) {
            decoder->position = decoder->size;
            return 0;
        }

        decoder->position += (size_t)decoder->info.frame_bytes;
        if (samples > 0) {
            return samples * decoder->info.channels;
        }
        /* A tag or reservoir warm-up can consume bytes without producing PCM. */
    }
    return 0;
}

#endif /* DECODER_H */
