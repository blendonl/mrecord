#include "pacing.h"

int pacing_default_bitrate_kbps(int width, int height, int fps) {
    if (width <= 0 || height <= 0 || fps <= 0) return 1000;
    int64_t bits = (int64_t)width * height * fps * 7 / 100;
    int64_t kbps = bits / 1000;
    if (kbps < 1000)  kbps = 1000;
    if (kbps > 80000) kbps = 80000;
    return (int)kbps;
}

int64_t pacing_frame_index(int64_t elapsed_ticks, int64_t ticks_per_second, int fps) {
    if (elapsed_ticks <= 0 || ticks_per_second <= 0 || fps <= 0) return 0;
    int64_t whole = elapsed_ticks / ticks_per_second;
    int64_t part  = elapsed_ticks % ticks_per_second;
    return whole * fps + part * fps / ticks_per_second;
}

int64_t pacing_frame_time(int64_t index, int fps) {
    if (fps <= 0 || index <= 0) return 0;
    return index / fps * HNS_PER_SECOND + index % fps * HNS_PER_SECOND / fps;
}

int64_t pacing_frame_duration(int fps) {
    return pacing_frame_time(1, fps);
}

int64_t pacing_ticks_until_frame(int64_t index, int64_t elapsed_ticks,
                                 int64_t ticks_per_second, int fps) {
    if (fps <= 0 || ticks_per_second <= 0) return 0;
    int64_t whole  = index / fps;
    int64_t part   = index % fps;
    int64_t target = whole * ticks_per_second +
                     (part * ticks_per_second + fps - 1) / fps;
    int64_t wait   = target - elapsed_ticks;
    return wait > 0 ? wait : 0;
}

int64_t pacing_ticks_to_hns(int64_t ticks, int64_t ticks_per_second) {
    if (ticks_per_second <= 0) return 0;
    int64_t whole = ticks / ticks_per_second;
    int64_t part  = ticks % ticks_per_second;
    return whole * HNS_PER_SECOND + part * HNS_PER_SECOND / ticks_per_second;
}

int64_t pacing_audio_time(int64_t frames_written, int rate) {
    if (rate <= 0 || frames_written <= 0) return 0;
    return frames_written / rate * HNS_PER_SECOND +
           frames_written % rate * HNS_PER_SECOND / rate;
}

int64_t pacing_silence_frames(int64_t frames_written, int64_t elapsed_hns,
                              int64_t lag_hns, int rate) {
    if (rate <= 0) return 0;
    int64_t target_hns = elapsed_hns - lag_hns;
    if (target_hns <= 0) return 0;
    int64_t target = target_hns / HNS_PER_SECOND * rate +
                     target_hns % HNS_PER_SECOND * rate / HNS_PER_SECOND;
    int64_t missing = target - frames_written;
    return missing > 0 ? missing : 0;
}
