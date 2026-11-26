/* nmea.c - serial port setup and NMEA RMC parsing */
#include "nmea.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool nmea_checksum_ok(const char *s)
{
    if (s[0] != '$') return false;
    uint8_t sum = 0;
    const char *p = s + 1;
    while (*p && *p != '*') sum ^= (uint8_t)*p++;
    if (*p != '*') return false;
    int hi = hexval(p[1]), lo = hexval(p[2]);
    if (hi < 0 || lo < 0) return false;
    return sum == (uint8_t)((hi << 4) | lo);
}

/* Copy comma-separated field `idx` (0 = sentence id) into buf. Empty fields are kept. */
static bool field(const char *s, int idx, char *buf, size_t n)
{
    const char *p = s;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, ',');
        if (!p) return false;
        p++;
    }
    size_t len = strcspn(p, ",*\r\n");
    if (len >= n) len = n - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return true;
}

/* ddmm.mmmm / dddmm.mmmm -> decimal degrees */
static double nmea_deg(const char *v, const char *hemi)
{
    if (!v[0]) return 0.0;
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') d = -d;
    return d;
}

bool nmea_parse_rmc(const char *line, nmea_rmc_t *out)
{
    /* $xxRMC,hhmmss.sss,A,lat,N,lon,E,spd,crs,ddmmyy,... (any talker: GP, GN, BD, GL) */
    if (strlen(line) < 16 || line[0] != '$') return false;
    if (strncmp(line + 3, "RMC,", 4) != 0) return false;
    if (!nmea_checksum_ok(line)) return false;

    char t[16], st[4], la[16], ns[4], lo[16], ew[4], d[16];
    if (!field(line, 1, t, sizeof t) || !field(line, 2, st, sizeof st) ||
        !field(line, 3, la, sizeof la) || !field(line, 4, ns, sizeof ns) ||
        !field(line, 5, lo, sizeof lo) || !field(line, 6, ew, sizeof ew) ||
        !field(line, 9, d, sizeof d))
        return false;

    if (strlen(t) < 6 || strlen(d) != 6) return false;
    int hh, mi, ss, dd, mo, yy;
    if (sscanf(t, "%2d%2d%2d", &hh, &mi, &ss) != 3) return false;
    if (sscanf(d, "%2d%2d%2d", &dd, &mo, &yy) != 3) return false;
    if (hh > 23 || mi > 59 || ss > 60 || mo < 1 || mo > 12 || dd < 1 || dd > 31) return false;

    struct tm tm = {0};
    tm.tm_year = yy + 100; /* 20yy */
    tm.tm_mon  = mo - 1;
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min  = mi;
    tm.tm_sec  = ss;
    time_t sec = timegm(&tm);
    if (sec == (time_t)-1) return false;

    out->valid   = (st[0] == 'A');
    out->utc_sec = sec;
    out->frac    = (t[6] == '.') ? atof(t + 6) : 0.0;
    out->lat     = nmea_deg(la, ns);
    out->lon     = nmea_deg(lo, ew);
    return true;
}

int serial_open(const char *dev, int baud)
{
    speed_t sp;
    switch (baud) {
    case 4800:   sp = B4800;   break;
    case 9600:   sp = B9600;   break;
    case 19200:  sp = B19200;  break;
    case 38400:  sp = B38400;  break;
    case 57600:  sp = B57600;  break;
    case 115200: sp = B115200; break;
    default: errno = EINVAL; return -1;
    }

    int fd = open(dev, O_RDONLY | O_NOCTTY);
    if (fd < 0) return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) { close(fd); return -1; }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) { close(fd); return -1; }
    tcflush(fd, TCIFLUSH);
    return fd;
}
