#ifndef MRECORD_ARGS_H
#define MRECORD_ARGS_H

#include <stdbool.h>
#include <wchar.h>

#define ARGS_PATH_MAX  1024
#define ARGS_ERROR_MAX 256

typedef enum {
    CMD_RECORD,
    CMD_TOGGLE,
    CMD_STOP,
    CMD_LIST,
    CMD_HELP,
    CMD_VERSION,
} Command;

typedef enum {
    REGION_SELECT,
    REGION_MONITOR,
    REGION_WINDOW,
    REGION_RECT,
} RegionKind;

typedef struct {
    Command    command;
    RegionKind region;
    bool       region_given;
    int        monitor;
    int        rect_x, rect_y, rect_w, rect_h;
    int        fps;
    int        bitrate_kbps;
    double     duration_s;
    double     delay_s;
    bool       audio;
    bool       cursor;
    bool       border;
    wchar_t    output[ARGS_PATH_MAX];
    wchar_t    error[ARGS_ERROR_MAX];
} Options;

bool args_parse(int argc, wchar_t **argv, Options *o);

#endif
