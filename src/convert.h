#ifndef MRECORD_CONVERT_H
#define MRECORD_CONVERT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void   convert_bgra_to_nv12(const uint8_t *bgra, int width, int height, uint8_t *nv12);

size_t convert_to_s16_stereo(const uint8_t *src, size_t frames, int channels,
                             int bits, bool is_float, int16_t *dst);

int    convert_decimation_factor(int rate);

typedef struct {
    int     factor;
    int     held;
    int32_t sum_l;
    int32_t sum_r;
} Decimator;

void   decimator_init(Decimator *d, int factor);
size_t decimator_run(Decimator *d, const int16_t *in, size_t frames, int16_t *out);

#endif
