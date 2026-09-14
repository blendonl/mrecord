#include "mrecord.h"

#include <mfapi.h>
#include <shlobj.h>

#define FIRST_FRAME_TIMEOUT_MS 3000
#define REOPEN_RETRY_MS        250
#define REOPEN_GIVE_UP_MS      15000

static const wchar_t USAGE[] =
L"mrecord " MRECORD_VERSION_W L" - record a region, window or monitor to MP4\n"
L"\n"
L"mrecord [region] [options]\n"
L"\n"
L"Region (pick one; default --select). The region must lie on a single monitor:\n"
L"  -s, --select          live dimmed overlay: drag a rectangle (WxH label), single click takes the window\n"
L"                        under the cursor, Enter/release confirms, Esc or right-click cancels; clamped to\n"
L"                        the monitor the drag started on\n"
L"  -m, --monitor [N]     monitor N from --list; without N, the monitor under the cursor\n"
L"  -w, --window          the foreground window's visible frame (region fixed at start)\n"
L"  -r, --rect X,Y,W,H    explicit rectangle, physical virtual-screen pixels\n"
L"Control:\n"
L"  -t, --toggle          if a recording is running, stop it; otherwise start one (one hotkey for both)\n"
L"      --stop            stop the running recording and exit\n"
L"  -D, --duration SECS   stop by itself\n"
L"Options:\n"
L"  -o, --output PATH     .mp4; default %USERPROFILE%\\Videos\\Screen Recordings\\mrecord-YYYYMMDD-HHMMSS.mp4\n"
L"      --fps N           default 30 (constant frame rate; repeat the last frame when nothing changed)\n"
L"      --bitrate KBPS    default derived from region size and fps\n"
L"  -a, --audio           also record system sound (WASAPI loopback, AAC)\n"
L"  -c, --cursor          draw the mouse pointer\n"
L"  -d, --delay SECONDS   wait before starting\n"
L"      --no-border       no on-screen frame around the recorded region\n"
L"      --list            print monitors: index, device, x,y,w,h, primary\n"
L"  -h, --help / -V, --version\n"
L"\n"
L"stdout: the written path when the recording finishes.\n"
L"Exit: 0 ok, 1 failure, 2 bad usage, 3 cancelled / nothing to stop.\n";

typedef struct {
    GRect   region;
    Monitor monitor;
} Target;

static HANDLE s_stop_event;

static BOOL WINAPI on_console_ctrl(DWORD type) {
    if (s_stop_event) SetEvent(s_stop_event);
    return TRUE;
}

