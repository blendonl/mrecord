#include "tests.h"
#include "../src/naming.h"

static void test_default_name(void) {
    wchar_t out[256];

    CHECK(naming_default_name(out, 256, L"C:\\Users\\me\\Videos\\Screen Recordings",
                              2026, 9, 14, 7, 5, 3, 1), "builds a name");
    CHECK(wcscmp(out, L"C:\\Users\\me\\Videos\\Screen Recordings\\mrecord-20260914-070503.mp4") == 0,
          "name shape");

    CHECK(naming_default_name(out, 256, L"C:\\v\\", 2026, 1, 2, 3, 4, 5, 3), "trailing slash dir");
    CHECK(wcscmp(out, L"C:\\v\\mrecord-20260102-030405-3.mp4") == 0, "attempt suffix, one separator");

    CHECK(!naming_default_name(out, 10, L"C:\\v", 2026, 1, 2, 3, 4, 5, 1), "too small buffer fails");
}

static void test_extension(void) {
    wchar_t p[64];

    wcscpy(p, L"C:\\out\\clip");
    CHECK(naming_ensure_extension(p, 64, L".mp4") && wcscmp(p, L"C:\\out\\clip.mp4") == 0,
          "appends when missing");

    wcscpy(p, L"C:\\out\\clip.mov");
    CHECK(naming_ensure_extension(p, 64, L".mp4") && wcscmp(p, L"C:\\out\\clip.mov") == 0,
          "keeps an existing extension");

    wcscpy(p, L"C:\\my.dir\\clip");
    CHECK(naming_ensure_extension(p, 64, L".mp4") && wcscmp(p, L"C:\\my.dir\\clip.mp4") == 0,
          "a dot in a folder name is not an extension");

    wcscpy(p, L".hidden");
    CHECK(naming_ensure_extension(p, 64, L".mp4") && wcscmp(p, L".hidden.mp4") == 0,
          "a leading dot is not an extension");

    wcscpy(p, L"abcdefgh");
    CHECK(!naming_ensure_extension(p, 10, L".mp4"), "no room fails");
}

int main(void) {
    test_default_name();
    test_extension();
    return tests_report("naming");
}
