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
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <syslog.h>
#endif

static audyn_log_level_t g_level = AUDYN_LOG_INFO;
static int g_use_syslog = 0;

void audyn_log_init(audyn_log_level_t level, int use_syslog)
{
    g_level = level;
    g_use_syslog = use_syslog;

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
}

void audyn_log_write(audyn_log_level_t level, const char *fmt, ...)
{
    if (level < g_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);

#ifdef __linux__
    if (g_use_syslog) {
        int priority;
        switch (level) {
            case AUDYN_LOG_DEBUG: priority = LOG_DEBUG; break;
            case AUDYN_LOG_INFO:  priority = LOG_INFO;  break;
            case AUDYN_LOG_ERROR: priority = LOG_ERR;   break;
            default:              priority = LOG_INFO;  break;
        }
        vsyslog(priority, fmt, args);
    } else
#endif
    {
        const char *level_str;
        switch (level) {
            case AUDYN_LOG_DEBUG: level_str = "DEBUG"; break;
            case AUDYN_LOG_INFO:  level_str = "INFO";  break;
            case AUDYN_LOG_ERROR: level_str = "ERROR"; break;
            default:              level_str = "???";   break;
        }

        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm_info = localtime_r(&now, &tm_buf);
        char time_buf[20];
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        } else {
            snprintf(time_buf, sizeof(time_buf), "0000-00-00 00:00:00");
        }

        fprintf(stderr, "[%s] [%s] ", time_buf, level_str);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    va_end(args);
}
