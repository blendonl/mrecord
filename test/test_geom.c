#include "tests.h"
#include "../src/geom.h"

static bool same(GRect a, GRect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static void test_rects(void) {
    GRect r = grect_make(10, 20, 30, 40);
    CHECK(grect_width(r) == 30 && grect_height(r) == 40, "make");

    CHECK(same(grect_from_points(50, 60, 10, 20), grect_make(10, 20, 40, 40)),
          "points normalise");

    CHECK(grect_contains_point(r, 10, 20), "top-left inside");
    CHECK(!grect_contains_point(r, 40, 20), "right edge outside");

    GRect mon = grect_make(0, 0, 1920, 1080);
    CHECK(grect_contains_rect(mon, grect_make(0, 0, 1920, 1080)), "whole monitor inside");
    CHECK(!grect_contains_rect(mon, grect_make(1900, 0, 40, 10)), "spill outside");
    CHECK(!grect_contains_rect(mon, grect_make(5, 5, 0, 10)), "empty is not contained");

    CHECK(same(grect_intersect(mon, grect_make(-10, -10, 20, 20)), grect_make(0, 0, 10, 10)),
          "intersect clips");
    CHECK(grect_empty(grect_intersect(mon, grect_make(2000, 0, 10, 10))), "disjoint empty");

    CHECK(same(grect_even(grect_make(1, 1, 641, 481)), grect_make(1, 1, 640, 480)), "even rounds down");
    CHECK(same(grect_even(grect_make(0, 0, 2, 2)), grect_make(0, 0, 2, 2)), "even stays");

    int x = -5, y = 2000;
    grect_clamp_point(mon, &x, &y);
    CHECK(x == 0 && y == 1080, "clamp point to %d,%d", x, y);
}

static void test_monitors(void) {
    GRect mons[3] = {
        grect_make(1920, 0, 1920, 1080),
        grect_make(0, 0, 1920, 1080),
        grect_make(0, -1080, 1920, 1080),
    };

    CHECK(geom_monitor_at(mons, 3, 2000, 10) == 0, "point on right monitor");
    CHECK(geom_monitor_at(mons, 3, 5, -5) == 2, "point on upper monitor");
    CHECK(geom_monitor_at(mons, 3, 5000, 5) == -1, "point nowhere");

    CHECK(geom_monitor_containing(mons, 3, grect_make(100, 100, 50, 50)) == 1, "rect contained");
    CHECK(geom_monitor_containing(mons, 3, grect_make(1900, 100, 50, 50)) == -1, "spanning rect");
    CHECK(geom_monitor_most_overlap(mons, 3, grect_make(1900, 100, 50, 50)) == 0, "overlap picks bigger share");

    int order[3];
    geom_sort_order(mons, 3, order);
    CHECK(order[0] == 2 && order[1] == 1 && order[2] == 0,
          "sorted by left then top: %d %d %d", order[0], order[1], order[2]);
}

static void test_border(void) {
    GRect mon = grect_make(0, 0, 1920, 1080);
    GRect bars[4];

    geom_border_bars(grect_make(100, 100, 200, 100), mon, 3, bars);
    CHECK(same(bars[0], (GRect){ 97, 97, 303, 100 }), "top outside");
    CHECK(same(bars[1], (GRect){ 97, 200, 303, 203 }), "bottom outside");
    CHECK(same(bars[2], (GRect){ 97, 100, 100, 200 }), "left outside");
    CHECK(same(bars[3], (GRect){ 300, 100, 303, 200 }), "right outside");

    geom_border_bars(mon, mon, 3, bars);
    CHECK(same(bars[0], (GRect){ 0, 0, 1920, 3 }), "top inside");
    CHECK(same(bars[1], (GRect){ 0, 1077, 1920, 1080 }), "bottom inside");
    CHECK(same(bars[2], (GRect){ 0, 0, 3, 1080 }), "left inside");
    CHECK(same(bars[3], (GRect){ 1917, 0, 1920, 1080 }), "right inside");

    for (int i = 0; i < 4; i++)
        CHECK(grect_contains_rect(mon, bars[i]), "bar %d stays on the monitor", i);
}

static void test_rotation(void) {
    int tx, ty;
    geom_desktop_to_texture(ROT_IDENTITY, 1920, 1080, 5, 7, &tx, &ty);
    CHECK(tx == 5 && ty == 7, "identity");

    geom_desktop_to_texture(ROT_90, 1920, 1080, 0, 0, &tx, &ty);
    CHECK(tx == 0 && ty == 1079, "rot90 origin -> %d,%d", tx, ty);

    geom_desktop_to_texture(ROT_180, 1920, 1080, 0, 0, &tx, &ty);
    CHECK(tx == 1919 && ty == 1079, "rot180 origin");

    geom_desktop_to_texture(ROT_270, 1920, 1080, 0, 0, &tx, &ty);
    CHECK(tx == 1919 && ty == 0, "rot270 origin");

    static unsigned char seen[1920 * 1080 / 64];
    int dups = 0, outside = 0;
    for (int rot = ROT_90; rot <= ROT_270; rot++) {
        memset(seen, 0, sizeof seen);
        int tw = 1920 / 8, th = 1080 / 8;
        int dw = rot == ROT_180 ? tw : th;
        int dh = rot == ROT_180 ? th : tw;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                geom_desktop_to_texture((Rotation)rot, tw, th, x, y, &tx, &ty);
                if (tx < 0 || ty < 0 || tx >= tw || ty >= th) { outside++; continue; }
                if (seen[ty * tw + tx]++) dups++;
            }
    }
    CHECK(outside == 0, "rotations stay inside the texture (%d outside)", outside);
    CHECK(dups == 0, "rotations are one-to-one (%d duplicates)", dups);
}

int main(void) {
    test_rects();
    test_monitors();
    test_border();
    test_rotation();
    return tests_report("geom");
}
