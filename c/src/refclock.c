/* refclock.c - GPS reference clock.
 *
 * PPS tells us *when* a second starts (kernel timestamp of the rising edge, ~us accuracy).
 * NMEA RMC tells us *which* second it was. The ATGM336H sends the RMC sentence for
 * second S a few hundred ms after the PPS edge that marks S, so each RMC is paired with
 * the most recent edge that arrived 0..950 ms before it.
 */
#include "refclock.h"
#include "clock_model.h"
#include "common.h"
#include "nmea.h"
#include "pps.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PAIR_WINDOW_NS (950LL * 1000000LL)

static refclock_cfg_t C;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pps_edge_t      latest_edge;
static bool            have_edge;
static refclock_stats_t S;

void refclock_stats(refclock_stats_t *out)
{
    pthread_mutex_lock(&mtx);
    *out = S;
    pthread_mutex_unlock(&mtx);
}

/* ---------------------------------------------------------------- PPS thread */
static void *pps_thread(void *arg)
{
    (void)arg;
    int  fd = -1;
    bool timed_out = false;

    for (;;) {
        if (fd < 0) {
            fd = pps_open(C.pps_dev);
            if (fd < 0) {
                logmsg("pps: cannot open %s: %s (retrying in 5 s)", C.pps_dev, strerror(errno));
                sleep(5);
                continue;
            }
            logmsg("pps: listening on %s", C.pps_dev);
        }

        pps_edge_t e;
        int r = pps_wait(fd, 3, &e);
        if (r == -ETIMEDOUT) {
            if (!timed_out) logmsg("pps: no pulses for 3 s (no GPS fix yet?)");
            timed_out = true;
            continue;
        }
        if (r == -EINTR) continue;
        if (r < 0) {
            logmsg("pps: fetch failed: %s", strerror(-r));
            close(fd);
            fd = -1;
            sleep(1);
            continue;
        }
        if (timed_out) logmsg("pps: pulses resumed");
        timed_out = false;

        if (C.debug) {
            static int64_t prev_ns;
            static uint32_t prev_seq;
            if (prev_ns && e.seq != prev_seq)
                logmsg("pps: edge seq=%u at %lld.%09lld  dt=%.6f s (%+.3f us from 1 s)", e.seq,
                       (long long)(e.sys_ns / NS_PER_SEC), (long long)(e.sys_ns % NS_PER_SEC),
                       (double)(e.sys_ns - prev_ns) / 1e9,
                       ((double)(e.sys_ns - prev_ns) / (double)(e.seq - prev_seq) - 1e9) / 1e3);
            prev_ns  = e.sys_ns;
            prev_seq = e.seq;
        }

        pthread_mutex_lock(&mtx);
        if (!have_edge || e.seq != latest_edge.seq) {
            latest_edge = e;
            have_edge   = true;
            S.pps_pulses++;
        }
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

/* --------------------------------------------------------------- NMEA thread */
static struct {
    bool     used_any;
    uint32_t last_used_seq;
    bool     have_prev;
    int64_t  prev_edge_ns, prev_label_ns;
    bool     first_reported;
} P;

static void handle_line(const char *line, int64_t rx_ns)
{
    nmea_rmc_t rmc;
    if (!nmea_parse_rmc(line, &rmc)) return;

    if (C.debug) {
        struct tm tm;
        time_t    t = rmc.utc_sec;
        gmtime_r(&t, &tm);
        logmsg("nmea: RMC %04d-%02d-%02dT%02d:%02d:%02dZ fix=%s lat=%.6f lon=%.6f", tm.tm_year + 1900,
               tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, rmc.valid ? "A" : "V",
               rmc.lat, rmc.lon);
    }

    pthread_mutex_lock(&mtx);
    S.rmc_sentences++;
    S.gps_fix = rmc.valid;
    if (rmc.valid) { S.lat = rmc.lat; S.lon = rmc.lon; }
    pps_edge_t e   = latest_edge;
    bool       ok  = have_edge;
    pthread_mutex_unlock(&mtx);

    if (!rmc.valid || !ok) return;
    if (rmc.frac > 0.01) return; /* label must be a whole second */
    if (P.used_any && e.seq == P.last_used_seq) return;

    int64_t delay = rx_ns - e.sys_ns;
    if (delay <= 0 || delay > PAIR_WINDOW_NS) {
        if (C.verbose) logmsg("pair: RMC %.3f s after last edge, outside window", (double)delay / 1e9);
        return;
    }

    int64_t label_ns = ((int64_t)rmc.utc_sec + C.label_shift) * NS_PER_SEC;

    /* Consistency: label must advance by exactly the number of seconds between edges. */
    if (P.have_prev) {
        int64_t dsec = llround((double)(e.sys_ns - P.prev_edge_ns) / 1e9);
        if (label_ns - P.prev_label_ns != dsec * NS_PER_SEC) {
            logmsg("pair: label jumped %+lld s across %lld s of pulses - skipping sample",
                   (long long)((label_ns - P.prev_label_ns) / NS_PER_SEC), (long long)dsec);
            P.prev_edge_ns  = e.sys_ns;
            P.prev_label_ns = label_ns;
            return;
        }
    }
    P.have_prev     = true;
    P.prev_edge_ns  = e.sys_ns;
    P.prev_label_ns = label_ns;
    P.used_any      = true;
    P.last_used_seq = e.seq;

    if (!P.first_reported) {
        double off = (double)(label_ns - e.sys_ns) / 1e9;
        logmsg("pair: first sample, GPS - system clock = %+.6f s (RMC arrived %.0f ms after edge)",
               off, (double)delay / 1e6);
        if (fabs(fabs(off) - 1.0) < 0.1)
            logmsg("pair: offset is ~1 s - if your system clock was already right, try -L %d",
                   off > 0 ? -1 : 1);
        P.first_reported = true;
    }

    pthread_mutex_lock(&mtx);
    S.pairs++;
    pthread_mutex_unlock(&mtx);

    model_add_sample(e.sys_ns, label_ns);
}

static void *nmea_thread(void *arg)
{
    (void)arg;
    int    fd = -1;
    char   line[256];
    size_t len = 0;

    for (;;) {
        if (fd < 0) {
            fd = serial_open(C.serial_dev, C.baud);
            if (fd < 0) {
                logmsg("nmea: cannot open %s: %s (retrying in 5 s)", C.serial_dev, strerror(errno));
                sleep(5);
                continue;
            }
            logmsg("nmea: reading %s at %d baud", C.serial_dev, C.baud);
            len = 0;
        }

        char    chunk[128];
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            logmsg("nmea: read failed: %s", n < 0 ? strerror(errno) : "EOF");
            close(fd);
            fd = -1;
            sleep(1);
            continue;
        }
        int64_t rx = now_ns();

        for (ssize_t i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '$') len = 0;
            if (len < sizeof line - 1) line[len++] = c;
            if (c == '\n') {
                line[len] = '\0';
                handle_line(line, rx);
                len = 0;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------- fake (test) source */
static void *fake_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t t = now_ns();
        int64_t next = (t / NS_PER_SEC + 1) * NS_PER_SEC;
        struct timespec ts = { .tv_sec = (time_t)((next - t) / NS_PER_SEC),
                               .tv_nsec = (long)((next - t) % NS_PER_SEC) };
        nanosleep(&ts, NULL);
        int64_t edge = now_ns();
        pthread_mutex_lock(&mtx);
        S.pps_pulses++;
        S.rmc_sentences++;
        S.pairs++;
        S.gps_fix = true;
        pthread_mutex_unlock(&mtx);
        model_add_sample(edge, edge); /* "GPS" agrees with the system clock exactly */
    }
    return NULL;
}

int refclock_start(const refclock_cfg_t *cfg)
{
    C = *cfg;
    pthread_t t;
    if (C.fake) {
        logmsg("refclock: FAKE mode - samples come from the system clock, not GPS");
        return pthread_create(&t, NULL, fake_thread, NULL);
    }
    int r = pthread_create(&t, NULL, pps_thread, NULL);
    if (r) return r;
    return pthread_create(&t, NULL, nmea_thread, NULL);
}
