#ifndef HOME_BINS_H
#define HOME_BINS_H
#include <stdbool.h>
#include <stdint.h>
#include "home_types.h"

/* Bin collection schedule. Pure: no I/O, no clock, no locks. Days are counted since 1970-01-01 in
 * the household's local calendar, weekdays start at Monday = 0. */
int32_t home_days_from_civil(int y, int m, int d);
void home_civil_from_days(int32_t day, int *y, int *m, int *d);
int home_weekday(int32_t day);
/* Bit i set = bins[i] is collected on `day`. 0 when bins are off. */
unsigned home_bins_on(const home_config_t *c, int32_t day);
/* First collection on or after `from` within 35 days: returns its day and sets *mask; -1 if none. */
int32_t home_bins_next(const home_config_t *c, int32_t from, unsigned *mask);
/* True while the reminder window of a collection is open: the day before it, from bins_from to
 * bins_until (minutes after local midnight). *mask is the bins due. */
bool home_bins_reminder(const home_config_t *c, int32_t today, int minute, unsigned *mask);
#endif
