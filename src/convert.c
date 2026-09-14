#include "convert.h"

#include <string.h>

static uint8_t luma(int r, int g, int b) {
    return (uint8_t)(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
}

void convert_bgra_to_nv12(const uint8_t *bgra, int width, int height, uint8_t *nv12) {
    uint8_t *y_plane  = nv12;
    uint8_t *uv_plane = nv12 + (size_t)width * height;

    for (int y = 0; y < height; y++) {
        const uint8_t *row = bgra + (size_t)y * width * 4;
        uint8_t       *out = y_plane + (size_t)y * width;
        for (int x = 0; x < width; x++) {
            const uint8_t *px = row + x * 4;
            out[x] = luma(px[2], px[1], px[0]);
        }
    }

    for (int y = 0; y + 1 < height; y += 2) {
        const uint8_t *row0 = bgra + (size_t)y * width * 4;
        const uint8_t *row1 = row0 + (size_t)width * 4;
        uint8_t       *out  = uv_plane + (size_t)(y / 2) * width;
        for (int x = 0; x + 1 < width; x += 2) {
            int b = row0[x * 4]     + row0[x * 4 + 4] + row1[x * 4]     + row1[x * 4 + 4];
            int g = row0[x * 4 + 1] + row0[x * 4 + 5] + row1[x * 4 + 1] + row1[x * 4 + 5];
            int r = row0[x * 4 + 2] + row0[x * 4 + 6] + row1[x * 4 + 2] + row1[x * 4 + 6];
            b = (b + 2) / 4;
            g = (g + 2) / 4;
            r = (r + 2) / 4;
            out[x]     = (uint8_t)(((-26 * r - 86 * g + 112 * b + 128) >> 8) + 128);
            out[x + 1] = (uint8_t)(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
        }
    }
}

static int16_t clamp16(int32_t v) {
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int16_t sample_at(const uint8_t *p, int bits, bool is_float) {
    if (is_float && bits == 32) {
        float f;
        memcpy(&f, p, sizeof f);
        if (!(f == f)) return 0;
        if (f > 1.0f)  f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return clamp16((int32_t)(f * 32767.0f));
    }
    if (is_float && bits == 64) {
        double f;
        memcpy(&f, p, sizeof f);
        if (!(f == f)) return 0;
        if (f > 1.0)  f = 1.0;
        if (f < -1.0) f = -1.0;
        return clamp16((int32_t)(f * 32767.0));
    }
    switch (bits) {
    case 8:  return (int16_t)(((int)p[0] - 128) * 256);
    case 16: return (int16_t)(p[0] | (p[1] << 8));
    case 24: {
        int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 |
                              (uint32_t)p[2] << 24);
        return (int16_t)(v >> 16);
    }
    case 32: {
        int32_t v;
        memcpy(&v, p, sizeof v);
        return (int16_t)(v >> 16);
    }
    }
    return 0;
}

size_t convert_to_s16_stereo(const uint8_t *src, size_t frames, int channels,
                             int bits, bool is_float, int16_t *dst) {
    if (channels <= 0 || bits <= 0 || bits % 8) return 0;
    size_t bytes = (size_t)(bits / 8);
    size_t frame_bytes = bytes * (size_t)channels;

    for (size_t i = 0; i < frames; i++) {
        const uint8_t *frame = src + i * frame_bytes;
        int16_t l = sample_at(frame, bits, is_float);
        int16_t r = channels > 1 ? sample_at(frame + bytes, bits, is_float) : l;
        dst[i * 2]     = l;
        dst[i * 2 + 1] = r;
    }
    return frames;
}

int convert_decimation_factor(int rate) {
    switch (rate) {
    case 44100:
    case 48000:  return 1;
    case 88200:
    case 96000:  return 2;
    case 176400:
    case 192000: return 4;
    }
    return 0;
}

void decimator_init(Decimator *d, int factor) {
    memset(d, 0, sizeof *d);
    d->factor = factor > 0 ? factor : 1;
}

size_t decimator_run(Decimator *d, const int16_t *in, size_t frames, int16_t *out) {
    if (d->factor == 1) {
        if (out != in) memmove(out, in, frames * 2 * sizeof(int16_t));
        return frames;
    }

    size_t produced = 0;
    for (size_t i = 0; i < frames; i++) {
        d->sum_l += in[i * 2];
        d->sum_r += in[i * 2 + 1];
        if (++d->held == d->factor) {
            out[produced * 2]     = (int16_t)(d->sum_l / d->factor);
            out[produced * 2 + 1] = (int16_t)(d->sum_r / d->factor);
            produced++;
            d->held  = 0;
            d->sum_l = 0;
            d->sum_r = 0;
        }
    }
    return produced;
}
