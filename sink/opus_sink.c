\
/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      opus_sink.c
 *
 *  Purpose:
 *      Implements a non-real-time sink that encodes interleaved float32 PCM audio
 *      into an Ogg Opus (.opus) file using libopus and libogg.
 *
 *  Key Properties:
 *      - Non-RT only (intended to run in Audyn worker thread)
 *      - Minimal external deps: libopus + libogg only
 *      - Writes standards-compliant OpusHead/OpusTags headers
 *      - Accepts arbitrary block sizes; internally buffers and encodes fixed 20ms frames
 *
 *  Notes on framing:
 *      Opus accepts only certain frame sizes (2.5/5/10/20/40/60ms). Audyn frames may
 *      be different (e.g. 1024). This sink buffers input and encodes fixed 20ms frames:
 *          frame_size = sample_rate / 50
 *      This is valid for Opus' supported sample rates (8/12/16/24/48 kHz).
 *
 *  Ogg Opus granulepos:
 *      Ogg Opus granulepos is expressed in 48 kHz sample units and represents the total
 *      number of decoded samples *after* pre-skip. We therefore track granulepos starting
 *      at -preskip and add frame_samples_48k for each encoded frame. The first audio packet
 *      thus typically has granulepos = 960 - 312 = 648 (for 20 ms frames).
 *
 *  Dependencies:
 *      - Standard C/POSIX: stdio, stdlib, string, time, unistd
 *      - libopus: <opus/opus.h>
 *      - libogg:  <ogg/ogg.h>
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

#include "opus_sink.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <ogg/ogg.h>
#include <opus/opus.h>

/* Audyn's Opus bitrate limits (bits per second) */
#define AUDYN_OPUS_BITRATE_MIN 6000
#define AUDYN_OPUS_BITRATE_MAX 510000

/* Maximum FIFO size in frames to prevent runaway memory usage */
#define FIFO_MAX_FRAMES (48000 * 10)  /* 10 seconds at 48kHz */

struct audyn_opus_sink
{
    FILE *fp;
    int fd;

    audyn_opus_cfg_t cfg;
    char *path;                 /* Output file path (for error messages) */

    ogg_stream_state os;
    int ogg_inited;

    OpusEncoder *enc;

    /* Opus packet buffer (reused; ogg_stream_packetin copies data internally) */
    unsigned char *pkt;
    int pkt_cap;

    /* Input accumulator (interleaved float32) */
    float *fifo;
    uint32_t fifo_cap_frames;   /* capacity in frames (per channel) */
    uint32_t fifo_len_frames;   /* current length in frames */
    uint32_t frame_size;        /* encoder frame size in frames (per channel), e.g. 960 @ 48k */

    /* Ogg packet numbering / granule position (always in 48kHz units for Ogg Opus) */
    ogg_int64_t granulepos_48k;
    ogg_int64_t packetno;

    /* OpusHead preskip (48k units) */
    uint16_t preskip_48k;

    /* Tracking for clean EOS */
    int wrote_audio;
    int eos_written;

    int closed;

    /* Statistics */
    audyn_opus_stats_t stats;
};

static void le16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint32_t choose_frame_size(uint32_t sample_rate)
{
    /* Prefer 20ms frames. Valid for Opus supported Fs. */
    return sample_rate / 50;
}

static int opus_frame_size_is_valid(uint32_t sample_rate, uint32_t frame_size)
{
    /* Valid sizes are: Fs/400, Fs/200, Fs/100, Fs/50, Fs/25, Fs*3/50 */
    const uint32_t a = sample_rate / 400;
    const uint32_t b = sample_rate / 200;
    const uint32_t c = sample_rate / 100;
    const uint32_t d = sample_rate / 50;
    const uint32_t e = sample_rate / 25;
    const uint32_t f = (sample_rate * 3) / 50;

    return (frame_size == a || frame_size == b || frame_size == c ||
            frame_size == d || frame_size == e || frame_size == f);
}

