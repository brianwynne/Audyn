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

/* Maximum reasonable channel count (8.1 surround is 9 channels) */
#define WAV_MAX_CHANNELS 32

/* Maximum reasonable sample rate (384kHz is high-end pro audio) */
#define WAV_MAX_SAMPLE_RATE 384000

struct audyn_wav_sink {
    audyn_wav_sink_cfg_t cfg;
    FILE    *fp;
    int      fd;                /* File descriptor for fsync */
    char    *path;              /* Output path (for error messages) */
    uint32_t sample_rate;
    uint16_t channels;
    uint64_t bytes_written;     /* data chunk bytes written */

    /* Statistics */
    audyn_wav_stats_t stats;
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
    if (!s) {
        LOG_ERROR("WAV: Failed to allocate sink structure");
        return NULL;
    }

    if (cfg) {
        s->cfg = *cfg;
    } else {
        s->cfg.format = AUDYN_WAV_PCM16;
        s->cfg.enable_fsync = 0;
    }

    s->fd = -1;
    memset(&s->stats, 0, sizeof(s->stats));

    return s;
}

void audyn_wav_sink_destroy(audyn_wav_sink_t *s)
{
    if (!s) return;
    if (s->fp) (void)audyn_wav_sink_close(s);
    if (s->path) {
        free(s->path);
        s->path = NULL;
    }
    free(s);
}

int audyn_wav_sink_open(audyn_wav_sink_t *s,
                        const char *path,
                        uint32_t sample_rate,
                        uint16_t channels)
{
    if (!s) {
        LOG_ERROR("WAV: NULL sink");
        return -1;
    }
    if (!path || !*path) {
        LOG_ERROR("WAV: NULL or empty path");
        return -1;
    }
    if (sample_rate == 0 || sample_rate > WAV_MAX_SAMPLE_RATE) {
        LOG_ERROR("WAV: Invalid sample rate %u (must be 1-%d)", sample_rate, WAV_MAX_SAMPLE_RATE);
        return -1;
    }
    if (channels == 0 || channels > WAV_MAX_CHANNELS) {
        LOG_ERROR("WAV: Invalid channel count %u (must be 1-%d)", channels, WAV_MAX_CHANNELS);
        return -1;
    }

    /* Close any previously-open file (defensive). */
    if (s->fp) (void)audyn_wav_sink_close(s);

    /* Free old path if any */
    if (s->path) {
        free(s->path);
        s->path = NULL;
    }

    if (s->cfg.format != AUDYN_WAV_PCM16) {
        LOG_ERROR("WAV: Unsupported format %d", s->cfg.format);
        return -1;
    }

    /* Store path for error messages */
    s->path = strdup(path);
    if (!s->path) {
        LOG_ERROR("WAV: Failed to allocate path string");
        return -1;
    }

    s->fp = fopen(path, "wb");
    if (!s->fp) {
        LOG_ERROR("WAV: Failed to open '%s': %s", path, strerror(errno));
        free(s->path);
        s->path = NULL;
        return -1;
    }

    /* Get file descriptor for fsync */
    s->fd = fileno(s->fp);
    if (s->fd < 0) {
        LOG_DEBUG("WAV: fileno() returned -1, fsync disabled");
        s->cfg.enable_fsync = 0;
    }

    s->sample_rate = sample_rate;
    s->channels = channels;
    s->bytes_written = 0;

    /* Reset statistics */
    memset(&s->stats, 0, sizeof(s->stats));

    if (write_header_placeholder(s) != 0) {
        LOG_ERROR("WAV: Failed to write header to '%s'", path);
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    LOG_INFO("WAV: Opened '%s' - %uHz %uch PCM16", path, sample_rate, channels);

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
    if (!s || !s->fp) {
        LOG_ERROR("WAV: Write called on NULL or closed sink");
        return -1;
    }
    if (!interleaved_f32) {
        LOG_ERROR("WAV: NULL audio data");
        return -1;
    }

    if (channels != s->channels) {
        LOG_ERROR("WAV: Channel mismatch (expected %u, got %u)", s->channels, channels);
        return -1;
    }

    if (frames == 0)
        return 0;

    /* Check for potential overflow: frames * channels must fit in size_t */
    if (frames > SIZE_MAX / channels) {
        LOG_ERROR("WAV: Frame count overflow (%u frames * %u channels)", frames, channels);
        return -1;
    }

    const size_t samples = (size_t)frames * (size_t)channels;

    /* RIFF/WAVE classic size limit: data chunk size must fit in uint32. */
    const uint64_t add_bytes = (uint64_t)samples * (uint64_t)sizeof(int16_t);
    if (s->bytes_written + add_bytes > 0xFFFFFFFFull) {
        LOG_ERROR("WAV: Size limit exceeded for '%s' (needs RF64)",
                  s->path ? s->path : "(unknown)");
        s->stats.size_limit_hit = 1;
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
            LOG_ERROR("WAV: Write failed for '%s': %s",
                      s->path ? s->path : "(unknown)", strerror(errno));
            return -1;
        }

        s->bytes_written += (uint64_t)(n * sizeof(int16_t));
        i += n;
    }

    /* Update statistics */
    s->stats.frames_written += frames;
    s->stats.bytes_written = s->bytes_written;

    if (s->cfg.enable_fsync)
        return audyn_wav_sink_sync(s);

    return 0;
}

