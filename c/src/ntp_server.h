/* ntp_server.h - NTPv4 (RFC 5905) server-mode responder */
#ifndef NTP_SERVER_H
#define NTP_SERVER_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int         port;          /* 123 */
    const char *refid;         /* "PPS" (or "SIM" in fake mode) */
    int         status_every;  /* seconds between status lines, 0 = never */
    bool        verbose;
    /* Fake-time mode: serve system time + custom_offset_ns as stratum 1 / LOCL, no GPS.
     * Lets you push clients to any date to see what breaks. */
    bool        custom;
    int64_t     custom_offset_ns;
} ntp_server_cfg_t;

int ntp_server_run(const ntp_server_cfg_t *cfg, volatile sig_atomic_t *stop);

#endif
