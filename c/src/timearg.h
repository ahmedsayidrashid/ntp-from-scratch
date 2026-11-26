/* timearg.h - parse a user-supplied time for fake-time mode (-t / NTP_CUSTOM_TIME) */
#ifndef TIMEARG_H
#define TIMEARG_H

#include <stdbool.h>
#include <stdint.h>

/* Accepts the same inputs as the Python server:
 *   Unix timestamp:  1700000000  or  1700000000.25
 *   ISO 8601:        YYYY-MM-DD[(T| )HH:MM[:SS[.fff...]]][Z|+HH:MM|-HH:MM|+HHMM]
 * A time without a zone suffix is taken as UTC. Writes Unix-epoch ns. */
bool parse_time_arg(const char *s, int64_t *out_ns);

#endif
