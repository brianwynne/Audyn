/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      audyn.c
 *
 *  Purpose:
 *      Main executable / orchestration layer.
 *
 *      - Parses CLI options for input source, output format, and audio parameters
 *      - Supports AES67/RTP input (primary) and PipeWire input (fallback)
 *      - Supports WAV and Opus output formats
 *      - Implements Rotter-compatible file chunking and naming (archive policy)
 *      - Creates core resources and runs capture until SIGINT/SIGTERM
 *
 *  Input Sources:
 *      - AES67: Multicast/unicast RTP audio (default)
 *      - PipeWire: Local audio capture (--pipewire flag)
 *
 *  Output Formats:
 *      - WAV: PCM16 RIFF/WAVE (.wav extension)
 *      - Opus: Ogg Opus compressed audio (.opus extension)
 *
 *  Archive Modes:
 *      - Single file: -o /path/to/file.wav (no rotation)
 *      - Archive: --archive-root /path --archive-layout flat (with rotation)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "log.h"
#include "frame_pool.h"
#include "audio_queue.h"
#include "wav_sink.h"
#include "opus_sink.h"
#include "aes_input.h"
#include "pipewire_input.h"
#include "ptp_clock.h"
#include "archive_policy.h"

/* -------- Types -------- */

typedef enum input_source {
    INPUT_AES67,
    INPUT_PIPEWIRE
} input_source_t;

typedef enum output_format {
    OUTPUT_WAV,
    OUTPUT_OPUS
} output_format_t;

/* -------- Globals -------- */

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0)  return -1;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return -1;
    return 0;
}

/* -------- Usage -------- */

static void usage(const char *argv0)
{
    fprintf(stderr,
        "audyn — AES67 Audio Capture & Archival Engine\n\n"
        "Usage:\n"
        "  %s -o <file> [options]           Single file mode\n"
        "  %s --archive-root <dir> [options] Archive mode with rotation\n\n"
        "Output (choose one):\n"
        "  -o <path>              Output file path (single file, no rotation)\n"
        "                         Format detected from extension: .wav or .opus\n"
        "  --archive-root <dir>   Root directory for archive files\n"
        "                         Enables time-based file rotation\n\n"
        "Archive Options (with --archive-root):\n"
        "  --archive-layout <L>   Naming layout (default: flat)\n"
        "                         Layouts: flat, hierarchy, combo, dailydir, accurate, custom\n"
        "  --archive-format <F>   strftime format string (for custom layout)\n"
        "  --archive-period <S>   Rotation period in seconds (default: 3600)\n"
        "                         Set to 0 for no rotation (continuous file)\n"
        "  --archive-clock <C>    Clock source: localtime, utc, ptp (default: localtime)\n"
        "  --archive-suffix <S>   File suffix without dot (default: wav)\n\n"
        "  Layout examples:\n"
        "    flat:      /root/2026-01-10-14.opus\n"
        "    hierarchy: /root/2026/01/10/14/archive.opus\n"
        "    combo:     /root/2026/01/10/14/2026-01-10-14.opus\n"
        "    dailydir:  /root/2026-01-10/2026-01-10-14.opus\n"
        "    accurate:  /root/2026-01-10/2026-01-10-14-30-00-00.opus\n"
        "    custom:    User-defined strftime format\n\n"
        "Input Source (default: AES67):\n"
        "  --pipewire             Use PipeWire input instead of AES67\n\n"
        "AES67 Options:\n"
        "  -m <ip>                Multicast/source IP address (required for AES67)\n"
        "  -p <port>              UDP port (default 5004)\n"
        "  --pt <type>            RTP payload type (default 96)\n"
        "  --spp <frames>         Samples per packet (default 48)\n"
        "  --rcvbuf <bytes>       Socket receive buffer size (default 2097152)\n\n"
        "PTP Clock Options (AES67 only):\n"
        "  --ptp-device <path>    Use hardware PTP clock (e.g., /dev/ptp0)\n"
        "  --ptp-interface <if>   Discover PHC from network interface (e.g., eth0)\n"
        "  --ptp-software         Use software PTP (CLOCK_REALTIME via linuxptp)\n\n"
        "Audio Parameters:\n"
        "  -r <rate>              Sample rate (default 48000)\n"
        "  -c <channels>          Channels: 1 or 2 (default 2)\n\n"
        "Opus Options (when output is .opus):\n"
        "  --bitrate <bps>        Target bitrate (default 128000)\n"
        "  --vbr                  Enable VBR (default)\n"
        "  --cbr                  Use CBR instead of VBR\n"
        "  --complexity <n>       Encoder complexity 0-10 (default 5)\n\n"
        "Buffer Tuning:\n"
        "  -Q <cap>               Queue capacity (default 1024)\n"
        "  -P <cap>               Pool frame count (default 256)\n"
        "  -F <size>              Frame size in samples (default 1024)\n\n"
        "Logging:\n"
        "  -v                     Debug logging\n"
        "  -q                     Errors only\n"
        "  --syslog               Log to syslog\n\n"
        "Examples:\n"
        "  Single file:\n"
        "    %s -o recording.wav -m 239.69.1.1 -p 5004\n"
        "    %s -o recording.opus -m 239.69.1.1 --bitrate 96000\n\n"
        "  Archive mode (hourly rotation):\n"
        "    %s --archive-root /var/lib/audyn --archive-layout flat \\\n"
        "       --archive-suffix opus -m 239.69.1.1\n\n"
        "  Archive mode (daily directories, UTC):\n"
        "    %s --archive-root /mnt/archive --archive-layout dailydir \\\n"
        "       --archive-clock utc --archive-period 3600 -m 239.69.1.1\n",
        argv0, argv0, argv0, argv0, argv0, argv0
    );
}

