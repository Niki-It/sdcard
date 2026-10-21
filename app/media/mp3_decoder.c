#include "mp3_decoder.h"
/* Compile minimp3 implementation in this translation unit only. */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "SEGGER_RTT.h"

#ifdef MINIMP3_FLOAT_OUTPUT
#error "This module requires minimp3 int16 output"
#endif

#include <stdint.h>
#include <string.h>

#define MP3_INPUT_BYTES    4096U
#define MP3_PCM_BYTES      4608U
#define MP3_FRAMES_PER_CALL 4U

_Static_assert(sizeof(mp3d_sample_t) == 2U, "16-bit PCM required");




/* ================= MP3 DEBUG VARIABLES: ADD TO WATCH =================
 * RAM only: no printf/UART, allocation, extra file reads, or I2S calls.
 * Reset at prepare(); close() NEVER clears the first failure/history.
 * Watch mp3_debug_error_valid, mp3_debug_error, mp3_debug, mp3_debug_history.
 * History is circular: history_next points to the next slot to overwrite.
 * Stop the CPU to inspect a consistent snapshot (not an atomic ISR API).
 */
#define MP3_DEBUG_HISTORY_COUNT 16U
#define MP3_DEBUG_BYTES 16U

typedef enum {
    MP3_DBG_PREPARE = 1, MP3_DBG_SEEK_BEFORE, MP3_DBG_SEEK_AFTER,
    MP3_DBG_ID3_BEFORE, MP3_DBG_ID3_AFTER, MP3_DBG_ID3_OFFSET,
    MP3_DBG_REFILL_BEFORE, MP3_DBG_MOVE_AFTER,
    MP3_DBG_READ_BEFORE, MP3_DBG_READ_AFTER,
    MP3_DBG_PREPARE_HEADER, MP3_DBG_PCM_ENTER, MP3_DBG_FRAME_BEGIN,
    MP3_DBG_PARSE, MP3_DBG_HEADER_OK, MP3_DBG_DECODE_BEFORE,
    MP3_DBG_DECODE_AFTER, MP3_DBG_CONSUMED, MP3_DBG_PCM_EXIT,
    MP3_DBG_CLOSE
} mp3_debug_stage_t;

typedef enum {
    MP3_DBG_REASON_NONE = 0, MP3_DBG_REASON_RETURN,
    MP3_DBG_REASON_BOUNDS, MP3_DBG_REASON_BAD_SYNC,
    MP3_DBG_REASON_VERSION_LAYER, MP3_DBG_REASON_RATE_BITRATE
} mp3_debug_reason_t;

typedef struct {
    uint32_t sequence, stage, phase; /* phase: 1 prepare, 2 read_pcm */
    uint32_t pcm_calls, frames_attempted, frames_consumed, zero_sample_frames;
    uintptr_t context_address, decoder_address, input_base, input_end;
    uintptr_t file_address, expected_file_address, p, expected_p;
    uintptr_t destination, pcm, result_address, stack_marker;
    uint32_t position, available, eof, sample_rate, bounds_ok, p_matches;
    FSIZE_t file_position, file_size, audio_offset, expected_frame_offset;
    FSIZE_t current_file_offset, seek_target, seek_before, seek_after;
    uint32_t file_offset_valid, offset_matches;
    uint32_t capacity, output_bytes, frame_index;
    uint32_t hz, frame_bytes, version_bits, layer_bits;
    uint32_t bitrate_index, rate_index, padding, channel_mode;
    uint32_t read_requested, read_received, read_old_available;
    FSIZE_t read_file_before, read_file_after;
    uintptr_t read_destination;
    int io_result, samples, decoded_bytes, decoded_offset;
    int decoded_hz, decoded_channels, decoded_layer, decoded_bitrate_kbps;
    int reservoir_before, reservoir_after;
    uint32_t id3_present, id3_size, id3_footer, id3_received;
    uint8_t id3_header[10];
    uint32_t head_count, current_count, next_count;
    uint8_t buffer_head[MP3_DEBUG_BYTES], current[MP3_DEBUG_BYTES];
    uint8_t next_before[MP3_DEBUG_BYTES], next_after[MP3_DEBUG_BYTES];
    uint32_t error_reason, error_line;
    int error_result;
} mp3_debug_state_t;

