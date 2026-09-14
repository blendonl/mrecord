#include "mrecord.h"

#include <windowsx.h>

#define SELECT_CLASS   L"mrecord_Select"
#define DIM_ALPHA      110
#define HOLE_COLOR     RGB(255, 0, 255)
#define EDGE_COLOR     RGB(255, 255, 255)
#define LABEL_BG       RGB(20, 20, 20)
#define DRAG_THRESHOLD 4
#define LABEL_MARGIN   260
#define HOTKEY_ESCAPE  1
#define HOTKEY_RETURN  2

typedef struct {
    HWND    hwnd;
    GRect   virt;
    Monitor monitors[MONITORS_MAX];
    GRect   monitor_rects[MONITORS_MAX];
    int     monitor_count;
    bool    dragging;
    bool    moved;
    bool    done;
    bool    cancelled;
    POINT   anchor;
    POINT   current;
    GRect   bounds;
    GRect   drawn;
    GRect   result;
    HFONT   font;
} SelectState;

static SelectState *s_sel;

static GRect selection(const SelectState *s) {
    return grect_from_points(s->anchor.x, s->anchor.y, s->current.x, s->current.y);
}

static RECT to_client(const SelectState *s, GRect r) {
    RECT out = { r.left - s->virt.left, r.top - s->virt.top,
                 r.right - s->virt.left, r.bottom - s->virt.top };
    return out;
}

static void invalidate_around(SelectState *s, GRect r) {
    if (grect_empty(r)) return;
    GRect grown = { r.left - 4, r.top - 4, r.right + LABEL_MARGIN, r.bottom + 80 };
    RECT rc = to_client(s, grown);
    InvalidateRect(s->hwnd, &rc, FALSE);
}

static void refresh(SelectState *s) {
    GRect now = (s->dragging && s->moved) ? selection(s) : (GRect){ 0, 0, 0, 0 };
    invalidate_around(s, s->drawn);
    invalidate_around(s, now);
    s->drawn = now;
}

static POINT screen_point(const SelectState *s, LPARAM lp) {
    POINT p = { GET_X_LPARAM(lp) + s->virt.left, GET_Y_LPARAM(lp) + s->virt.top };
    return p;
}

static GRect monitor_under(const SelectState *s, int x, int y) {
    int i = geom_monitor_at(s->monitor_rects, s->monitor_count, x, y);
    return i >= 0 ? s->monitor_rects[i] : s->virt;
}

static void finish(SelectState *s, GRect r, bool cancelled) {
    s->result    = r;
    s->cancelled = cancelled;
    s->done      = true;
    PostMessageW(s->hwnd, WM_NULL, 0, 0);
}

