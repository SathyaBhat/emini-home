#include "home_parse.h"
#include "cJSON.h"
#include "home_places.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BODY_LIMIT (128U * 1024U)
#define XML_DEPTH 16U
#define XML_ITEMS 128U
#define XML_NAMESPACES 48U

static bool fail(char error[97], const char *message)
{
    if (error)
        snprintf(error, 97, "%s", message);
    return false;
}
static bool space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static int digit(unsigned char c)
{
    return c >= '0' && c <= '9' ? c - '0' : -1;
}
static bool number(const char **at, unsigned count, int *result)
{
    int n = 0;
    for (unsigned i = 0; i < count; ++i) {
        int d = digit((unsigned char)(*at)[i]);
        if (d < 0)
            return false;
        n = n * 10 + d;
    }
    *at += count;
    *result = n;
    return true;
}
static bool take(const char **at, char c)
{
    if (**at != c)
        return false;
    ++*at;
    return true;
}
static bool leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static int64_t civil_seconds(int y, int m, int d, int h, int minute, int second, int offset)
{
    static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 ||
        d > month_days[m - 1] + (m == 2 && leap(y)) || h > 23 || minute > 59 || second > 59)
        return -1;
    /* Gregorian civil date to Unix days; independent of process TZ/time_t. */
    int yy = y - (m <= 2), era = yy / 400, year_of_era = yy - era * 400;
    int shifted_month = m + (m > 2 ? -3 : 9);
    int day_of_year = (153 * shifted_month + 2) / 5 + d - 1;
    int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    int64_t days = (int64_t)era * 146097 + day_of_era - 719468;
    int64_t result = days * 86400 + h * 3600 + minute * 60 + second - offset;
    return result >= 0 ? result : -1;
}
static bool timezone_offset(const char **at, int *offset)
{
    const char *p = *at;
    if (*p == 'Z' || *p == 'z') {
        *at = p + 1;
        *offset = 0;
        return true;
    }
    if (*p == '+' || *p == '-') {
        int sign = *p++ == '+' ? 1 : -1, h, m;
        if (!number(&p, 2, &h))
            return false;
        if (*p == ':')
            ++p;
        if (!number(&p, 2, &m) || h > 14 || m > 59 || (h == 14 && m))
            return false;
        *at = p;
        *offset = sign * (h * 3600 + m * 60);
        return true;
    }
    static const struct {
        const char *name;
        int seconds;
    } zones[] = {{"GMT", 0},      {"UTC", 0},      {"UT", 0},       {"EST", -18000},
                 {"EDT", -14400}, {"CST", -21600}, {"CDT", -18000}, {"MST", -25200},
                 {"MDT", -21600}, {"PST", -28800}, {"PDT", -25200}};
    for (size_t i = 0; i < sizeof zones / sizeof zones[0]; ++i) {
        size_t n = strlen(zones[i].name);
        if (strncmp(p, zones[i].name, n) == 0 && (p[n] == '\0' || space((unsigned char)p[n]))) {
            *at = p + n;
            *offset = zones[i].seconds;
            return true;
        }
    }
    return false;
}

int64_t home_parse_time(const char *text)
{
    if (!text || strlen(text) > 127)
        return -1;
    const char *p = text;
    while (space((unsigned char)*p))
        ++p;
    int y = 0, m = 0, d = 0, h = 0, minute = 0, second = 0, offset = 0;
    if (strlen(p) >= 10 && p[4] == '-') {
        if (!number(&p, 4, &y) || !take(&p, '-') || !number(&p, 2, &m) || !take(&p, '-') ||
            !number(&p, 2, &d) || (*p != 'T' && *p != 't'))
            return -1;
        ++p;
        if (!number(&p, 2, &h) || !take(&p, ':') || !number(&p, 2, &minute) || !take(&p, ':') ||
            !number(&p, 2, &second))
            return -1;
        if (*p == '.') {
            ++p;
            unsigned n = 0;
            while (digit((unsigned char)*p) >= 0 && n < 10) {
                ++p;
                ++n;
            }
            if (!n || n > 9)
                return -1;
        }
        if ((*p != 'Z' && *p != 'z' && *p != '+' && *p != '-') || !timezone_offset(&p, &offset))
            return -1;
    } else {
        if (isalpha((unsigned char)*p)) {
            static const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
            bool matched = false;
            for (size_t i = 0; i < 7; ++i)
                if (!strncmp(p, weekdays[i], 3) && p[3] == ',')
                    matched = true;
            if (!matched)
                return -1;
            p += 4;
            if (!space((unsigned char)*p))
                return -1;
            while (space((unsigned char)*p))
                ++p;
        }
        if (digit((unsigned char)*p) < 0)
            return -1;
        d = *p++ - '0';
        if (digit((unsigned char)*p) >= 0)
            d = d * 10 + (*p++ - '0');
        if (!take(&p, ' '))
            return -1;
        static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        if (strlen(p) < 4)
            return -1;
        for (int i = 0; i < 12; ++i)
            if (!strncmp(p, months + 3 * i, 3))
                m = i + 1;
        if (!m)
            return -1;
        p += 3;
        if (!take(&p, ' ') || !number(&p, 2, &y))
            return -1;
        if (digit((unsigned char)*p) >= 0) {
            int remaining;
            if (!number(&p, 2, &remaining))
                return -1;
            y = y * 100 + remaining;
        } else
            y += y >= 50 ? 1900 : 2000;
        if (!take(&p, ' ') || !number(&p, 2, &h) || !take(&p, ':') || !number(&p, 2, &minute))
            return -1;
        if (*p == ':') {
            ++p;
            if (!number(&p, 2, &second))
                return -1;
        }
        if (!take(&p, ' ') || !timezone_offset(&p, &offset))
            return -1;
    }
    while (space((unsigned char)*p))
        ++p;
    if (*p)
        return -1;
    return civil_seconds(y, m, d, h, minute, second, offset);
}