typedef struct {
    uint32_t sequence, stage, phase, position, available, frame_bytes;
    FSIZE_t file_position, expected_frame_offset;
    uintptr_t p;
    int samples, decoded_bytes;
    uint8_t current[MP3_DEBUG_BYTES];
} mp3_debug_event_t;

volatile mp3_debug_state_t mp3_debug;
volatile mp3_debug_state_t mp3_debug_error;
volatile mp3_debug_event_t mp3_debug_history[MP3_DEBUG_HISTORY_COUNT];
volatile uint32_t mp3_debug_history_next;
volatile uint32_t mp3_debug_history_used;
volatile uint32_t mp3_debug_error_valid;
/* Wider error window: starts up to 32 bytes BEFORE the failing position.
 * Its offset is an INPUT BUFFER index, not an absolute file position.
 */
volatile uint8_t mp3_debug_error_window[128];
volatile uint32_t mp3_debug_error_window_offset;
volatile uint32_t mp3_debug_error_window_count;
/* ================= END MP3 DEBUG VARIABLES ================= */

static struct
{
    FIL *file;
    mp3dec_t decoder;
    uint8_t input[MP3_INPUT_BYTES];
    uint32_t position;
    uint32_t available;
    uint32_t sample_rate;
    bool eof;
} mp3;

