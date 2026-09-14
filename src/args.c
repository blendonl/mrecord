#include "args.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPT_SELECT,
    OPT_MONITOR,
    OPT_WINDOW,
    OPT_RECT,
    OPT_TOGGLE,
    OPT_STOP,
    OPT_DURATION,
    OPT_OUTPUT,
    OPT_FPS,
    OPT_BITRATE,
    OPT_AUDIO,
    OPT_CURSOR,
    OPT_DELAY,
    OPT_NO_BORDER,
    OPT_LIST,
    OPT_HELP,
    OPT_VERSION,
} OptId;

typedef enum {
    VALUE_NONE,
    VALUE_REQUIRED,
    VALUE_OPTIONAL_INT,
} ValueKind;

typedef struct {
    OptId          id;
    wchar_t        short_name;
    const wchar_t *long_name;
    ValueKind      value;
} OptSpec;

static const OptSpec specs[] = {
    { OPT_SELECT,    L's', L"select",    VALUE_NONE         },
    { OPT_MONITOR,   L'm', L"monitor",   VALUE_OPTIONAL_INT },
    { OPT_WINDOW,    L'w', L"window",    VALUE_NONE         },
    { OPT_RECT,      L'r', L"rect",      VALUE_REQUIRED     },
    { OPT_TOGGLE,    L't', L"toggle",    VALUE_NONE         },
    { OPT_STOP,      0,    L"stop",      VALUE_NONE         },
    { OPT_DURATION,  L'D', L"duration",  VALUE_REQUIRED     },
    { OPT_OUTPUT,    L'o', L"output",    VALUE_REQUIRED     },
    { OPT_FPS,       0,    L"fps",       VALUE_REQUIRED     },
    { OPT_BITRATE,   0,    L"bitrate",   VALUE_REQUIRED     },
    { OPT_AUDIO,     L'a', L"audio",     VALUE_NONE         },
    { OPT_CURSOR,    L'c', L"cursor",    VALUE_NONE         },
    { OPT_DELAY,     L'd', L"delay",     VALUE_REQUIRED     },
    { OPT_NO_BORDER, 0,    L"no-border", VALUE_NONE         },
    { OPT_LIST,      0,    L"list",      VALUE_NONE         },
    { OPT_HELP,      L'h', L"help",      VALUE_NONE         },
    { OPT_VERSION,   L'V', L"version",   VALUE_NONE         },
};

#define SPEC_COUNT (sizeof specs / sizeof specs[0])

typedef struct {
    Options       *o;
    bool           command_given;
    bool           record_option_given;
    const wchar_t *region_flag;
    const wchar_t *command_flag;
} Parser;

static void set_error(Options *o, const wchar_t *fmt, const wchar_t *a,
                      const wchar_t *b) {
    swprintf(o->error, ARGS_ERROR_MAX, fmt, a ? a : L"", b ? b : L"");
    o->error[ARGS_ERROR_MAX - 1] = L'\0';
}

static bool parse_int(const wchar_t *s, long lo, long hi, int *out) {
    if (!s || !*s) return false;
    wchar_t *end;
    errno = 0;
    long v = wcstol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi) return false;
    *out = (int)v;
    return true;
}

static bool parse_seconds(const wchar_t *s, bool allow_zero, double *out) {
    if (!s || !*s) return false;
    wchar_t *end;
    errno = 0;
    double v = wcstod(s, &end);
    if (errno || end == s || *end) return false;
    if (!(v >= 0.0) || v > 7.0 * 86400.0) return false;
    if (!allow_zero && v <= 0.0) return false;
    *out = v;
    return true;
}

static bool parse_rect(const wchar_t *s, Options *o) {
    long v[4];
    const wchar_t *p = s;
    for (int i = 0; i < 4; i++) {
        wchar_t *end;
        errno = 0;
        v[i] = wcstol(p, &end, 10);
        if (errno || end == p) return false;
        if (i < 3) {
            if (*end != L',') return false;
            p = end + 1;
        } else if (*end) {
            return false;
        }
    }
    for (int i = 0; i < 4; i++)
        if (v[i] < -100000 || v[i] > 100000) return false;
    if (v[2] < 1 || v[3] < 1) return false;

    o->rect_x = (int)v[0];
    o->rect_y = (int)v[1];
    o->rect_w = (int)v[2];
    o->rect_h = (int)v[3];
    return true;
}

static const OptSpec *find_long(const wchar_t *name, size_t len) {
    for (size_t i = 0; i < SPEC_COUNT; i++) {
        if (wcslen(specs[i].long_name) == len &&
            wcsncmp(specs[i].long_name, name, len) == 0)
            return &specs[i];
    }
    return NULL;
}

static const OptSpec *find_short(wchar_t c) {
    for (size_t i = 0; i < SPEC_COUNT; i++)
        if (specs[i].short_name && specs[i].short_name == c) return &specs[i];
    return NULL;
}

static bool set_region(Parser *p, RegionKind kind, const wchar_t *flag) {
    if (p->region_flag) {
        set_error(p->o, L"pick one region: %ls and %ls both given",
                  p->region_flag, flag);
        return false;
    }
    p->region_flag     = flag;
    p->o->region       = kind;
    p->o->region_given = true;
    return true;
}

static bool set_command(Parser *p, Command command, const wchar_t *flag) {
    if (p->command_flag) {
        set_error(p->o, L"%ls and %ls cannot be combined", p->command_flag, flag);
        return false;
    }
    p->command_flag = flag;
    p->o->command   = command;
    return true;
}

