#include "home_bins.h"

static int32_t floor_div(int32_t a, int32_t b)
{
    int32_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

int32_t home_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = floor_div(y, 400);
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void home_civil_from_days(int32_t day, int *y, int *m, int *d)
{
    int32_t z = day + 719468;
    int32_t era = floor_div(z, 146097);
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

int home_weekday(int32_t day)
{
    /* 1970-01-01 was a Thursday (3 with Monday 0). */
    int32_t w = (day + 3) % 7;
    return (int)(w < 0 ? w + 7 : w);
}

unsigned home_bins_on(const home_config_t *c, int32_t day)
{
    if (!c->bin_count || home_weekday(day) != c->bin_weekday)
        return 0;
    int32_t weeks = floor_div(day - c->bin_reference, 7);
    unsigned mask = 0;
    for (int i = 0; i < c->bin_count && i < HOME_BINS; i++) {
        int every = c->bins[i].every;
        if (every < 1)
            continue;
        int32_t r = weeks % every;
        if (r < 0)
            r += every;
        if (r == c->bins[i].week)
            mask |= 1U << i;
    }
    return mask;
}

int32_t home_bins_next(const home_config_t *c, int32_t from, unsigned *mask)
{
    for (int32_t day = from; day < from + 35; day++) {
        unsigned m = home_bins_on(c, day);
        if (m) {
            if (mask)
                *mask = m;
            return day;
        }
    }
    return -1;
}

bool home_bins_reminder(const home_config_t *c, int32_t today, int minute, unsigned *mask)
{
    if (!c->bin_count || minute < c->bins_from || minute >= c->bins_until)
        return false;
    unsigned m = home_bins_on(c, today + 1);
    if (!m)
        return false;
    if (mask)
        *mask = m;
    return true;
}
