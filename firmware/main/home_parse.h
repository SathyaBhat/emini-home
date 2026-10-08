#ifndef HOME_PARSE_H
#define HOME_PARSE_H
#include "home_types.h"

/* Parsers accept at most 128 KiB, never fetch resources, and only assign *out
 * on success. Caller owns HTTP freshness/ETag/checked/fetched/expires fields.
 * Weather uses forecast instants at/after now, no more than 1 h ahead; optional
 * missing values are NAN. low/high are sampled extrema of 24 hourly forecast
 * instants starting at forecast_at (NAN unless all 24 samples are available).
 * Returns Unix UTC seconds, or -1 for malformed/unsupported dates. */
int64_t home_parse_time(const char *text);
/* zone (IANA name, may be NULL or unknown = UTC) only buckets the day[] summary; it is looked up,
 * never set as the process TZ. hourly_* hold up to 24 consecutive hourly instants. */
bool home_parse_weather(const char *json, size_t len, home_weather_t *out,
                        int64_t now, const char *zone, char error[97]);
/* Maps a met.no symbol_code ("partlycloudy_night") to home_symbol_t, NIGHT bit included. */
uint8_t home_symbol_code(const char *code);
#endif
