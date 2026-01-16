/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      archive_policy.c
 *
 *  Purpose:
 *      Archive file rotation and naming policy implementation.
 *
 *      Implements Rotter-compatible file chunking with wall-clock aligned
 *      rotation boundaries and multiple naming layouts.
 *
 *  Rotter Behaviour Replicated:
 *      - Rotation aligned to wall-clock boundaries (e.g., hourly = on the hour)
 *      - Default period: 3600 seconds
 *      - Rotation triggered even without audio at boundary
 *      - All naming layouts: flat, hierarchy, combo, dailydir, accurate, custom
 *      - Zero-padded, locale-independent formatting
 *      - Recursive directory creation
 *
 *  Wall-Clock Alignment:
 *      If period is 3600 seconds (1 hour) and current time is 14:23:45,
 *      the current period started at 14:00:00 and ends at 15:00:00.
 *      Files are named for the period start time.
 *
 *  Threading Model:
 *      - NOT thread-safe. Single worker thread only.
 *
 *  Dependencies:
 *      - Standard C: stdio, stdlib, string, time
 *      - POSIX: mkdir, stat, sys/stat.h
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

#define _GNU_SOURCE

#include "archive_policy.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Nanoseconds per second */
#define NS_PER_SEC 1000000000ULL

/* Default rotation period (Rotter default) */
#define DEFAULT_ROTATION_PERIOD_SEC 3600

/* Maximum path length */
#define MAX_PATH_LEN 4096

/* TAI-UTC offset (as of 2017, 37 seconds; may need updating) */
/* For simplicity, we assume the caller provides correct TAI timestamps */
#define TAI_UTC_OFFSET_SEC 37

/*
 * Internal archive policy state.
 */
struct audyn_archive_policy {
    /* Configuration (owned copies) */
    char *root_dir;
    char *suffix;
    char *custom_format;
    audyn_archive_layout_t layout;
    audyn_archive_clock_t clock_source;
    uint32_t rotation_period_sec;
    int create_directories;

    /* Runtime state */
    int initialized;              /* 1 after first file opened */
    uint64_t current_period_ns;   /* Start of current period (ns since epoch) */
    uint64_t next_boundary_ns;    /* Next rotation boundary (ns since epoch) */
    struct tm current_tm;         /* Broken-down time for current period */
    uint32_t current_centisec;    /* Centiseconds for accurate layout */

    /* Statistics */
    uint64_t rotations;           /* Number of file rotations */
    uint64_t paths_generated;     /* Total paths generated */
    uint64_t directories_created; /* Directories created via mkdir */
};

/* -------- Static helper functions -------- */

/*
 * Duplicate a string (like strdup but handles NULL).
 */
static char *safe_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/*
 * Create directories recursively (like mkdir -p).
 *
 * Returns 0 on success, -1 on failure.
 */
static int mkdir_recursive(const char *path, mode_t mode)
{
    if (!path || !*path) {
        return -1;
    }

    /* Check if already exists */
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;  /* Already exists as directory */
        }
        errno = ENOTDIR;
        return -1;  /* Exists but not a directory */
    }

    /* Make a mutable copy */
    size_t len = strlen(path);
    char *tmp = malloc(len + 1);
    if (!tmp) {
        return -1;
    }
    memcpy(tmp, path, len + 1);

    /* Remove trailing slash if present */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* Create each component */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                    int saved_errno = errno;
                    free(tmp);
                    errno = saved_errno;
                    return -1;
                }
            } else if (!S_ISDIR(st.st_mode)) {
                free(tmp);
                errno = ENOTDIR;
                return -1;
            }
            *p = '/';
        }
    }

    /* Create final component */
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
            int saved_errno = errno;
            free(tmp);
            errno = saved_errno;
            return -1;
        }
    }

    free(tmp);
    return 0;
}

/*
 * Extract directory portion of a path.
 *
 * Returns newly allocated string, or NULL on failure.
 */
static char *get_directory(const char *path)
{
    if (!path) return NULL;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        /* No directory component */
        return safe_strdup(".");
    }

    size_t len = (size_t)(last_slash - path);
    if (len == 0) {
        /* Root directory */
        return safe_strdup("/");
    }

    char *dir = malloc(len + 1);
    if (dir) {
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    return dir;
}

