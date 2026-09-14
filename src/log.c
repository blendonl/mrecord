#include "log.h"

#include <stdio.h>
#include <wchar.h>

#define LOG_MAX_BYTES (2 * 1024 * 1024)

static CRITICAL_SECTION s_cs;
static INIT_ONCE        s_once = INIT_ONCE_STATIC_INIT;
static FILE            *s_fp;
static LogLevel         s_level = LOG_INFO;

static BOOL CALLBACK log_once_init(PINIT_ONCE o, PVOID p, PVOID *ctx) {
    InitializeCriticalSection(&s_cs);
    return TRUE;
}

static void log_lock(void) {
    InitOnceExecuteOnce(&s_once, log_once_init, NULL, NULL);
    EnterCriticalSection(&s_cs);
}

static void log_unlock(void) {
    LeaveCriticalSection(&s_cs);
}

static const wchar_t *level_tag(LogLevel l) {
    switch (l) {
    case LOG_ERROR: return L"ERROR";
    case LOG_WARN:  return L"WARN ";
    case LOG_INFO:  return L"INFO ";
    default:        return L"DEBUG";
    }
}

static bool log_path(const wchar_t *basename, wchar_t *out, size_t cap) {
    wchar_t dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        n = GetTempPathW(MAX_PATH, dir);
        if (n == 0 || n >= MAX_PATH) return false;
        if (dir[n - 1] == L'\\') dir[n - 1] = L'\0';
    }

    if (_snwprintf(out, cap, L"%ls\\mrecord", dir) < 0) return false;
    out[cap - 1] = L'\0';
    CreateDirectoryW(out, NULL);

    if (_snwprintf(out, cap, L"%ls\\mrecord\\%ls.log", dir, basename) < 0) return false;
    out[cap - 1] = L'\0';
    return true;
}

void log_init(const wchar_t *basename, LogLevel level) {
    log_lock();
    if (!s_fp) {
        s_level = level;
        wchar_t path[MAX_PATH + 32];
        if (log_path(basename, path, MAX_PATH + 32)) {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad) &&
                fad.nFileSizeHigh == 0 && fad.nFileSizeLow >= LOG_MAX_BYTES) {
                wchar_t old[MAX_PATH + 40];
                _snwprintf(old, MAX_PATH + 40, L"%ls.1", path);
                old[MAX_PATH + 39] = L'\0';
                MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
            }
            s_fp = _wfopen(path, L"a, ccs=UTF-8");
        }
    }
    log_unlock();
}

void log_shutdown(void) {
    log_lock();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    log_unlock();
}

void log_msg(LogLevel level, const wchar_t *fmt, ...) {
    if (level > s_level) return;

    wchar_t body[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(body, 1023, fmt, ap);
    va_end(ap);
    body[1023] = L'\0';

    SYSTEMTIME t;
    GetLocalTime(&t);

    OutputDebugStringW(body);
    OutputDebugStringW(L"\n");

    log_lock();
    if (s_fp) {
        fwprintf(s_fp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%ls] %ls\n",
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                 t.wMilliseconds, level_tag(level), body);
        fflush(s_fp);
    }
    log_unlock();
}
