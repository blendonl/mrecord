#include "geom.h"

GRect grect_make(int x, int y, int w, int h) {
    GRect r = { x, y, x + w, y + h };
    return r;
}

GRect grect_from_points(int x0, int y0, int x1, int y1) {
    GRect r;
    r.left   = x0 < x1 ? x0 : x1;
    r.right  = x0 < x1 ? x1 : x0;
    r.top    = y0 < y1 ? y0 : y1;
    r.bottom = y0 < y1 ? y1 : y0;
    return r;
}

int grect_width(GRect r)  { return r.right - r.left; }
int grect_height(GRect r) { return r.bottom - r.top; }

bool grect_empty(GRect r) {
    return r.right <= r.left || r.bottom <= r.top;
}

bool grect_contains_point(GRect r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

bool grect_contains_rect(GRect outer, GRect inner) {
    if (grect_empty(inner)) return false;
    return inner.left >= outer.left && inner.right <= outer.right &&
           inner.top >= outer.top && inner.bottom <= outer.bottom;
}

GRect grect_intersect(GRect a, GRect b) {
    GRect r;
    r.left   = a.left   > b.left   ? a.left   : b.left;
    r.top    = a.top    > b.top    ? a.top    : b.top;
    r.right  = a.right  < b.right  ? a.right  : b.right;
    r.bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    if (grect_empty(r)) {
        GRect none = { 0, 0, 0, 0 };
        return none;
    }
    return r;
}

GRect grect_even(GRect r) {
    if (grect_empty(r)) return r;
    r.right  -= grect_width(r) % 2;
    r.bottom -= grect_height(r) % 2;
    return r;
}

void grect_clamp_point(GRect bounds, int *x, int *y) {
    if (*x < bounds.left)   *x = bounds.left;
    if (*x > bounds.right)  *x = bounds.right;
    if (*y < bounds.top)    *y = bounds.top;
    if (*y > bounds.bottom) *y = bounds.bottom;
}

int geom_monitor_at(const GRect *monitors, int count, int x, int y) {
    for (int i = 0; i < count; i++)
        if (grect_contains_point(monitors[i], x, y)) return i;
    return -1;
}

int geom_monitor_containing(const GRect *monitors, int count, GRect r) {
    for (int i = 0; i < count; i++)
        if (grect_contains_rect(monitors[i], r)) return i;
    return -1;
}

int geom_monitor_most_overlap(const GRect *monitors, int count, GRect r) {
    int  best = -1;
    long best_area = 0;
    for (int i = 0; i < count; i++) {
        GRect hit  = grect_intersect(monitors[i], r);
        long  area = (long)grect_width(hit) * grect_height(hit);
        if (area > best_area) {
            best_area = area;
            best      = i;
        }
    }
    return best;
}

static bool placed_before(GRect a, GRect b) {
    if (a.left != b.left) return a.left < b.left;
    return a.top < b.top;
}

void geom_sort_order(const GRect *monitors, int count, int *order) {
    for (int i = 0; i < count; i++) order[i] = i;
    for (int i = 1; i < count; i++) {
        int moving = order[i];
        int j = i - 1;
        while (j >= 0 && placed_before(monitors[moving], monitors[order[j]])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = moving;
    }
}

void geom_border_bars(GRect region, GRect bounds, int t, GRect bars[4]) {
    bool out_l = region.left - t   >= bounds.left;
    bool out_t = region.top - t    >= bounds.top;
    bool out_r = region.right + t  <= bounds.right;
    bool out_b = region.bottom + t <= bounds.bottom;

    int x0 = out_l ? region.left - t : region.left;
    int x1 = out_r ? region.right + t : region.right;

    bars[0].left   = x0;
    bars[0].right  = x1;
    bars[0].top    = out_t ? region.top - t : region.top;
    bars[0].bottom = out_t ? region.top : region.top + t;

    bars[1].left   = x0;
    bars[1].right  = x1;
    bars[1].top    = out_b ? region.bottom : region.bottom - t;
    bars[1].bottom = out_b ? region.bottom + t : region.bottom;

    bars[2].left   = out_l ? region.left - t : region.left;
    bars[2].right  = out_l ? region.left : region.left + t;
    bars[2].top    = region.top;
    bars[2].bottom = region.bottom;

    bars[3].left   = out_r ? region.right : region.right - t;
    bars[3].right  = out_r ? region.right + t : region.right;
    bars[3].top    = region.top;
    bars[3].bottom = region.bottom;
}

void geom_desktop_to_texture(Rotation rotation, int tex_w, int tex_h,
                             int dx, int dy, int *tx, int *ty) {
    switch (rotation) {
    case ROT_90:
        *tx = dy;
        *ty = tex_h - 1 - dx;
        break;
    case ROT_180:
        *tx = tex_w - 1 - dx;
        *ty = tex_h - 1 - dy;
        break;
    case ROT_270:
        *tx = tex_w - 1 - dy;
        *ty = dx;
        break;
    default:
        *tx = dx;
        *ty = dy;
        break;
    }
}
