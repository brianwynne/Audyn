/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      archive_policy.h
 *
 *  Purpose:
 *      Archive file rotation and naming policy module.
 *
 *      This module implements Rotter-compatible file chunking and naming
 *      conventions, extended with Audyn's improved architecture.
 *
 *      Key responsibilities:
 *        - Time-based file rotation aligned to wall-clock boundaries
 *        - Multiple file naming layouts (flat, hierarchy, combo, etc.)
 *        - Clock source abstraction (localtime, UTC, PTP/TAI)
 *        - Recursive directory creation
 *        - Path generation for archive files
 *
 *  Rotter Compatibility:
 *      This module replicates Rotter's exact behaviour for:
 *        - Rotation period (default 3600 seconds)
 *        - Wall-clock aligned boundaries
 *        - All naming layouts (flat, hierarchy, combo, dailydir, accurate, custom)
 *        - Zero-padded, locale-independent formatting
 *
 *  Threading Model:
 *      - NOT thread-safe. Intended for use by a single worker thread.
 *      - The worker owns the archive_policy instance.
 *
 *  Dependencies:
 *      - Standard C: time.h, stdint.h
 *      - POSIX: mkdir(), stat()
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

#ifndef AUDYN_ARCHIVE_POLICY_H
#define AUDYN_ARCHIVE_POLICY_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * File naming layout types.
 *
 * These match Rotter's layouts exactly:
 *
 *   FLAT:       /root/YYYY-MM-DD-HH.<suffix>
 *   HIERARCHY:  /root/YYYY/MM/DD/HH/archive.<suffix>
 *   COMBO:      /root/YYYY/MM/DD/HH/YYYY-MM-DD-HH.<suffix>
 *   DAILYDIR:   /root/YYYY-MM-DD/YYYY-MM-DD-HH.<suffix>
 *   ACCURATE:   /root/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-cc.<suffix>
 *               (cc = centiseconds, i.e., hundredths of a second)
 *   CUSTOM:     User-supplied strftime() format string
 */
typedef enum audyn_archive_layout {
    AUDYN_ARCHIVE_LAYOUT_FLAT = 0,
    AUDYN_ARCHIVE_LAYOUT_HIERARCHY,
    AUDYN_ARCHIVE_LAYOUT_COMBO,
    AUDYN_ARCHIVE_LAYOUT_DAILYDIR,
    AUDYN_ARCHIVE_LAYOUT_ACCURATE,
    AUDYN_ARCHIVE_LAYOUT_CUSTOM
} audyn_archive_layout_t;

/*
 * Clock source for timestamps.
 *
 *   LOCALTIME:  System local time (default, matches Rotter)
 *   UTC:        Coordinated Universal Time
 *   PTP_TAI:    PTP/TAI time from capture timestamps
 *               (for broadcast applications requiring TAI alignment)
 */
typedef enum audyn_archive_clock {
    AUDYN_ARCHIVE_CLOCK_LOCALTIME = 0,
    AUDYN_ARCHIVE_CLOCK_UTC,
    AUDYN_ARCHIVE_CLOCK_PTP_TAI
} audyn_archive_clock_t;

/*
 * Archive policy configuration.
 */
typedef struct audyn_archive_cfg {
    /* Root directory for archive files (required) */
    const char *root_dir;

    /* File suffix/extension without dot (e.g., "opus", "wav") */
    const char *suffix;

    /* Naming layout */
    audyn_archive_layout_t layout;

    /* Custom strftime format (only used when layout == CUSTOM) */
    /* This string is appended to root_dir with a path separator */
    const char *custom_format;

    /* Rotation period in seconds (default: 3600) */
    /* Set to 0 to disable rotation (single continuous file) */
    uint32_t rotation_period_sec;

    /* Clock source for timestamps */
    audyn_archive_clock_t clock_source;

    /* Create directories recursively if they don't exist */
    int create_directories;

} audyn_archive_cfg_t;

/*
 * Archive policy state (opaque).
 */
typedef struct audyn_archive_policy audyn_archive_policy_t;

/*
 * Create an archive policy instance.
 *
 * Parameters:
 *   cfg - Configuration (copied internally; caller may free after call)
 *
 * Returns:
 *   New archive policy instance, or NULL on failure.
 *
 * Notes:
 *   - root_dir and suffix are required (must not be NULL or empty)
 *   - If rotation_period_sec is 0, rotation is disabled
 *   - Default rotation_period_sec is 3600 if not specified
 */
audyn_archive_policy_t *
audyn_archive_policy_create(const audyn_archive_cfg_t *cfg);

/*
 * Destroy an archive policy instance.
 */