static bool utf8_next(const unsigned char *p, size_t n, size_t *used, uint32_t *code)
{
    if (!n)
        return false;
    uint32_t cp = p[0];
    size_t k = 1;
    if (cp >= 0xC2 && cp <= 0xDF) {
        cp &= 31;
        k = 2;
    } else if (cp >= 0xE0 && cp <= 0xEF) {
        cp &= 15;
        k = 3;
    } else if (cp >= 0xF0 && cp <= 0xF4) {
        cp &= 7;
        k = 4;
    } else if (cp >= 0x80)
        return false;
    if (k > n)
        return false;
    for (size_t i = 1; i < k; ++i) {
        if ((p[i] & 0xC0) != 0x80)
            return false;
        cp = (cp << 6) | (p[i] & 63);
    }
    if ((k == 2 && cp < 0x80) || (k == 3 && cp < 0x800) || (k == 4 && cp < 0x10000) ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || (cp & 0xFFFF) >= 0xFFFE ||
        (cp < 32 && cp != 9 && cp != 10 && cp != 13))
        return false;
    *used = k;
    *code = cp;
    return true;
}
static bool valid_body(const char *body, size_t len)
{
    if (!body || !len || len > BODY_LIMIT)
        return false;
    for (size_t pos = 0; pos < len;) {
        size_t used;
        uint32_t cp;
        if (!utf8_next((const unsigned char *)body + pos, len - pos, &used, &cp))
            return false;
        pos += used;
    }
    return true;
}
static bool json_shape(const char *body, size_t len)
{
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)body[i];
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < len && !strncmp(body + i + 1, "0000", 4))
                    return false;
                escaped = false;
            } else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            else if (c < 32)
                return false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 16)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !quoted && !depth;
}
static const cJSON *member(const cJSON *object, const char *key)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
}
static const char *string_value(const cJSON *value)
{
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
static const cJSON *instant(const cJSON *point)
{
    return member(member(member(point, "data"), "instant"), "details");
}
static bool metric(const cJSON *object, const char *key, double min, double max, bool required,
                   double *out)
{
    const cJSON *value = member(object, key);
    if (!value) {
        *out = NAN;
        return !required;
    }
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < min ||
        value->valuedouble > max)
        return false;
    *out = value->valuedouble;
    return true;
}
static bool rain(const cJSON *point, double *out)
{
    return metric(member(member(member(point, "data"), "next_1_hours"), "details"),
                  "precipitation_amount", 0, 1000, false, out);
}
static const cJSON *period(const cJSON *point, const char *name)
{
    return member(member(point, "data"), name);
}
static bool period_rain(const cJSON *point, const char *name, double *out)
{
    return metric(member(period(point, name), "details"), "precipitation_amount", 0, 1000, false,
                  out);
}
static const char *period_symbol(const cJSON *point, const char *name)
{
    return string_value(member(member(period(point, name), "summary"), "symbol_code"));
}
uint8_t home_symbol_code(const char *code)
{
    static const struct {
        const char *part;
        uint8_t symbol;
    } families[] = {{"thunder", HOME_SYMBOL_THUNDER},     {"sleet", HOME_SYMBOL_SLEET},
                    {"snow", HOME_SYMBOL_SNOW},           {"showers", HOME_SYMBOL_SHOWERS},
                    {"heavyrain", HOME_SYMBOL_HEAVYRAIN}, {"lightrain", HOME_SYMBOL_LIGHTRAIN},
                    {"rain", HOME_SYMBOL_RAIN},           {"fog", HOME_SYMBOL_FOG},
                    {"partlycloudy", HOME_SYMBOL_PARTLY}, {"cloudy", HOME_SYMBOL_CLOUDY},
                    {"fair", HOME_SYMBOL_FAIR},           {"clearsky", HOME_SYMBOL_CLEAR}};
    if (!code)
        return HOME_SYMBOL_UNKNOWN;
    for (size_t i = 0; i < sizeof families / sizeof *families; ++i)
        if (strstr(code, families[i].part)) {
            size_t n = strlen(code);
            bool night = n >= 6 && !strcmp(code + n - 6, "_night");
            return (uint8_t)(families[i].symbol | (night ? HOME_SYMBOL_NIGHT : 0));
        }
    return HOME_SYMBOL_UNKNOWN;
}
static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}
/* Local civil day of a UTC instant. An unknown zone, or an instant outside the pinned table,
 * falls back to UTC. */
