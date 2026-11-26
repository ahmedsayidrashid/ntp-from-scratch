/* ntp_packet.h - NTPv4 (RFC 5905) header layout and timestamp conversions */
#ifndef NTP_PACKET_H
#define NTP_PACKET_H

#include <stdint.h>

typedef struct { /* naturally aligned, no padding: matches the wire format */
    uint8_t  li_vn_mode;
    uint8_t  stratum;
    int8_t   poll;
    int8_t   precision;
    uint32_t root_delay;      /* 16.16 short format, network order */
    uint32_t root_dispersion; /* 16.16 short format, network order */
    uint8_t  ref_id[4];
    uint32_t ref_sec, ref_frac; /* 32.32 timestamps, network order */
    uint32_t org_sec, org_frac;
    uint32_t rec_sec, rec_frac;
    uint32_t xmt_sec, xmt_frac;
} ntp_packet_t;

_Static_assert(sizeof(ntp_packet_t) == 48, "NTP header must be 48 bytes");

/* Unix-epoch nanoseconds -> NTP 32.32 timestamp (network byte order). Wraps into
 * era 1 after 2036-02-07, as the protocol intends. */
void ns_to_ntp(int64_t ns, uint32_t *sec, uint32_t *frac);

/* NTP 32.32 timestamp (network byte order) -> Unix-epoch nanoseconds, era 0 (1900-2036). */
int64_t ntp_to_ns(uint32_t sec, uint32_t frac);

/* Seconds -> NTP 16.16 short format (network byte order), clamped to [0, 65535]. */
uint32_t ntp_short(double seconds);

#endif
