/* refclock.h - GPS reference clock: PPS thread + NMEA thread, pairs them into samples */
#ifndef REFCLOCK_H
#define REFCLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *pps_dev;    /* /dev/pps0 */
    const char *serial_dev; /* /dev/ttyAMA0 (PL011 on GPIO 14/15 with disable-bt) */
    int  baud;              /* 9600 for ATGM336H */
    int  label_shift;       /* seconds added to the NMEA label for the paired edge (default 0) */
    bool fake;              /* no hardware: feed samples from the system clock (testing) */
    bool verbose;
} refclock_cfg_t;

typedef struct {
    uint64_t pps_pulses;
    uint64_t rmc_sentences;
    uint64_t pairs;
    bool     gps_fix;
    double   lat, lon;
} refclock_stats_t;

int  refclock_start(const refclock_cfg_t *cfg);
void refclock_stats(refclock_stats_t *out);

#endif