static int64_t now_ticks(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static bool stop_requested(void) {
    return WaitForSingleObject(s_stop_event, 0) == WAIT_OBJECT_0;
}

static bool wait_or_stop(double seconds) {
    DWORD ms = seconds > 0 ? (DWORD)(seconds * 1000.0 + 0.5) : 0;
    return WaitForSingleObject(s_stop_event, ms) == WAIT_OBJECT_0;
}

static int target_on_monitor(Target *t, const Monitor *mons, int count, GRect r) {
    GRect rects[MONITORS_MAX];
    for (int k = 0; k < count; k++) rects[k] = mons[k].rect;

    int i = geom_monitor_containing(rects, count, r);
    if (i < 0) return -1;
    t->region  = r;
    t->monitor = mons[i];
    return i;
}

static int resolve_target(const Options *o, Target *t) {
    Monitor mons[MONITORS_MAX];
    int count = monitors_list(mons, MONITORS_MAX);
    if (count <= 0) {
        con_err(L"mrecord: no monitors found\n");
        return MR_EXIT_FAIL;
    }

    GRect rects[MONITORS_MAX];
    for (int i = 0; i < count; i++) rects[i] = mons[i].rect;

    switch (o->region) {
    case REGION_RECT: {
        GRect r = grect_make(o->rect_x, o->rect_y, o->rect_w, o->rect_h);
        if (target_on_monitor(t, mons, count, r) < 0) {
            con_err(L"mrecord: --rect %d,%d,%d,%d does not lie on a single monitor "
                    L"(see mrecord --list)\n", o->rect_x, o->rect_y, o->rect_w, o->rect_h);
            return MR_EXIT_USAGE;
        }
        break;
    }
    case REGION_MONITOR: {
        int i = o->monitor;
        if (i < 0) {
            POINT pt;
            GetCursorPos(&pt);
            i = geom_monitor_at(rects, count, pt.x, pt.y);
            if (i < 0) i = 0;
        }
        if (i >= count) {
            con_err(L"mrecord: there is no monitor %d (see mrecord --list)\n", i);
            return MR_EXIT_USAGE;
        }
        t->region  = mons[i].rect;
        t->monitor = mons[i];
        break;
    }
    case REGION_WINDOW: {
        HWND fg = GetForegroundWindow();
        GRect frame;
        if (!fg || !window_visible_frame(fg, &frame)) {
            con_err(L"mrecord: no foreground window to record\n");
            return MR_EXIT_FAIL;
        }
        int i = monitors_find(mons, count, MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST));
        if (i < 0) i = geom_monitor_most_overlap(rects, count, frame);
        if (i < 0) {
            con_err(L"mrecord: the foreground window is not on any monitor\n");
            return MR_EXIT_FAIL;
        }
        t->region  = grect_intersect(frame, mons[i].rect);
        t->monitor = mons[i];
        break;
    }
    case REGION_SELECT: {
        GRect r;
        SelectResult sr = select_region(&r);
        if (sr == SELECT_CANCELLED) {
            con_err(L"mrecord: selection cancelled\n");
            return MR_EXIT_CANCELLED;
        }
        if (sr == SELECT_FAILED) {
            con_err(L"mrecord: could not show the selection overlay\n");
            return MR_EXIT_FAIL;
        }
        int i = geom_monitor_most_overlap(rects, count, r);
        if (i < 0) return MR_EXIT_CANCELLED;
        t->region  = grect_intersect(r, mons[i].rect);
        t->monitor = mons[i];
        break;
    }
    }

    t->region = grect_even(t->region);
    if (grect_width(t->region) < 2 || grect_height(t->region) < 2) {
        con_err(L"mrecord: the region is too small to record\n");
        return MR_EXIT_USAGE;
    }
    return MR_EXIT_OK;
}

