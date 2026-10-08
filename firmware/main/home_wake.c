#include "home_wake.h"

bool home_radio_wanted(const home_radio_in_t *in)
{
    if (!in)
        return true;
    /* Always on: Open mode, anything that holds the chip awake anyway, someone at the panel, or
     * work that a stop would cut in half. */
    if (!in->breath || in->reasons || in->now < in->awake_until || in->busy)
        return true;
    if (in->now < in->retry_at)
        return false;
    return in->fetch_due || in->clock_unset || in->now < in->hold_until;
}

unsigned home_sources_wanted(const home_config_t *c)
{
    if (!c)
        return 0;
    /* Bit 1 weather, bit 4 air (bit 2 was the news feed, retired in 0.7). Today shows the weather
     * too; air only feeds the optional UV and pollen alerts. */
    return ((c->enabled[HOME_TODAY] || c->enabled[HOME_WEATHER]) && c->location_ready ? 1U : 0U) |
           (c->enabled[HOME_TODAY] && c->alerts_air && c->location_ready ? 4U : 0U);
}

bool home_sources_due(const home_config_t *c, const home_data_t *d, int64_t now, int64_t ahead)
{
    if (!c || !d)
        return false;
    unsigned wanted = home_sources_wanted(c);
    int64_t at = now + ahead;
    return ((wanted & 1U) && at >= d->weather.meta.next_fetch) ||
           ((wanted & 4U) && at >= d->air.meta.next_fetch);
}
