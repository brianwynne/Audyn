/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      wav_sink.c
 *
 *  Purpose:
 *      Implements a minimal PCM16 WAV writer (RIFF/WAVE).
 *
 *  Notes:
 *      - Input samples: float32 interleaved (-1..+1) (values are clamped)
 *      - Output: little-endian PCM16
 *      - Classic RIFF/WAVE has a < 4 GiB data limit; we enforce it.
 *
 *  Dependencies:
 *      - POSIX: fileno(), fsync()
 *      - Standard C: stdio.h, stdint.h, errno.h
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  Author:
 *      B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#include "wav_sink.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct audyn_wav_sink {
    audyn_wav_sink_cfg_t cfg;
    FILE    *fp;
    uint32_t sample_rate;
    uint16_t channels;
    uint64_t bytes_written; /* data chunk bytes written (capped to UINT32_MAX) */
};

static int write_u16le(FILE *fp, uint16_t v)
{
    const unsigned char b[2] = {
        (unsigned char)(v & 0xffu),
        (unsigned char)((v >> 8) & 0xffu)
    };
    return (fwrite(b, 1, 2, fp) == 2) ? 0 : -1;
}

static int write_u32le(FILE *fp, uint32_t v)
{
    const unsigned char b[4] = {
        (unsigned char)(v & 0xffu),
        (unsigned char)((v >> 8) & 0xffu),
        (unsigned char)((v >> 16) & 0xffu),
        (unsigned char)((v >> 24) & 0xffu)
    };
    return (fwrite(b, 1, 4, fp) == 4) ? 0 : -1;
}

static int write_header_placeholder(audyn_wav_sink_t *s)
{
    /* RIFF header */
    if (fwrite("RIFF", 1, 4, s->fp) != 4) return -1;
    if (write_u32le(s->fp, 0) != 0) return -1; /* riff size placeholder */
    if (fwrite("WAVE", 1, 4, s->fp) != 4) return -1;

    /* fmt chunk */
    if (fwrite("fmt ", 1, 4, s->fp) != 4) return -1;
    if (write_u32le(s->fp, 16) != 0) return -1;          /* PCM fmt chunk size */
    if (write_u16le(s->fp, 1) != 0) return -1;           /* PCM */
    if (write_u16le(s->fp, s->channels) != 0) return -1; /* channels */
    if (write_u32le(s->fp, s->sample_rate) != 0) return -1;

    const uint16_t bits = 16;
    const uint32_t byte_rate = s->sample_rate * (uint32_t)s->channels * (bits / 8u);
    const uint16_t block_align = (uint16_t)(s->channels * (bits / 8u));

    if (write_u32le(s->fp, byte_rate) != 0) return -1;
    if (write_u16le(s->fp, block_align) != 0) return -1;
    if (write_u16le(s->fp, bits) != 0) return -1;

    /* data chunk */
    if (fwrite("data", 1, 4, s->fp) != 4) return -1;
    if (write_u32le(s->fp, 0) != 0) return -1; /* data size placeholder */

    return ferror(s->fp) ? -1 : 0;
}

