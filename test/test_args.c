#include "tests.h"
#include "../src/args.h"

#define PARSE(o, ...)                                                          \
    ({                                                                         \
        wchar_t *argv_[] = { L"mrecord", __VA_ARGS__ };                        \
        args_parse((int)(sizeof argv_ / sizeof argv_[0]), argv_, (o));        \
    })

static void test_defaults(void) {
    Options o;
    wchar_t *argv[] = { L"mrecord" };
    CHECK(args_parse(1, argv, &o), "no arguments parses");
    CHECK(o.command == CMD_RECORD, "records by default");
    CHECK(o.region == REGION_SELECT, "selects by default");
    CHECK(!o.region_given, "region not marked as given");
    CHECK(o.fps == 30, "fps defaults to 30, got %d", o.fps);
    CHECK(o.bitrate_kbps == 0, "bitrate defaults to derived");
    CHECK(o.border, "border on by default");
    CHECK(!o.audio && !o.cursor, "audio and cursor off by default");
    CHECK(o.output[0] == L'\0', "no output by default");
}

static void test_regions(void) {
    Options o;

    CHECK(PARSE(&o, L"--rect", L"100,200,640,480"), "rect parses");
    CHECK(o.region == REGION_RECT && o.rect_x == 100 && o.rect_y == 200 &&
          o.rect_w == 640 && o.rect_h == 480, "rect values");

    CHECK(PARSE(&o, L"-r", L"-1920,-10,800,600"), "negative origin parses");
    CHECK(o.rect_x == -1920 && o.rect_y == -10, "negative origin kept");

    CHECK(PARSE(&o, L"--rect=1,2,3,4"), "inline rect parses");
    CHECK(o.rect_w == 3 && o.rect_h == 4, "inline rect values");

    CHECK(!PARSE(&o, L"--rect", L"1,2,0,4"), "zero width rejected");
    CHECK(!PARSE(&o, L"--rect", L"1,2,3"), "three numbers rejected");
    CHECK(!PARSE(&o, L"--rect", L"1,2,3,4,5"), "five numbers rejected");
    CHECK(!PARSE(&o, L"--rect", L"a,2,3,4"), "letters rejected");
    CHECK(!PARSE(&o, L"--rect"), "missing value rejected");

    CHECK(PARSE(&o, L"--monitor"), "bare monitor parses");
    CHECK(o.region == REGION_MONITOR && o.monitor == -1, "bare monitor is under cursor");

    CHECK(PARSE(&o, L"-m", L"1"), "monitor index parses");
    CHECK(o.monitor == 1, "monitor index kept, got %d", o.monitor);

    CHECK(PARSE(&o, L"--monitor=2"), "inline monitor parses");
    CHECK(o.monitor == 2, "inline monitor index");

    CHECK(PARSE(&o, L"-m", L"-D", L"3"), "monitor followed by a flag");
    CHECK(o.monitor == -1 && o.duration_s == 3.0, "flag not eaten as index");

    CHECK(!PARSE(&o, L"--monitor=x"), "bad inline monitor rejected");

    CHECK(PARSE(&o, L"--window"), "window parses");
    CHECK(o.region == REGION_WINDOW && o.region_given, "window region");

    CHECK(PARSE(&o, L"-s"), "select parses");
    CHECK(o.region == REGION_SELECT && o.region_given, "select marked given");

    CHECK(!PARSE(&o, L"--window", L"--rect", L"0,0,10,10"), "two regions rejected");
    CHECK(wcsstr(o.error, L"pick one region") != NULL, "two regions message");
    CHECK(!PARSE(&o, L"-s", L"-s"), "repeated region rejected");
}

static void test_commands(void) {
    Options o;

    CHECK(PARSE(&o, L"--stop"), "stop parses");
    CHECK(o.command == CMD_STOP, "stop command");
    CHECK(!PARSE(&o, L"--stop", L"--audio"), "stop with options rejected");
    CHECK(!PARSE(&o, L"--stop", L"-m"), "stop with region rejected");

    CHECK(PARSE(&o, L"-t", L"--monitor", L"0", L"-a"), "toggle with options parses");
    CHECK(o.command == CMD_TOGGLE && o.audio && o.monitor == 0, "toggle keeps options");

    CHECK(!PARSE(&o, L"--toggle", L"--stop"), "toggle and stop rejected");

    CHECK(PARSE(&o, L"--list"), "list parses");
    CHECK(o.command == CMD_LIST, "list command");
    CHECK(!PARSE(&o, L"--list", L"--fps", L"60"), "list with options rejected");

    CHECK(PARSE(&o, L"--bogus", L"--help"), "help wins over a bad argument");
    CHECK(o.command == CMD_HELP, "help command");
    CHECK(PARSE(&o, L"-V"), "version parses");
    CHECK(o.command == CMD_VERSION, "version command");
}

static void test_options(void) {
    Options o;

    CHECK(PARSE(&o, L"-D", L"2.5", L"-d", L"0", L"--fps", L"60",
                L"--bitrate", L"8000", L"-a", L"-c", L"--no-border",
                L"-o", L"C:\\out\\a.mp4"), "every option parses");
    CHECK(o.duration_s == 2.5, "duration fractional");
    CHECK(o.delay_s == 0.0, "zero delay allowed");
    CHECK(o.fps == 60 && o.bitrate_kbps == 8000, "fps and bitrate");
    CHECK(o.audio && o.cursor && !o.border, "switches");
    CHECK(wcscmp(o.output, L"C:\\out\\a.mp4") == 0, "output path");

    CHECK(!PARSE(&o, L"-D", L"0"), "zero duration rejected");
    CHECK(!PARSE(&o, L"-D", L"-1"), "negative duration rejected");
    CHECK(!PARSE(&o, L"-D", L"nan"), "nan duration rejected");
    CHECK(!PARSE(&o, L"-d", L"-1"), "negative delay rejected");
    CHECK(!PARSE(&o, L"--fps", L"0"), "zero fps rejected");
    CHECK(!PARSE(&o, L"--fps", L"241"), "fps above 240 rejected");
    CHECK(!PARSE(&o, L"--fps", L"30x"), "trailing junk rejected");
    CHECK(!PARSE(&o, L"--bitrate", L"99"), "tiny bitrate rejected");
    CHECK(!PARSE(&o, L"-o", L""), "empty output rejected");
    CHECK(!PARSE(&o, L"--audio=yes"), "value on a switch rejected");
    CHECK(!PARSE(&o, L"stray"), "positional rejected");
    CHECK(wcsstr(o.error, L"stray") != NULL, "positional named in message");
    CHECK(!PARSE(&o, L"-x"), "unknown short rejected");
    CHECK(!PARSE(&o, L"-am"), "combined shorts rejected");
}

int main(void) {
    test_defaults();
    test_regions();
    test_commands();
    test_options();
    return tests_report("args");
}