/* -------- Helpers -------- */

static int parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) return -1;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return -1;
    if (v > 0xFFFFFFFFul) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_u16(const char *s, uint16_t *out)
{
    if (!s || !*s || !out) return -1;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return -1;
    if (v > 0xFFFFu) return -1;
    *out = (uint16_t)v;
    return 0;
}

static int parse_u8(const char *s, uint8_t *out)
{
    if (!s || !*s || !out) return -1;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return -1;
    if (v > 0xFFu) return -1;
    *out = (uint8_t)v;
    return 0;
}

static output_format_t detect_output_format(const char *suffix)
{
    if (!suffix) return OUTPUT_WAV;
    if (strcasecmp(suffix, "opus") == 0) {
        return OUTPUT_OPUS;
    }
    return OUTPUT_WAV;
}

static const char *get_suffix_from_path(const char *path)
{
    if (!path) return "wav";
    const char *ext = strrchr(path, '.');
    if (ext && ext[1]) {
        return ext + 1;
    }
    return "wav";
}

/* -------- Worker context with archive support -------- */

typedef struct worker_ctx {
    /* Core resources (not owned) */
    audyn_frame_pool_t  *pool;
    audyn_audio_queue_t *queue;

    /* Archive policy (may be NULL for single-file mode) */
    audyn_archive_policy_t *archive;

    /* Single-file mode path (NULL if using archive) */
    const char *single_file_path;

    /* Output format */
    output_format_t format;

    /* Audio parameters */
    uint32_t sample_rate;
    uint16_t channels;

    /* Opus parameters */
    uint32_t opus_bitrate;
    int opus_vbr;
    int opus_complexity;

    /* PTP clock for TAI timestamps (may be NULL) */
    audyn_ptp_clock_t *ptp_clk;

    /* Stop flag */
    volatile int *stop_flag;

    /* Worker status */
    int status;
    char error[256];

    /* Current sinks (owned by worker) */
    audyn_wav_sink_t  *wav_sink;
    audyn_opus_sink_t *opus_sink;

    /* Statistics */
    uint64_t files_written;
    uint64_t frames_written;
    uint64_t rotations;

} worker_ctx_t;

/* -------- Sink management -------- */