void mp3_debug_print(mp3_debug_state_t *debug)
{
    if (debug == NULL)
        return;

#define MP3_PRINT_U(field) \
    SEGGER_RTT_printf(0, #field "=%u\r\n", (unsigned int)debug->field)

#define MP3_PRINT_I(field) \
    SEGGER_RTT_printf(0, #field "=%d\r\n", (int)debug->field)

#define MP3_PRINT_PTR(field) \
    SEGGER_RTT_printf(0, #field "=0x%08X\r\n", (unsigned int)debug->field)

#define MP3_PRINT_FS(field) \
    do \
    { \
        uint64_t value = (uint64_t)debug->field; \
        SEGGER_RTT_printf(0, #field "=0x%08X%08X\r\n", \
            (unsigned int)(value >> 32), \
            (unsigned int)(value & UINT64_C(0xFFFFFFFF))); \
    } while (0)

#define MP3_PRINT_ARRAY(field) \
    do \
    { \
        SEGGER_RTT_printf(0, #field ":"); \
        for (unsigned int i = 0; i < sizeof(debug->field); i++) \
        { \
            SEGGER_RTT_printf(0, " %02X", (unsigned int)debug->field[i]); \
        } \
        SEGGER_RTT_printf(0, "\r\n"); \
    } while (0)

    SEGGER_RTT_printf(0, "\r\n========== MP3 DEBUG BEGIN ==========\r\n");

    MP3_PRINT_U(sequence);
    MP3_PRINT_U(stage);
    MP3_PRINT_U(phase);

    MP3_PRINT_U(pcm_calls);
    MP3_PRINT_U(frames_attempted);
    MP3_PRINT_U(frames_consumed);
    MP3_PRINT_U(zero_sample_frames);

    MP3_PRINT_PTR(context_address);
    MP3_PRINT_PTR(decoder_address);
    MP3_PRINT_PTR(input_base);
    MP3_PRINT_PTR(input_end);

    MP3_PRINT_PTR(file_address);
    MP3_PRINT_PTR(expected_file_address);
    MP3_PRINT_PTR(p);
    MP3_PRINT_PTR(expected_p);

    MP3_PRINT_PTR(destination);
    MP3_PRINT_PTR(pcm);
    MP3_PRINT_PTR(result_address);
    MP3_PRINT_PTR(stack_marker);

    MP3_PRINT_U(position);
    MP3_PRINT_U(available);
    MP3_PRINT_U(eof);
    MP3_PRINT_U(sample_rate);
    MP3_PRINT_U(bounds_ok);
    MP3_PRINT_U(p_matches);

    MP3_PRINT_FS(file_position);
    MP3_PRINT_FS(file_size);
    MP3_PRINT_FS(audio_offset);
    MP3_PRINT_FS(expected_frame_offset);

    MP3_PRINT_FS(current_file_offset);
    MP3_PRINT_FS(seek_target);
    MP3_PRINT_FS(seek_before);
    MP3_PRINT_FS(seek_after);

    MP3_PRINT_U(file_offset_valid);
    MP3_PRINT_U(offset_matches);

    MP3_PRINT_U(capacity);
    MP3_PRINT_U(output_bytes);
    MP3_PRINT_U(frame_index);

    MP3_PRINT_U(hz);
    MP3_PRINT_U(frame_bytes);
    MP3_PRINT_U(version_bits);
    MP3_PRINT_U(layer_bits);

    MP3_PRINT_U(bitrate_index);
    MP3_PRINT_U(rate_index);
    MP3_PRINT_U(padding);
    MP3_PRINT_U(channel_mode);

    MP3_PRINT_U(read_requested);
    MP3_PRINT_U(read_received);
    MP3_PRINT_U(read_old_available);

    MP3_PRINT_FS(read_file_before);
    MP3_PRINT_FS(read_file_after);
    MP3_PRINT_PTR(read_destination);

    MP3_PRINT_I(io_result);
    MP3_PRINT_I(samples);
    MP3_PRINT_I(decoded_bytes);
    MP3_PRINT_I(decoded_offset);

    MP3_PRINT_I(decoded_hz);
    MP3_PRINT_I(decoded_channels);
    MP3_PRINT_I(decoded_layer);
    MP3_PRINT_I(decoded_bitrate_kbps);

    MP3_PRINT_I(reservoir_before);
    MP3_PRINT_I(reservoir_after);

    MP3_PRINT_U(id3_present);
    MP3_PRINT_U(id3_size);
    MP3_PRINT_U(id3_footer);
    MP3_PRINT_U(id3_received);
    MP3_PRINT_ARRAY(id3_header);

    MP3_PRINT_U(head_count);
    MP3_PRINT_U(current_count);
    MP3_PRINT_U(next_count);

    MP3_PRINT_ARRAY(buffer_head);
    MP3_PRINT_ARRAY(current);
    MP3_PRINT_ARRAY(next_before);
    MP3_PRINT_ARRAY(next_after);

    MP3_PRINT_U(error_reason);
    MP3_PRINT_U(error_line);
    MP3_PRINT_I(error_result);

    SEGGER_RTT_printf(0, "========== MP3 DEBUG END ==========\r\n");

#undef MP3_PRINT_U
#undef MP3_PRINT_I
#undef MP3_PRINT_PTR
#undef MP3_PRINT_FS
#undef MP3_PRINT_ARRAY
}

/* All debug helpers are main-context only, like the decoder itself. */
static FIL *mp3_debug_file;

static void mp3_debug_zero(volatile void *p, size_t n)
{
    volatile uint8_t *bytes = (volatile uint8_t *)p;
    while (n-- != 0U) *bytes++ = 0U;
}

static int mp3_debug_bounds(void)
{
    return mp3.position <= MP3_INPUT_BYTES &&
           mp3.available <= MP3_INPUT_BYTES - mp3.position;
}

static void mp3_debug_mark(mp3_debug_stage_t stage)
{
    uint8_t stack_marker;
    mp3_debug.sequence++;
    mp3_debug.stage = stage;
    mp3_debug.context_address = (uintptr_t)&mp3;
    mp3_debug.decoder_address = (uintptr_t)&mp3.decoder;
    mp3_debug.input_base = (uintptr_t)mp3.input;
    mp3_debug.input_end = (uintptr_t)(mp3.input + MP3_INPUT_BYTES);
    mp3_debug.file_address = (uintptr_t)mp3.file;
    mp3_debug.expected_file_address = (uintptr_t)mp3_debug_file;
    mp3_debug.stack_marker = (uintptr_t)&stack_marker; /* address only */
    mp3_debug.position = mp3.position;
    mp3_debug.available = mp3.available;
    mp3_debug.eof = mp3.eof;
    mp3_debug.sample_rate = mp3.sample_rate;
    mp3_debug.bounds_ok = (uint32_t)mp3_debug_bounds();
    mp3_debug.expected_p = (uintptr_t)mp3.input + mp3.position;
    mp3_debug.p_matches = mp3_debug.p == mp3_debug.expected_p;
    if (mp3_debug_file != NULL) {
        mp3_debug.file_position = f_tell(mp3_debug_file);
        mp3_debug.file_size = f_size(mp3_debug_file);
    }
    mp3_debug.file_offset_valid = mp3_debug.file_position >= mp3.available;
    mp3_debug.current_file_offset = mp3_debug.file_offset_valid ?
        mp3_debug.file_position - mp3.available : 0U;
    mp3_debug.offset_matches = mp3_debug.file_offset_valid &&
        mp3_debug.current_file_offset == mp3_debug.expected_frame_offset;
    mp3_debug.current_count = 0U;
    mp3_debug.head_count = 0U;
    if (mp3_debug.bounds_ok) {
        uint32_t end = mp3.position + mp3.available;
        mp3_debug.head_count = end < MP3_DEBUG_BYTES ? end : MP3_DEBUG_BYTES;
        mp3_debug.current_count = mp3.available < MP3_DEBUG_BYTES ?
                                  mp3.available : MP3_DEBUG_BYTES;
    }
    for (uint32_t i = 0; i < MP3_DEBUG_BYTES; ++i) {
        mp3_debug.buffer_head[i] = i < mp3_debug.head_count ? mp3.input[i] : 0U;
        mp3_debug.current[i] = i < mp3_debug.current_count ?
                              mp3.input[mp3.position + i] : 0U;
    }
    if (!mp3_debug_error_valid) {
        volatile mp3_debug_event_t *e = &mp3_debug_history[mp3_debug_history_next];
        e->sequence = mp3_debug.sequence; e->stage = stage;
        e->phase = mp3_debug.phase; e->position = mp3.position;
        e->available = mp3.available; e->frame_bytes = mp3_debug.frame_bytes;
        e->file_position = mp3_debug.file_position;
        e->expected_frame_offset = mp3_debug.expected_frame_offset;
        e->p = mp3_debug.p; e->samples = mp3_debug.samples;
        e->decoded_bytes = mp3_debug.decoded_bytes;
        for (uint32_t i = 0; i < MP3_DEBUG_BYTES; ++i)
            e->current[i] = mp3_debug.current[i];
        mp3_debug_history_next = (mp3_debug_history_next + 1U) % MP3_DEBUG_HISTORY_COUNT;
        if (mp3_debug_history_used < MP3_DEBUG_HISTORY_COUNT)
            mp3_debug_history_used++;
    }
}

static FRESULT mp3_debug_return(FRESULT value, uint32_t line)
{
    if (value != FR_OK && !mp3_debug_error_valid) {
        mp3_debug_mark((mp3_debug_stage_t)mp3_debug.stage);
        if (mp3_debug.error_reason == MP3_DBG_REASON_NONE)
            mp3_debug.error_reason = MP3_DBG_REASON_RETURN;
        mp3_debug.error_line = line;
        mp3_debug.error_result = (int)value;
        /* Byte-wise copy avoids a large automatic snapshot on the stack. */
        volatile uint8_t *dst = (volatile uint8_t *)&mp3_debug_error;
        const volatile uint8_t *src = (const volatile uint8_t *)&mp3_debug;
        for (size_t i = 0; i < sizeof(mp3_debug); ++i) dst[i] = src[i];
        if (mp3_debug_bounds()) {
            uint32_t start = mp3.position > 32U ? mp3.position - 32U : 0U;
            uint32_t n = mp3.position + mp3.available - start;
            if (n > sizeof(mp3_debug_error_window)) n = sizeof(mp3_debug_error_window);
            mp3_debug_error_window_offset = start;
            mp3_debug_error_window_count = n;
            for (uint32_t i = 0; i < n; ++i)
                mp3_debug_error_window[i] = mp3.input[start + i];
        }
        mp3_debug_error_valid = 1U; /* Set LAST; history is frozen now. */
    }
    return value;
}
#define MP3_DEBUG_RETURN(value) mp3_debug_return((value), __LINE__)

static void mp3_debug_next(uint32_t frame_bytes, int after)
{
    uint32_t n = 0U;
    if (mp3_debug_bounds() && frame_bytes <= mp3.available) {
        n = mp3.available - frame_bytes;
        if (n > MP3_DEBUG_BYTES) n = MP3_DEBUG_BYTES;
    }
    mp3_debug.next_count = n;
    for (uint32_t i = 0; i < MP3_DEBUG_BYTES; ++i) {
        uint8_t b = i < n ? mp3.input[mp3.position + frame_bytes + i] : 0U;
        if (after) mp3_debug.next_after[i] = b;
        else mp3_debug.next_before[i] = b;
    }
}

static FRESULT seek_to(FIL *file, FSIZE_t offset)
{
    mp3_debug.seek_target = offset;
    mp3_debug.seek_before = f_tell(file);
    mp3_debug_mark(MP3_DBG_SEEK_BEFORE);
    if (offset > f_size(file))
        return MP3_DEBUG_RETURN(FR_INT_ERR);

    FRESULT error = f_lseek(file, offset);
    mp3_debug.io_result = (int)error;
    mp3_debug.seek_after = f_tell(file);
    mp3_debug_mark(MP3_DBG_SEEK_AFTER);

    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);

    return MP3_DEBUG_RETURN(f_tell(file) == offset ? FR_OK : FR_INT_ERR);
}


static FRESULT refill(void)
{
    mp3_debug_mark(MP3_DBG_REFILL_BEFORE);
    if (!mp3_debug_bounds()) {
        mp3_debug.error_reason = MP3_DBG_REASON_BOUNDS;
        return MP3_DEBUG_RETURN(FR_INT_ERR);
    }
    if (mp3.eof)
        return MP3_DEBUG_RETURN(FR_OK);

    memmove(
        mp3.input,
        mp3.input + mp3.position,
        mp3.available
    );
    mp3.position = 0U;
    mp3_debug_mark(MP3_DBG_MOVE_AFTER);

    UINT requested = sizeof(mp3.input) - mp3.available;
    UINT received = 0U;

    if (requested == 0U)
        return MP3_DEBUG_RETURN(FR_OK);

    mp3_debug.read_requested = requested;
    mp3_debug.read_received = 0U;
    mp3_debug.read_old_available = mp3.available;
    mp3_debug.read_destination = (uintptr_t)(mp3.input + mp3.available);
    mp3_debug.read_file_before = f_tell(mp3.file);
    mp3_debug_mark(MP3_DBG_READ_BEFORE);
    FRESULT error = f_read(
        mp3.file,
        mp3.input + mp3.available,
        requested,
        &received
    );

    mp3_debug.read_received = received;
    mp3_debug.read_file_after = f_tell(mp3.file);
    mp3_debug.io_result = (int)error;
    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);

    mp3.available += received;
    mp3.eof = f_tell(mp3.file) == f_size(mp3.file);

    mp3_debug_mark(MP3_DBG_READ_AFTER);

    if (received < requested && !mp3.eof)
        return MP3_DEBUG_RETURN(FR_INT_ERR);

    return MP3_DEBUG_RETURN(FR_OK);
}


/* Strict MPEG-1 Layer III framing; no free-format or resynchronization.
 * This keeps the existing 4096-byte input buffer. minimp3 receives exactly
 * one complete frame, so its sync scanner cannot discard a partial frame.
 */
static FRESULT parse_header(const uint8_t *p, uint32_t *hz,
                           uint32_t *frame_bytes)
{
    static const uint16_t bitrate_kbps[16] = {
        0, 32, 40, 48, 56, 64, 80, 96,
        112, 128, 160, 192, 224, 256, 320, 0
    };
    static const uint32_t rates[3] = {44100U, 48000U, 32000U};

    mp3_debug.p = (uintptr_t)p;
    mp3_debug.frame_bytes = 0U;
    mp3_debug.hz = 0U;
    mp3_debug_mark(MP3_DBG_PARSE);
    if (!mp3_debug_bounds() || mp3.available < 4U ||
        (uintptr_t)p != (uintptr_t)mp3.input + mp3.position) {
        mp3_debug.error_reason = MP3_DBG_REASON_BOUNDS;
        mp3_debug_print(&mp3_debug);
        return MP3_DEBUG_RETURN(FR_INT_ERR);
    }
    mp3_debug.version_bits = (p[1] >> 3) & 3U;
    mp3_debug.layer_bits = (p[1] >> 1) & 3U;
    mp3_debug.bitrate_index = p[2] >> 4;
    mp3_debug.rate_index = (p[2] >> 2) & 3U;
    mp3_debug.padding = (p[2] >> 1) & 1U;
    mp3_debug.channel_mode = p[3] >> 6;
    if (p[0] != 0xFFU || (p[1] & 0xE0U) != 0xE0U) {
        mp3_debug.error_reason = MP3_DBG_REASON_BAD_SYNC;
        mp3_debug_print(&mp3_debug);
        return MP3_DEBUG_RETURN(FR_INT_ERR); /* BREAKPOINT HERE */
    }
    /* version == MPEG-1, layer == III */
    if ((p[1] & 0x1EU) != 0x1AU) {
        mp3_debug.error_reason = MP3_DBG_REASON_VERSION_LAYER;
        mp3_debug_print(&mp3_debug);
        return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);
    }

    uint32_t rate_index = (p[2] >> 2) & 3U;
    uint32_t bitrate = bitrate_kbps[p[2] >> 4];
    if (rate_index == 3U || bitrate == 0U) {
        mp3_debug.error_reason = MP3_DBG_REASON_RATE_BITRATE;
        mp3_debug_print(&mp3_debug);
        return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);
    }

    *hz = rates[rate_index];
    *frame_bytes = 144000U * bitrate / *hz + ((p[2] >> 1) & 1U);
    mp3_debug.hz = *hz;
    mp3_debug.frame_bytes = *frame_bytes;
    mp3_debug_mark(MP3_DBG_HEADER_OK);
    return MP3_DEBUG_RETURN(FR_OK);
}