/*
 * Convert nanoseconds since epoch to struct tm.
 *
 * Parameters:
 *   ns          - Nanoseconds since epoch
 *   clock_src   - Clock source (determines timezone)
 *   out_tm      - Output struct tm
 *   out_csec    - Output centiseconds (0-99), may be NULL
 *
 * Returns 0 on success, -1 on failure.
 */
static int ns_to_tm(uint64_t ns, audyn_archive_clock_t clock_src,
                    struct tm *out_tm, uint32_t *out_csec)
{
    time_t sec = (time_t)(ns / NS_PER_SEC);
    uint64_t nsec_part = ns % NS_PER_SEC;

    struct tm *result;
    struct tm tm_buf;

    switch (clock_src) {
        case AUDYN_ARCHIVE_CLOCK_LOCALTIME:
            result = localtime_r(&sec, &tm_buf);
            break;

        case AUDYN_ARCHIVE_CLOCK_UTC:
            result = gmtime_r(&sec, &tm_buf);
            break;

        case AUDYN_ARCHIVE_CLOCK_PTP_TAI:
            /* TAI doesn't have leap seconds, but for struct tm we convert to UTC */
            /* Note: For file naming, we use TAI time directly without UTC conversion */
            /* This matches broadcast practice where TAI is used for timing */
            result = gmtime_r(&sec, &tm_buf);
            break;

        default:
            return -1;
    }

    if (!result) {
        return -1;
    }

    *out_tm = tm_buf;

    if (out_csec) {
        /* Convert nanoseconds to centiseconds (hundredths of a second) */
        *out_csec = (uint32_t)(nsec_part / 10000000ULL);  /* ns / 10^7 = centisec */
    }

    return 0;
}

/*
 * Convert struct tm to nanoseconds since epoch.
 *
 * Parameters:
 *   tm          - Input struct tm
 *   clock_src   - Clock source (determines timezone)
 *
 * Returns nanoseconds since epoch.
 */
static uint64_t tm_to_ns(const struct tm *tm, audyn_archive_clock_t clock_src)
{
    struct tm tm_copy = *tm;
    time_t sec;

    switch (clock_src) {
        case AUDYN_ARCHIVE_CLOCK_LOCALTIME:
            sec = mktime(&tm_copy);
            break;

        case AUDYN_ARCHIVE_CLOCK_UTC:
        case AUDYN_ARCHIVE_CLOCK_PTP_TAI:
            /* timegm is a GNU extension, but widely available */
            sec = timegm(&tm_copy);
            break;

        default:
            return 0;
    }

    if (sec == (time_t)-1) {
        return 0;
    }

    return (uint64_t)sec * NS_PER_SEC;
}

/*
 * Calculate the wall-clock aligned period start time.
 *
 * For a given time and period, finds the start of the period that contains
 * that time. Periods are aligned to wall-clock boundaries.
 *
 * Examples (period = 3600 seconds = 1 hour):
 *   14:23:45 -> period starts at 14:00:00
 *   00:00:00 -> period starts at 00:00:00
 *   23:59:59 -> period starts at 23:00:00
 *
 * Parameters:
 *   now_ns      - Current time in nanoseconds
 *   period_sec  - Period length in seconds
 *   clock_src   - Clock source
 *   out_start   - Output: period start in nanoseconds
 *   out_end     - Output: period end in nanoseconds (next boundary)
 *   out_tm      - Output: struct tm for period start
 *   out_csec    - Output: centiseconds for period start (for accurate layout)
 *
 * Returns 0 on success, -1 on failure.
 */
