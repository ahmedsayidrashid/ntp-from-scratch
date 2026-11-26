#!/usr/bin/env python3
"""Minimal SNTP client to sanity-check pps-ntpd: python3 ntp_query.py HOST [PORT] [COUNT]"""
import socket, struct, sys, time

NTP_DELTA = 2208988800

def to_unix(sec, frac):
    # NTP seconds wrap every 2^32 s (era 0 ends 2036-02-07). Like real clients, assume a
    # value with the top bit clear belongs to era 1, so the window is 1968..2104.
    if sec < 2**31:
        sec += 2**32
    return sec - NTP_DELTA + frac / 2**32

def query(host, port):
    pkt = bytearray(48)
    pkt[0] = (0 << 6) | (4 << 3) | 3  # LI=0 VN=4 mode=3
    family, _, _, _, addr = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)[0]
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        sock.settimeout(2)
        sock.connect(addr)
        # take t1 as late as possible so socket setup doesn't bias the offset
        t1 = time.time()
        s, f = int(t1) + NTP_DELTA, int((t1 % 1) * 2**32)
        pkt[40:48] = struct.pack("!II", s, f)
        sock.send(pkt)
        data = sock.recv(512)
        t4 = time.time()
    (lvm, stratum, poll, prec, rdelay, rdisp, refid,
     refs, reff, orgs, orgf, recs, recf, xmts, xmtf) = struct.unpack("!BBbbII4s8I", data[:48])
    assert (orgs, orgf) == (s, f), "origin timestamp not echoed"
    t2, t3 = to_unix(recs, recf), to_unix(xmts, xmtf)
    offset = ((t2 - t1) + (t3 - t4)) / 2
    delay = (t4 - t1) - (t3 - t2)
    print(f"LI={lvm >> 6} VN={(lvm >> 3) & 7} mode={lvm & 7} stratum={stratum} "
          f"refid={refid.rstrip(b'\0').decode(errors='replace')} prec=2^{prec} "
          f"rootdisp={rdisp / 65536 * 1e6:.1f}us "
          f"time={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(t3))} offset={offset * 1e6:+.1f}us delay={delay * 1e6:.1f}us")
    return stratum

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 123
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    for i in range(count):
        query(host, port)
        if i + 1 < count:
            time.sleep(1)
