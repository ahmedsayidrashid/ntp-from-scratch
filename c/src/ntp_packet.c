/* ntp_packet.c - NTP timestamp conversions */
#include "ntp_packet.h"
#include "common.h"

#include <arpa/inet.h>

void ns_to_ntp(int64_t ns, uint32_t *sec, uint32_t *frac)
{
    int64_t s = ns / NS_PER_SEC, r = ns % NS_PER_SEC;
    if (r < 0) { r += NS_PER_SEC; s--; }
    /* round to nearest 2^-32 s so ns -> ntp -> ns is exact to the nanosecond */
    uint64_t f = (((uint64_t)r << 32) + NS_PER_SEC / 2) / (uint64_t)NS_PER_SEC;
    if (f > 0xFFFFFFFFu) { f = 0; s++; }
    *sec  = htonl((uint32_t)(s + NTP_UNIX_OFFSET));
    *frac = htonl((uint32_t)f);
}

int64_t ntp_to_ns(uint32_t sec, uint32_t frac)
{
    int64_t s = (int64_t)ntohl(sec) - NTP_UNIX_OFFSET;
    int64_t f = (int64_t)(((uint64_t)ntohl(frac) * (uint64_t)NS_PER_SEC + (1ULL << 31)) >> 32);
    return s * NS_PER_SEC + f;
}

uint32_t ntp_short(double seconds)
{
    if (!(seconds > 0)) seconds = 0; /* also catches NaN */
    if (seconds > 65535.0) seconds = 65535.0;
    double v = seconds * 65536.0;
    return htonl(v >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)v);
}