static int open_wav_sink(worker_ctx_t *ctx, const char *path)
{
    audyn_wav_sink_cfg_t wcfg;
    memset(&wcfg, 0, sizeof(wcfg));
    wcfg.format = AUDYN_WAV_PCM16;
    wcfg.enable_fsync = 0;

    ctx->wav_sink = audyn_wav_sink_create(&wcfg);
    if (!ctx->wav_sink) {
        snprintf(ctx->error, sizeof(ctx->error), "WAV sink create failed");
        return -1;
    }

    if (audyn_wav_sink_open(ctx->wav_sink, path, ctx->sample_rate, ctx->channels) != 0) {
        snprintf(ctx->error, sizeof(ctx->error), "WAV sink open failed: %s", path);
        audyn_wav_sink_destroy(ctx->wav_sink);
        ctx->wav_sink = NULL;
        return -1;
    }

    LOG_INFO("Opened WAV file: %s", path);
    return 0;
}

static int open_opus_sink(worker_ctx_t *ctx, const char *path)
{
    audyn_opus_cfg_t ocfg;
    memset(&ocfg, 0, sizeof(ocfg));
    ocfg.sample_rate = ctx->sample_rate;
    ocfg.channels = ctx->channels;
    ocfg.bitrate = ctx->opus_bitrate;
    ocfg.vbr = ctx->opus_vbr;
    ocfg.complexity = ctx->opus_complexity;
    ocfg.application = AUDYN_OPUS_APP_AUDIO;
    ocfg.enable_fsync = 0;

    ctx->opus_sink = audyn_opus_sink_create(path, &ocfg);
    if (!ctx->opus_sink) {
        snprintf(ctx->error, sizeof(ctx->error), "Opus sink create failed: %s", path);
        return -1;
    }

    LOG_INFO("Opened Opus file: %s", path);
    return 0;
}

static void close_current_sink(worker_ctx_t *ctx)
{
    if (ctx->wav_sink) {
        audyn_wav_sink_close(ctx->wav_sink);
        audyn_wav_sink_destroy(ctx->wav_sink);
        ctx->wav_sink = NULL;
        ctx->files_written++;
    }

    if (ctx->opus_sink) {
        audyn_opus_sink_flush(ctx->opus_sink);
        audyn_opus_sink_close(ctx->opus_sink);
        audyn_opus_sink_destroy(ctx->opus_sink);
        ctx->opus_sink = NULL;
        ctx->files_written++;
    }
}

static int open_sink(worker_ctx_t *ctx, const char *path)
{
    if (ctx->format == OUTPUT_WAV) {
        return open_wav_sink(ctx, path);
    } else {
        return open_opus_sink(ctx, path);
    }
}

static int write_to_sink(worker_ctx_t *ctx, audyn_audio_frame_t *frame)
{
    int ret = 0;

    if (ctx->format == OUTPUT_WAV && ctx->wav_sink) {
        ret = audyn_wav_sink_write(ctx->wav_sink, frame->data,
                                   frame->sample_frames, frame->channels);
    } else if (ctx->format == OUTPUT_OPUS && ctx->opus_sink) {
        ret = audyn_opus_sink_write(ctx->opus_sink, frame->data, frame->sample_frames);
    } else {
        ret = -1;
    }

    if (ret == 0) {
        ctx->frames_written++;
    }

    return ret;
}

/* -------- Worker thread -------- */

