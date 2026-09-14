#include "mrecord.h"

#include <dwmapi.h>

typedef struct {
    Monitor *items;
    int      count;
    int      cap;
} MonitorBag;

static BOOL CALLBACK collect_monitor(HMONITOR h, HDC dc, LPRECT r, LPARAM lp) {
    MonitorBag *bag = (MonitorBag *)lp;
    if (bag->count >= bag->cap) return FALSE;

    MONITORINFOEXW mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(h, (MONITORINFO *)&mi)) return TRUE;

    Monitor *m = &bag->items[bag->count++];
    m->handle      = h;
    m->rect.left   = mi.rcMonitor.left;
    m->rect.top    = mi.rcMonitor.top;
    m->rect.right  = mi.rcMonitor.right;
    m->rect.bottom = mi.rcMonitor.bottom;
    m->primary     = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    lstrcpynW(m->device, mi.szDevice, CCHDEVICENAME);
    return TRUE;
}

int monitors_list(Monitor *out, int cap) {
    if (cap > MONITORS_MAX) cap = MONITORS_MAX;

    Monitor    raw[MONITORS_MAX];
    MonitorBag bag = { raw, 0, cap };
    EnumDisplayMonitors(NULL, NULL, collect_monitor, (LPARAM)&bag);

    GRect rects[MONITORS_MAX];
    int   order[MONITORS_MAX];
    for (int i = 0; i < bag.count; i++) rects[i] = raw[i].rect;
    geom_sort_order(rects, bag.count, order);

    for (int i = 0; i < bag.count; i++) out[i] = raw[order[i]];
    return bag.count;
}

int monitors_find(const Monitor *mons, int count, HMONITOR handle) {
    for (int i = 0; i < count; i++)
        if (mons[i].handle == handle) return i;
    return -1;
}

void monitors_print(void) {
    Monitor mons[MONITORS_MAX];
    int n = monitors_list(mons, MONITORS_MAX);
    for (int i = 0; i < n; i++) {
        con_out(L"%d %ls %d,%d,%d,%d%ls\n", i, mons[i].device,
                mons[i].rect.left, mons[i].rect.top,
                grect_width(mons[i].rect), grect_height(mons[i].rect),
                mons[i].primary ? L" primary" : L"");
    }
}

bool window_visible_frame(HWND hwnd, GRect *out) {
    RECT r;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)) &&
        !GetWindowRect(hwnd, &r))
        return false;

    out->left   = r.left;
    out->top    = r.top;
    out->right  = r.right;
    out->bottom = r.bottom;
    return !grect_empty(*out);
}
