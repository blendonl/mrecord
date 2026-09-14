#include "mrecord.h"

#define PATH_BYTES (sizeof(wchar_t) * ARGS_PATH_MAX)

static Control *s_control;

static void object_name(wchar_t *out, size_t cap, const wchar_t *what) {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    _snwprintf(out, cap, L"Local\\mrecord-%ls-%lu", what, (unsigned long)session);
    out[cap - 1] = L'\0';
}

static LRESULT CALLBACK control_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        control_request_stop();
        return 0;
    case WM_QUERYENDSESSION:
        control_request_stop();
        return TRUE;
    case WM_ENDSESSION:
        if (wp && s_control) {
            SetEvent(s_control->stop);
            WaitForSingleObject(s_control->finished, 15000);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI control_thread(LPVOID param) {
    HANDLE ready = (HANDLE)param;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = control_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"mrecord_Control";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                wc.lpszClassName, L"mrecord",
                                WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (hwnd) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetEvent(ready);
    if (!hwnd) return 1;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(hwnd);
    return 0;
}

bool control_acquire(Control *c) {
    ZeroMemory(c, sizeof *c);
    wchar_t name[96];

    object_name(name, 96, L"running");
    c->mutex = CreateMutexW(NULL, TRUE, name);
    if (!c->mutex) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(c->mutex);
        c->mutex = NULL;
        return false;
    }

    object_name(name, 96, L"stop");
    c->stop = CreateEventW(NULL, TRUE, FALSE, name);
    if (c->stop) ResetEvent(c->stop);
    c->finished = CreateEventW(NULL, TRUE, FALSE, NULL);

    object_name(name, 96, L"path");
    c->path_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     0, (DWORD)PATH_BYTES, name);

    s_control = c;
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    c->thread = CreateThread(NULL, 0, control_thread, ready, 0, &c->thread_id);
    if (c->thread && ready) WaitForSingleObject(ready, 2000);
    if (ready) CloseHandle(ready);

    return c->stop != NULL;
}

void control_publish_path(Control *c, const wchar_t *path) {
    if (!c->path_map) return;
    wchar_t *view = (wchar_t *)MapViewOfFile(c->path_map, FILE_MAP_WRITE, 0, 0, PATH_BYTES);
    if (!view) return;
    lstrcpynW(view, path, ARGS_PATH_MAX);
    UnmapViewOfFile(view);
}

void control_request_stop(void) {
    if (s_control && s_control->stop) SetEvent(s_control->stop);
}

void control_finished(Control *c) {
    if (c->finished) SetEvent(c->finished);
}

void control_release(Control *c) {
    if (c->thread) {
        PostThreadMessageW(c->thread_id, WM_QUIT, 0, 0);
        WaitForSingleObject(c->thread, 2000);
        CloseHandle(c->thread);
    }
    s_control = NULL;
    if (c->path_map) CloseHandle(c->path_map);
    if (c->finished) CloseHandle(c->finished);
    if (c->stop) CloseHandle(c->stop);
    if (c->mutex) {
        ReleaseMutex(c->mutex);
        CloseHandle(c->mutex);
    }
    ZeroMemory(c, sizeof *c);
}

bool control_running(void) {
    wchar_t name[96];
    object_name(name, 96, L"running");
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!m) return false;
    CloseHandle(m);
    return true;
}

static void read_published_path(wchar_t *out) {
    out[0] = L'\0';
    wchar_t name[96];
    object_name(name, 96, L"path");
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!map) return;
    const wchar_t *view = (const wchar_t *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, PATH_BYTES);
    if (view) {
        lstrcpynW(out, view, ARGS_PATH_MAX);
        UnmapViewOfFile(view);
    }
    CloseHandle(map);
}

int control_stop_running(void) {
    wchar_t name[96];

    object_name(name, 96, L"running");
    HANDLE m = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    if (!m) {
        con_err(L"mrecord: no recording is running\n");
        return MR_EXIT_CANCELLED;
    }

    object_name(name, 96, L"stop");
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!ev) {
        CloseHandle(m);
        con_err(L"mrecord: could not reach the running recording (%lu)\n", GetLastError());
        return MR_EXIT_FAIL;
    }

    wchar_t path[ARGS_PATH_MAX];
    read_published_path(path);

    SetEvent(ev);
    DWORD w = WaitForSingleObject(m, 60000);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ReleaseMutex(m);
    CloseHandle(ev);
    CloseHandle(m);

    if (w == WAIT_TIMEOUT) {
        con_err(L"mrecord: the recording did not finish within 60 seconds\n");
        return MR_EXIT_FAIL;
    }
    if (path[0]) con_out(L"%ls\n", path);
    return MR_EXIT_OK;
}
