/* pps.h - Linux kernel PPS API (/dev/ppsN) via ioctl, no pps-tools needed */
#ifndef PPS_H
#define PPS_H

#include <stdint.h>

typedef struct {
    int64_t  sys_ns; /* CLOCK_REALTIME at the rising edge, captured in the kernel IRQ */
    uint32_t seq;    /* assert sequence number */
} pps_edge_t;

int pps_open(const char *path);
/* Blocks until the next pulse. Returns 0, or -errno (-ETIMEDOUT if no pulse). */
int pps_wait(int fd, int timeout_s, pps_edge_t *e);

#endif