static void pick_window(SelectState *s, POINT pt) {
    LONG_PTR ex = GetWindowLongPtrW(s->hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(s->hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    HWND hit = WindowFromPoint(pt);
    SetWindowLongPtrW(s->hwnd, GWL_EXSTYLE, ex);

    GRect bounds = monitor_under(s, pt.x, pt.y);
    GRect frame;
    if (hit) hit = GetAncestor(hit, GA_ROOT);
    if (hit && hit != s->hwnd && window_visible_frame(hit, &frame)) {
        GRect r = grect_intersect(frame, bounds);
        if (!grect_empty(r)) {
            finish(s, r, false);
            return;
        }
    }
    finish(s, bounds, false);
}

static void confirm_keyboard(SelectState *s) {
    if (s->dragging && s->moved) {
        finish(s, selection(s), false);
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    finish(s, monitor_under(s, pt.x, pt.y), false);
}

static void paint_label(SelectState *s, HDC dc, GRect sel) {
    wchar_t text[48];
    int n = _snwprintf(text, 48, L" %d \u00d7 %d ", grect_width(sel), grect_height(sel));
    if (n <= 0) return;

    HGDIOBJ old = SelectObject(dc, s->font);
    SIZE size;
    GetTextExtentPoint32W(dc, text, n, &size);

    int x = sel.right - size.cx;
    int y = sel.bottom + 6;
    if (y + size.cy > s->bounds.bottom) y = sel.bottom - size.cy - 6;
    if (x < s->bounds.left) x = s->bounds.left;

    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, LABEL_BG);
    SetTextColor(dc, EDGE_COLOR);
    TextOutW(dc, x - s->virt.left, y - s->virt.top, text, n);
    SelectObject(dc, old);
}

static void paint(SelectState *s) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(s->hwnd, &ps);
    int w = ps.rcPaint.right - ps.rcPaint.left;
    int h = ps.rcPaint.bottom - ps.rcPaint.top;
    if (w <= 0 || h <= 0) {
        EndPaint(s->hwnd, &ps);
        return;
    }

    HDC     mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetViewportOrgEx(mem, -ps.rcPaint.left, -ps.rcPaint.top, NULL);

    HBRUSH dim = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(mem, &ps.rcPaint, dim);

    if (s->dragging && s->moved) {
        GRect sel  = selection(s);
        RECT  edge = to_client(s, (GRect){ sel.left - 2, sel.top - 2, sel.right + 2, sel.bottom + 2 });
        RECT  hole = to_client(s, sel);

        HBRUSH edge_brush = CreateSolidBrush(EDGE_COLOR);
        HBRUSH hole_brush = CreateSolidBrush(HOLE_COLOR);
        FillRect(mem, &edge, edge_brush);
        FillRect(mem, &hole, hole_brush);
        DeleteObject(edge_brush);
        DeleteObject(hole_brush);

        paint_label(s, mem, sel);
    }

    SetViewportOrgEx(mem, 0, 0, NULL);
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(s->hwnd, &ps);
}

static LRESULT CALLBACK select_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SelectState *s = s_sel;
    if (!s || s->hwnd != hwnd) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_CROSS));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(s);
        return 0;

    case WM_LBUTTONDOWN:
        s->anchor   = screen_point(s, lp);
        s->current  = s->anchor;
        s->bounds   = monitor_under(s, s->anchor.x, s->anchor.y);
        s->dragging = true;
        s->moved    = false;
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (s->dragging) {
            POINT p = screen_point(s, lp);
            int x = p.x, y = p.y;
            grect_clamp_point(s->bounds, &x, &y);
            s->current.x = x;
            s->current.y = y;
            if (abs(x - s->anchor.x) >= DRAG_THRESHOLD || abs(y - s->anchor.y) >= DRAG_THRESHOLD)
                s->moved = true;
            refresh(s);
        }
        return 0;

    case WM_LBUTTONUP:
        if (s->dragging) {
            ReleaseCapture();
            GRect sel = selection(s);
            bool moved = s->moved;
            s->dragging = false;
            if (moved && grect_width(sel) >= 2 && grect_height(sel) >= 2)
                finish(s, sel, false);
            else
                pick_window(s, s->anchor);
        }
        return 0;

    case WM_RBUTTONDOWN:
        finish(s, (GRect){ 0, 0, 0, 0 }, true);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) finish(s, (GRect){ 0, 0, 0, 0 }, true);
        if (wp == VK_RETURN) confirm_keyboard(s);
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_ESCAPE) finish(s, (GRect){ 0, 0, 0, 0 }, true);
        if (wp == HOTKEY_RETURN) confirm_keyboard(s);
        return 0;

    case WM_CLOSE:
        finish(s, (GRect){ 0, 0, 0, 0 }, true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

SelectResult select_region(GRect *out) {
    SelectState s;
    ZeroMemory(&s, sizeof s);

    s.virt.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    s.virt.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s.virt.right  = s.virt.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s.virt.bottom = s.virt.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    s.monitor_count = monitors_list(s.monitors, MONITORS_MAX);
    for (int i = 0; i < s.monitor_count; i++) s.monitor_rects[i] = s.monitors[i].rect;

    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = select_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_CROSS);
    wc.lpszClassName = SELECT_CLASS;
    RegisterClassW(&wc);

    s.font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Segoe UI");

    s_sel = &s;
    s.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                             SELECT_CLASS, L"mrecord", WS_POPUP,
                             s.virt.left, s.virt.top,
                             grect_width(s.virt), grect_height(s.virt),
                             NULL, NULL, inst, NULL);
    if (!s.hwnd) {
        log_msg(LOG_ERROR, L"select: overlay window failed (%lu)", GetLastError());
        s_sel = NULL;
        if (s.font) DeleteObject(s.font);
        UnregisterClassW(SELECT_CLASS, inst);
        return SELECT_FAILED;
    }

    SetLayeredWindowAttributes(s.hwnd, HOLE_COLOR, DIM_ALPHA, LWA_ALPHA | LWA_COLORKEY);
    ShowWindow(s.hwnd, SW_SHOW);
    UpdateWindow(s.hwnd);
    SetForegroundWindow(s.hwnd);
    bool focused    = GetForegroundWindow() == s.hwnd;
    bool esc_hotkey = !focused && RegisterHotKey(s.hwnd, HOTKEY_ESCAPE, MOD_NOREPEAT, VK_ESCAPE);
    bool ret_hotkey = !focused && RegisterHotKey(s.hwnd, HOTKEY_RETURN, MOD_NOREPEAT, VK_RETURN);
    if (!focused)
        log_msg(LOG_INFO, L"select: the overlay did not get the foreground; Esc and Enter "
                          L"are taken as global hotkeys until the selection ends");

    MSG msg;
    while (!s.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (esc_hotkey) UnregisterHotKey(s.hwnd, HOTKEY_ESCAPE);
    if (ret_hotkey) UnregisterHotKey(s.hwnd, HOTKEY_RETURN);
    DestroyWindow(s.hwnd);
    s_sel = NULL;
    if (s.font) DeleteObject(s.font);
    UnregisterClassW(SELECT_CLASS, inst);

    if (!s.done || s.cancelled) return SELECT_CANCELLED;
    *out = s.result;
    return grect_empty(s.result) ? SELECT_CANCELLED : SELECT_OK;
}