static ogg_uint32_t make_serial(void)
{
    ogg_uint32_t r = 0;

    /* Try /dev/urandom first for proper randomness */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, &r, sizeof(r));
        close(fd);
        if (n == sizeof(r) && r != 0) {
            return r;
        }
    }

    /* Fallback: time xor pid with better mixing */
    ogg_uint32_t t = (ogg_uint32_t)time(NULL);
    ogg_uint32_t p = (ogg_uint32_t)getpid();
    r = (t << 16) ^ (t >> 16) ^ (p * 2654435761u);
    if (r == 0) r = 1;
    return r;
}

static int write_page(struct audyn_opus_sink *s, const ogg_page *og)
{
    if (!s || !s->fp || !og) return -1;

    if (fwrite(og->header, 1, (size_t)og->header_len, s->fp) != (size_t)og->header_len)
        return -1;
    if (fwrite(og->body, 1, (size_t)og->body_len, s->fp) != (size_t)og->body_len)
        return -1;

    if (s->cfg.enable_fsync) {
        (void)fflush(s->fp);
        if (s->fd >= 0) (void)fsync(s->fd);
    }

    return 0;
}

static int flush_pages(struct audyn_opus_sink *s, int force_flush)
{
    ogg_page og;

    if (!s || !s->ogg_inited) return -1;

    while (1) {
        int out = force_flush ? ogg_stream_flush(&s->os, &og)
                              : ogg_stream_pageout(&s->os, &og);
        if (out == 0) break;

        if (write_page(s, &og) != 0) return -1;
    }

    return 0;
}

static int write_opus_headers(struct audyn_opus_sink *s)
{
    /* --- OpusHead (per Ogg Opus spec) --- */
    unsigned char head[19];
    memset(head, 0, sizeof(head));
    memcpy(head + 0, "OpusHead", 8);
    head[8] = 1; /* version */
    head[9] = (unsigned char)s->cfg.channels;

    /* pre-skip in 48kHz samples. 312 is common and recommended. */
    s->preskip_48k = 312;
    le16(head + 10, s->preskip_48k);

    /* original input sample rate (informational) */
    le32(head + 12, s->cfg.sample_rate);

    /* output gain (Q8 dB), 0 */
    le16(head + 16, 0);

    /* channel mapping family: 0 for mono/stereo */
    head[18] = 0;

    ogg_packet op;
    memset(&op, 0, sizeof(op));
    op.packet = head;
    op.bytes = (long)sizeof(head);
    op.b_o_s = 1;
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = s->packetno++;

    if (ogg_stream_packetin(&s->os, &op) != 0)
        return -1;

    if (flush_pages(s, 1) != 0)
        return -1;

    /* --- OpusTags --- */
    const char *vendor = opus_get_version_string();
    if (!vendor) vendor = "libopus";

    const char *comment = "ENCODER=Audyn";
    const uint32_t vendor_len = (uint32_t)strlen(vendor);
    const uint32_t user_comment_count = 1;
    const uint32_t comment_len = (uint32_t)strlen(comment);

    /* Packet layout:
       "OpusTags" (8)
       vendor_len (LE32)
       vendor_string (vendor_len)
       user_comment_list_length (LE32)
       comment_len (LE32)
       comment_string (comment_len)
    */
    const size_t tags_size = 8 + 4 + vendor_len + 4 + 4 + comment_len;

    unsigned char *tags = (unsigned char *)malloc(tags_size);
    if (!tags) return -1;

    unsigned char *p = tags;
    memcpy(p, "OpusTags", 8); p += 8;
    le32(p, vendor_len); p += 4;
    memcpy(p, vendor, vendor_len); p += vendor_len;
    le32(p, user_comment_count); p += 4;
    le32(p, comment_len); p += 4;
    memcpy(p, comment, comment_len); p += comment_len;

    memset(&op, 0, sizeof(op));
    op.packet = tags;
    op.bytes = (long)tags_size;
    op.b_o_s = 0;
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = s->packetno++;

    const int ok = (ogg_stream_packetin(&s->os, &op) == 0);
    free(tags);
    if (!ok) return -1;

    if (flush_pages(s, 1) != 0)
        return -1;

    /* Initialize granulepos tracker with pre-skip offset */
    s->granulepos_48k = -(ogg_int64_t)s->preskip_48k;

    return 0;
}

