#include "mrecord.h"

#define BORDER_THICKNESS 3
#define BORDER_TIMER     1
#define BORDER_COLOR     RGB(230, 40, 40)

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

static LRESULT CALLBACK border_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_CLOSE:
        control_request_stop();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI border_thread(LPVOID param) {
    Border *b = (Border *)param;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = border_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hbrBackground = CreateSolidBrush(BORDER_COLOR);
    wc.lpszClassName = L"mrecord_Border";
    RegisterClassW(&wc);

    GRect bars[4];
    geom_border_bars(b->region, b->bounds, BORDER_THICKNESS, bars);

    HWND windows[4] = {0};
    bool excluded   = true;
    for (int i = 0; i < 4; i++) {
        if (grect_empty(bars[i])) continue;
        windows[i] = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                WS_EX_TRANSPARENT | WS_EX_LAYERED,
            wc.lpszClassName, L"mrecord", WS_POPUP,
            bars[i].left, bars[i].top, grect_width(bars[i]), grect_height(bars[i]),
            NULL, NULL, wc.hInstance, NULL);
        if (!windows[i]) continue;
        SetLayeredWindowAttributes(windows[i], 0, 255, LWA_ALPHA);
        if (!SetWindowDisplayAffinity(windows[i], WDA_EXCLUDEFROMCAPTURE))
            excluded = false;
        ShowWindow(windows[i], SW_SHOWNOACTIVATE);
        SetTimer(windows[i], BORDER_TIMER, 1000, NULL);
    }
    if (!excluded)
        log_msg(LOG_WARN, L"border: this Windows cannot exclude the frame from "
                          L"capture (%lu); it may show at the recording's edge",
                GetLastError());

    SetEvent(b->ready);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (int i = 0; i < 4; i++)
        if (windows[i]) DestroyWindow(windows[i]);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    DeleteObject(wc.hbrBackground);
    return 0;
}

bool border_start(Border *b, GRect region, GRect bounds) {
    ZeroMemory(b, sizeof *b);
    b->region = region;
    b->bounds = bounds;
    b->ready  = CreateEventW(NULL, TRUE, FALSE, NULL);
    b->thread = CreateThread(NULL, 0, border_thread, b, 0, &b->thread_id);
    if (!b->thread) {
        if (b->ready) CloseHandle(b->ready);
        ZeroMemory(b, sizeof *b);
        return false;
    }
    if (b->ready) WaitForSingleObject(b->ready, 2000);
    return true;
}

void border_stop(Border *b) {
    if (!b->thread) return;
    PostThreadMessageW(b->thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(b->thread, 2000);
    CloseHandle(b->thread);
    if (b->ready) CloseHandle(b->ready);
    ZeroMemory(b, sizeof *b);
}