static int calculate_period_boundary(
    uint64_t now_ns,
    uint32_t period_sec,
    audyn_archive_clock_t clock_src,
    uint64_t *out_start,
    uint64_t *out_end,
    struct tm *out_tm,
    uint32_t *out_csec)
{
    if (period_sec == 0) {
        /* No rotation - period is "infinite" */
        if (out_start) *out_start = 0;
        if (out_end) *out_end = UINT64_MAX;
        if (out_tm) {
            if (ns_to_tm(now_ns, clock_src, out_tm, out_csec) != 0) {
                return -1;
            }
        }
        return 0;
    }

    /* Convert to struct tm to work with wall-clock time */
    struct tm tm;
    uint32_t csec;
    if (ns_to_tm(now_ns, clock_src, &tm, &csec) != 0) {
        return -1;
    }

    /*
     * Rotter-style wall-clock alignment:
     *
     * For periods that divide evenly into a day (like 3600, 1800, 900, 600, 300),
     * align to those boundaries within the day.
     *
     * For other periods, align to midnight and count periods from there.
     */

    /* Calculate seconds since midnight */
    uint32_t sec_since_midnight = (uint32_t)tm.tm_hour * 3600 +
                                   (uint32_t)tm.tm_min * 60 +
                                   (uint32_t)tm.tm_sec;

    /* Find which period we're in (0-indexed from midnight) */
    uint32_t period_index = sec_since_midnight / period_sec;

    /* Calculate period start time (seconds since midnight) */
    uint32_t period_start_sec = period_index * period_sec;

    /* Build struct tm for period start */
    struct tm period_tm = tm;
    period_tm.tm_hour = (int)(period_start_sec / 3600);
    period_tm.tm_min = (int)((period_start_sec % 3600) / 60);
    period_tm.tm_sec = (int)(period_start_sec % 60);

    /* For accurate layout, period start has 0 centiseconds */
    if (out_csec) {
        *out_csec = 0;
    }

    /* Convert back to nanoseconds */
    uint64_t start_ns = tm_to_ns(&period_tm, clock_src);
    if (start_ns == 0 && period_start_sec != 0) {
        /* Conversion failed */
        return -1;
    }

    if (out_start) *out_start = start_ns;
    if (out_end) *out_end = start_ns + (uint64_t)period_sec * NS_PER_SEC;
    if (out_tm) *out_tm = period_tm;

    return 0;
}

/*
 * Generate path for FLAT layout: /root/YYYY-MM-DD-HH.<suffix>
 */
static int generate_path_flat(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    char *out_path,
    size_t out_size)
{
    int ret = snprintf(out_path, out_size,
                       "%s/%04d-%02d-%02d-%02d.%s",
                       p->root_dir,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       p->suffix);

    return (ret > 0 && (size_t)ret < out_size) ? 0 : -1;
}

/*
 * Generate path for HIERARCHY layout: /root/YYYY/MM/DD/HH/archive.<suffix>
 */
static int generate_path_hierarchy(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    char *out_path,
    size_t out_size)
{
    int ret = snprintf(out_path, out_size,
                       "%s/%04d/%02d/%02d/%02d/archive.%s",
                       p->root_dir,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       p->suffix);

    return (ret > 0 && (size_t)ret < out_size) ? 0 : -1;
}

/*
 * Generate path for COMBO layout: /root/YYYY/MM/DD/HH/YYYY-MM-DD-HH.<suffix>
 */
static int generate_path_combo(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    char *out_path,
    size_t out_size)
{
    int ret = snprintf(out_path, out_size,
                       "%s/%04d/%02d/%02d/%02d/%04d-%02d-%02d-%02d.%s",
                       p->root_dir,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       p->suffix);

    return (ret > 0 && (size_t)ret < out_size) ? 0 : -1;
}

/*
 * Generate path for DAILYDIR layout: /root/YYYY-MM-DD/YYYY-MM-DD-HH.<suffix>
 */
static int generate_path_dailydir(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    char *out_path,
    size_t out_size)
{
    int ret = snprintf(out_path, out_size,
                       "%s/%04d-%02d-%02d/%04d-%02d-%02d-%02d.%s",
                       p->root_dir,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       p->suffix);

    return (ret > 0 && (size_t)ret < out_size) ? 0 : -1;
}

/*
 * Generate path for ACCURATE layout: /root/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-cc.<suffix>
 */
static int generate_path_accurate(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    uint32_t centisec,
    char *out_path,
    size_t out_size)
{
    int ret = snprintf(out_path, out_size,
                       "%s/%04d-%02d-%02d/%04d-%02d-%02d-%02d-%02d-%02d-%02u.%s",
                       p->root_dir,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       tm->tm_min,
                       tm->tm_sec,
                       centisec,
                       p->suffix);

    return (ret > 0 && (size_t)ret < out_size) ? 0 : -1;
}

/*
 * Generate path for CUSTOM layout using strftime.
 */