static bool apply(Parser *p, const OptSpec *spec, const wchar_t *flag,
                  const wchar_t *value) {
    Options *o = p->o;

    switch (spec->id) {
    case OPT_SELECT:  return set_region(p, REGION_SELECT, flag);
    case OPT_WINDOW:  return set_region(p, REGION_WINDOW, flag);
    case OPT_MONITOR:
        if (!set_region(p, REGION_MONITOR, flag)) return false;
        o->monitor = -1;
        if (value && !parse_int(value, 0, 64, &o->monitor)) {
            set_error(o, L"%ls wants a monitor index from --list, not '%ls'",
                      flag, value);
            return false;
        }
        return true;
    case OPT_RECT:
        if (!set_region(p, REGION_RECT, flag)) return false;
        if (!parse_rect(value, o)) {
            set_error(o, L"%ls wants X,Y,W,H with W and H above zero, not '%ls'",
                      flag, value);
            return false;
        }
        return true;
    case OPT_TOGGLE: return set_command(p, CMD_TOGGLE, flag);
    case OPT_STOP:   return set_command(p, CMD_STOP, flag);
    case OPT_LIST:   return set_command(p, CMD_LIST, flag);
    case OPT_DURATION:
        p->record_option_given = true;
        if (!parse_seconds(value, false, &o->duration_s)) {
            set_error(o, L"%ls wants a number of seconds above zero, not '%ls'",
                      flag, value);
            return false;
        }
        return true;
    case OPT_DELAY:
        p->record_option_given = true;
        if (!parse_seconds(value, true, &o->delay_s)) {
            set_error(o, L"%ls wants a number of seconds, not '%ls'", flag, value);
            return false;
        }
        return true;
    case OPT_OUTPUT:
        p->record_option_given = true;
        if (!value[0] || wcslen(value) >= ARGS_PATH_MAX) {
            set_error(o, L"%ls wants a file path%ls", flag, NULL);
            return false;
        }
        wcscpy(o->output, value);
        return true;
    case OPT_FPS:
        p->record_option_given = true;
        if (!parse_int(value, 1, 240, &o->fps)) {
            set_error(o, L"%ls wants a frame rate from 1 to 240, not '%ls'",
                      flag, value);
            return false;
        }
        return true;
    case OPT_BITRATE:
        p->record_option_given = true;
        if (!parse_int(value, 100, 500000, &o->bitrate_kbps)) {
            set_error(o, L"%ls wants kilobits per second from 100 to 500000, "
                         L"not '%ls'", flag, value);
            return false;
        }
        return true;
    case OPT_AUDIO:     p->record_option_given = true; o->audio  = true;  return true;
    case OPT_CURSOR:    p->record_option_given = true; o->cursor = true;  return true;
    case OPT_NO_BORDER: p->record_option_given = true; o->border = false; return true;
    case OPT_HELP:
    case OPT_VERSION:
        return true;
    }
    return true;
}

static bool looks_like_index(const wchar_t *s) {
    int ignored;
    return parse_int(s, 0, 64, &ignored);
}

bool args_parse(int argc, wchar_t **argv, Options *o) {
    memset(o, 0, sizeof *o);
    o->command = CMD_RECORD;
    o->region  = REGION_SELECT;
    o->monitor = -1;
    o->fps     = 30;
    o->border  = true;

    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--help") || !wcscmp(argv[i], L"-h")) {
            o->command = CMD_HELP;
            return true;
        }
        if (!wcscmp(argv[i], L"--version") || !wcscmp(argv[i], L"-V")) {
            o->command = CMD_VERSION;
            return true;
        }
    }

    Parser p = { .o = o };

    for (int i = 1; i < argc; i++) {
        const wchar_t *arg    = argv[i];
        const OptSpec *spec   = NULL;
        const wchar_t *inline_value = NULL;

        if (arg[0] == L'-' && arg[1] == L'-' && arg[2]) {
            const wchar_t *name = arg + 2;
            const wchar_t *eq   = wcschr(name, L'=');
            size_t len = eq ? (size_t)(eq - name) : wcslen(name);
            spec = find_long(name, len);
            if (eq) inline_value = eq + 1;
        } else if (arg[0] == L'-' && arg[1] && !arg[2]) {
            spec = find_short(arg[1]);
        }

        if (!spec) {
            set_error(o, L"unknown argument '%ls'%ls", arg, NULL);
            return false;
        }

        const wchar_t *value = NULL;
        switch (spec->value) {
        case VALUE_NONE:
            if (inline_value) {
                set_error(o, L"%ls takes no value%ls", arg, NULL);
                return false;
            }
            break;
        case VALUE_REQUIRED:
            if (inline_value) {
                value = inline_value;
            } else if (i + 1 < argc) {
                value = argv[++i];
            } else {
                set_error(o, L"%ls needs a value%ls", arg, NULL);
                return false;
            }
            break;
        case VALUE_OPTIONAL_INT:
            if (inline_value)
                value = inline_value;
            else if (i + 1 < argc && looks_like_index(argv[i + 1]))
                value = argv[++i];
            break;
        }

        if (!apply(&p, spec, arg, value)) return false;
    }

    if (o->command == CMD_STOP || o->command == CMD_LIST) {
        if (p.region_flag || p.record_option_given) {
            set_error(o, L"%ls takes no other options%ls", p.command_flag, NULL);
            return false;
        }
    }

    return true;
}