static int ensure_fifo_capacity(struct audyn_opus_sink *s, uint32_t want_frames)
{
    if (want_frames <= s->fifo_cap_frames)
        return 0;

    uint32_t new_cap = s->fifo_cap_frames ? s->fifo_cap_frames : (s->frame_size * 2);
    while (new_cap < want_frames) {
        if (new_cap < (1u << 20)) new_cap *= 2;
        else { new_cap = want_frames; break; }
    }

    const size_t samples = (size_t)new_cap * (size_t)s->cfg.channels;
    float *nf = (float *)realloc(s->fifo, samples * sizeof(float));
    if (!nf) return -1;

    s->fifo = nf;
    s->fifo_cap_frames = new_cap;
    return 0;
}

static void fifo_consume_frames(struct audyn_opus_sink *s, uint32_t frames)
{
    if (frames == 0 || frames > s->fifo_len_frames) return;

    const size_t ch = (size_t)s->cfg.channels;
    const size_t remain_frames = (size_t)(s->fifo_len_frames - frames);
    if (remain_frames > 0) {
        memmove(s->fifo,
                s->fifo + (size_t)frames * ch,
                remain_frames * ch * sizeof(float));
    }
    s->fifo_len_frames -= frames;
}

static ogg_int64_t frames_to_48k(uint32_t frames, uint32_t sample_rate)
{
    /* For the supported rates and 20ms frames, this is exact. */
    return (ogg_int64_t)((uint64_t)frames * 48000ull / (uint64_t)sample_rate);
}

