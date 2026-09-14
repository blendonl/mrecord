#include "naming.h"

#include <stdio.h>
#include <string.h>

bool naming_default_name(wchar_t *out, size_t cap, const wchar_t *dir,
                         int year, int month, int day,
                         int hour, int minute, int second, int attempt) {
    if (!out || cap == 0 || !dir) return false;

    size_t dir_len = wcslen(dir);
    while (dir_len && (dir[dir_len - 1] == L'\\' || dir[dir_len - 1] == L'/'))
        dir_len--;

    wchar_t stamp[64];
    int n = attempt > 1
        ? swprintf(stamp, 64, L"mrecord-%04d%02d%02d-%02d%02d%02d-%d.mp4",
                   year, month, day, hour, minute, second, attempt)
        : swprintf(stamp, 64, L"mrecord-%04d%02d%02d-%02d%02d%02d.mp4",
                   year, month, day, hour, minute, second);
    if (n <= 0) return false;

    size_t need = dir_len + (dir_len ? 1 : 0) + (size_t)n + 1;
    if (need > cap) return false;

    size_t pos = 0;
    if (dir_len) {
        memcpy(out, dir, dir_len * sizeof(wchar_t));
        pos = dir_len;
        out[pos++] = L'\\';
    }
    memcpy(out + pos, stamp, ((size_t)n + 1) * sizeof(wchar_t));
    return true;
}

bool naming_ensure_extension(wchar_t *path, size_t cap, const wchar_t *ext) {
    if (!path || !ext) return false;

    size_t len = wcslen(path);
    size_t base = 0;
    for (size_t i = 0; i < len; i++)
        if (path[i] == L'\\' || path[i] == L'/') base = i + 1;

    for (size_t i = base; i < len; i++)
        if (path[i] == L'.' && i > base) return true;

    size_t ext_len = wcslen(ext);
    if (len + ext_len + 1 > cap) return false;
    memcpy(path + len, ext, (ext_len + 1) * sizeof(wchar_t));
    return true;
}
