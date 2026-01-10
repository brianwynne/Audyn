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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum audyn_log_level {
    AUDYN_LOG_DEBUG = 0,
    AUDYN_LOG_INFO  = 1,
    AUDYN_LOG_ERROR = 2
} audyn_log_level_t;

void audyn_log_init(audyn_log_level_t level, int use_syslog);
void audyn_log_shutdown(void);

void audyn_log_write(audyn_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOG_DEBUG(...) audyn_log_write(AUDYN_LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  audyn_log_write(AUDYN_LOG_INFO,  __VA_ARGS__)
#define LOG_ERROR(...) audyn_log_write(AUDYN_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_LOG_H */