audyn_opus_sink_t *
audyn_opus_sink_create(const char *path, const audyn_opus_cfg_t *cfg)
{
    if (!path || !cfg) {
        LOG_ERROR("OPUS: NULL path or config");
        return NULL;
    }
    if (!(cfg->channels == 1 || cfg->channels == 2)) {
        LOG_ERROR("OPUS: Invalid channel count %u (must be 1 or 2)", cfg->channels);
        return NULL;
    }

    /* Opus supports these sample rates. We accept only those to avoid hidden resampling. */
    const uint32_t sr = cfg->sample_rate;
    if (!(sr == 8000 || sr == 12000 || sr == 16000 || sr == 24000 || sr == 48000)) {
        LOG_ERROR("OPUS: Unsupported sample rate %u (must be 8000/12000/16000/24000/48000)", sr);
        return NULL;
    }

    audyn_opus_sink_t *s = (audyn_opus_sink_t *)calloc(1, sizeof(*s));
    if (!s) {
        LOG_ERROR("OPUS: Failed to allocate sink structure");
        return NULL;
    }

    s->cfg = *cfg;
    s->fd = -1;

    /* Store path for error messages */
    s->path = strdup(path);
    if (!s->path) {
        LOG_ERROR("OPUS: Failed to allocate path string");
        free(s);
        return NULL;
    }

    if (s->cfg.complexity < 0) s->cfg.complexity = 5;
    if (s->cfg.complexity > 10) s->cfg.complexity = 10;

    /* Validate and default bitrate */
    if (s->cfg.bitrate == 0) {
        s->cfg.bitrate = (s->cfg.channels == 1) ? 64000u : 96000u;
    } else if (s->cfg.bitrate < AUDYN_OPUS_BITRATE_MIN) {
        LOG_INFO("OPUS: Clamping bitrate %u to minimum %d", s->cfg.bitrate, AUDYN_OPUS_BITRATE_MIN);
        s->cfg.bitrate = AUDYN_OPUS_BITRATE_MIN;
    } else if (s->cfg.bitrate > AUDYN_OPUS_BITRATE_MAX) {
        LOG_INFO("OPUS: Clamping bitrate %u to maximum %d", s->cfg.bitrate, AUDYN_OPUS_BITRATE_MAX);
        s->cfg.bitrate = AUDYN_OPUS_BITRATE_MAX;
    }

    if (s->cfg.application != AUDYN_OPUS_APP_VOIP &&
        s->cfg.application != AUDYN_OPUS_APP_AUDIO &&
        s->cfg.application != AUDYN_OPUS_APP_RESTRICTED_LOWDELAY) {
        s->cfg.application = AUDYN_OPUS_APP_AUDIO;
    }

    s->frame_size = choose_frame_size(sr);
    if (!opus_frame_size_is_valid(sr, s->frame_size)) {
        LOG_ERROR("OPUS: Invalid frame size %u for sample rate %u", s->frame_size, sr);
        free(s->path);
        free(s);
        return NULL;
    }

    /* Open file */
    s->fp = fopen(path, "wb");
    if (!s->fp) {
        LOG_ERROR("OPUS: Failed to open file '%s' for writing", path);
        free(s->path);
        free(s);
        return NULL;
    }

    /* Get file descriptor for fsync, validate it */
    s->fd = fileno(s->fp);
    if (s->fd < 0) {
        LOG_DEBUG("OPUS: fileno() returned -1, fsync will be disabled");
        s->cfg.enable_fsync = 0;
    }

    /* Init ogg stream */
    if (ogg_stream_init(&s->os, (int)make_serial()) != 0) {
        LOG_ERROR("OPUS: Failed to initialize Ogg stream for '%s'", path);
        fclose(s->fp);
        free(s->path);
        free(s);
        return NULL;
    }
    s->ogg_inited = 1;
    s->packetno = 0;
    s->granulepos_48k = 0;

    /* Init Opus encoder */
    int err = OPUS_OK;
    s->enc = opus_encoder_create((opus_int32)sr,
                                 (int)s->cfg.channels,
                                 (int)s->cfg.application,
                                 &err);
    if (!s->enc || err != OPUS_OK) {
        LOG_ERROR("OPUS: Failed to create encoder: %s", opus_strerror(err));
        ogg_stream_clear(&s->os);
        fclose(s->fp);
        free(s->path);
        free(s);
        return NULL;
    }

    /* Configure encoder */
    (void)opus_encoder_ctl(s->enc, OPUS_SET_BITRATE((opus_int32)s->cfg.bitrate));
    (void)opus_encoder_ctl(s->enc, OPUS_SET_VBR(s->cfg.vbr ? 1 : 0));
    (void)opus_encoder_ctl(s->enc, OPUS_SET_COMPLEXITY(s->cfg.complexity));

    /* Packet buffer: maximum payload is small, but allow headroom */
    s->pkt_cap = 4096;
    s->pkt = (unsigned char *)malloc((size_t)s->pkt_cap);
    if (!s->pkt) {
        LOG_ERROR("OPUS: Failed to allocate packet buffer");
        opus_encoder_destroy(s->enc);
        ogg_stream_clear(&s->os);
        fclose(s->fp);
        free(s->path);
        free(s);
        return NULL;
    }

    /* FIFO buffer: start at 2x frame_size */
    s->fifo = NULL;
    s->fifo_cap_frames = 0;
    s->fifo_len_frames = 0;
    if (ensure_fifo_capacity(s, s->frame_size * 2) != 0) {
        LOG_ERROR("OPUS: Failed to allocate FIFO buffer");
        free(s->pkt);
        opus_encoder_destroy(s->enc);
        ogg_stream_clear(&s->os);
        fclose(s->fp);
        free(s->path);
        free(s);
        return NULL;
    }

    /* Write Ogg Opus headers */
    if (write_opus_headers(s) != 0) {
        LOG_ERROR("OPUS: Failed to write Ogg Opus headers to '%s'", path);
        audyn_opus_sink_destroy(s);
        return NULL;
    }

    /* Initialize statistics */
    memset(&s->stats, 0, sizeof(s->stats));

    LOG_INFO("OPUS: Created sink '%s' - %uHz %uch %ubps %s complexity=%d",
             path, sr, s->cfg.channels, s->cfg.bitrate,
             s->cfg.vbr ? "VBR" : "CBR", s->cfg.complexity);

    return s;
}