void audyn_archive_policy_destroy(audyn_archive_policy_t *p);

/*
 * Check if file rotation should occur.
 *
 * Parameters:
 *   p       - Archive policy instance
 *   now_ns  - Current time in nanoseconds
 *             For LOCALTIME/UTC: nanoseconds since Unix epoch
 *             For PTP_TAI: TAI nanoseconds from capture timestamp
 *
 * Returns:
 *   1 if rotation should occur now, 0 otherwise.
 *
 * Notes:
 *   - Rotation is triggered when now_ns crosses the next boundary
 *   - Boundaries are aligned to wall-clock (e.g., hourly = on the hour)
 *   - First call always returns 1 (to open initial file)
 *   - If rotation is disabled (period=0), always returns 0 after first file
 */
int audyn_archive_policy_should_rotate(
    audyn_archive_policy_t *p,
    uint64_t now_ns);

/*
 * Generate the next archive file path.
 *
 * Parameters:
 *   p         - Archive policy instance
 *   now_ns    - Current time in nanoseconds (same semantics as should_rotate)
 *   out_path  - Buffer to receive the generated path
 *   out_size  - Size of out_path buffer
 *
 * Returns:
 *   0 on success, -1 on failure (buffer too small, mkdir failed, etc.)
 *
 * Notes:
 *   - Directories are created recursively if create_directories is set
 *   - Path is null-terminated
 *   - Recommended buffer size: at least 512 bytes
 */
int audyn_archive_policy_next_path(
    audyn_archive_policy_t *p,
    uint64_t now_ns,
    char *out_path,
    size_t out_size);

/*
 * Advance to the next rotation period.
 *
 * Call this after successfully opening a new file to update internal state.
 *
 * Parameters:
 *   p - Archive policy instance
 *
 * Notes:
 *   - Updates the current period boundary
 *   - Must be called after each rotation to prevent repeated triggers
 */
void audyn_archive_policy_advance(audyn_archive_policy_t *p);

/*
 * Get the current rotation boundary time.
 *
 * Parameters:
 *   p - Archive policy instance
 *
 * Returns:
 *   Next rotation boundary in nanoseconds, or 0 if rotation is disabled.
 */
uint64_t audyn_archive_policy_next_boundary_ns(const audyn_archive_policy_t *p);

/*
 * Get current period start time as struct tm.
 *
 * Parameters:
 *   p      - Archive policy instance
 *   out_tm - Output struct tm (filled with current period's start time)
 *
 * Returns:
 *   0 on success, -1 on failure.
 *
 * Notes:
 *   - The tm is in the configured clock source's timezone
 *   - Useful for external logging/reporting
 */
int audyn_archive_policy_current_time(
    const audyn_archive_policy_t *p,
    struct tm *out_tm);

/*
 * Parse a layout name string to enum value.
 *
 * Parameters:
 *   name - Layout name ("flat", "hierarchy", "combo", "dailydir", "accurate", "custom")
 *
 * Returns:
 *   Layout enum value, or -1 if name is invalid.
 */
int audyn_archive_layout_from_string(const char *name);

/*
 * Get layout name string from enum value.
 *
 * Parameters:
 *   layout - Layout enum value
 *
 * Returns:
 *   Static string name, or "unknown" if invalid.
 */
const char *audyn_archive_layout_to_string(audyn_archive_layout_t layout);

/*
 * Parse a clock source name string to enum value.
 *
 * Parameters:
 *   name - Clock name ("localtime", "utc", "ptp", "tai")
 *
 * Returns:
 *   Clock enum value, or -1 if name is invalid.
 */
int audyn_archive_clock_from_string(const char *name);

/*
 * Get clock source name string from enum value.
 *
 * Parameters:
 *   clock_src - Clock enum value
 *
 * Returns:
 *   Static string name, or "unknown" if invalid.
 */
const char *audyn_archive_clock_to_string(audyn_archive_clock_t clock_src);

/*
 * Utility: Get current time in nanoseconds for the given clock source.
 *
 * Parameters:
 *   clock_src    - Clock source
 *   ptp_tai_ns   - PTP/TAI timestamp (only used if clock_src == PTP_TAI)
 *
 * Returns:
 *   Time in nanoseconds since epoch.
 *
 * Notes:
 *   - For LOCALTIME/UTC, uses system clock
 *   - For PTP_TAI, returns ptp_tai_ns directly (caller must provide)
 */
uint64_t audyn_archive_get_time_ns(
    audyn_archive_clock_t clock_src,
    uint64_t ptp_tai_ns);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_ARCHIVE_POLICY_H */
