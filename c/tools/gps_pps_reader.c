#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>
#include <sys/timepps.h>

#define PPS_DEVICE "/dev/pps0"
#define SERIAL_DEVICE "/dev/ttyAMA0"
#define BAUD_RATE B9600

// Shared state between threads
typedef struct {
    char latitude[16];
    char lat_dir;
    char longitude[16];
    char lon_dir;
    char utc_time[16];
    int fix_valid;
    pthread_mutex_t lock;
} GpsData;

static GpsData g_gps = {
    .latitude = "N/A",
    .lat_dir = ' ',
    .longitude = "N/A",
    .lon_dir = ' ',
    .utc_time = "N/A",
    .fix_valid = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// Configure the hardware UART port (9600 8N1 raw)
int open_serial(const char *port) {
    int fd = open(port, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        perror("[!] Failed to open serial port");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        perror("[!] tcgetattr failed");
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, BAUD_RATE);
    cfsetispeed(&tty, BAUD_RATE);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit
    tty.c_cflag |= (CLOCAL | CREAD);            // Ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);          // No parity
    tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                    // No hardware flow control

    // Raw input mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[!] tcsetattr failed");
        close(fd);
        return -1;
    }

    return fd;
}

// Convert NMEA DDMM.MMMM format to standard Decimal Degrees
void format_coord(const char *raw, char dir, char *out, size_t out_len) {
    if (!raw || strlen(raw) < 4) {
        snprintf(out, out_len, "N/A");
        return;
    }
    double val = atof(raw);
    int deg = (int)(val / 100);
    double min = val - (deg * 100);
    double dec = deg + (min / 60.0);
    if (dir == 'S' || dir == 'W') {
        dec = -dec;
    }
    snprintf(out, out_len, "%.6f", dec);
}

// Parse NMEA $GNRMC / $GPRMC line
void parse_rmc(char *line) {
    char *tokens[15];
    int count = 0;
    char *ptr = line;

    // Split fields by comma (preserving empty fields)
    while (ptr && count < 15) {
        tokens[count++] = ptr;
        ptr = strchr(ptr, ',');
        if (ptr) {
            *ptr = '\0';
            ptr++;
        }
    }

    // $GNRMC format:
    // [0] Tag, [1] Time, [2] Status (A=valid, V=void), [3] Lat, [4] N/S, [5] Lon, [6] E/W
    if (count > 6) {
        pthread_mutex_lock(&g_gps.lock);
        if (strcmp(tokens[2], "A") == 0) {
            g_gps.fix_valid = 1;
            format_coord(tokens[3], tokens[4][0], g_gps.latitude, sizeof(g_gps.latitude));
            format_coord(tokens[5], tokens[6][0], g_gps.longitude, sizeof(g_gps.longitude));
            snprintf(g_gps.utc_time, sizeof(g_gps.utc_time), "%s", tokens[1]);
        } else {
            g_gps.fix_valid = 0;
            snprintf(g_gps.utc_time, sizeof(g_gps.utc_time), "%s", tokens[1]);
        }
        pthread_mutex_unlock(&g_gps.lock);
    }
}

// Thread: Reads NMEA text lines from UART
void *nmea_worker(void *arg) {
    (void)arg;
    int fd = open_serial(SERIAL_DEVICE);
    if (fd < 0) return NULL;

    char buf[512];
    int idx = 0;

    while (1) {
        char ch;
        int n = read(fd, &ch, 1);
        if (n > 0) {
            if (ch == '\n' || ch == '\r') {
                buf[idx] = '\0';
                if (strncmp(buf, "$GNRMC", 6) == 0 || strncmp(buf, "$GPRMC", 6) == 0) {
                    parse_rmc(buf);
                }
                idx = 0;
            } else if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = ch;
            }
        }
    }

    close(fd);
    return NULL;
}

// Thread: Waits for hardware PPS assert edge on GPIO
void *pps_worker(void *arg) {
    (void)arg;
    int fd = open(PPS_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("[!] Failed to open /dev/pps0");
        return NULL;
    }

    pps_handle_t handle;
    if (time_pps_create(fd, &handle) < 0) {
        perror("[!] time_pps_create failed");
        close(fd);
        return NULL;
    }

    pps_info_t info;
    struct timespec timeout = { 3, 0 }; // 3-second timeout

    printf("[*] PPS Listener started. Awaiting satellite lock...\n\n");

    while (1) {
        // Blocks until the rising edge fires on GPIO 18
        int ret = time_pps_fetch(handle, PPS_TSFMT_TSPEC, &info, &timeout);
        if (ret < 0) {
            printf("[~] PPS timeout (acquiring satellite fix...)\n");
            continue;
        }

        // Pull latest location data cached from the NMEA thread
        pthread_mutex_lock(&g_gps.lock);
        int fix = g_gps.fix_valid;
        char lat[16], lon[16], utc[16];
        strncpy(lat, g_gps.latitude, sizeof(lat));
        strncpy(lon, g_gps.longitude, sizeof(lon));
        strncpy(utc, g_gps.utc_time, sizeof(utc));
        pthread_mutex_unlock(&g_gps.lock);

        // Print combined PPS kernel timestamp and GPS telemetry
        printf("[PPS TRIGGER] Kernel Time: %ld.%09ld | Seq: %lu\n",
               (long)info.assert_tu.tspec.tv_sec,
               (long)info.assert_tu.tspec.tv_nsec,
               (unsigned long)info.assert_sequence);

        if (fix) {
            printf("              Fix: 3D LOCK | UTC: %s | Lat: %s | Lon: %s\n\n",
                   utc, lat, lon);
        } else {
            printf("              Fix: SEARCHING (No 3D Lock Yet) | Last Raw UTC: %s\n\n",
                   utc);
        }
    }

    time_pps_destroy(handle);
    close(fd);
    return NULL;
}

int main() {
    pthread_t nmea_tid, pps_tid;

    printf("====================================================\n");
    printf("   Dual GPS Telemetry & PPS Reader (Raspberry Pi)   \n");
    printf("====================================================\n");

    if (pthread_create(&nmea_tid, NULL, nmea_worker, NULL) != 0) {
        perror("[!] Failed to create NMEA thread");
        return 1;
    }

    if (pthread_create(&pps_tid, NULL, pps_worker, NULL) != 0) {
        perror("[!] Failed to create PPS thread");
        return 1;
    }

    pthread_join(nmea_tid, NULL);
    pthread_join(pps_tid, NULL);

    return 0;
}
