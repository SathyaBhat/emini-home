#include "home_alerts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WINDOW 12

typedef struct {
    home_alert_t a[8];
    int n;
} list_t;

static void add(list_t *l, int kind, int level, const char *text)
{
    if (l->n >= 8)
        return;
    home_alert_t *a = &l->a[l->n++];
    a->kind = (uint8_t)kind;
    a->level = (uint8_t)level;
    snprintf(a->text, sizeof a->text, "%s", text);
}
/* "12" for 12.0, "2.5" for 2.5 */
static void mm_text(char *out, size_t len, double mm)
{
    if (mm < 10)
        snprintf(out, len, "%.1f", mm);
    else
        snprintf(out, len, "%.0f", mm);
}
/* The hours of the window not yet over, as indices into the hourly arrays. */
static int first_hour(const home_weather_t *w, int64_t now, int *end)
{
    int64_t ago = now > w->forecast_at && w->forecast_at > 0 ? now - w->forecast_at : 0;
    int first = ago >= 24 * 3600 ? 24 : (int)(ago / 3600);
    int n = w->hourly_count < HOME_WEATHER_HOURS ? w->hourly_count : HOME_WEATHER_HOURS;
    *end = first + WINDOW < n ? first + WINDOW : n;
    return first;
}
static void weather_alerts(list_t *l, const home_config_t *cfg, const home_weather_t *w,
                           int64_t now, int32_t today_day, bool evening)
{
    double sum_limit = cfg->rain_sum_x10 / 10.0, hour_limit = cfg->rain_hour_x10 / 10.0;
    double sum = 0, peak = 0, wind = 0;
    bool have = false;
    if (evening) {
        for (int i = 0; i < w->day_count; ++i)
            if (w->day[i].date == today_day + 1) {
                sum = isfinite(w->day[i].rain) ? w->day[i].rain : 0;
                wind = isfinite(w->day[i].wind) ? w->day[i].wind : 0;
                have = true;
            }
    } else {
        int end, first = first_hour(w, now, &end);
        for (int k = first; k < end; ++k) {
            have = true;
            if (isfinite(w->hourly_rain[k])) {
                sum += w->hourly_rain[k];
                if (w->hourly_rain[k] > peak)
                    peak = w->hourly_rain[k];
            }
            if (isfinite(w->hourly_wind[k]) && w->hourly_wind[k] > wind)
                wind = w->hourly_wind[k];
        }
    }
    if (!have)
        return;
    char mm[16], text[48];
    if (sum_limit > 0 && sum >= sum_limit) {
        mm_text(mm, sizeof mm, sum);
        snprintf(text, sizeof text, "Rain %s mm", mm);
        add(l, HOME_ALERT_RAIN, HOME_ALERT_WARN, text);
    } else if (hour_limit > 0 && peak >= hour_limit) {
        mm_text(mm, sizeof mm, peak);
        snprintf(text, sizeof text, "Rain %s mm/h", mm);
        add(l, HOME_ALERT_RAIN, HOME_ALERT_WARN, text);
    }
    if (cfg->wind_kmh > 0 && wind >= cfg->wind_kmh) {
        snprintf(text, sizeof text, "Wind %.0f km/h", wind);
        add(l, HOME_ALERT_WIND, HOME_ALERT_WARN, text);
    }
}
/* BOM's daily forecast carries the day's highest UV index. */
static void uv_alert(list_t *l, const home_weather_t *w, int32_t day)
{
    for (int i = 0; i < w->day_count; ++i)
        if (w->day[i].date == day && isfinite(w->day[i].uv) && w->day[i].uv >= HOME_ALERT_UV_MIN) {
            char text[48];
            snprintf(text, sizeof text, "UV %.0f", w->day[i].uv);
            add(l, HOME_ALERT_UV, HOME_ALERT_INFO, text);
        }
}
int home_alerts(const home_config_t *cfg, const home_data_t *data, int64_t now, int32_t today_day,
                bool evening, home_alert_t out[HOME_ALERT_SLOTS], int *more)
{
    list_t l = {.n = 0};
    const home_weather_t *w = &data->weather;
    bool clock = now > 1700000000;
    if (clock && w->meta.valid && w->meta.state != HOME_ERROR) {
        weather_alerts(&l, cfg, w, now, today_day, evening);
        uv_alert(&l, w, evening ? today_day + 1 : today_day);
    }
    if (w->meta.valid && (w->meta.state == HOME_ERROR ||
                          (clock && now - w->meta.fetched_at > HOME_ALERT_OLD_S)))
        add(&l, HOME_ALERT_OLD, HOME_ALERT_OUTLINE, "Old weather");
    /* warn, then info, then outline; stable within a level */
    int count = 0, total = l.n;
    for (int level = HOME_ALERT_WARN; level <= HOME_ALERT_OUTLINE; ++level)
        for (int i = 0; i < l.n; ++i)
            if (l.a[i].level == level && count < HOME_ALERT_SLOTS)
                out[count++] = l.a[i];
    if (more)
        *more = total - count;
    return count;
}
