#include "mrecord.h"

bool cursor_canvas_open(CursorCanvas *c, int width, int height) {
    ZeroMemory(c, sizeof *c);

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    c->dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!c->dc) return false;

    void *bits = NULL;
    c->dib = CreateDIBSection(c->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!c->dib || !bits) {
        cursor_canvas_close(c);
        return false;
    }

    c->old    = SelectObject(c->dc, c->dib);
    c->bits   = (BYTE *)bits;
    c->width  = width;
    c->height = height;
    return true;
}

const BYTE *cursor_compose(CursorCanvas *c, const BYTE *frame, GRect region) {
    memcpy(c->bits, frame, (size_t)c->width * c->height * 4);

    CURSORINFO ci = { .cbSize = sizeof ci };
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return c->bits;

    ICONINFO ii;
    if (!GetIconInfo(ci.hCursor, &ii)) return c->bits;

    int x = ci.ptScreenPos.x - (int)ii.xHotspot - region.left;
    int y = ci.ptScreenPos.y - (int)ii.yHotspot - region.top;
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);

    if (x < c->width && y < c->height && x > -256 && y > -256) {
        DrawIconEx(c->dc, x, y, ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);
        GdiFlush();
    }
    return c->bits;
}

void cursor_canvas_close(CursorCanvas *c) {
    if (c->dc && c->old) SelectObject(c->dc, c->old);
    if (c->dib) DeleteObject(c->dib);
    if (c->dc)  DeleteDC(c->dc);
    ZeroMemory(c, sizeof *c);
}
