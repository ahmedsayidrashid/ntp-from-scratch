/* test_nmea.c - unit tests for the NMEA RMC parser */
#include "nmea.h"

#include <math.h>
#include <stdio.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void)
{
    nmea_rmc_t r;

    /* ATGM336H-style multi-constellation RMC (NMEA 4.1 form; position replaced with Greenwich) */
    CHECK(nmea_parse_rmc("$GNRMC,010133.000,A,5128.66560,N,00000.00000,W,0.00,0.00,161126,,,A,V*16\r\n", &r));
    CHECK(r.valid);
    CHECK(r.utc_sec == 1794790893); /* 2026-11-16 01:01:33 UTC */
    CHECK(r.frac == 0.0);
    CHECK(fabs(r.lat - 51.477760) < 1e-5);
    CHECK(fabs(r.lon - -0.0) < 1e-5);

    /* ATGM336H 12-field NMEA 4.0 form (no nav-status field); position replaced with Greenwich */
    CHECK(nmea_parse_rmc("$GNRMC,000551.000,A,5128.66560,N,00000.00000,W,2.34,346.67,261126,,,A*6B\r\n", &r));
    CHECK(r.valid);
    CHECK(r.utc_sec == 1795651551); /* 2026-11-26 00:05:51 UTC */

    /* No-fix sentence parses but is flagged invalid */
    CHECK(nmea_parse_rmc("$GPRMC,000000.000,V,,,,,,,010125,,,N*4A", &r));
    CHECK(!r.valid);

    /* Bad checksum rejected */
    CHECK(!nmea_parse_rmc("$GNRMC,010133.000,A,5128.66560,N,00000.00000,W,0.00,0.00,161126,,,A,V*17", &r));
    /* Missing checksum rejected */
    CHECK(!nmea_parse_rmc("$GNRMC,010133.000,A,5128.66560,N,00000.00000,W,0.00,0.00,161126,,,A,V", &r));
    /* Other sentence types ignored */
    CHECK(!nmea_parse_rmc("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", &r));

    printf(fails ? "%d FAILED\n" : "all NMEA tests passed\n", fails);
    return fails ? 1 : 0;
}