audyn_wav_sink_t *audyn_wav_sink_create(const audyn_wav_sink_cfg_t *cfg)
{
    audyn_wav_sink_t *s = (audyn_wav_sink_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    if (cfg) {
        s->cfg = *cfg;
    } else {
        s->cfg.format = AUDYN_WAV_PCM16;
        s->cfg.enable_fsync = 0;
    }

    return s;
}

void audyn_wav_sink_destroy(audyn_wav_sink_t *s)
{
    if (!s) return;
    if (s->fp) (void)audyn_wav_sink_close(s);
    free(s);
}

int audyn_wav_sink_open(audyn_wav_sink_t *s,
                        const char *path,
                        uint32_t sample_rate,
                        uint16_t channels)
{
    if (!s || !path || !*path || sample_rate == 0 || channels == 0)
        return -1;

    /* Close any previously-open file (defensive). */
    if (s->fp) (void)audyn_wav_sink_close(s);

    if (s->cfg.format != AUDYN_WAV_PCM16) {
        LOG_ERROR("wav_sink: unsupported format");
        return -1;
    }

    s->fp = fopen(path, "wb");
    if (!s->fp) {
        LOG_ERROR("wav_sink open failed: %s", strerror(errno));
        return -1;
    }

    s->sample_rate = sample_rate;
    s->channels = channels;
    s->bytes_written = 0;

    if (write_header_placeholder(s) != 0) {
        LOG_ERROR("wav_sink failed to write header");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    return 0;
}

static inline int16_t f32_to_i16(float x)
{
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;

    /* Use 32767 scaling so +1.0 maps to INT16_MAX. */
    int v = (int)(x * 32767.0f);

    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

int audyn_wav_sink_write(audyn_wav_sink_t *s,
                         const float *interleaved_f32,
                         uint32_t frames,
                         uint16_t channels)
{
    if (!s || !s->fp || !interleaved_f32)
        return -1;

    if (channels != s->channels)
        return -1;

    if (frames == 0)
        return 0;

    /* Compute sample count in size_t to avoid overflow on multiplication. */
    const size_t samples = (size_t)frames * (size_t)channels;

    /* RIFF/WAVE classic size limit: data chunk size must fit in uint32. */
    const uint64_t add_bytes = (uint64_t)samples * (uint64_t)sizeof(int16_t);
    if (s->bytes_written + add_bytes > 0xFFFFFFFFull) {
        LOG_ERROR("wav_sink: WAV size limit exceeded (needs RF64)");
        return -1;
    }

    enum { CHUNK = 4096 };
    size_t i = 0;
    int16_t tmp[CHUNK];

    while (i < samples) {
        size_t n = samples - i;
        if (n > CHUNK) n = CHUNK;

        for (size_t j = 0; j < n; ++j) {
            tmp[j] = f32_to_i16(interleaved_f32[i + j]);
        }

        const size_t wrote = fwrite(tmp, sizeof(int16_t), n, s->fp);
        if (wrote != n) {
            LOG_ERROR("wav_sink write failed: %s", strerror(errno));
            return -1;
        }

        s->bytes_written += (uint64_t)(n * sizeof(int16_t));
        i += n;
    }

    if (s->cfg.enable_fsync)
        return audyn_wav_sink_sync(s);

    return 0;
}

int audyn_wav_sink_sync(audyn_wav_sink_t *s)
{
    if (!s || !s->fp)
        return -1;

    if (fflush(s->fp) != 0)
        return -1;

    const int fd = fileno(s->fp);
    if (fd < 0)
        return -1;

    return (fsync(fd) == 0) ? 0 : -1;
}

int audyn_wav_sink_close(audyn_wav_sink_t *s)
{
    if (!s || !s->fp)
        return -1;

    if (fflush(s->fp) != 0) {
        LOG_ERROR("wav_sink: fflush failed");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    if (s->bytes_written > 0xFFFFFFFFull) {
        LOG_ERROR("wav_sink: WAV size limit exceeded");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    const uint32_t data_size = (uint32_t)s->bytes_written;

    /* RIFF size = 4 ("WAVE") + (8+fmt) + (8+data) */
    const uint32_t riff_size = 4u + (8u + 16u) + (8u + data_size);

    /* Patch RIFF size at offset 4. */
    if (fseek(s->fp, 4L, SEEK_SET) != 0 || write_u32le(s->fp, riff_size) != 0) {
        LOG_ERROR("wav_sink: failed to patch RIFF size");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    /* Patch data size at offset 40 (PCM16, no extra chunks). */
    if (fseek(s->fp, 40L, SEEK_SET) != 0 || write_u32le(s->fp, data_size) != 0) {
        LOG_ERROR("wav_sink: failed to patch data size");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    if (fflush(s->fp) != 0) {
        LOG_ERROR("wav_sink: fflush after patch failed");
        fclose(s->fp);
        s->fp = NULL;
        return -1;
    }

    fclose(s->fp);
    s->fp = NULL;
    return 0;
}
