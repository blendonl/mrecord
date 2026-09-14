#ifndef MRECORD_H
#define MRECORD_H

#define COBJMACROS

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "convert.h"
#include "geom.h"
#include "log.h"
#include "naming.h"
#include "pacing.h"

#define MR_EXIT_OK        0
#define MR_EXIT_FAIL      1
#define MR_EXIT_USAGE     2
#define MR_EXIT_CANCELLED 3

#define MONITORS_MAX 32

#define MRECORD_VERSION_W L"" MRECORD_VERSION

void con_out(const wchar_t *fmt, ...);
void con_err(const wchar_t *fmt, ...);

typedef struct {
    HMONITOR handle;
    GRect    rect;
    wchar_t  device[CCHDEVICENAME];
    bool     primary;
} Monitor;

int  monitors_list(Monitor *out, int cap);
int  monitors_find(const Monitor *mons, int count, HMONITOR handle);
void monitors_print(void);
bool window_visible_frame(HWND hwnd, GRect *out);

typedef struct {
    HANDLE mutex;
    HANDLE stop;
    HANDLE finished;
    HANDLE path_map;
    HANDLE thread;
    DWORD  thread_id;
} Control;

bool control_acquire(Control *c);
void control_publish_path(Control *c, const wchar_t *path);
void control_request_stop(void);
void control_finished(Control *c);
void control_release(Control *c);
bool control_running(void);
int  control_stop_running(void);

typedef enum {
    SELECT_OK,
    SELECT_CANCELLED,
    SELECT_FAILED,
} SelectResult;

SelectResult select_region(GRect *out);

typedef struct {
    HANDLE thread;
    DWORD  thread_id;
    HANDLE ready;
    GRect  region;
    GRect  bounds;
} Border;

bool border_start(Border *b, GRect region, GRect bounds);
void border_stop(Border *b);

typedef enum {
    CAPTURE_SAME,
    CAPTURE_NEW,
    CAPTURE_LOST,
} CaptureStatus;

typedef struct {
    struct ID3D11Device           *device;
    struct ID3D11DeviceContext    *context;
    struct IDXGIOutputDuplication *dup;
    struct ID3D11Texture2D        *staging;
    wchar_t  device_name[CCHDEVICENAME];
    GRect    output;
    GRect    region;
    int      rotation;
    int      tex_w, tex_h;
    int      width, height;
    BYTE    *frame;
    bool     have_frame;
} Capture;

bool          capture_open(Capture *c, const wchar_t *device_name, GRect region);
bool          capture_reopen(Capture *c);
CaptureStatus capture_next(Capture *c, UINT timeout_ms);
void          capture_close(Capture *c);

typedef struct {
    HDC     dc;
    HBITMAP dib;
    HGDIOBJ old;
    BYTE   *bits;
    int     width, height;
} CursorCanvas;

bool        cursor_canvas_open(CursorCanvas *c, int width, int height);
const BYTE *cursor_compose(CursorCanvas *c, const BYTE *frame, GRect region);
void        cursor_canvas_close(CursorCanvas *c);

typedef struct {
    struct IMFSinkWriter *writer;
    CRITICAL_SECTION      lock;
    DWORD                 video_stream;
    DWORD                 audio_stream;
    bool                  has_audio;
    bool                  nv12;
    int                   width, height;
    int                   audio_rate;
    int64_t               video_samples;
} Encoder;

bool encoder_open(Encoder *e, const wchar_t *path, int width, int height,
                  int fps, int kbps, int audio_rate);
bool encoder_write_video(Encoder *e, const BYTE *bgra, int64_t time_hns,
                         int64_t duration_hns);
bool encoder_write_audio(Encoder *e, const int16_t *pcm, uint32_t frames,
                         int64_t time_hns, int64_t duration_hns);
bool encoder_close(Encoder *e);

typedef struct {
    struct IAudioClient        *client;
    struct IAudioCaptureClient *capture;
    int                         channels;
    int                         bits;
    bool                        is_float;
    int                         rate_in;
    int                         rate_out;
    Decimator                   decimator;
    HANDLE                      thread;
    volatile LONG               stop;
    volatile LONG               failed;
    Encoder                    *encoder;
    int64_t                     t0_ticks;
    int64_t                     ticks_per_second;
    int64_t                     frames_written;
    int16_t                    *buffer;
    size_t                      buffer_frames;
} Audio;

bool audio_open(Audio *a);
bool audio_start(Audio *a, Encoder *encoder, int64_t t0_ticks, int64_t ticks_per_second);
void audio_stop(Audio *a);
void audio_close(Audio *a);

#endif