int audyn_wav_sink_sync(audyn_wav_sink_t *s)
{
    if (!s || !s->fp)
        return -1;

    if (fflush(s->fp) != 0) {
        LOG_ERROR("WAV: fflush failed for '%s': %s",
                  s->path ? s->path : "(unknown)", strerror(errno));
        return -1;
    }

    if (s->fd >= 0) {
        if (fsync(s->fd) != 0) {
            LOG_ERROR("WAV: fsync failed for '%s': %s",
                      s->path ? s->path : "(unknown)", strerror(errno));
            return -1;
        }
    }

    return 0;
}

int audyn_wav_sink_close(audyn_wav_sink_t *s)
{
    if (!s || !s->fp)
        return -1;

    const char *path = s->path ? s->path : "(unknown)";

    if (fflush(s->fp) != 0) {
        LOG_ERROR("WAV: fflush failed for '%s': %s", path, strerror(errno));
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    if (s->bytes_written > 0xFFFFFFFFull) {
        LOG_ERROR("WAV: Size limit exceeded for '%s'", path);
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    const uint32_t data_size = (uint32_t)s->bytes_written;

    /* RIFF size = 4 ("WAVE") + (8+fmt) + (8+data) */
    const uint32_t riff_size = 4u + (8u + 16u) + (8u + data_size);

    /* Patch RIFF size at offset 4. */
    if (fseek(s->fp, 4L, SEEK_SET) != 0 || write_u32le(s->fp, riff_size) != 0) {
        LOG_ERROR("WAV: Failed to patch RIFF size for '%s'", path);
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    /* Patch data size at offset 40 (PCM16, no extra chunks). */
    if (fseek(s->fp, 40L, SEEK_SET) != 0 || write_u32le(s->fp, data_size) != 0) {
        LOG_ERROR("WAV: Failed to patch data size for '%s'", path);
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    if (fflush(s->fp) != 0) {
        LOG_ERROR("WAV: fflush after patch failed for '%s': %s", path, strerror(errno));
        fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
        return -1;
    }

    /* fsync after patching if enabled */
    if (s->cfg.enable_fsync && s->fd >= 0) {
        if (fsync(s->fd) != 0) {
            LOG_ERROR("WAV: fsync failed for '%s': %s", path, strerror(errno));
            /* Continue to close anyway */
        }
    }

    fclose(s->fp);
    s->fp = NULL;
    s->fd = -1;

    LOG_DEBUG("WAV: Closed '%s' - frames=%lu bytes=%lu",
              path,
              (unsigned long)s->stats.frames_written,
              (unsigned long)s->stats.bytes_written);

    return 0;
}

void audyn_wav_sink_get_stats(const audyn_wav_sink_t *s, audyn_wav_stats_t *stats)
{
    if (!s || !stats) return;
    *stats = s->stats;
}
