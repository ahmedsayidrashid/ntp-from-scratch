# ntp-from-scratch

An NTP server built from scratch that clients on a local network can reference for time synchronization.

It runs on a Raspberry Pi 4 with a cheap GPS module and serves **stratum 1** time: the Pi gets the exact
start of every second from the GPS's pulse-per-second (PPS) signal, and answers NTP requests with that
time, accurate to a few microseconds. It can also do the opposite and serve any **fake time** you like,
to see what breaks when a machine trusts the wrong clock.

## Demo Video

<making some changes, new demo video coming soon!>

## Why

My Raspberry Pi is connected to my PC via Ethernet, with the PC acting as a DHCP server.

Although that may sound fancy, I simply configured my Ethernet port to share my network connection with anything connected via Ethernet, allowing my PC to act as a router for the Pi.

While setting this up, I realized that Raspberry Pis do not have a secure way of determining the current time. They generally trust any available time source and, in most cases, depend on a network connection to reach a pool of NTP servers, a local NTP server, or a GPS module.

On boot, Raspberry Pis typically do not know the current time and rely on an external source to correct it. This project explores that dependency and examines how far it can be pushed.

Through my research, I've found that a surprising number of systems are time-dependent, including package management, certificates, and containers, among others.

This project is primarily for learning purposes. I have worked with time synchronization before, and this project is an opportunity to deepen my understanding of networking concepts through hands-on experimentation.

## Milestones

- [x] **Python server**: answered NTP requests on UDP 123 with the system clock or a custom time.
  Retired in favour of the C version, which does everything it did.
- [x] **C port** (`c/`): same protocol, IPv4 + IPv6, kernel receive timestamps.
- [x] **GPS + PPS reference clock**: the Pi becomes a stratum 1 server.
- [x] **systemd service**: starts on boot, runs unprivileged.
- [ ] Steer the Pi's own system clock from GPS (today only the *served* time is corrected).
- [ ] Rate limiting before exposing it beyond the LAN.

## How it works

```
 PPS edge (GPIO 18) ──kernel IRQ timestamp──► pps thread ─┐
                                                          ├─► pair ─► clock model ─► NTP server (UDP 123)
 NMEA $GNRMC (UART) ──"this was second S"───► nmea thread ┘   (offset + drift,     (kernel RX timestamps,
                                                               16 s fit)            corrected RX/TX)
```

- **PPS** says *when* each second starts. The GPS raises a pin at the top of every second, and the
  kernel timestamps that edge in its interrupt handler (about ±2 µs on a Pi 4).
- **NMEA** says *which* second it was. The module sends a `$GNRMC` sentence about 0.7 s after each edge,
  so each sentence is paired with the edge just before it.
- **Clock model**: each pair is a sample of "system clock read X when GPS said Y". A least-squares fit
  over the last 16 samples gives the system clock's offset and drift; outliers are dropped, and a run of
  outliers (someone stepped the clock) restarts the fit.
- **Server**: every request's receive and transmit timestamps go through the model. Until it has a fit
  (or after 5 minutes without PPS) it replies honestly as unsynchronized (stratum 16) instead of
  pretending.

Every reply is a 48-byte NTP header ([RFC 5905](https://www.rfc-editor.org/rfc/rfc5905)):

![NTP packet header](NTP-protocol.png)

Measured on the Pi: PPS edges 1.000000 s apart within a few µs, model jitter about 1.5 µs.

## Hardware

- Raspberry Pi 4 (Ubuntu 24.04)
- ATGM336H GPS module with antenna: **TX → GPIO 15** (UART RX), **PPS → GPIO 18**, plus 3.3 V and GND

## Quick start

```sh
cd c
make && make test                       # build + unit tests
./build/pps-ntpd -f -p 12300            # no GPS needed: fake reference, for trying it out
python3 tests/ntp_query.py 127.0.0.1 12300 5
```

With the GPS wired up (Pi setup is in [DEVELOPER_NOTES.md](DEVELOPER_NOTES.md)):

```sh
sudo make install && sudo systemctl enable --now pps-ntpd
journalctl -u pps-ntpd -f               # wait for "status: SYNCED"
```

Then point any machine at it, e.g. `sntp 10.42.0.97` or a `server` line in chrony.

### Breaking things: fake time

```sh
./build/pps-ntpd -t 2038-01-19T03:14:00Z -p 12300     # serve a made-up time as stratum 1
NTP_CUSTOM_TIME=1999-12-31T23:59:50Z ./build/pps-ntpd -p 12300
```

The time can be ISO 8601 (`2024-01-15T10:30:00`, `...Z`, `...+02:00`) or Unix seconds. Clients that
trust it will jump; see the developer notes for making chrony accept it.

### Options

| Flag | Meaning | Default |
|---|---|---|
| `-p PORT` | UDP port | 123 |
| `-d DEV` / `-s DEV` / `-b BAUD` | PPS device / GPS serial port / baud | `/dev/pps0`, `/dev/ttyAMA0`, 9600 |
| `-t TIME` | serve a fake time (also `$NTP_CUSTOM_TIME`) | off |
| `-f` | fake reference from the system clock, no GPS | off |
| `-H SEC` | holdover after PPS loss before going unsynced | 300 |
| `-L N` | shift the NMEA label by N s (if offset shows ~1 s) | 0 |
| `-S SEC` | status line interval, 0 = off | 16 |
| `-v` / `-D` | log every client / log every NMEA sentence and PPS edge | off |

## Repository layout

```
c/src/      server: main, ntp_server, ntp_packet, refclock, nmea, pps, clock_model, timearg
c/tests/    unit tests (make test) and ntp_query.py, a tiny NTP client
c/tools/    gps_pps_reader.c - standalone GPS/PPS wiring check (make tools)
c/pps-ntpd.service, c/60-pps.rules - installed by make install
```

## Developer Notes

For Pi setup (UART and PPS overlays, chrony, installing the service, pointing clients at the Pi, debugging the GPS), see [DEVELOPER_NOTES.md](DEVELOPER_NOTES.md).