static bool default_output(wchar_t *out, size_t cap) {
    PWSTR videos = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Videos, KF_FLAG_CREATE, NULL, &videos)))
        return false;

    wchar_t dir[ARGS_PATH_MAX];
    _snwprintf(dir, ARGS_PATH_MAX, L"%ls\\Screen Recordings", videos);
    dir[ARGS_PATH_MAX - 1] = L'\0';
    CoTaskMemFree(videos);
    CreateDirectoryW(dir, NULL);

    SYSTEMTIME t;
    GetLocalTime(&t);
    for (int attempt = 1; attempt < 100; attempt++) {
        if (!naming_default_name(out, cap, dir, t.wYear, t.wMonth, t.wDay,
                                 t.wHour, t.wMinute, t.wSecond, attempt))
            return false;
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

static bool output_path(const Options *o, wchar_t *out, size_t cap) {
    if (!o->output[0]) return default_output(out, cap);

    wchar_t given[ARGS_PATH_MAX];
    lstrcpynW(given, o->output, ARGS_PATH_MAX);
    if (!naming_ensure_extension(given, ARGS_PATH_MAX, L".mp4")) return false;

    DWORD n = GetFullPathNameW(given, (DWORD)cap, out, NULL);
    return n > 0 && n < cap;
}

typedef struct {
    Capture      capture;
    CursorCanvas canvas;
    Encoder      encoder;
    Audio        audio;
    Border       border;
    bool         canvas_open;
    bool         audio_open;
    bool         audio_running;
    bool         border_on;
} Session;

static bool reopen_capture(Session *s) {
    ULONGLONG give_up = GetTickCount64() + REOPEN_GIVE_UP_MS;
    while (GetTickCount64() < give_up) {
        if (capture_reopen(&s->capture)) {
            log_msg(LOG_INFO, L"capture: duplication recreated");
            return true;
        }
        if (wait_or_stop(REOPEN_RETRY_MS / 1000.0)) return false;
    }
    log_msg(LOG_ERROR, L"capture: could not recreate the duplication within %d ms",
            REOPEN_GIVE_UP_MS);
    return false;
}

static bool first_frame(Session *s) {
    ULONGLONG give_up = GetTickCount64() + FIRST_FRAME_TIMEOUT_MS;
    while (GetTickCount64() < give_up) {
        CaptureStatus st = capture_next(&s->capture, 100);
        if (st == CAPTURE_NEW) return true;
        if (st == CAPTURE_LOST && !reopen_capture(s)) return false;
        if (stop_requested()) return false;
    }
    return s->capture.have_frame;
}

static int run_session(const Options *o, const Target *t, const wchar_t *path, Control *control) {
    Session s;
    ZeroMemory(&s, sizeof s);

    int width  = grect_width(t->region);
    int height = grect_height(t->region);
    int kbps   = o->bitrate_kbps ? o->bitrate_kbps
                                 : pacing_default_bitrate_kbps(width, height, o->fps);

    if (!capture_open(&s.capture, t->monitor.device, t->region)) {
        con_err(L"mrecord: cannot capture %ls (see %%LOCALAPPDATA%%\\mrecord\\mrecord.log)\n",
                t->monitor.device);
        return MR_EXIT_FAIL;
    }

    if (o->audio) {
        s.audio_open = audio_open(&s.audio);
        if (!s.audio_open)
            con_err(L"mrecord: system sound is unavailable, recording video only\n");
    }

    bool encoder_ok = encoder_open(&s.encoder, path, width, height, o->fps, kbps,
                                   s.audio_open ? s.audio.rate_out : 0);
    if (!encoder_ok && s.audio_open) {
        con_err(L"mrecord: the AAC encoder refused system sound, recording video only\n");
        audio_close(&s.audio);
        s.audio_open = false;
        encoder_ok = encoder_open(&s.encoder, path, width, height, o->fps, kbps, 0);
    }
    if (!encoder_ok) {
        con_err(L"mrecord: cannot create %ls\n", path);
        capture_close(&s.capture);
        return MR_EXIT_FAIL;
    }

    if (o->cursor) s.canvas_open = cursor_canvas_open(&s.canvas, width, height);

    log_msg(LOG_INFO, L"recording %ls: %d,%d %dx%d on %ls, %d fps, %d kbps%ls%ls",
            path, t->region.left, t->region.top, width, height, t->monitor.device,
            o->fps, kbps, s.audio_open ? L", audio" : L"", s.canvas_open ? L", cursor" : L"");

    int  exit_code = MR_EXIT_OK;
    bool have = first_frame(&s);
    if (!have) {
        if (!stop_requested()) {
            con_err(L"mrecord: no frame arrived from %ls\n", t->monitor.device);
            exit_code = MR_EXIT_FAIL;
        }
    }

    if (have) {
        if (o->border) s.border_on = border_start(&s.border, t->region, t->monitor.rect);
        control_publish_path(control, path);

        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        int64_t t0       = now_ticks();
        int64_t duration = pacing_frame_duration(o->fps);
        int64_t limit    = o->duration_s > 0
                         ? (int64_t)(o->duration_s * (double)freq.QuadPart) : 0;
        int64_t next     = 0;

        if (s.audio_open)
            s.audio_running = audio_start(&s.audio, &s.encoder, t0, freq.QuadPart);

        for (;;) {
            int64_t elapsed = now_ticks() - t0;
            if (limit && elapsed >= limit) break;

            int64_t due = pacing_frame_index(elapsed, freq.QuadPart, o->fps);
            if (due < next) {
                int64_t wait = pacing_ticks_until_frame(next, elapsed, freq.QuadPart, o->fps);
                DWORD ms = (DWORD)((wait * 1000 + freq.QuadPart - 1) / freq.QuadPart);
                if (WaitForSingleObject(s_stop_event, ms ? ms : 1) == WAIT_OBJECT_0) break;
                continue;
            }
            if (stop_requested()) break;

            if (capture_next(&s.capture, 0) == CAPTURE_LOST && !reopen_capture(&s)) {
                if (!stop_requested()) exit_code = MR_EXIT_FAIL;
                break;
            }

            const BYTE *frame = s.canvas_open
                ? cursor_compose(&s.canvas, s.capture.frame, t->region)
                : s.capture.frame;

            if (!encoder_write_video(&s.encoder, frame, pacing_frame_time(due, o->fps), duration)) {
                exit_code = MR_EXIT_FAIL;
                break;
            }
            next = due + 1;

            if (s.audio_running && InterlockedCompareExchange(&s.audio.failed, 0, 0)) {
                con_err(L"mrecord: system sound stopped, the rest is video only\n");
                s.audio_running = false;
            }
        }

        log_msg(LOG_INFO, L"stopping after %lld frames",
                (long long)s.encoder.video_samples);
    }

    if (s.audio_open) {
        audio_stop(&s.audio);
        audio_close(&s.audio);
    }
    if (s.border_on) border_stop(&s.border);
    capture_close(&s.capture);
    if (s.canvas_open) cursor_canvas_close(&s.canvas);

    int64_t samples = s.encoder.video_samples;
    bool finalized = encoder_close(&s.encoder);

    if (samples == 0) {
        DeleteFileW(path);
        return exit_code == MR_EXIT_OK ? MR_EXIT_CANCELLED : exit_code;
    }
    if (!finalized) {
        con_err(L"mrecord: %ls could not be finalized and may not play\n", path);
        return MR_EXIT_FAIL;
    }
    con_out(L"%ls\n", path);
    return exit_code;
}

static int record(const Options *o) {
    Control control;
    if (!control_acquire(&control)) {
        con_err(L"mrecord: a recording is already running (mrecord --stop ends it)\n");
        return MR_EXIT_FAIL;
    }
    s_stop_event = control.stop;
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);

    int code = MR_EXIT_OK;
    Target target;
    ZeroMemory(&target, sizeof target);

    if (o->region == REGION_SELECT) {
        code = resolve_target(o, &target);
        if (code == MR_EXIT_OK && wait_or_stop(o->delay_s)) code = MR_EXIT_CANCELLED;
    } else {
        if (wait_or_stop(o->delay_s)) code = MR_EXIT_CANCELLED;
        else code = resolve_target(o, &target);
    }

    wchar_t path[ARGS_PATH_MAX];
    if (code == MR_EXIT_OK && !output_path(o, path, ARGS_PATH_MAX)) {
        con_err(L"mrecord: cannot work out where to save the recording\n");
        code = MR_EXIT_FAIL;
    }

    if (code == MR_EXIT_OK) {
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) {
            con_err(L"mrecord: Media Foundation is unavailable (0x%08lX)\n", (unsigned long)hr);
            code = MR_EXIT_FAIL;
        } else {
            code = run_session(o, &target, path, &control);
            MFShutdown();
        }
    }

    control_finished(&control);
    SetConsoleCtrlHandler(on_console_ctrl, FALSE);
    s_stop_event = NULL;
    control_release(&control);
    return code;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return MR_EXIT_FAIL;

    Options o;
    if (!args_parse(argc, argv, &o)) {
        con_err(L"mrecord: %ls\nTry 'mrecord --help'.\n", o.error);
        LocalFree(argv);
        return MR_EXIT_USAGE;
    }
    LocalFree(argv);

    switch (o.command) {
    case CMD_HELP:
        con_out(L"%ls", USAGE);
        return MR_EXIT_OK;
    case CMD_VERSION:
        con_out(L"mrecord %ls\n", MRECORD_VERSION_W);
        return MR_EXIT_OK;
    case CMD_LIST:
        monitors_print();
        return MR_EXIT_OK;
    case CMD_STOP:
        return control_stop_running();
    case CMD_TOGGLE:
        if (control_running()) return control_stop_running();
        break;
    case CMD_RECORD:
        break;
    }

    log_init(L"mrecord", LOG_INFO);
    timeBeginPeriod(1);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    int code = record(&o);

    CoUninitialize();
    timeEndPeriod(1);
    log_msg(LOG_INFO, L"exit %d", code);
    log_shutdown();
    return code;
}
