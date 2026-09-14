#ifndef MRECORD_GEOM_H
#define MRECORD_GEOM_H

#include <stdbool.h>

typedef struct {
    int left, top, right, bottom;
} GRect;

typedef enum {
    ROT_UNSPECIFIED = 0,
    ROT_IDENTITY    = 1,
    ROT_90          = 2,
    ROT_180         = 3,
    ROT_270         = 4,
} Rotation;

GRect grect_make(int x, int y, int w, int h);
GRect grect_from_points(int x0, int y0, int x1, int y1);
int   grect_width(GRect r);
int   grect_height(GRect r);
bool  grect_empty(GRect r);
bool  grect_contains_point(GRect r, int x, int y);
bool  grect_contains_rect(GRect outer, GRect inner);
GRect grect_intersect(GRect a, GRect b);
GRect grect_even(GRect r);
void  grect_clamp_point(GRect bounds, int *x, int *y);

int   geom_monitor_at(const GRect *monitors, int count, int x, int y);
int   geom_monitor_containing(const GRect *monitors, int count, GRect r);
int   geom_monitor_most_overlap(const GRect *monitors, int count, GRect r);
void  geom_sort_order(const GRect *monitors, int count, int *order);

void  geom_border_bars(GRect region, GRect bounds, int thickness, GRect bars[4]);

void  geom_desktop_to_texture(Rotation rotation, int tex_w, int tex_h,
                              int dx, int dy, int *tx, int *ty);

#endif
