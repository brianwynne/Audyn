/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      log.h
 *
 *  Purpose:
 *      Simple logging interface with optional syslog support.
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

#ifndef AUDYN_LOG_H
#define AUDYN_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum audyn_log_level {
    AUDYN_LOG_DEBUG = 0,
    AUDYN_LOG_INFO  = 1,
    AUDYN_LOG_WARN  = 2,
    AUDYN_LOG_ERROR = 3
} audyn_log_level_t;

/*
 * Log statistics.
 */
typedef struct audyn_log_stats {
    uint64_t debug_count;
    uint64_t info_count;
    uint64_t warn_count;
    uint64_t error_count;
    uint64_t total_count;
} audyn_log_stats_t;

/*
 * Initialize the logging subsystem.
 *
 * Parameters:
 *   level      - Minimum log level to output
 *   use_syslog - Non-zero to log to syslog (Linux only)
 */
void audyn_log_init(audyn_log_level_t level, int use_syslog);

/*
 * Shutdown the logging subsystem.
 */
void audyn_log_shutdown(void);

/*
 * Check if logging is initialized.
 *
 * Returns: Non-zero if initialized, 0 otherwise.
 */
int audyn_log_is_initialized(void);

/*
 * Get current log level.
 *
 * Returns: Current minimum log level.
 */
audyn_log_level_t audyn_log_get_level(void);

/*
 * Set log level at runtime.
 *
 * Parameters:
 *   level - New minimum log level
 */
void audyn_log_set_level(audyn_log_level_t level);

/*
 * Write a log message.
 */
void audyn_log_write(audyn_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * Get log statistics.
 *
 * Parameters:
 *   stats - Output statistics structure
 */
void audyn_log_get_stats(audyn_log_stats_t *stats);

/*
 * Parse a log level name string to enum value.
 *
 * Parameters:
 *   name - Level name ("debug", "info", "warn", "error")
 *
 * Returns: Level enum value, or -1 if invalid.
 */
int audyn_log_level_from_string(const char *name);

/*
 * Get log level name string from enum value.
 *
 * Parameters:
 *   level - Level enum value
 *
 * Returns: Static string name, or "unknown" if invalid.
 */
const char *audyn_log_level_to_string(audyn_log_level_t level);

/* Convenience macros */
#define LOG_DEBUG(...) audyn_log_write(AUDYN_LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  audyn_log_write(AUDYN_LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  audyn_log_write(AUDYN_LOG_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) audyn_log_write(AUDYN_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_LOG_H */
