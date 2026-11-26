/* timearg.c - parse a user-supplied time for fake-time mode */
#include "timearg.h"
#include "common.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Read exactly n digits. */
static bool digits(const char **p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!isdigit((unsigned char)(*p)[i])) return false;
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += n;
    *out = v;
    return true;
}

static bool parse_unix(const char *s, int64_t *out_ns)
{
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) return false;
    /* only plain decimal numbers, so "2024" is not mistaken for anything else */
    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p) && *p != '.' && *p != '-' && *p != '+') return false;
    if (fabs(v) > 9e9) return false; /* beyond year 2255: almost certainly a typo */
    double ip;
    double fp = modf(v, &ip);
    *out_ns = (int64_t)ip * NS_PER_SEC + (int64_t)llround(fp * 1e9);
    return true;
}

static bool parse_iso(const char *s, int64_t *out_ns)
{
    const char *p = s;
    int Y, M, D, h = 0, m = 0, sec = 0;
    int64_t frac_ns = 0, zone_s = 0;

    if (!digits(&p, 4, &Y) || *p++ != '-' || !digits(&p, 2, &M) || *p++ != '-' || !digits(&p, 2, &D))
        return false;
    if (*p == 'T' || *p == 't' || *p == ' ') {
        p++;
        if (!digits(&p, 2, &h) || *p++ != ':' || !digits(&p, 2, &m)) return false;
        if (*p == ':') {
            p++;
            if (!digits(&p, 2, &sec)) return false;
            if (*p == '.' || *p == ',') {
                p++;
                int64_t scale = NS_PER_SEC / 10;
                if (!isdigit((unsigned char)*p)) return false;
                while (isdigit((unsigned char)*p)) {
                    frac_ns += (*p - '0') * scale;
                    scale /= 10;
                    p++;
                }
            }
        }
        if (*p == 'Z' || *p == 'z') {
            p++;
        } else if (*p == '+' || *p == '-') {
            int sign = *p++ == '-' ? -1 : 1, zh, zm = 0;
            if (!digits(&p, 2, &zh)) return false;
            if (*p == ':') p++;
            if (*p && !digits(&p, 2, &zm)) return false;
            if (zh > 23 || zm > 59) return false;
            zone_s = sign * (zh * 3600 + zm * 60);
        }
    }
    if (*p != '\0') return false;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 59) return false;

    struct tm tm = {0};
    tm.tm_year = Y - 1900;
    tm.tm_mon  = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min  = m;
    tm.tm_sec  = sec;
    time_t t = timegm(&tm);
    if (tm.tm_mday != D || tm.tm_mon != M - 1) return false; /* e.g. Feb 30 normalised away */
    *out_ns = ((int64_t)t - zone_s) * NS_PER_SEC + frac_ns;
    return true;
}

bool parse_time_arg(const char *s, int64_t *out_ns)
{
    if (!s || !*s) return false;
    return parse_unix(s, out_ns) || parse_iso(s, out_ns);
}
