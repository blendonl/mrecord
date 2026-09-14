#include "tests.h"
#include "../src/pacing.h"

static void test_bitrate(void) {
    CHECK(pacing_default_bitrate_kbps(1920, 1080, 30) == 4354,
          "1080p30 -> %d", pacing_default_bitrate_kbps(1920, 1080, 30));
    CHECK(pacing_default_bitrate_kbps(320, 240, 30) == 1000, "floor at 1000");
    CHECK(pacing_default_bitrate_kbps(7680, 4320, 240) == 80000, "ceiling at 80000");
    CHECK(pacing_default_bitrate_kbps(0, 0, 30) == 1000, "nonsense size");
}

static void test_frames(void) {
    const int64_t f = 10000000;

    CHECK(pacing_frame_index(0, f, 30) == 0, "start is frame 0");
    CHECK(pacing_frame_index(f / 30 - 1, f, 30) == 0, "just before frame 1");
    CHECK(pacing_frame_index(f, f, 30) == 30, "one second is 30 frames");
    CHECK(pacing_frame_index(3600 * f + f / 2, f, 60) == 3600 * 60 + 30, "an hour in");

    CHECK(pacing_frame_time(30, 30) == 10000000, "frame 30 at one second");
    CHECK(pacing_frame_time(1, 30) == 333333, "frame 1 time");
    CHECK(pacing_frame_duration(60) == 166666, "60fps duration");

    CHECK(pacing_ticks_until_frame(1, 0, 3000, 30) == 100, "wait for frame 1");
    CHECK(pacing_ticks_until_frame(1, 150, 3000, 30) == 0, "late frame is due now");
    CHECK(pacing_ticks_until_frame(2, 0, 1000, 3) == 667, "rounds the target up");

    for (int64_t i = 0; i < 1000; i++) {
        int64_t wait = pacing_ticks_until_frame(i, 0, f, 30);
        if (pacing_frame_index(wait, f, 30) < i) {
            CHECK(0, "waiting until frame %lld lands before it", (long long)i);
            break;
        }
    }

    CHECK(pacing_ticks_to_hns(3000, 3000) == 10000000, "ticks to hns");
    CHECK(pacing_ticks_to_hns(1500, 3000) == 5000000, "half second");
}

static void test_audio(void) {
    CHECK(pacing_audio_time(48000, 48000) == 10000000, "one second of audio");
    CHECK(pacing_audio_time(480, 48000) == 100000, "10ms of audio");

    CHECK(pacing_silence_frames(0, 10000000, 1000000, 48000) == 43200, "fills to elapsed minus lag");
    CHECK(pacing_silence_frames(48000, 10000000, 0, 48000) == 0, "nothing missing");
    CHECK(pacing_silence_frames(50000, 10000000, 0, 48000) == 0, "ahead never negative");
    CHECK(pacing_silence_frames(0, 500000, 1000000, 48000) == 0, "inside the lag window");
}

int main(void) {
    test_bitrate();
    test_frames();
    test_audio();
    return tests_report("pacing");
}
