/* ntp_server.c - NTPv4 (RFC 5905) server-mode responder.
 *
 * Receive timestamps come from the kernel (SO_TIMESTAMPNS) so they are taken when the
 * packet arrives, not when we get scheduled. Both receive and transmit timestamps are
 * passed through the PPS clock model before going on the wire.
 */
#define _GNU_SOURCE
#include "ntp_server.h"
#include "clock_model.h"
#include "common.h"
#include "ntp_packet.h"
#include "refclock.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PRECISION   (-20)     /* ~1 us */
#define PHI         15e-6     /* RFC 5905 frequency tolerance, s/s */

static int open_socket(int port)
{
    int on = 1, off = 0;
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd >= 0) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
        struct sockaddr_in6 a = { .sin6_family = AF_INET6, .sin6_port = htons((uint16_t)port),
                                  .sin6_addr = in6addr_any };
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); fd = -1; }
    }
    if (fd < 0) { /* no IPv6: fall back to IPv4 only */
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                                 .sin_addr.s_addr = htonl(INADDR_ANY) };
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    }
    setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof on);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

static void print_status(uint64_t served)
{
    model_status_t   st;
    refclock_stats_t rs;
    model_time(now_ns(), NULL, &st);
    refclock_stats(&rs);
    logmsg("status: %s | fix=%s pps=%llu rmc=%llu pairs=%llu | sys-err=%+.3f us freq=%+.3f ppm "
           "jitter=%.3f us age=%.0f s n=%d rej=%llu | served=%llu",
           st.synced ? "SYNCED " : "NO SYNC", rs.gps_fix ? "yes" : "no",
           (unsigned long long)rs.pps_pulses, (unsigned long long)rs.rmc_sentences,
           (unsigned long long)rs.pairs, (double)-st.offset_ns / 1e3, st.freq_ppm,
           st.jitter_s * 1e6, st.age_s > 1e8 ? -1.0 : st.age_s, st.nfit,
           (unsigned long long)st.rejected, (unsigned long long)served);
}

int ntp_server_run(const ntp_server_cfg_t *cfg, volatile sig_atomic_t *stop)
{
    int fd = open_socket(cfg->port);
    if (fd < 0) {
        logmsg("server: cannot bind UDP port %d: %s%s", cfg->port, strerror(errno),
               cfg->port < 1024 ? " (need root or CAP_NET_BIND_SERVICE)" : "");
        return -1;
    }
    logmsg("server: listening on UDP port %d", cfg->port);

    uint64_t served = 0;
    int64_t  next_status = now_ns() + (int64_t)cfg->status_every * NS_PER_SEC;

    while (!*stop) {
        if (!cfg->custom && cfg->status_every > 0 && now_ns() >= next_status) {
            print_status(served);
            next_status += (int64_t)cfg->status_every * NS_PER_SEC;
        }

        ntp_packet_t            req;
        struct sockaddr_storage peer;
        char                    cbuf[256];
        struct iovec            iov = { .iov_base = &req, .iov_len = sizeof req };
        struct msghdr           msg = { .msg_name = &peer, .msg_namelen = sizeof peer,
                                        .msg_iov = &iov, .msg_iovlen = 1,
                                        .msg_control = cbuf, .msg_controllen = sizeof cbuf };

        ssize_t n = recvmsg(fd, &msg, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            logmsg("server: recvmsg: %s", strerror(errno));
            continue;
        }

        int64_t rx_sys = 0;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS) {
                struct timespec ts;
                memcpy(&ts, CMSG_DATA(c), sizeof ts);
                rx_sys = ts_to_ns(&ts);
            }
        }
        if (!rx_sys) rx_sys = now_ns();

        if (n < (ssize_t)sizeof req) continue; /* too short (extension fields are fine) */
        uint8_t vn   = (req.li_vn_mode >> 3) & 7;
        uint8_t mode = req.li_vn_mode & 7;
        if (mode != 3 || vn < 1 || vn > 4) continue; /* only answer client requests */

        ntp_packet_t resp;
        memset(&resp, 0, sizeof resp);
        resp.poll      = req.poll;
        resp.precision = PRECISION;
        resp.org_sec   = req.xmt_sec; /* echo client's transmit time verbatim */
        resp.org_frac  = req.xmt_frac;
        bool synced;

        if (cfg->custom) {
            /* Same fields the Python server sends: confident stratum 1 with small, non-zero
             * root delay/dispersion so chrony accepts the source. */
            synced = true;
            resp.li_vn_mode      = (uint8_t)((0 << 6) | (vn << 3) | 4);
            resp.stratum         = 1;
            resp.root_delay      = ntp_short(0.001);
            resp.root_dispersion = ntp_short(0.001);
            memcpy(resp.ref_id, "LOCL", 4);
            int64_t tx_ns = now_ns() + cfg->custom_offset_ns;
            ns_to_ntp(rx_sys + cfg->custom_offset_ns, &resp.rec_sec, &resp.rec_frac);
            ns_to_ntp(tx_ns, &resp.ref_sec, &resp.ref_frac);
            ns_to_ntp(tx_ns, &resp.xmt_sec, &resp.xmt_frac);
        } else {
            model_status_t st;
            int64_t        rx_ns, tx_ns;
            synced = model_time(rx_sys, &rx_ns, &st);
            resp.li_vn_mode = (uint8_t)(((synced ? 0 : 3) << 6) | (vn << 3) | 4);
            resp.stratum    = synced ? 1 : 16;
            resp.root_delay = 0;
            if (synced) {
                memcpy(resp.ref_id, cfg->refid, strnlen(cfg->refid, 4));
                ns_to_ntp(st.ref_gps_ns, &resp.ref_sec, &resp.ref_frac);
                resp.root_dispersion = ntp_short(st.jitter_s + st.age_s * PHI + ldexp(1.0, PRECISION));
            } else {
                memcpy(resp.ref_id, "INIT", 4);
                resp.root_dispersion = ntp_short(16.0);
            }
            ns_to_ntp(rx_ns, &resp.rec_sec, &resp.rec_frac);
            model_time(now_ns(), &tx_ns, NULL);
            ns_to_ntp(tx_ns, &resp.xmt_sec, &resp.xmt_frac);
        }

        if (sendto(fd, &resp, sizeof resp, 0, (struct sockaddr *)&peer, msg.msg_namelen) < 0) {
            logmsg("server: sendto: %s", strerror(errno));
            continue;
        }
        served++;

        if (cfg->verbose) {
            char host[INET6_ADDRSTRLEN] = "?";
            if (peer.ss_family == AF_INET6)
                inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&peer)->sin6_addr, host, sizeof host);
            else
                inet_ntop(AF_INET, &((struct sockaddr_in *)&peer)->sin_addr, host, sizeof host);
            logmsg("server: answered %s (v%u, %s)", host, vn, cfg->custom ? "fake time" : synced ? "stratum 1" : "unsynced");
        }
    }

    close(fd);
    return 0;
}