static int generate_path_custom(
    const audyn_archive_policy_t *p,
    const struct tm *tm,
    char *out_path,
    size_t out_size)
{
    if (!p->custom_format || !*p->custom_format) {
        LOG_ERROR("archive: custom layout requires format string");
        return -1;
    }

    /* Format the time portion */
    char time_part[MAX_PATH_LEN];
    size_t ret = strftime(time_part, sizeof(time_part), p->custom_format, tm);
    if (ret == 0) {
        LOG_ERROR("archive: strftime failed for format '%s'", p->custom_format);
        return -1;
    }

    /* Combine root_dir + formatted string + suffix */
    int n = snprintf(out_path, out_size, "%s/%s.%s",
                     p->root_dir, time_part, p->suffix);

    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

/* -------- Public API implementation -------- */

audyn_archive_policy_t *
audyn_archive_policy_create(const audyn_archive_cfg_t *cfg)
{
    if (!cfg) {
        LOG_ERROR("archive: NULL configuration");
        return NULL;
    }

    if (!cfg->root_dir || !*cfg->root_dir) {
        LOG_ERROR("archive: root_dir is required");
        return NULL;
    }

    if (!cfg->suffix || !*cfg->suffix) {
        LOG_ERROR("archive: suffix is required");
        return NULL;
    }

    if (cfg->layout == AUDYN_ARCHIVE_LAYOUT_CUSTOM &&
        (!cfg->custom_format || !*cfg->custom_format)) {
        LOG_ERROR("archive: custom layout requires custom_format");
        return NULL;
    }

    /* Validate layout enum */
    if (cfg->layout < AUDYN_ARCHIVE_LAYOUT_FLAT ||
        cfg->layout > AUDYN_ARCHIVE_LAYOUT_CUSTOM) {
        LOG_ERROR("archive: invalid layout %d", cfg->layout);
        return NULL;
    }

    /* Validate clock_source enum */
    if (cfg->clock_source < AUDYN_ARCHIVE_CLOCK_LOCALTIME ||
        cfg->clock_source > AUDYN_ARCHIVE_CLOCK_PTP_TAI) {
        LOG_ERROR("archive: invalid clock_source %d", cfg->clock_source);
        return NULL;
    }

    /* Validate rotation_period_sec bounds (if rotation enabled) */
    if (cfg->rotation_period_sec != 0) {
        if (cfg->rotation_period_sec < AUDYN_ARCHIVE_MIN_ROTATION_SEC) {
            LOG_ERROR("archive: rotation_period_sec %u too small (min=%u)",
                      cfg->rotation_period_sec, AUDYN_ARCHIVE_MIN_ROTATION_SEC);
            return NULL;
        }
        if (cfg->rotation_period_sec > AUDYN_ARCHIVE_MAX_ROTATION_SEC) {
            LOG_ERROR("archive: rotation_period_sec %u too large (max=%u)",
                      cfg->rotation_period_sec, AUDYN_ARCHIVE_MAX_ROTATION_SEC);
            return NULL;
        }
    }

    audyn_archive_policy_t *p = calloc(1, sizeof(*p));
    if (!p) {
        LOG_ERROR("archive: failed to allocate policy");
        return NULL;
    }

    /* Copy configuration */
    p->root_dir = safe_strdup(cfg->root_dir);
    p->suffix = safe_strdup(cfg->suffix);
    p->custom_format = safe_strdup(cfg->custom_format);
    p->layout = cfg->layout;
    p->clock_source = cfg->clock_source;
    p->create_directories = cfg->create_directories;

    /* Set rotation period (default if not specified) */
    if (cfg->rotation_period_sec == 0) {
        /* Explicit 0 means no rotation */
        p->rotation_period_sec = 0;
    } else {
        p->rotation_period_sec = cfg->rotation_period_sec;
    }

    if (!p->root_dir || !p->suffix) {
        LOG_ERROR("archive: failed to copy configuration strings");
        audyn_archive_policy_destroy(p);
        return NULL;
    }

    /* Not initialized until first rotation check */
    p->initialized = 0;
    p->current_period_ns = 0;
    p->next_boundary_ns = 0;

    LOG_INFO("archive: created policy - layout=%s clock=%s period=%us root=%s suffix=%s",
             audyn_archive_layout_to_string(p->layout),
             audyn_archive_clock_to_string(p->clock_source),
             p->rotation_period_sec,
             p->root_dir,
             p->suffix);

    return p;
}

void audyn_archive_policy_destroy(audyn_archive_policy_t *p)
{
    if (!p) return;

    LOG_DEBUG("archive: destroyed (rotations=%lu paths=%lu dirs=%lu)",
              (unsigned long)p->rotations,
              (unsigned long)p->paths_generated,
              (unsigned long)p->directories_created);

    free(p->root_dir);
    free(p->suffix);
    free(p->custom_format);
    free(p);
}

int audyn_archive_policy_should_rotate(
    audyn_archive_policy_t *p,
    uint64_t now_ns)
{
    if (!p) return 0;

    /* First call - always need to open initial file */
    if (!p->initialized) {
        return 1;
    }

    /* No rotation if period is 0 */
    if (p->rotation_period_sec == 0) {
        return 0;
    }

    /* Check if we've crossed the boundary */
    if (now_ns >= p->next_boundary_ns) {
        return 1;
    }

    return 0;
}

int audyn_archive_policy_next_path(
    audyn_archive_policy_t *p,
    uint64_t now_ns,
    char *out_path,
    size_t out_size)
{
    if (!p || !out_path || out_size == 0) {
        return -1;
    }

    /* Calculate period boundary and get struct tm */
    uint64_t period_start, period_end;
    struct tm tm;
    uint32_t csec = 0;

    if (calculate_period_boundary(now_ns, p->rotation_period_sec, p->clock_source,
                                  &period_start, &period_end, &tm, &csec) != 0) {
        LOG_ERROR("archive: failed to calculate period boundary");
        return -1;
    }

    /* For accurate layout, always use actual time (not period boundary) */
    if (p->layout == AUDYN_ARCHIVE_LAYOUT_ACCURATE) {
        if (ns_to_tm(now_ns, p->clock_source, &tm, &csec) != 0) {
            LOG_ERROR("archive: failed to convert time for accurate layout");
            return -1;
        }
    }

    /* Generate path based on layout */
    int ret;
    switch (p->layout) {
        case AUDYN_ARCHIVE_LAYOUT_FLAT:
            ret = generate_path_flat(p, &tm, out_path, out_size);
            break;

        case AUDYN_ARCHIVE_LAYOUT_HIERARCHY:
            ret = generate_path_hierarchy(p, &tm, out_path, out_size);
            break;

        case AUDYN_ARCHIVE_LAYOUT_COMBO:
            ret = generate_path_combo(p, &tm, out_path, out_size);
            break;

        case AUDYN_ARCHIVE_LAYOUT_DAILYDIR:
            ret = generate_path_dailydir(p, &tm, out_path, out_size);
            break;

        case AUDYN_ARCHIVE_LAYOUT_ACCURATE:
            ret = generate_path_accurate(p, &tm, csec, out_path, out_size);
            break;

        case AUDYN_ARCHIVE_LAYOUT_CUSTOM:
            ret = generate_path_custom(p, &tm, out_path, out_size);
            break;

        default:
            LOG_ERROR("archive: unknown layout %d", p->layout);
            return -1;
    }

    if (ret != 0) {
        LOG_ERROR("archive: failed to generate path");
        return -1;
    }

    /* Create directories if needed */
    if (p->create_directories) {
        char *dir = get_directory(out_path);
        if (dir) {
            /* Check if directory exists before mkdir to track creations */
            struct stat st;
            int dir_existed = (stat(dir, &st) == 0 && S_ISDIR(st.st_mode));

            if (mkdir_recursive(dir, 0755) != 0) {
                LOG_ERROR("archive: failed to create directory '%s': %s",
                          dir, strerror(errno));
                free(dir);
                return -1;
            }

            if (!dir_existed) {
                p->directories_created++;
            }
            free(dir);
        }
    }

    p->paths_generated++;

    /* Store period info for advance() */
    p->current_period_ns = period_start;
    p->next_boundary_ns = period_end;
    p->current_tm = tm;
    p->current_centisec = csec;

    LOG_DEBUG("archive: next path '%s' (period %lu-%lu)",
              out_path,
              (unsigned long)(period_start / NS_PER_SEC),
              (unsigned long)(period_end / NS_PER_SEC));

    return 0;
}

void audyn_archive_policy_advance(audyn_archive_policy_t *p)
{
    if (!p) return;

    p->initialized = 1;
    p->rotations++;

    LOG_DEBUG("archive: advanced to period starting at %04d-%02d-%02d %02d:%02d:%02d (rotation #%lu)",
              p->current_tm.tm_year + 1900,
              p->current_tm.tm_mon + 1,
              p->current_tm.tm_mday,
              p->current_tm.tm_hour,
              p->current_tm.tm_min,
              p->current_tm.tm_sec,
              (unsigned long)p->rotations);
}

uint64_t audyn_archive_policy_next_boundary_ns(const audyn_archive_policy_t *p)
{
    if (!p || p->rotation_period_sec == 0) {
        return 0;
    }
    return p->next_boundary_ns;
}

int audyn_archive_policy_current_time(
    const audyn_archive_policy_t *p,
    struct tm *out_tm)
{
    if (!p || !out_tm) {
        return -1;
    }

    if (!p->initialized) {
        return -1;
    }

    *out_tm = p->current_tm;
    return 0;
}

int audyn_archive_layout_from_string(const char *name)
{
    if (!name) return -1;

    if (strcasecmp(name, "flat") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_FLAT;
    } else if (strcasecmp(name, "hierarchy") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_HIERARCHY;
    } else if (strcasecmp(name, "combo") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_COMBO;
    } else if (strcasecmp(name, "dailydir") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_DAILYDIR;
    } else if (strcasecmp(name, "accurate") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_ACCURATE;
    } else if (strcasecmp(name, "custom") == 0) {
        return AUDYN_ARCHIVE_LAYOUT_CUSTOM;
    }

    return -1;
}

const char *audyn_archive_layout_to_string(audyn_archive_layout_t layout)
{
    switch (layout) {
        case AUDYN_ARCHIVE_LAYOUT_FLAT:      return "flat";
        case AUDYN_ARCHIVE_LAYOUT_HIERARCHY: return "hierarchy";
        case AUDYN_ARCHIVE_LAYOUT_COMBO:     return "combo";
        case AUDYN_ARCHIVE_LAYOUT_DAILYDIR:  return "dailydir";
        case AUDYN_ARCHIVE_LAYOUT_ACCURATE:  return "accurate";
        case AUDYN_ARCHIVE_LAYOUT_CUSTOM:    return "custom";
        default:                              return "unknown";
    }
}

int audyn_archive_clock_from_string(const char *name)
{
    if (!name) return -1;

    if (strcasecmp(name, "localtime") == 0 || strcasecmp(name, "local") == 0) {
        return AUDYN_ARCHIVE_CLOCK_LOCALTIME;
    } else if (strcasecmp(name, "utc") == 0) {
        return AUDYN_ARCHIVE_CLOCK_UTC;
    } else if (strcasecmp(name, "ptp") == 0 || strcasecmp(name, "tai") == 0 ||
               strcasecmp(name, "ptp_tai") == 0) {
        return AUDYN_ARCHIVE_CLOCK_PTP_TAI;
    }

    return -1;
}

const char *audyn_archive_clock_to_string(audyn_archive_clock_t clock_src)
{
    switch (clock_src) {
        case AUDYN_ARCHIVE_CLOCK_LOCALTIME: return "localtime";
        case AUDYN_ARCHIVE_CLOCK_UTC:       return "utc";
        case AUDYN_ARCHIVE_CLOCK_PTP_TAI:   return "ptp_tai";
        default:                             return "unknown";
    }
}

void audyn_archive_policy_get_stats(
    const audyn_archive_policy_t *p,
    audyn_archive_stats_t *stats)
{
    if (!stats) return;

    if (!p) {
        memset(stats, 0, sizeof(*stats));
        return;
    }

    stats->rotations = p->rotations;
    stats->paths_generated = p->paths_generated;
    stats->directories_created = p->directories_created;
}

uint64_t audyn_archive_get_time_ns(
    audyn_archive_clock_t clock_src,
    uint64_t ptp_tai_ns)
{
    switch (clock_src) {
        case AUDYN_ARCHIVE_CLOCK_LOCALTIME:
        case AUDYN_ARCHIVE_CLOCK_UTC:
        {
            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
                return 0;
            }
            return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
        }

        case AUDYN_ARCHIVE_CLOCK_PTP_TAI:
            /* Caller provides TAI timestamp from PTP clock */
            return ptp_tai_ns;

        default:
            return 0;
    }
}