FRESULT mp3_decoder_prepare(FIL *file)
{
    mp3_decoder_close();
    mp3_debug_zero(&mp3_debug, sizeof(mp3_debug));
    mp3_debug_zero(&mp3_debug_error, sizeof(mp3_debug_error));
    mp3_debug_zero(mp3_debug_history, sizeof(mp3_debug_history));
    mp3_debug_zero(mp3_debug_error_window, sizeof(mp3_debug_error_window));
    mp3_debug_error_valid = 0U;
    mp3_debug_history_next = mp3_debug_history_used = 0U;
    mp3_debug_error_window_count = mp3_debug_error_window_offset = 0U;
    mp3_debug_file = file;
    mp3_debug.phase = 1U;
    mp3_debug_mark(MP3_DBG_PREPARE);

    if (file == NULL)
        return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);

    FRESULT error = seek_to(file, 0U);
    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);

    uint8_t header[10] = {0};
    UINT received = 0U;

    mp3_debug_mark(MP3_DBG_ID3_BEFORE);
    error = f_read(file, header, sizeof(header), &received);
    mp3_debug.id3_received = received;
    mp3_debug.io_result = (int)error;
    for (uint32_t i = 0; i < sizeof(header); ++i)
        mp3_debug.id3_header[i] = header[i];
    mp3_debug_mark(MP3_DBG_ID3_AFTER);
    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);
    if (received != sizeof(header))
        return MP3_DEBUG_RETURN(FR_INT_ERR);

    FSIZE_t offset = 0U;

    if (memcmp(header, "ID3", 3U) == 0)
    {
        mp3_debug.id3_present = 1U;
        if (header[3] < 2U || header[3] > 4U ||
            header[4] == 0xFFU ||
            ((header[6] | header[7] |
              header[8] | header[9]) & 0x80U))
        {
            return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);
        }

        uint32_t size =
            ((uint32_t)header[6] << 21) |
            ((uint32_t)header[7] << 14) |
            ((uint32_t)header[8] << 7) |
            header[9];

        mp3_debug.id3_size = size;
        offset = 10U + size;

        /* У ID3v2.4 может быть дополнительный footer. */
        if (header[3] == 4U && (header[5] & 0x10U)) {
            mp3_debug.id3_footer = 1U;
            offset += 10U;
        }
    }

    mp3_debug.audio_offset = offset;
    mp3_debug.expected_frame_offset = offset;
    mp3_debug_mark(MP3_DBG_ID3_OFFSET);
    error = seek_to(file, offset);
    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);

    mp3dec_init(&mp3.decoder);

    mp3.file = file;

    error = refill();
    if (error != FR_OK)
    {
        (void)MP3_DEBUG_RETURN(error); /* Capture BEFORE cleanup. */
        mp3_decoder_close();
        return MP3_DEBUG_RETURN(error);
    }

    if (mp3.available < 6U)
    {
        (void)MP3_DEBUG_RETURN(FR_INT_ERR);
        mp3_decoder_close();
        return MP3_DEBUG_RETURN(FR_INT_ERR);
    }

    uint32_t hz, frame_bytes;
    mp3_debug_mark(MP3_DBG_PREPARE_HEADER);
    error = parse_header(mp3.input, &hz, &frame_bytes);
    if (error != FR_OK)
    {
        (void)MP3_DEBUG_RETURN(error); /* Capture BEFORE cleanup. */
        mp3_decoder_close();
        return MP3_DEBUG_RETURN(error);
    }
    if (frame_bytes > mp3.available)
    {
        (void)MP3_DEBUG_RETURN(FR_INT_ERR);
        mp3_decoder_close();
        return MP3_DEBUG_RETURN(FR_INT_ERR);
    }
    mp3.sample_rate = hz;

    /*
    * Первый кадр остаётся в input.
    * read_pcm() декодирует его обычным порядком.
    */
    return MP3_DEBUG_RETURN(FR_OK);
}


