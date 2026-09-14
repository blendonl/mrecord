#include "mrecord.h"

#include <stdarg.h>
#include <stdio.h>

static bool s_attach_tried;

static HANDLE usable(HANDLE h) {
    return (h && h != INVALID_HANDLE_VALUE) ? h : NULL;
}

static HANDLE output_handle(DWORD which) {
    HANDLE h = usable(GetStdHandle(which));
    if (h) return h;

    if (!s_attach_tried) {
        s_attach_tried = true;
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            HANDLE con = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                     OPEN_EXISTING, 0, NULL);
            if (usable(con)) {
                if (!usable(GetStdHandle(STD_OUTPUT_HANDLE)))
                    SetStdHandle(STD_OUTPUT_HANDLE, con);
                if (!usable(GetStdHandle(STD_ERROR_HANDLE)))
                    SetStdHandle(STD_ERROR_HANDLE, con);
            }
        }
    }
    return usable(GetStdHandle(which));
}

static void con_write(DWORD which, const wchar_t *fmt, va_list ap) {
    wchar_t buf[4096];
    int n = _vsnwprintf(buf, 4095, fmt, ap);
    if (n < 0) n = 4095;
    buf[n] = L'\0';

    HANDLE h = output_handle(which);
    if (!h) return;

    DWORD written, mode;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, buf, (DWORD)n, &written, NULL);
        return;
    }

    char utf8[12288];
    int m = WideCharToMultiByte(CP_UTF8, 0, buf, n, utf8, (int)sizeof utf8, NULL, NULL);
    if (m > 0) WriteFile(h, utf8, (DWORD)m, &written, NULL);
}

void con_out(const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    con_write(STD_OUTPUT_HANDLE, fmt, ap);
    va_end(ap);
}

void con_err(const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    con_write(STD_ERROR_HANDLE, fmt, ap);
    va_end(ap);
}