static uint64_t get_current_time_ns(worker_ctx_t *ctx)
{
    if (ctx->archive) {
        audyn_archive_clock_t clock_src = AUDYN_ARCHIVE_CLOCK_LOCALTIME;
        /* Get clock source from archive config - we need to track this */
        /* For now, use system time; PTP TAI would come from frames */
        uint64_t ptp_ns = 0;
        if (ctx->ptp_clk) {
            ptp_ns = audyn_ptp_clock_now_ns(ctx->ptp_clk);
        }
        return audyn_archive_get_time_ns(clock_src, ptp_ns);
    }

    /* Default: system time */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int maybe_rotate(worker_ctx_t *ctx)
{
    if (!ctx->archive) {
        return 0;  /* No rotation in single-file mode */
    }

    uint64_t now_ns = get_current_time_ns(ctx);

    if (!audyn_archive_policy_should_rotate(ctx->archive, now_ns)) {
        return 0;  /* No rotation needed */
    }

    /* Close current file if open */
    if (ctx->wav_sink || ctx->opus_sink) {
        LOG_INFO("Rotating archive file");
        close_current_sink(ctx);
        ctx->rotations++;
    }

    /* Generate new path */
    char path[1024];
    if (audyn_archive_policy_next_path(ctx->archive, now_ns, path, sizeof(path)) != 0) {
        snprintf(ctx->error, sizeof(ctx->error), "Failed to generate archive path");
        return -1;
    }

    /* Open new file */
    if (open_sink(ctx, path) != 0) {
        return -1;
    }

    /* Advance archive policy */
    audyn_archive_policy_advance(ctx->archive);

    return 0;
}

static void *worker_main(void *arg)
{
    worker_ctx_t *ctx = (worker_ctx_t *)arg;

    /* Initial file open */
    if (ctx->archive) {
        /* Archive mode - open first file */
        if (maybe_rotate(ctx) != 0) {
            LOG_ERROR("Worker: failed to open initial archive file");
            ctx->status = -1;
            return NULL;
        }
    } else if (ctx->single_file_path) {
        /* Single file mode */
        if (open_sink(ctx, ctx->single_file_path) != 0) {
            LOG_ERROR("Worker: failed to open output file");
            ctx->status = -1;
            return NULL;
        }
    } else {
        LOG_ERROR("Worker: no output configured");
        ctx->status = -1;
        return NULL;
    }

    /* Main processing loop */
    while (!*ctx->stop_flag) {
        /* Check for rotation (archive mode only) */
        if (ctx->archive && maybe_rotate(ctx) != 0) {
            LOG_ERROR("Worker: rotation failed: %s", ctx->error);
            ctx->status = -1;
            break;
        }

        /* Get next frame from queue */
        audyn_audio_frame_t *frame = (audyn_audio_frame_t *)audyn_audio_queue_pop(ctx->queue);
        if (!frame) {
            usleep(1000);
            continue;
        }

        /* Write to sink */
        if (write_to_sink(ctx, frame) != 0) {
            LOG_ERROR("Worker: write failed");
            ctx->status = -1;
            audyn_frame_release(frame);
            break;
        }

        audyn_frame_release(frame);
    }

    /* Drain remaining frames */
    while (1) {
        audyn_audio_frame_t *frame = (audyn_audio_frame_t *)audyn_audio_queue_pop(ctx->queue);
        if (!frame) break;
        (void)write_to_sink(ctx, frame);
        audyn_frame_release(frame);
    }

    /* Close final file */
    close_current_sink(ctx);

    LOG_INFO("Worker finished: %lu files, %lu frames, %lu rotations",
             (unsigned long)ctx->files_written,
             (unsigned long)ctx->frames_written,
             (unsigned long)ctx->rotations);

    return NULL;
}

/* -------- Main -------- */

int main(int argc, char **argv)
{
    /* Defaults */
    const char *out_path = NULL;
    const char *source_ip = NULL;

    input_source_t input_src = INPUT_AES67;
    uint32_t rate = 48000;
    uint16_t channels = 2;

    /* AES67 defaults */
    uint16_t port = 5004;
    uint8_t  payload_type = 96;
    uint16_t samples_per_packet = 48;
    uint32_t rcvbuf = 2097152;

    /* PTP defaults */
    const char *ptp_device = NULL;
    const char *ptp_interface = NULL;
    int ptp_software = 0;

    /* Archive defaults */
    const char *archive_root = NULL;
    const char *archive_layout_str = "flat";
    const char *archive_format = NULL;
    const char *archive_suffix = "wav";
    const char *archive_clock_str = "localtime";
    uint32_t archive_period = 3600;

    /* Opus defaults */
    uint32_t opus_bitrate = 128000;
    int      opus_vbr = 1;
    int      opus_complexity = 5;

    /* Buffer defaults */
    uint32_t qcap = 1024;
    uint32_t pcap = 256;
    uint32_t fcap = 1024;

    /* Logging */
    int use_syslog = 0;
    audyn_log_level_t lvl = AUDYN_LOG_INFO;

    /* --- Parse args --- */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            source_ip = argv[++i];
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            if (parse_u16(argv[++i], &port) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--pt") && i + 1 < argc) {
            if (parse_u8(argv[++i], &payload_type) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--spp") && i + 1 < argc) {
            if (parse_u16(argv[++i], &samples_per_packet) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--rcvbuf") && i + 1 < argc) {
            if (parse_u32(argv[++i], &rcvbuf) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            if (parse_u32(argv[++i], &rate) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            uint32_t ch;
            if (parse_u32(argv[++i], &ch) != 0 || ch > 2 || ch == 0) { usage(argv[0]); return 2; }
            channels = (uint16_t)ch;
        } else if (!strcmp(argv[i], "--bitrate") && i + 1 < argc) {
            if (parse_u32(argv[++i], &opus_bitrate) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--vbr")) {
            opus_vbr = 1;
        } else if (!strcmp(argv[i], "--cbr")) {
            opus_vbr = 0;
        } else if (!strcmp(argv[i], "--complexity") && i + 1 < argc) {
            uint32_t c;
            if (parse_u32(argv[++i], &c) != 0 || c > 10) { usage(argv[0]); return 2; }
            opus_complexity = (int)c;
        } else if (!strcmp(argv[i], "-Q") && i + 1 < argc) {
            if (parse_u32(argv[++i], &qcap) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "-P") && i + 1 < argc) {
            if (parse_u32(argv[++i], &pcap) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "-F") && i + 1 < argc) {
            if (parse_u32(argv[++i], &fcap) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--pipewire")) {
            input_src = INPUT_PIPEWIRE;
        } else if (!strcmp(argv[i], "--ptp-device") && i + 1 < argc) {
            ptp_device = argv[++i];
        } else if (!strcmp(argv[i], "--ptp-interface") && i + 1 < argc) {
            ptp_interface = argv[++i];
        } else if (!strcmp(argv[i], "--ptp-software")) {
            ptp_software = 1;
        } else if (!strcmp(argv[i], "--archive-root") && i + 1 < argc) {
            archive_root = argv[++i];
        } else if (!strcmp(argv[i], "--archive-layout") && i + 1 < argc) {
            archive_layout_str = argv[++i];
        } else if (!strcmp(argv[i], "--archive-format") && i + 1 < argc) {
            archive_format = argv[++i];
        } else if (!strcmp(argv[i], "--archive-suffix") && i + 1 < argc) {
            archive_suffix = argv[++i];
        } else if (!strcmp(argv[i], "--archive-clock") && i + 1 < argc) {
            archive_clock_str = argv[++i];
        } else if (!strcmp(argv[i], "--archive-period") && i + 1 < argc) {
            if (parse_u32(argv[++i], &archive_period) != 0) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--syslog")) {
            use_syslog = 1;
        } else if (!strcmp(argv[i], "-v")) {
            lvl = AUDYN_LOG_DEBUG;
        } else if (!strcmp(argv[i], "-q")) {
            lvl = AUDYN_LOG_ERROR;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    /* Validate: must have either -o or --archive-root */
    if (!out_path && !archive_root) {
        fprintf(stderr, "Error: Either -o <path> or --archive-root <dir> is required.\n\n");
        usage(argv[0]);
        return 2;
    }

    if (out_path && archive_root) {
        fprintf(stderr, "Error: Cannot use both -o and --archive-root.\n\n");
        usage(argv[0]);
        return 2;
    }

    if (input_src == INPUT_AES67 && !source_ip) {
        fprintf(stderr, "Error: Source IP (-m) is required for AES67 input.\n\n");
        usage(argv[0]);
        return 2;
    }

    if (qcap < 2) { fprintf(stderr, "Error: Queue capacity must be >= 2\n"); return 2; }
    if (pcap == 0) { fprintf(stderr, "Error: Pool frames must be > 0\n"); return 2; }
    if (fcap == 0) { fprintf(stderr, "Error: Frame capacity must be > 0\n"); return 2; }

    /* Validate PTP options */
    int ptp_opts = (ptp_device ? 1 : 0) + (ptp_interface ? 1 : 0) + ptp_software;
    if (ptp_opts > 1) {
        fprintf(stderr, "Error: Only one of --ptp-device, --ptp-interface, --ptp-software allowed\n");
        return 2;
    }
    if (ptp_opts > 0 && input_src != INPUT_AES67) {
        fprintf(stderr, "Error: PTP options only apply to AES67 input\n");
        return 2;
    }

    /* Validate archive layout */
    int archive_layout = AUDYN_ARCHIVE_LAYOUT_FLAT;
    if (archive_root) {
        archive_layout = audyn_archive_layout_from_string(archive_layout_str);
        if (archive_layout < 0) {
            fprintf(stderr, "Error: Unknown archive layout '%s'\n", archive_layout_str);
            fprintf(stderr, "Valid layouts: flat, hierarchy, combo, dailydir, accurate, custom\n");
            return 2;
        }

        if (archive_layout == AUDYN_ARCHIVE_LAYOUT_CUSTOM && !archive_format) {
            fprintf(stderr, "Error: Custom layout requires --archive-format\n");
            return 2;
        }
    }

    /* Validate archive clock */
    int archive_clock = AUDYN_ARCHIVE_CLOCK_LOCALTIME;
    if (archive_root) {
        archive_clock = audyn_archive_clock_from_string(archive_clock_str);
        if (archive_clock < 0) {
            fprintf(stderr, "Error: Unknown archive clock '%s'\n", archive_clock_str);
            fprintf(stderr, "Valid clocks: localtime, utc, ptp, tai\n");
            return 2;
        }
    }

    /* Determine output format */
    output_format_t out_fmt;
    if (archive_root) {
        out_fmt = detect_output_format(archive_suffix);
    } else {
        out_fmt = detect_output_format(get_suffix_from_path(out_path));
    }

    /* --- Init logging & signals --- */
    audyn_log_init(lvl, use_syslog);

    if (install_signal_handlers() != 0) {
        LOG_ERROR("Failed to install signal handlers.");
        audyn_log_shutdown();
        return 1;
    }

    if (archive_root) {
        LOG_INFO("Audyn starting: input=%s format=%s archive=%s layout=%s period=%us",
                 input_src == INPUT_AES67 ? "AES67" : "PipeWire",
                 out_fmt == OUTPUT_OPUS ? "Opus" : "WAV",
                 archive_root,
                 archive_layout_str,
                 archive_period);
    } else {
        LOG_INFO("Audyn starting: input=%s output=%s format=%s",
                 input_src == INPUT_AES67 ? "AES67" : "PipeWire",
                 out_path,
                 out_fmt == OUTPUT_OPUS ? "Opus" : "WAV");
    }

    if (input_src == INPUT_AES67) {
        LOG_INFO("AES67: %s:%u PT=%u SPP=%u rate=%u ch=%u",
                 source_ip, port, payload_type, samples_per_packet, rate, channels);
    }

    /* --- Create core objects --- */
    audyn_frame_pool_t *pool = NULL;
    audyn_audio_queue_t *q = NULL;
    audyn_archive_policy_t *archive_policy = NULL;
    audyn_ptp_clock_t *ptp_clk = NULL;

    pool = audyn_frame_pool_create(pcap, channels, fcap);
    if (!pool) {
        LOG_ERROR("frame_pool create failed");
        audyn_log_shutdown();
        return 1;
    }

    q = audyn_audio_queue_create(qcap);
    if (!q) {
        LOG_ERROR("audio_queue create failed");
        audyn_frame_pool_destroy(pool);
        audyn_log_shutdown();
        return 1;
    }

    /* --- Create archive policy (if archive mode) --- */
    if (archive_root) {
        audyn_archive_cfg_t acfg;
        memset(&acfg, 0, sizeof(acfg));
        acfg.root_dir = archive_root;
        acfg.suffix = archive_suffix;
        acfg.layout = (audyn_archive_layout_t)archive_layout;
        acfg.custom_format = archive_format;
        acfg.rotation_period_sec = archive_period;
        acfg.clock_source = (audyn_archive_clock_t)archive_clock;
        acfg.create_directories = 1;

        archive_policy = audyn_archive_policy_create(&acfg);
        if (!archive_policy) {
            LOG_ERROR("archive_policy create failed");
            goto cleanup;
        }
    }

    /* --- Create PTP clock (if configured) --- */
    if (ptp_device || ptp_interface || ptp_software) {
        audyn_ptp_cfg_t pcfg;
        memset(&pcfg, 0, sizeof(pcfg));

        if (ptp_device) {
            pcfg.mode = AUDYN_PTP_MODE_HARDWARE;
            pcfg.phc_device = ptp_device;
        } else if (ptp_interface) {
            pcfg.mode = AUDYN_PTP_MODE_HARDWARE;
            pcfg.interface = ptp_interface;
        } else {
            pcfg.mode = AUDYN_PTP_MODE_SOFTWARE;
        }

        ptp_clk = audyn_ptp_clock_create(&pcfg);
        if (!ptp_clk) {
            LOG_ERROR("PTP clock creation failed");
            goto cleanup;
        }
    }

    /* --- Create worker context --- */
    worker_ctx_t worker_ctx;
    memset(&worker_ctx, 0, sizeof(worker_ctx));
    worker_ctx.pool = pool;
    worker_ctx.queue = q;
    worker_ctx.archive = archive_policy;
    worker_ctx.single_file_path = out_path;
    worker_ctx.format = out_fmt;
    worker_ctx.sample_rate = rate;
    worker_ctx.channels = channels;
    worker_ctx.opus_bitrate = opus_bitrate;
    worker_ctx.opus_vbr = opus_vbr;
    worker_ctx.opus_complexity = opus_complexity;
    worker_ctx.ptp_clk = ptp_clk;
    worker_ctx.stop_flag = (volatile int *)&g_stop;

    /* --- Start worker thread --- */
    pthread_t worker_thread;
    int worker_started = 0;

    if (pthread_create(&worker_thread, NULL, worker_main, &worker_ctx) != 0) {
        LOG_ERROR("Worker thread create failed");
        goto cleanup;
    }
    worker_started = 1;

    /* --- Create input --- */
    audyn_aes_input_t *aes_in = NULL;
    audyn_pw_input_t *pw_in = NULL;

    if (input_src == INPUT_AES67) {
        audyn_aes_input_cfg_t aescfg;
        memset(&aescfg, 0, sizeof(aescfg));
        aescfg.source_ip = source_ip;
        aescfg.port = port;
        aescfg.payload_type = payload_type;
        aescfg.sample_rate = rate;
        aescfg.channels = channels;
        aescfg.samples_per_packet = samples_per_packet;
        aescfg.socket_rcvbuf = rcvbuf;

        aes_in = audyn_aes_input_create(pool, q, &aescfg);
        if (!aes_in) {
            LOG_ERROR("AES67 input create failed");
            goto cleanup;
        }

        if (ptp_clk) {
            audyn_aes_input_set_ptp_clock(aes_in, ptp_clk);
        }

        if (audyn_aes_input_start(aes_in) != 0) {
            LOG_ERROR("AES67 input start failed: %s", audyn_aes_input_last_error(aes_in));
            goto cleanup;
        }

    } else {
        pw_in = audyn_pw_input_create(pool, q, rate, channels);
        if (!pw_in) {
            LOG_ERROR("PipeWire input create failed");
            goto cleanup;
        }

        if (audyn_pw_input_start(pw_in) != 0) {
            LOG_ERROR("PipeWire input start failed");
            goto cleanup;
        }
    }

    LOG_INFO("Audyn running (Ctrl+C to stop)");

    /* --- Main loop --- */
    while (!g_stop) {
        usleep(50u * 1000u);

        /* Check worker status */
        if (worker_ctx.status != 0) {
            LOG_ERROR("Worker error: %s", worker_ctx.error);
            break;
        }
    }

    LOG_INFO("Stopping...");

    /* --- Shutdown --- */
cleanup:
    /* Stop input first */
    if (aes_in) {
        audyn_aes_input_stop(aes_in);
        audyn_aes_input_destroy(aes_in);
    }
    if (pw_in) {
        audyn_pw_input_stop(pw_in);
        audyn_pw_input_destroy(pw_in);
    }

    /* Stop worker */
    g_stop = 1;
    if (worker_started) {
        pthread_join(worker_thread, NULL);
    }

    /* Destroy PTP clock (after input is stopped) */
    if (ptp_clk) {
        audyn_ptp_clock_destroy(ptp_clk);
    }

    /* Destroy archive policy */
    if (archive_policy) {
        audyn_archive_policy_destroy(archive_policy);
    }

    /* Destroy core objects */
    if (q) audyn_audio_queue_destroy(q);
    if (pool) audyn_frame_pool_destroy(pool);

    audyn_log_shutdown();

    return (g_stop && worker_ctx.status == 0) ? 0 : 1;
}
