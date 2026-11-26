/* common.h - shared helpers for the PPS NTP server */
#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define NS_PER_SEC 1000000000LL
#define NTP_UNIX_OFFSET 2208988800LL /* seconds from 1900-01-01 to 1970-01-01 */

static inline int64_t ts_to_ns(const struct timespec *t)
{
    return (int64_t)t->tv_sec * NS_PER_SEC + t->tv_nsec;
}

static inline int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return ts_to_ns(&t);
}

/* printf-style logger with a UTC wall-clock prefix (defined in main.c) */
void logmsg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
