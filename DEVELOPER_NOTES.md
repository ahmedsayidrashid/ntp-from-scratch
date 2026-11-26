# Developer Notes

How to set things up so the server actually works end-to-end. It uses my own setup as a reference.

## Reference Setup

- **Server**: Raspberry Pi 4 (Ubuntu 24.04) running `pps-ntpd` from `c/`, with an ATGM336H GPS module.
- **Client**: my PC running `chrony`, taking its time from the Pi.
- **Network**: the Pi is connected to the PC via Ethernet, with the PC's Ethernet port configured to share its network connection (acting as a router/DHCP server for the Pi). The PC is `10.42.0.1`, the Pi `10.42.0.97`.

The Pi's IP address can be found from the PC with:

```bash
sudo nmap -sn 10.42.0.0/24
```

## 1. Wire the GPS

| ATGM336H | Raspberry Pi 4 |
|---|---|
| VCC | 3.3 V (pin 1) |
| GND | GND (pin 6) |
| TX | GPIO 15 / UART RX (pin 10) |
| PPS | GPIO 18 (pin 12) |

The module needs a clear view of the sky (or an antenna near a window) to get a fix. Its PPS LED starts
blinking once per second when it has one.

## 2. Enable the UART and PPS on the Pi

Add to `/boot/firmware/config.txt` under `[all]`, then reboot:

```text
enable_uart=1
dtoverlay=disable-bt
dtoverlay=pps-gpio,gpiopin=18
```

`disable-bt` gives the good UART (PL011) to GPIO 14/15, so the GPS shows up as **`/dev/ttyAMA0`**
(Ubuntu has no `/dev/serial0` alias). Make sure no serial console is using it: `cmdline.txt` must not
contain `console=serial0` or `console=ttyAMA0`.

Check both signals:

```bash
ls -l /dev/pps0 /dev/ttyAMA0
cat /sys/class/pps/pps0/assert          # run twice: the number after # should go up
timeout 3 cat /dev/ttyAMA0              # NMEA text, including $GNRMC lines
```

Your user needs to be in the `dialout` group (`sudo usermod -aG dialout $USER`, then log in again).

## 3. Stop chrony on the Pi from syncing to anything

Most Linux systems already run `chrony` or `systemd-timesyncd`. On the Pi it must not bind UDP 123 and
should not keep steering the clock that `pps-ntpd` is measuring. In `/etc/chrony/chrony.conf` on the Pi,
comment out every `pool`/`server` line (in particular any line pointing back at the PC), then:

```bash
sudo systemctl restart chrony
chronyc sources                         # should list nothing
sudo ss -ulnp | grep ':123 '            # should print nothing
```

Note: `pps-ntpd` corrects the time it *serves*; it does not set the Pi's own clock. The Pi's clock can
drift or be off after a reboot without changing what clients receive.

## 4. Build, test and install

```bash
sudo apt install build-essential
cd c
make && make test
sudo make install                       # binary, systemd unit, udev rule for /dev/pps0
sudo systemctl enable --now pps-ntpd
journalctl -u pps-ntpd -f
```

Within a few seconds you should see:

```text
pair: first sample, GPS - system clock = +0.004710 s (RMC arrived 688 ms after edge)
status: SYNCED  | fix=yes pps=60 rmc=60 pairs=60 | sys-err=-4711.804 us freq=+0.025 ppm jitter=1.378 us ...
```

- `sys-err`: how far the Pi's own clock is from GPS (expected; see the note above).
- `freq`: how fast the Pi's crystal drifts, in parts per million.
- `jitter`: noise of the PPS fit. A few µs is normal.

The service runs as an unprivileged user; `c/60-pps.rules` gives the `dialout` group access to
`/dev/pps0`, and the unit grants only the right to bind port 123. `sudo make uninstall` removes it all.

To run it by hand instead (e.g. while developing), stop the service and use a high port so no root is needed:

```bash
sudo systemctl stop pps-ntpd
./build/pps-ntpd -p 12300 -v
python3 tests/ntp_query.py 127.0.0.1 12300 5
```

## 5. Use the Pi as the time source on the PC

On the PC, edit `/etc/chrony/chrony.conf` (or `/etc/chrony.conf` depending on distro):

```text
server 10.42.0.97 iburst prefer
```

Keeping the default `pool` lines as well is a good sanity check: chrony will pick the Pi (lowest stratum
and error) but still notice if it disagrees with the internet. Then:

```bash
sudo systemctl restart chrony
chronyc sources -v                      # 10.42.0.97 should get a '*' and show stratum 1
chronyc tracking                        # Reference ID should be the Pi, Stratum 2
```

From a Mac: `sntp 10.42.0.97`.

If the PC used to serve time to the Pi (the old setup), it's fine to leave its `allow` lines; the Pi no
longer asks it.

## 6. Fake time (breaking things on purpose)

```bash
sudo systemctl stop pps-ntpd
sudo ./build/pps-ntpd -t 2038-01-19T03:14:00Z
```

chrony refuses large jumps by default. On the client, while testing, allow it to step any amount at
any time:

```text
makestep 1.0 -1
```

and restart chrony. Undo both afterwards (`sudo systemctl start pps-ntpd`, remove the `makestep` line),
then `sudo chronyc makestep` on the client to jump back to real time.

## Debugging the GPS

```bash
./build/pps-ntpd -D -p 12300            # every $GNRMC sentence and every PPS edge with its spacing
make tools && ./build/gps_pps_reader    # standalone reader (needs pps-tools headers)
```

- **`pps: no pulses for 3 s`**: no fix yet (move the antenna), or PPS not on GPIO 18 / overlay missing.
- **`nmea: cannot open /dev/ttyAMA0`**: not in `dialout`, or the UART isn't enabled.
- **First sample says GPS − system is about ±1 s**: the sentence is being paired with the wrong edge.
  If the Pi's clock was already right, restart with `-L -1` or `-L 1`.
- **Clients see stratum 16 / refid `INIT`**: the server is up but not synced yet (needs ~4 s of PPS), or
  PPS has been missing for longer than the holdover (`-H`, 300 s).
- **`cannot bind UDP port 123`**: something else (chrony, ntpd) already owns it; see step 3.

## Troubleshooting the network

- **Client never syncs / source shows as unreachable**: check the server is running
  (`systemctl status pps-ntpd`), that no firewall blocks UDP/123 between the two machines, and that the
  IP in `chrony.conf` is right (it can change with DHCP).
- **Client rejects the fake time**: see the `makestep` note in section 6.
