#include "tests.h"
#include "../src/convert.h"

#include <stdint.h>

static void fill(uint8_t *bgra, int count, uint8_t b, uint8_t g, uint8_t r) {
    for (int i = 0; i < count; i++) {
        bgra[i * 4]     = b;
        bgra[i * 4 + 1] = g;
        bgra[i * 4 + 2] = r;
        bgra[i * 4 + 3] = 255;
    }
}

static void test_nv12(void) {
    uint8_t bgra[4 * 4 * 2];
    uint8_t nv12[4 * 2 * 3 / 2];

    fill(bgra, 8, 255, 255, 255);
    convert_bgra_to_nv12(bgra, 4, 2, nv12);
    CHECK(nv12[0] == 235 && nv12[7] == 235, "white luma %d", nv12[0]);
    CHECK(nv12[8] == 128 && nv12[9] == 128, "white chroma %d %d", nv12[8], nv12[9]);

    fill(bgra, 8, 0, 0, 0);
    convert_bgra_to_nv12(bgra, 4, 2, nv12);
    CHECK(nv12[0] == 16, "black luma %d", nv12[0]);
    CHECK(nv12[10] == 128 && nv12[11] == 128, "black chroma");

    fill(bgra, 8, 0, 0, 255);
    convert_bgra_to_nv12(bgra, 4, 2, nv12);
    CHECK(nv12[0] >= 60 && nv12[0] <= 64, "red luma %d", nv12[0]);
    CHECK(nv12[9] > 230, "red has high V %d", nv12[9]);
    CHECK(nv12[8] < 128, "red has low U %d", nv12[8]);

    fill(bgra, 8, 255, 0, 0);
    convert_bgra_to_nv12(bgra, 4, 2, nv12);
    CHECK(nv12[8] > 230, "blue has high U %d", nv12[8]);
}

static void test_samples(void) {
    int16_t out[8];

    float fl[4] = { 1.0f, -1.0f, 0.5f, 2.0f };
    CHECK(convert_to_s16_stereo((const uint8_t *)fl, 2, 2, 32, true, out) == 2, "float frames");
    CHECK(out[0] == 32767 && out[1] == -32767, "float full scale %d %d", out[0], out[1]);
    CHECK(out[2] == 16383 && out[3] == 32767, "float half and clamp %d %d", out[2], out[3]);

    int16_t s16[2] = { 1234, -4321 };
    convert_to_s16_stereo((const uint8_t *)s16, 1, 1, 16, false, out);
    CHECK(out[0] == 1234 && out[1] == 1234, "mono duplicates");

    float surround[6] = { 0.25f, -0.25f, 1, 1, 1, 1 };
    convert_to_s16_stereo((const uint8_t *)surround, 1, 6, 32, true, out);
    CHECK(out[0] == 8191 && out[1] == -8191, "surround keeps front pair %d %d", out[0], out[1]);

    uint8_t s24[3] = { 0x00, 0x00, 0x80 };
    convert_to_s16_stereo(s24, 1, 1, 24, false, out);
    CHECK(out[0] == -32768, "24-bit minimum %d", out[0]);

    int32_t s32[1] = { 0x40000000 };
    convert_to_s16_stereo((const uint8_t *)s32, 1, 1, 32, false, out);
    CHECK(out[0] == 16384, "32-bit int %d", out[0]);

    CHECK(convert_to_s16_stereo((const uint8_t *)s32, 1, 1, 12, false, out) == 0, "odd bit depth refused");
}

static void test_decimate(void) {
    CHECK(convert_decimation_factor(48000) == 1, "48k native");
    CHECK(convert_decimation_factor(96000) == 2, "96k halves");
    CHECK(convert_decimation_factor(192000) == 4, "192k quarters");
    CHECK(convert_decimation_factor(32000) == 0, "32k unsupported");

    Decimator d;
    decimator_init(&d, 2);
    int16_t in[6] = { 10, 20, 30, 40, 50, 60 };
    int16_t out[6];
    size_t n = decimator_run(&d, in, 3, out);
    CHECK(n == 1 && out[0] == 20 && out[1] == 30, "first pair averaged");
    int16_t more[2] = { 70, 80 };
    n = decimator_run(&d, more, 1, out);
    CHECK(n == 1 && out[0] == 60 && out[1] == 70, "held frame carries across calls %d %d", out[0], out[1]);

    decimator_init(&d, 1);
    n = decimator_run(&d, in, 3, in);
    CHECK(n == 3 && in[4] == 50, "factor 1 passes through");
}

int main(void) {
    test_nv12();
    test_samples();
    test_decimate();
    return tests_report("convert");
}
