/* main.c - pps-ntpd: stratum-1 NTP server disciplined by GPS PPS (ATGM336H on a Pi 4) */
#include "clock_model.h"
#include "common.h"
#include "ntp_server.h"
#include "refclock.h"
#include "timearg.h"

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t stop_flag;

void logmsg(const char *fmt, ...)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);

    pthread_mutex_lock(&log_mtx);
    fprintf(stdout, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
    pthread_mutex_unlock(&log_mtx);
}

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "  -d DEV   PPS device            (default /dev/pps0)\n"
            "  -s DEV   GPS serial device     (default /dev/ttyAMA0)\n"
            "  -b BAUD  GPS baud rate         (default 9600)\n"
            "  -p PORT  UDP port              (default 123)\n"
            "  -L N     add N s to NMEA label (default 0; try -1/+1 if 1 s off)\n"
            "  -H SEC   holdover after PPS loss before reporting unsynced (default 300)\n"
            "  -S SEC   status line interval  (default 16, 0 = off)\n"
            "  -f       fake mode: no GPS, discipline to system clock (for testing)\n"
            "  -t TIME  serve a made-up time instead (ISO 8601 or Unix seconds, e.g.\n"
            "           2038-01-19T03:14:00Z); also read from $NTP_CUSTOM_TIME\n"
            "  -v       verbose (every sample and every client)\n"
            "  -D       debug the GPS: log every RMC sentence and every PPS edge\n",
            prog);
}

int main(int argc, char **argv)
{
    refclock_cfg_t   rc  = { .pps_dev = "/dev/pps0", .serial_dev = "/dev/ttyAMA0", .baud = 9600 };
    ntp_server_cfg_t sc  = { .port = 123, .refid = "PPS", .status_every = 16 };
    double           holdover = 300.0;
    const char      *custom   = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:b:p:L:H:S:t:fvDh")) != -1) {
        switch (opt) {
        case 'd': rc.pps_dev = optarg; break;
        case 's': rc.serial_dev = optarg; break;
        case 'b': rc.baud = atoi(optarg); break;
        case 'p': sc.port = atoi(optarg); break;
        case 'L': rc.label_shift = atoi(optarg); break;
        case 'H': holdover = atof(optarg); break;
        case 'S': sc.status_every = atoi(optarg); break;
        case 'f': rc.fake = true; break;
        case 't': custom = optarg; break;
        case 'v': rc.verbose = sc.verbose = true; break;
        case 'D': rc.debug = true; break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 2;
        }
    }
    if (rc.fake) sc.refid = "SIM";
    if (!custom) {
        custom = getenv("NTP_CUSTOM_TIME");
        if (custom && !*custom) custom = NULL;
    }
    if (custom) {
        int64_t t;
        if (!parse_time_arg(custom, &t)) {
            fprintf(stderr, "%s: cannot parse time '%s' (want ISO 8601 like 2038-01-19T03:14:00Z, "
                            "or Unix seconds)\n", argv[0], custom);
            return 2;
        }
        sc.custom           = true;
        sc.custom_offset_ns = t - now_ns();
    }

    /* Block signals in worker threads; only main handles them. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    if (sc.custom) {
        time_t    t = (time_t)((now_ns() + sc.custom_offset_ns) / NS_PER_SEC);
        struct tm tm;
        char      buf[32];
        gmtime_r(&t, &tm);
        strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
        logmsg("pps-ntpd starting in FAKE TIME mode on port %d: serving %s (%+.3f s from the "
               "system clock) as stratum 1 - clients that trust this will jump", sc.port, buf,
               (double)sc.custom_offset_ns / 1e9);
    } else {
        logmsg("pps-ntpd starting (pps=%s serial=%s@%d port=%d)",
               rc.pps_dev, rc.serial_dev, rc.baud, sc.port);
        model_init(4, holdover, rc.verbose);
        if (refclock_start(&rc) != 0) {
            logmsg("failed to start reference clock threads");
            return 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal; /* no SA_RESTART: recvmsg returns EINTR */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    int r = ntp_server_run(&sc, &stop_flag);
    logmsg("pps-ntpd exiting");
    return r == 0 ? 0 : 1;
}
