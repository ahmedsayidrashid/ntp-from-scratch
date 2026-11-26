# pps-ntpd — C port of the NTP server, disciplined by GPS PPS

Stratum-1 NTPv4 server for a Raspberry Pi 4 with an ATGM336H GPS module
(TX → GPIO 15 / `/dev/ttyAMA0`, PPS → GPIO 18 / `/dev/pps0`).

## How it works

```
 PPS edge (GPIO 18) ──kernel IRQ timestamp──► pps thread ─┐
                                                          ├─► pair ─► clock model ─► NTP server (UDP 123)
 NMEA $xxRMC (UART) ──"this was second S"───► nmea thread ┘   (offset + freq,     (kernel RX timestamps,
                                                               16 s LSQ fit)        corrected RX/TX)
```

* **PPS** gives the exact instant each UTC second starts (kernel timestamp, ~µs).
* **NMEA RMC** says *which* second. The ATGM336H sends RMC for second S a few hundred ms
  after the edge for S, so each RMC is paired with the edge 0–950 ms before it.
* **Clock model**: each pair is a sample `(system time, GPS time)`. A least-squares fit
  over the last 16 samples gives offset + frequency. Outliers are rejected; 3 in a row
  means the system clock was stepped and the fit restarts.
* **Server**: kernel receive timestamps (`SO_TIMESTAMPNS`) and the transmit timestamp
  are passed through the model. LI=0 / stratum 1 / refid `PPS` when synced; LI=3 /
  stratum 16 / `INIT` before lock or after `-H` seconds without PPS.

The system clock is **not** modified — the server serves corrected time on its own.

## Build & test

```sh
make          # build/pps-ntpd
make test     # NMEA parser + clock model unit tests
```

No dependencies beyond libc (uses `<linux/pps.h>` directly; `pps-tools` not required).

## Run

```sh
sudo ./build/pps-ntpd            # defaults: /dev/pps0, /dev/ttyAMA0 @ 9600, UDP 123
sudo ./build/pps-ntpd -v         # log every sample and every client
./build/pps-ntpd -f -p 12300     # fake mode: no GPS needed, for testing the server
```

Query it:

```sh
python3 tests/ntp_query.py 127.0.0.1 123 5     # from the Pi
sntp 10.42.0.97                                # from a Mac
```

Options: `-d` PPS dev, `-s` serial dev, `-b` baud, `-p` port, `-L` label shift (if the
reported offset is ~1 s, try `-L -1` or `-L 1`), `-H` holdover seconds, `-S` status interval.

## Install as a service

```sh
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now pps-ntpd
journalctl -u pps-ntpd -f
```
