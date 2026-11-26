/* clock_model.c - least-squares offset/frequency model fed by PPS samples */
#include "clock_model.h"
#include "common.h"

#include <math.h>
#include <pthread.h>

#define RING            16      /* fit window: last 16 seconds */
#define OUTLIER_FLOOR_NS 50000.0 /* never reject anything closer than 50 us */
#define OUTLIER_SIGMA   8.0
#define STEP_COUNT      3       /* this many outliers in a row => clock stepped, refit */

static struct {
    pthread_mutex_t mtx;
    int     min_samples;
    double  holdover_s;
    bool    verbose;

    /* sample ring: y = (gps - sys) - base_ns, stored as double for the fit */
    int64_t sys[RING];
    double  y[RING];
    int     n, head;
    int64_t base_ns;

    /* fitted model: offset(sys) = base_ns + intercept + slope * (sys - ref_sys)/1e9 */
    int64_t ref_sys;
    double  intercept;  /* ns */
    double  slope;      /* ns per second == ppb */
    double  jitter_ns;
    int64_t last_sys, last_gps;
    bool    have_model;

    int      outliers_in_row;
    uint64_t accepted, rejected;
} M = { .mtx = PTHREAD_MUTEX_INITIALIZER };

void model_init(int min_samples, double holdover_s, bool verbose)
{
    pthread_mutex_lock(&M.mtx);
    M.min_samples = min_samples;
    M.holdover_s  = holdover_s;
    M.verbose     = verbose;
    M.n = M.head = 0;
    M.have_model = false;
    pthread_mutex_unlock(&M.mtx);
}

static double predict_locked(int64_t sys_ns) /* offset relative to base_ns */
{
    return M.intercept + M.slope * (double)(sys_ns - M.ref_sys) / 1e9;
}

static void fit_locked(void)
{
    int newest = (M.head + RING - 1) % RING;
    M.ref_sys = M.sys[newest];

    if (M.n < 3) {
        M.intercept = M.y[newest];
        M.slope     = 0.0;
        M.jitter_ns = 1e6; /* unknown: report 1 ms until we have a real fit */
        return;
    }

    double sx = 0, sy = 0;
    for (int i = 0; i < M.n; i++) {
        int k = (M.head + RING - 1 - i) % RING;
        sx += (double)(M.sys[k] - M.ref_sys) / 1e9;
        sy += M.y[k];
    }
    double mx = sx / M.n, my = sy / M.n, sxx = 0, sxy = 0;
    for (int i = 0; i < M.n; i++) {
        int k = (M.head + RING - 1 - i) % RING;
        double dx = (double)(M.sys[k] - M.ref_sys) / 1e9 - mx;
        sxx += dx * dx;
        sxy += dx * (M.y[k] - my);
    }
    M.slope     = sxx > 0 ? sxy / sxx : 0.0;
    M.intercept = my - M.slope * mx;

    double ss = 0;
    for (int i = 0; i < M.n; i++) {
        int k = (M.head + RING - 1 - i) % RING;
        double r = M.y[k] - predict_locked(M.sys[k]);
        ss += r * r;
    }
    M.jitter_ns = sqrt(ss / (M.n - 2));
}

void model_add_sample(int64_t sys_ns, int64_t gps_ns)
{
    pthread_mutex_lock(&M.mtx);
    int64_t off = gps_ns - sys_ns;

    if (M.have_model && M.n >= 8) {
        double err = (double)(off - M.base_ns) - predict_locked(sys_ns);
        double lim = fmax(OUTLIER_FLOOR_NS, OUTLIER_SIGMA * M.jitter_ns);
        if (fabs(err) > lim) {
            M.rejected++;
            if (++M.outliers_in_row < STEP_COUNT) {
                logmsg("model: outlier %+.3f us (limit %.3f us), ignored", err / 1e3, lim / 1e3);
                pthread_mutex_unlock(&M.mtx);
                return;
            }
            logmsg("model: %d outliers in a row - clock step detected, restarting fit", STEP_COUNT);
            M.n = M.head = 0;
        }
    }
    M.outliers_in_row = 0;

    if (M.n == 0) M.base_ns = off;
    M.sys[M.head] = sys_ns;
    M.y[M.head]   = (double)(off - M.base_ns);
    M.head = (M.head + 1) % RING;
    if (M.n < RING) M.n++;

    fit_locked();
    M.have_model = true;
    M.last_sys   = sys_ns;
    M.last_gps   = gps_ns;
    M.accepted++;

    if (M.verbose)
        logmsg("model: sample off=%+.3f us  fit: off=%+.3f us freq=%+.3f ppm jitter=%.3f us n=%d",
               (double)off / 1e3, ((double)M.base_ns + M.intercept) / 1e3,
               M.slope / 1e3, M.jitter_ns / 1e3, M.n);
    pthread_mutex_unlock(&M.mtx);
}

bool model_time(int64_t sys_ns, int64_t *out_ns, model_status_t *st)
{
    pthread_mutex_lock(&M.mtx);
    bool    synced = false;
    int64_t corr   = 0;
    double  age    = 1e9;

    if (M.have_model) {
        corr   = M.base_ns + (int64_t)llround(predict_locked(sys_ns));
        age    = (double)(sys_ns - M.last_sys) / 1e9;
        synced = M.n >= M.min_samples && age <= M.holdover_s;
    }
    if (out_ns) *out_ns = sys_ns + corr;
    if (st) {
        st->synced     = synced;
        st->offset_ns  = corr;
        st->freq_ppm   = M.slope / 1e3;
        st->jitter_s   = M.have_model ? M.jitter_ns / 1e9 : 1.0;
        st->age_s      = age;
        st->ref_gps_ns = M.last_gps;
        st->accepted   = M.accepted;
        st->rejected   = M.rejected;
        st->nfit       = M.n;
    }
    pthread_mutex_unlock(&M.mtx);
    return synced;
}
