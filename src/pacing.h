#ifndef MRECORD_PACING_H
#define MRECORD_PACING_H

#include <stdint.h>

#define HNS_PER_SECOND 10000000LL

int     pacing_default_bitrate_kbps(int width, int height, int fps);

int64_t pacing_frame_index(int64_t elapsed_ticks, int64_t ticks_per_second, int fps);
int64_t pacing_frame_time(int64_t index, int fps);
int64_t pacing_frame_duration(int fps);
int64_t pacing_ticks_until_frame(int64_t index, int64_t elapsed_ticks,
                                 int64_t ticks_per_second, int fps);
int64_t pacing_ticks_to_hns(int64_t ticks, int64_t ticks_per_second);

int64_t pacing_audio_time(int64_t frames_written, int rate);
int64_t pacing_silence_frames(int64_t frames_written, int64_t elapsed_hns,
                              int64_t lag_hns, int rate);

#endif
