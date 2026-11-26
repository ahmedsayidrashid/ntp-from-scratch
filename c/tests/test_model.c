/* test_model.c - clock model: drift tracking, noise, outlier rejection, step recovery */
#include "clock_model.h"
#include "common.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  log: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

/* Simulated Pi clock: starts 0.25 s behind GPS and runs 12 ppm slow, ~1 us PPS noise. */
static int64_t sys_at(int64_t gps_ns, double off0_ns, double ppm)
{
    double t = (double)(gps_ns - 1794790000LL * NS_PER_SEC) / 1e9;
    return gps_ns - (int64_t)(off0_ns + ppm * 1e3 * t);
}

static double noise_ns(void) { return ((double)rand() / RAND_MAX - 0.5) * 2000.0; }

int main(void)
{
    srand(1);
    model_init(4, 300.0, false);
    model_status_t st;
    int64_t out;
    double off0 = 250e6, ppm = 12.0;
    int64_t gps = 1794790000LL * NS_PER_SEC;

    CHECK(!model_time(gps, &out, &st));

    for (int i = 0; i < 40; i++, gps += NS_PER_SEC)
        model_add_sample(sys_at(gps, off0, ppm) + (int64_t)noise_ns(), gps);

    /* Query half a second after the last edge */
    int64_t q_gps = gps - NS_PER_SEC / 2;
    CHECK(model_time(sys_at(q_gps, off0, ppm), &out, &st));
    printf("after 40 s: err=%+.3f us freq=%+.3f ppm jitter=%.3f us\n",
           (double)(out - q_gps) / 1e3, st.freq_ppm, st.jitter_s * 1e6);
    CHECK(llabs(out - q_gps) < 2000);       /* within 2 us */
    CHECK(fabs(st.freq_ppm - ppm) < 0.5);

    /* A single 3 ms glitch is rejected */
    uint64_t rej_before = st.rejected;
    model_add_sample(sys_at(gps, off0, ppm) + 3000000, gps);
    gps += NS_PER_SEC;
    model_time(0, NULL, &st);
    CHECK(st.rejected == rej_before + 1);

    /* System clock steps +0.1 s: model restarts and re-converges */
    off0 -= 100e6;
    for (int i = 0; i < 20; i++, gps += NS_PER_SEC)
        model_add_sample(sys_at(gps, off0, ppm) + (int64_t)noise_ns(), gps);
    q_gps = gps - NS_PER_SEC / 2;
    CHECK(model_time(sys_at(q_gps, off0, ppm), &out, &st));
    printf("after step: err=%+.3f us freq=%+.3f ppm n=%d\n",
           (double)(out - q_gps) / 1e3, st.freq_ppm, st.nfit);
    CHECK(llabs(out - q_gps) < 2000);

    /* Holdover: 10 minutes without PPS => unsynced */
    q_gps = gps + 600LL * NS_PER_SEC;
    CHECK(!model_time(sys_at(q_gps, off0, ppm), &out, &st));
    printf("holdover 600 s: err=%+.3f us (unsynced as expected)\n", (double)(out - q_gps) / 1e3);

    printf(fails ? "%d FAILED\n" : "all model tests passed\n", fails);
    return fails ? 1 : 0;
}
