CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE
LDLIBS  += -lpthread -lm

BUILD   := build
SRCS    := src/main.c src/nmea.c src/pps.c src/clock_model.c src/refclock.c src/ntp_server.c \
           src/ntp_packet.c src/timearg.c
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)
BIN     := $(BUILD)/pps-ntpd

PREFIX  ?= /usr/local

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

$(BUILD)/test_nmea: tests/test_nmea.c src/nmea.c src/nmea.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_nmea.c src/nmea.c $(LDLIBS)

$(BUILD)/test_model: tests/test_model.c src/clock_model.c src/clock_model.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_model.c src/clock_model.c $(LDLIBS)

$(BUILD)/test_ntp_packet: tests/test_ntp_packet.c src/ntp_packet.c src/ntp_packet.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_ntp_packet.c src/ntp_packet.c $(LDLIBS)

$(BUILD)/test_timearg: tests/test_timearg.c src/timearg.c src/timearg.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_timearg.c src/timearg.c $(LDLIBS)

TESTS   := test_nmea test_model test_ntp_packet test_timearg

test: $(TESTS:%=$(BUILD)/%)
	@set -e; for t in $(TESTS); do $(BUILD)/$$t; done

# Standalone GPS + PPS reader (debug tool, uses pps-tools' <sys/timepps.h>)
$(BUILD)/gps_pps_reader: tools/gps_pps_reader.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

tools: $(BUILD)/gps_pps_reader

install: $(BIN)
	install -m 755 $(BIN) $(PREFIX)/bin/pps-ntpd
	install -m 644 pps-ntpd.service /etc/systemd/system/pps-ntpd.service

clean:
	rm -rf $(BUILD)

.PHONY: all test tools install clean
