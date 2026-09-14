#ifndef MRECORD_LOG_H
#define MRECORD_LOG_H

#include <windows.h>
#include <stdarg.h>
#include <stdbool.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3,
} LogLevel;

void log_init(const wchar_t *basename, LogLevel level);
void log_shutdown(void);
void log_msg(LogLevel level, const wchar_t *fmt, ...);

#endif
