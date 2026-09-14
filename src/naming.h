#ifndef MRECORD_NAMING_H
#define MRECORD_NAMING_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

bool naming_default_name(wchar_t *out, size_t cap, const wchar_t *dir,
                         int year, int month, int day,
                         int hour, int minute, int second, int attempt);

bool naming_ensure_extension(wchar_t *path, size_t cap, const wchar_t *ext);

#endif