int
audyn_opus_sink_write(audyn_opus_sink_t *s,
                      const float *interleaved_f32,
                      uint32_t frames)
{
    if (!s || s->closed) {
        LOG_ERROR("OPUS: Write called on NULL or closed sink");
        return -1;
    }
    if (!interleaved_f32 || frames == 0) return 0;

    /* Check for integer overflow */
    if (frames > UINT32_MAX - s->fifo_len_frames) {
        LOG_ERROR("OPUS: FIFO length overflow (current=%u, adding=%u)",
                  s->fifo_len_frames, frames);
        return -1;
    }

    const uint32_t new_len = s->fifo_len_frames + frames;

    /* Enforce maximum FIFO size to prevent runaway memory usage */
    if (new_len > FIFO_MAX_FRAMES) {
        LOG_ERROR("OPUS: FIFO size limit exceeded (requested=%u, max=%d)",
                  new_len, FIFO_MAX_FRAMES);
        s->stats.fifo_overflows++;
        return -1;
    }

    if (ensure_fifo_capacity(s, new_len) != 0) {
        LOG_ERROR("OPUS: Failed to expand FIFO to %u frames", new_len);
        return -1;
    }

    const size_t ch = (size_t)s->cfg.channels;
    memcpy(s->fifo + (size_t)s->fifo_len_frames * ch,
           interleaved_f32,
           (size_t)frames * ch * sizeof(float));
    s->fifo_len_frames = new_len;

    /* Track input frames */
    s->stats.frames_in += frames;

    /* Encode while we have a full Opus frame */
    while (s->fifo_len_frames >= s->frame_size) {
        const float *pcm = s->fifo; /* interleaved */

        const int nb = opus_encode_float(s->enc,
                                        pcm,
                                        (int)s->frame_size,
                                        s->pkt,
                                        s->pkt_cap);
        if (nb < 0) {
            LOG_ERROR("OPUS: Encode failed: %s", opus_strerror(nb));
            return -1;
        }

        ogg_packet op;
        memset(&op, 0, sizeof(op));
        op.packet = s->pkt;
        op.bytes = nb;
        op.b_o_s = 0;
        op.e_o_s = 0;

        s->granulepos_48k += frames_to_48k(s->frame_size, s->cfg.sample_rate);
        if (s->granulepos_48k < 0) s->granulepos_48k = 0; /* should only happen for first packet */
        op.granulepos = s->granulepos_48k;
        op.packetno = s->packetno++;

        if (ogg_stream_packetin(&s->os, &op) != 0) {
            LOG_ERROR("OPUS: Failed to submit packet to Ogg stream");
            return -1;
        }

        s->wrote_audio = 1;

        /* Update statistics */
        s->stats.frames_encoded += s->frame_size;
        s->stats.packets_encoded++;
        s->stats.bytes_encoded += (uint64_t)nb;

        if (flush_pages(s, 0) != 0) {
            LOG_ERROR("OPUS: Failed to flush Ogg pages");
            return -1;
        }

        fifo_consume_frames(s, s->frame_size);
    }

    return 0;
}

int
audyn_opus_sink_flush(audyn_opus_sink_t *s)
{
    if (!s || s->closed) return -1;
    return flush_pages(s, 1);
}

static int write_eos_marker(struct audyn_opus_sink *s)
{
    if (s->eos_written) return 0;

    /* Write an empty Ogg packet with EOS flag set.
       This is widely accepted as an end marker for Ogg streams and avoids adding silence. */
    ogg_packet op;
    memset(&op, 0, sizeof(op));
    op.packet = (unsigned char *)""; /* non-NULL */
    op.bytes = 0;
    op.b_o_s = 0;
    op.e_o_s = 1;
    op.granulepos = s->granulepos_48k < 0 ? 0 : s->granulepos_48k;
    op.packetno = s->packetno++;

    if (ogg_stream_packetin(&s->os, &op) != 0)
        return -1;

    s->eos_written = 1;
    return flush_pages(s, 1);
}

