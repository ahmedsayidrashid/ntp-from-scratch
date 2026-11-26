/* clock_model.h - software clock discipline.
 *
 * Every good PPS edge yields one sample: (system time at edge, true GPS time at edge).
 * We fit offset + frequency over the last N samples (least squares) and use that
 * model to turn any system timestamp into GPS-disciplined time. The system clock
 * itself is never touched, so this works alongside anything else on the Pi.
 */
#ifndef CLOCK_MODEL_H
#define CLOCK_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     synced;
    int64_t  offset_ns;  /* GPS - system, at the query time */
    double   freq_ppm;   /* system clock rate error (+ = system runs slow) */
    double   jitter_s;   /* RMS residual of the fit */
    double   age_s;      /* time since the last accepted sample */
    int64_t  ref_gps_ns; /* GPS time of the last accepted edge (NTP reference timestamp) */
    uint64_t accepted;   /* total samples accepted */
    uint64_t rejected;   /* total outliers rejected */
    int      nfit;       /* samples currently in the fit window */
} model_status_t;

void model_init(int min_samples, double holdover_s, bool verbose);
void model_add_sample(int64_t sys_ns, int64_t gps_ns);
/* Converts a system timestamp to disciplined time. Returns synced flag. st may be NULL. */
bool model_time(int64_t sys_ns, int64_t *out_ns, model_status_t *st);

#endif
