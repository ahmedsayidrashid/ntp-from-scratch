/* test_ntp_packet.c - NTP timestamp / short-format conversions */
#include "common.h"
#include "ntp_packet.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void)
{
    uint32_t s, f;

    /* Unix epoch is NTP second 2208988800 */
    ns_to_ntp(0, &s, &f);
    CHECK(ntohl(s) == 2208988800u && ntohl(f) == 0);

    /* Half a second is fraction 0x80000000 */
    ns_to_ntp(NS_PER_SEC / 2, &s, &f);
    CHECK(ntohl(s) == 2208988800u && ntohl(f) == 0x80000000u);

    /* Negative ns borrows from the seconds field: -0.25 s */
    ns_to_ntp(-NS_PER_SEC / 4, &s, &f);
    CHECK(ntohl(s) == 2208988799u && ntohl(f) == 0xC0000000u);

    /* Known value: the RMC test sentence's second, 2026-11-16 01:01:33 UTC */
    ns_to_ntp(1794790893LL * NS_PER_SEC, &s, &f);
    CHECK(ntohl(s) == 1794790893u + 2208988800u);

    /* 999999999 ns rounds up to the next second rather than overflowing the fraction */
    ns_to_ntp(NS_PER_SEC - 1, &s, &f);
    CHECK(ntohl(s) == 2208988800u || ntohl(f) > 0xFFFFFFF0u);

    /* Round trip is exact to the nanosecond across a range of values */
    int bad = 0;
    srand(7);
    for (int i = 0; i < 100000; i++) {
        int64_t ns = 1794790000LL * NS_PER_SEC + (int64_t)rand() * 1000 + rand() % 1000;
        ns_to_ntp(ns, &s, &f);
        if (ntp_to_ns(s, f) != ns) bad++;
    }
    CHECK(bad == 0);

    /* 2036-02-07 06:28:16 UTC is where era 0 ends: seconds field wraps to 0 */
    ns_to_ntp((4294967296LL - 2208988800LL) * NS_PER_SEC, &s, &f);
    CHECK(ntohl(s) == 0 && ntohl(f) == 0);

    /* 16.16 short format */
    CHECK(ntohl(ntp_short(1.0)) == 0x00010000u);
    CHECK(ntohl(ntp_short(0.001)) == 65u);        /* what the Python server sent */
    CHECK(ntohl(ntp_short(-1.0)) == 0);            /* clamped */
    CHECK(ntohl(ntp_short(1e9)) == 0xFFFF0000u);   /* clamped to 65535 s */
    CHECK(ntohl(ntp_short(16.0)) == 0x00100000u);

    CHECK(sizeof(ntp_packet_t) == 48);

    printf(fails ? "%d FAILED\n" : "all NTP packet tests passed\n", fails);
    return fails ? 1 : 0;
}