FRESULT mp3_decoder_read_pcm(
    uint8_t *destination,
    uint32_t capacity,
    audio_pcm_result_t *result
)
{
    mp3_debug.phase = 2U;
    mp3_debug.pcm_calls++;
    mp3_debug.destination = (uintptr_t)destination;
    mp3_debug.result_address = (uintptr_t)result;
    mp3_debug.capacity = capacity;
    mp3_debug.output_bytes = 0U;
    mp3_debug_mark(MP3_DBG_PCM_ENTER);
    if (result == NULL)
        return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);

    result->bytes = 0U;
    result->end = false;

    if (mp3.file == NULL)
        return MP3_DEBUG_RETURN(FR_NOT_READY);

    if (mp3.eof && mp3.available == 0U)
    {
        result->end = true;
        return MP3_DEBUG_RETURN(FR_OK);
    }

    if (destination == NULL ||
        ((uintptr_t)destination % _Alignof(mp3d_sample_t)) != 0U ||
        capacity < MP3_PCM_BYTES)
    {
        return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);
    }

    FRESULT error = refill();
    if (error != FR_OK)
        return MP3_DEBUG_RETURN(error);

    for (uint32_t frame = 0U; frame < MP3_FRAMES_PER_CALL; frame++)
    {
        mp3_debug.frame_index = frame;
        mp3_debug_mark(MP3_DBG_FRAME_BEGIN);
        if (!mp3_debug_bounds()) {
            mp3_debug.error_reason = MP3_DBG_REASON_BOUNDS;
            return MP3_DEBUG_RETURN(FR_INT_ERR);
        }
        uint8_t *input = mp3.input + mp3.position;

        /* ID3v1 занимает последние 128 байт файла. */
        if (mp3.eof && mp3.available == 128U &&
            memcmp(input, "TAG", 3U) == 0)
        {
            mp3.available = 0U;
        }

        if (mp3.available == 0U)
            break;

        if (capacity - result->bytes < MP3_PCM_BYTES)
            break;

        /* Заголовок плюс возможные два байта CRC. */
        if (mp3.available < 6U)
        {
            if (mp3.eof)
                return MP3_DEBUG_RETURN(FR_INT_ERR);
            break;
        }

        uint32_t hz, frame_bytes;
        error = parse_header(input, &hz, &frame_bytes);
        if (error != FR_OK)
            return MP3_DEBUG_RETURN(error);
        if (hz != mp3.sample_rate)
            return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);

        if (frame_bytes > MP3_INPUT_BYTES)
        {
            return MP3_DEBUG_RETURN(FR_INVALID_PARAMETER);
        }

        /* minimp3 получает ровно один полный кадр. */
        if (mp3.available < frame_bytes)
        {
            if (mp3.eof)
                return MP3_DEBUG_RETURN(FR_INT_ERR);
            break;
        }

        mp3d_sample_t *pcm = (mp3d_sample_t *)(destination + result->bytes);
        mp3dec_frame_info_t info = {0};
        mp3_debug.pcm = (uintptr_t)pcm;
        mp3_debug.frames_attempted++;
        mp3_debug.reservoir_before = mp3.decoder.reserv;
        mp3_debug_next(frame_bytes, 0);
        mp3_debug_mark(MP3_DBG_DECODE_BEFORE);
        int samples = mp3dec_decode_frame(
            &mp3.decoder, input, (int)frame_bytes, pcm, &info
        );

        mp3_debug.samples = samples;
        mp3_debug.decoded_bytes = info.frame_bytes;
        mp3_debug.decoded_offset = info.frame_offset;
        mp3_debug.decoded_hz = info.hz;
        mp3_debug.decoded_channels = info.channels;
        mp3_debug.decoded_layer = info.layer;
        mp3_debug.decoded_bitrate_kbps = info.bitrate_kbps;
        mp3_debug.reservoir_after = mp3.decoder.reserv;
        mp3_debug_next(frame_bytes, 1);
        mp3_debug_mark(MP3_DBG_DECODE_AFTER);
        if (info.frame_offset != 0 ||
            info.frame_bytes != (int)frame_bytes ||
            info.hz != (int)mp3.sample_rate || info.layer != 3 ||
            (info.channels != 1 && info.channels != 2))
        {
            return MP3_DEBUG_RETURN(FR_INT_ERR);
        }

        mp3.position += frame_bytes;
        mp3.available -= frame_bytes;
        mp3_debug.frames_consumed++;
        mp3_debug.expected_frame_offset += frame_bytes;
        mp3_debug_mark(MP3_DBG_CONSUMED);

        /* Zero samples may mean reservoir underflow OR a damaged frame.
         * minimp3 does not provide separate error codes for these cases.
         * Consume this frame and retain decoder state for the next one.
         */
        if (samples == 0) {
            mp3_debug.zero_sample_frames++;
            continue;
        }
        /* minimp3 returns samples PER CHANNEL, unlike Helix outputSamps. */
        if (samples != 1152)
            return MP3_DEBUG_RETURN(FR_INT_ERR);

        if (info.channels == 1)
        {
            /* Расширяем с конца, чтобы не затереть исходное моно. */
            for (int i = 1151; i >= 0; i--)
            {
                mp3d_sample_t sample = pcm[i];
                pcm[2 * i] = sample;
                pcm[2 * i + 1] = sample;
            }
        }

        result->bytes += MP3_PCM_BYTES;
        mp3_debug.output_bytes = result->bytes;
    }

    mp3_debug_mark(MP3_DBG_PCM_EXIT);
    result->end = mp3.eof && mp3.available == 0U;
    return MP3_DEBUG_RETURN(FR_OK);
}

uint32_t mp3_decoder_get_sample_rate(void)
{
    return mp3.sample_rate;
}


void mp3_decoder_close(void)
{
    mp3_debug_file = NULL; /* Never access a stale FIL on a later close. */
    mp3_debug_mark(MP3_DBG_CLOSE);
    mp3dec_init(&mp3.decoder);
    mp3.file = NULL;
    mp3.position = 0U;
    mp3.available = 0U;
    mp3.sample_rate = 0U;
    mp3.eof = false;
}