/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      log.c
 *
 *  Purpose:
 *      Simple logging implementation with optional syslog support.
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

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <strings.h>  /* For strcasecmp */

#ifdef __linux__
#include <syslog.h>
#endif

static audyn_log_level_t g_level = AUDYN_LOG_INFO;
static int g_use_syslog = 0;
static int g_initialized = 0;

/* Statistics counters */
static uint64_t g_debug_count = 0;
static uint64_t g_info_count = 0;
static uint64_t g_warn_count = 0;
static uint64_t g_error_count = 0;

void audyn_log_init(audyn_log_level_t level, int use_syslog)
{
    g_level = level;
    g_use_syslog = use_syslog;
    g_initialized = 1;

    /* Reset statistics */
    g_debug_count = 0;
    g_info_count = 0;
    g_warn_count = 0;
    g_error_count = 0;

#ifdef __linux__
    if (use_syslog) {
        openlog("audyn", LOG_PID | LOG_NDELAY, LOG_USER);
    }
#endif
}

void audyn_log_shutdown(void)
{
#ifdef __linux__
    if (g_use_syslog) {
        closelog();
    }
#endif
    g_initialized = 0;
}

int audyn_log_is_initialized(void)
{
    return g_initialized;
}

audyn_log_level_t audyn_log_get_level(void)
{
    return g_level;
}

void audyn_log_set_level(audyn_log_level_t level)
{
    g_level = level;
}

void audyn_log_write(audyn_log_level_t level, const char *fmt, ...)
{
    if (level < g_level) {
        return;
    }

    /* Update statistics */
    switch (level) {
        case AUDYN_LOG_DEBUG: g_debug_count++; break;
        case AUDYN_LOG_INFO:  g_info_count++;  break;
        case AUDYN_LOG_WARN:  g_warn_count++;  break;
        case AUDYN_LOG_ERROR: g_error_count++; break;
        default: break;
    }

    va_list args;
    va_start(args, fmt);

#ifdef __linux__
    if (g_use_syslog) {
        int priority;
        switch (level) {
            case AUDYN_LOG_DEBUG: priority = LOG_DEBUG;   break;
            case AUDYN_LOG_INFO:  priority = LOG_INFO;    break;
            case AUDYN_LOG_WARN:  priority = LOG_WARNING; break;
            case AUDYN_LOG_ERROR: priority = LOG_ERR;     break;
            default:              priority = LOG_INFO;    break;
        }
        vsyslog(priority, fmt, args);
    } else
#endif
    {
        const char *level_str;
        switch (level) {
            case AUDYN_LOG_DEBUG: level_str = "DEBUG"; break;
            case AUDYN_LOG_INFO:  level_str = "INFO";  break;
            case AUDYN_LOG_WARN:  level_str = "WARN";  break;
            case AUDYN_LOG_ERROR: level_str = "ERROR"; break;
            default:              level_str = "???";   break;
        }

        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm_info = localtime_r(&now, &tm_buf);
        char time_buf[24];
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        } else {
            snprintf(time_buf, sizeof(time_buf), "0000-00-00 00:00:00");
        }

        fprintf(stderr, "[%s] [%-5s] ", time_buf, level_str);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    va_end(args);
}

void audyn_log_get_stats(audyn_log_stats_t *stats)
{
    if (!stats) return;

    stats->debug_count = g_debug_count;
    stats->info_count = g_info_count;
    stats->warn_count = g_warn_count;
    stats->error_count = g_error_count;
    stats->total_count = g_debug_count + g_info_count + g_warn_count + g_error_count;
}

int audyn_log_level_from_string(const char *name)
{
    if (!name) return -1;

    if (strcasecmp(name, "debug") == 0) {
        return AUDYN_LOG_DEBUG;
    } else if (strcasecmp(name, "info") == 0) {
        return AUDYN_LOG_INFO;
    } else if (strcasecmp(name, "warn") == 0 || strcasecmp(name, "warning") == 0) {
        return AUDYN_LOG_WARN;
    } else if (strcasecmp(name, "error") == 0 || strcasecmp(name, "err") == 0) {
        return AUDYN_LOG_ERROR;
    }

    return -1;
}

const char *audyn_log_level_to_string(audyn_log_level_t level)
{
    switch (level) {
        case AUDYN_LOG_DEBUG: return "debug";
        case AUDYN_LOG_INFO:  return "info";
        case AUDYN_LOG_WARN:  return "warn";
        case AUDYN_LOG_ERROR: return "error";
        default:              return "unknown";
    }
}
