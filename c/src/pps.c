/* pps.c - Linux kernel PPS API (/dev/ppsN) via ioctl */
#include "pps.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/pps.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int pps_open(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    int caps = 0;
    if (ioctl(fd, PPS_GETCAP, &caps) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    if (!(caps & PPS_CAPTUREASSERT))
        logmsg("pps: warning: %s cannot capture assert (rising) edges", path);

    struct pps_kparams p;
    if (ioctl(fd, PPS_GETPARAMS, &p) == 0 && !(p.mode & PPS_CAPTUREASSERT)) {
        p.mode |= PPS_CAPTUREASSERT;
        if (ioctl(fd, PPS_SETPARAMS, &p) < 0)
            logmsg("pps: could not enable assert capture: %s", strerror(errno));
    }
    return fd;
}

int pps_wait(int fd, int timeout_s, pps_edge_t *e)
{
    struct pps_fdata fdata;
    memset(&fdata, 0, sizeof fdata);
    fdata.timeout.sec   = timeout_s;
    fdata.timeout.nsec  = 0;
    fdata.timeout.flags = 0; /* PPS_TIME_INVALID clear => honour the timeout */

    if (ioctl(fd, PPS_FETCH, &fdata) < 0) return -errno;

    e->sys_ns = (int64_t)fdata.info.assert_tu.sec * NS_PER_SEC + fdata.info.assert_tu.nsec;
    e->seq    = fdata.info.assert_sequence;
    return 0;
}
