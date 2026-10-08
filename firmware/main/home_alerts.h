#ifndef HOME_ALERTS_H
#define HOME_ALERTS_H
#include <stdbool.h>
#include <stdint.h>
#include "home_types.h"

/* The alerts corner of Today. Pure: no I/O, no clock, no locks. Empty when nothing needs doing. */
#define HOME_ALERT_SLOTS 3
enum { HOME_ALERT_RAIN = 1, HOME_ALERT_WIND, HOME_ALERT_UV, HOME_ALERT_OLD };
/* warn = red chip, info = yellow chip with a border, outline = paper chip with an outline. */
enum { HOME_ALERT_WARN = 0, HOME_ALERT_INFO = 1, HOME_ALERT_OUTLINE = 2 };
typedef struct {
    uint8_t kind, level;
    char text[25];
} home_alert_t;
/* Fills out[] with the most urgent alerts (warn, then info, then outline), returns how many, and
 * sets *more to how many did not fit. today_day is the local civil day (days since 1970-01-01);
 * evening looks at tomorrow instead of the next twelve hours. */
int home_alerts(const home_config_t *cfg, const home_data_t *data, int64_t now, int32_t today_day,
                bool evening, home_alert_t out[HOME_ALERT_SLOTS], int *more);
/* UV index at or above which the corner shows a chip, and how old the weather may get. */
#define HOME_ALERT_UV_MIN 6.0
#define HOME_ALERT_OLD_S (6 * 3600)
#endif
