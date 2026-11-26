/* test_timearg.c - fake-time argument parsing (same inputs the Python server accepts) */
#include "common.h"
#include "timearg.h"

#include <stdio.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static bool is(const char *s, int64_t want)
{
    int64_t got = 0;
    bool ok = parse_time_arg(s, &got);
    if (!ok || got != want) printf("  '%s' -> %s %lld (want %lld)\n", s, ok ? "ok" : "fail",
                                   (long long)got, (long long)want);
    return ok && got == want;
}

static bool bad(const char *s)
{
    int64_t t;
    return !parse_time_arg(s, &t);
}

int main(void)
{
    const int64_t T = 1705314600LL * NS_PER_SEC; /* 2024-01-15T10:30:00Z */

    CHECK(is("1705314600", T));
    CHECK(is("1705314600.5", T + NS_PER_SEC / 2));
    CHECK(is("2024-01-15T10:30:00", T));
    CHECK(is("2024-01-15 10:30:00", T));
    CHECK(is("2024-01-15T10:30:00Z", T));
    CHECK(is("2024-01-15T10:30", T));
    CHECK(is("2024-01-15T10:30:00.25", T + NS_PER_SEC / 4));
    CHECK(is("2024-01-15 10:30:00.123456", T + 123456000));
    CHECK(is("2024-01-15T12:30:00+02:00", T));
    CHECK(is("2024-01-15T05:30:00-0500", T));
    CHECK(is("2024-01-15", T - (10 * 3600 + 30 * 60) * NS_PER_SEC));
    CHECK(is("2038-01-19T03:14:08Z", 2147483648LL * NS_PER_SEC)); /* the Y2038 moment */
    CHECK(is("1999-12-31T23:59:59Z", 946684799LL * NS_PER_SEC));

    CHECK(bad(""));
    CHECK(bad("YYYY-MM-DDTHH:MM:SS")); /* the .env.example placeholder */
    CHECK(bad("2024-13-01T00:00:00"));
    CHECK(bad("2024-02-30T00:00:00"));
    CHECK(bad("2024-01-15T25:00:00"));
    CHECK(bad("2024-01-15T10:30:00junk"));
    CHECK(bad("tomorrow"));
    CHECK(bad("1e3"));

    printf(fails ? "%d FAILED\n" : "all time-argument tests passed\n", fails);
    return fails ? 1 : 0;
}
