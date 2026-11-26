/* nmea.h - serial port setup and NMEA RMC parsing */
#ifndef NMEA_H
#define NMEA_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    bool   valid;    /* status field == 'A' (GPS fix) */
    time_t utc_sec;  /* UTC seconds since the Unix epoch (whole seconds) */
    double frac;     /* fractional seconds from the time field (usually 0) */
    double lat, lon; /* decimal degrees, +N / +E */
} nmea_rmc_t;

bool nmea_checksum_ok(const char *line);
bool nmea_parse_rmc(const char *line, nmea_rmc_t *out);
int  serial_open(const char *dev, int baud);

#endif