static int64_t local_day(const char *zone, int64_t at, int32_t *offset_out)
{
    int32_t offset = 0;
    bool dst;
    if (!zone || !home_tz_offset_at(zone, at, &offset, &dst))
        offset = 0;
    if (offset_out)
        *offset_out = offset;
    return floor_div(at + offset, 86400);
}
static bool unit(const cJSON *units, const char *key, const char *expected)
{
    const cJSON *value = member(units, key);
    return !value || (cJSON_IsString(value) && !strcmp(value->valuestring, expected));
}
static bool json_members(const cJSON *node, unsigned depth)
{
    if (!node || depth > 16)
        return false;
    unsigned count = 0;
    for (const cJSON *child = node->child; child; child = child->next) {
        if (++count > 512 || !json_members(child, depth + 1))
            return false;
        if (cJSON_IsObject(node)) {
            if (count > 128 || !child->string)
                return false;
            for (const cJSON *other = child->next; other; other = other->next)
                if (other->string && !strcmp(child->string, other->string))
                    return false;
        }
    }
    return true;
}

bool home_parse_weather(const char *json, size_t len, home_weather_t *out, int64_t now,
                        const char *zone, char error[97])
{
    if (!out || now <= 0 || now > INT64_C(253402300799) || !valid_body(json, len) ||
        !json_shape(json, len))
        return fail(error, "Invalid or oversized weather JSON");
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &end, false);
    if (!root)
        return fail(error, "Malformed weather JSON");
    while (end < json + len && space((unsigned char)*end))
        ++end;
    bool ok = false;
    const char *reason = "Invalid weather schema";
    home_weather_t parsed = {0};
    parsed.low = parsed.high = parsed.precipitation = parsed.wind_speed = parsed.cloud_cover = NAN;
    for (unsigned i = 0; i < HOME_WEATHER_HOURS; ++i) {
        parsed.hourly_temperature[i] = parsed.hourly_rain[i] = NAN;
        parsed.hourly_wind[i] = NAN;
    }
    const cJSON *properties = member(root, "properties"), *meta = member(properties, "meta");
    const cJSON *units = member(meta, "units"), *series = member(properties, "timeseries");
    parsed.meta.issued_at = home_parse_time(string_value(member(meta, "updated_at")));
    int count = cJSON_GetArraySize(series);
    if (end != json + len || !json_members(root, 0) || !cJSON_IsArray(series) || count < 1 ||
        count > 512 || parsed.meta.issued_at < 0 || parsed.meta.issued_at > now + 300 ||
        !unit(units, "air_temperature", "celsius") || !unit(units, "wind_speed", "m/s") ||
        !unit(units, "cloud_area_fraction", "%") || !unit(units, "precipitation_amount", "mm"))
        goto done;
    int64_t previous = -1, selected_time = -1;
    unsigned hours = 0;
    double min24 = INFINITY, max24 = -INFINITY;
    for (const cJSON *point = series->child; point; point = point->next) {
        int64_t at = home_parse_time(string_value(member(point, "time")));
        double temperature;
        if (at < 0 || at <= previous ||
            !metric(instant(point), "air_temperature", -100, 70, true, &temperature)) {
            reason = "Invalid forecast time or temperature";
            goto done;
        }
        previous = at;
        if (selected_time < 0 && at >= now) {
            if (at > now + 3600) {
                reason = "No current or next hourly forecast";
                goto done;
            }
            selected_time = at;
            parsed.forecast_at = at;
            parsed.temperature = temperature;
            if (!metric(instant(point), "wind_speed", 0, 150, false, &parsed.wind_speed) ||
                !metric(instant(point), "cloud_area_fraction", 0, 100, false,
                        &parsed.cloud_cover) ||
                !rain(point, &parsed.precipitation)) {
                reason = "Invalid weather metric";
                goto done;
            }
            const cJSON *summary = member(member(member(point, "data"), "next_1_hours"), "summary");
            const cJSON *symbol_value = member(summary, "symbol_code");
            if (symbol_value) {
                const char *symbol = string_value(symbol_value);
                if (!symbol || !*symbol || strlen(symbol) >= sizeof parsed.symbol) {
                    reason = "Invalid forecast symbol";
                    goto done;
                }
                for (const char *s = symbol; *s; ++s)
                    if (!((*s >= 'a' && *s <= 'z') || digit((unsigned char)*s) >= 0 || *s == '_')) {
                        reason = "Invalid forecast symbol";
                        goto done;
                    }
                snprintf(parsed.symbol, sizeof parsed.symbol, "%s", symbol);
            }
        }
        if (selected_time >= 0 && hours < 24 && at == selected_time + (int64_t)hours * 3600) {
            double wind;
            if (!rain(point, &parsed.hourly_rain[hours])) {
                reason = "Invalid hourly precipitation";
                goto done;
            }
            if (!metric(instant(point), "wind_speed", 0, 150, false, &wind)) {
                reason = "Invalid weather metric";
                goto done;
            }
            parsed.hourly_temperature[hours] = temperature;
            parsed.hourly_wind[hours] = (float)wind;
            parsed.hourly_symbol[hours] = home_symbol_code(period_symbol(point, "next_1_hours"));
            parsed.hourly_count = (uint8_t)(hours + 1);
            if (temperature < min24)
                min24 = temperature;
            if (temperature > max24)
                max24 = temperature;
            ++hours;
        }
    }
    if (selected_time < 0) {
        reason = "Forecast has expired";
        goto done;
    }
    if (hours == 24) {
        parsed.low = min24;
        parsed.high = max24;
    }
    /* Per-local-day summary. Instants give low/high/wind; rain is the interval [at, next_at)
     * credited to the day of `at`, taken from the period that exactly spans it, so no block is
     * counted twice. */
    int64_t today = local_day(zone, now, NULL);
    double noon_gap[HOME_WEATHER_DAYS];
    double day_low[HOME_WEATHER_DAYS], day_high[HOME_WEATHER_DAYS];
    for (unsigned i = 0; i < HOME_WEATHER_DAYS; ++i)
        noon_gap[i] = INFINITY, day_low[i] = INFINITY, day_high[i] = -INFINITY;
    for (const cJSON *point = series->child; point; point = point->next) {
        int64_t at = home_parse_time(string_value(member(point, "time")));
        int32_t offset;
        int64_t day = local_day(zone, at, &offset), idx = day - today;
        if (idx < 0 || idx >= HOME_WEATHER_DAYS)
            continue;
        home_day_t *d = &parsed.day[idx];
        double temperature, wind, amount = 0;
        if (!metric(instant(point), "air_temperature", -100, 70, true, &temperature) ||
            !metric(instant(point), "wind_speed", 0, 150, false, &wind)) {
            reason = "Invalid weather metric";
            goto done;
        }
        d->date = (int32_t)day;
        if (temperature < day_low[idx])
            day_low[idx] = temperature;
        if (temperature > day_high[idx])
            day_high[idx] = temperature;
        if (isfinite(wind) && wind > d->wind)
            d->wind = (float)wind;
        if (d->samples < UINT8_MAX)
            ++d->samples;
        const cJSON *following = point->next;
        int64_t next_at = following ? home_parse_time(string_value(member(following, "time"))) : -1;
        const char *span = next_at - at == 3600 ? "next_1_hours"
                           : next_at - at == 21600 ? "next_6_hours" : NULL;
        if (span && period_rain(point, span, &amount) && isfinite(amount))
            d->rain += (float)amount;
        double gap = fabs((double)(at + offset - day * 86400 - 43200));
        const char *code = period_symbol(point, "next_6_hours");
        if (!code)
            code = period_symbol(point, "next_1_hours");
        if (code && gap < noon_gap[idx]) {
            noon_gap[idx] = gap;
            d->symbol = home_symbol_code(code);
        }
    }
    for (unsigned i = 0; i < HOME_WEATHER_DAYS; ++i) {
        if (!parsed.day[i].samples)
            break;
        parsed.day[i].low = (float)day_low[i];
        parsed.day[i].high = (float)day_high[i];
        parsed.day_count = (uint8_t)(i + 1);
    }
    parsed.meta.valid = true;
    *out = parsed;
    if (error)
        error[0] = '\0';
    ok = true;
done:
    cJSON_Delete(root);
    return ok ? true : fail(error, reason);
}