static int pad_and_encode_final(struct audyn_opus_sink *s)
{
    if (s->fifo_len_frames == 0)
        return 0;

    /* Pad to a full frame with zeros */
    const uint32_t need = s->frame_size - s->fifo_len_frames;
    const uint32_t new_len = s->fifo_len_frames + need;
    if (ensure_fifo_capacity(s, new_len) != 0) return -1;

    const size_t ch = (size_t)s->cfg.channels;
    memset(s->fifo + (size_t)s->fifo_len_frames * ch, 0, (size_t)need * ch * sizeof(float));
    s->fifo_len_frames = new_len;

    const int nb = opus_encode_float(s->enc,
                                    s->fifo,
                                    (int)s->frame_size,
                                    s->pkt,
                                    s->pkt_cap);
    if (nb < 0) return -1;

    ogg_packet op;
    memset(&op, 0, sizeof(op));
    op.packet = s->pkt;
    op.bytes = nb;
    op.b_o_s = 0;
    op.e_o_s = 1; /* end of stream */

    s->granulepos_48k += frames_to_48k(s->frame_size, s->cfg.sample_rate);
    if (s->granulepos_48k < 0) s->granulepos_48k = 0;
    op.granulepos = s->granulepos_48k;
    op.packetno = s->packetno++;

    if (ogg_stream_packetin(&s->os, &op) != 0)
        return -1;

    s->wrote_audio = 1;
    s->eos_written = 1;

    if (flush_pages(s, 1) != 0)
        return -1;

    fifo_consume_frames(s, s->frame_size);
    return 0;
}

int
audyn_opus_sink_close(audyn_opus_sink_t *s)
{
    if (!s) return -1;
    if (s->closed) return 0;

    /* Best-effort: encode any remaining partial audio */
    (void)pad_and_encode_final(s);

    /* If we wrote audio but never set EOS (aligned frames), emit an EOS marker packet. */
    if (s->wrote_audio && !s->eos_written) {
        (void)write_eos_marker(s);
    } else {
        (void)flush_pages(s, 1);
    }

    if (s->ogg_inited) {
        ogg_stream_clear(&s->os);
        s->ogg_inited = 0;
    }

    if (s->enc) {
        opus_encoder_destroy(s->enc);
        s->enc = NULL;
    }

    if (s->fp) {
        (void)fflush(s->fp);
        if (s->cfg.enable_fsync && s->fd >= 0) (void)fsync(s->fd);
        (void)fclose(s->fp);
        s->fp = NULL;
        s->fd = -1;
    }

    s->closed = 1;

    LOG_DEBUG("OPUS: Closed '%s' - frames_in=%lu encoded=%lu packets=%lu bytes=%lu",
              s->path ? s->path : "(unknown)",
              (unsigned long)s->stats.frames_in,
              (unsigned long)s->stats.frames_encoded,
              (unsigned long)s->stats.packets_encoded,
              (unsigned long)s->stats.bytes_encoded);

    return 0;
}

void
audyn_opus_sink_destroy(audyn_opus_sink_t *s)
{
    if (!s) return;

    (void)audyn_opus_sink_close(s);

    if (s->pkt) {
        free(s->pkt);
        s->pkt = NULL;
    }

    if (s->fifo) {
        free(s->fifo);
        s->fifo = NULL;
    }

    if (s->path) {
        free(s->path);
        s->path = NULL;
    }

    free(s);
}

void
audyn_opus_sink_get_stats(const audyn_opus_sink_t *s, audyn_opus_stats_t *stats)
{
    if (!s || !stats) return;
    *stats = s->stats;
}
