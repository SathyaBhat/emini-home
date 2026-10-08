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

/* ---- Bureau of Meteorology (api.weather.bom.gov.au/v1), the hourly and daily forecasts of
 * one location. BOM names the weather with its own words (icon_descriptor); they are mapped to
 * the met.no symbol codes the renderer was written against, so home_symbol_code() and
 * condition() keep working. The location is a geohash, which we compute from the coordinates. */
static const struct {
    const char *descriptor, *symbol;
    int cloud; /* percent, for the Weather screen's sky */
} bom_icons[] = {{"sunny", "clearsky", 0},
                 {"clear", "clearsky", 0},
                 {"mostly_sunny", "fair", 20},
                 {"partly_cloudy", "partlycloudy", 50},
                 {"cloudy", "cloudy", 100},
                 {"hazy", "fair", 40},
                 {"light_haze", "fair", 30},
                 {"fog", "fog", 100},
                 {"dusty", "fair", 40},
                 {"light_shower", "lightrainshowers", 70},
                 {"shower", "rainshowers", 80},
                 {"heavy_shower", "heavyrainshowers", 90},
                 {"light_rain", "lightrain", 85},
                 {"drizzle", "lightrain", 85},
                 {"rain", "rain", 90},
                 {"heavy_rain", "heavyrain", 100},
                 {"storm", "rainandthunder", 100},
                 {"snow", "snow", 100},
                 {"frost", "clearsky", 0},
                 {"windy", "fair", 20},
                 {"wind", "fair", 20}};
/* met.no style symbol code for a BOM descriptor ("" when unknown); *cloud gets the cover. */
static const char *bom_symbol(const char *descriptor, int *cloud)
{
    if (descriptor)
        for (size_t i = 0; i < sizeof bom_icons / sizeof *bom_icons; ++i)
            if (!strcmp(descriptor, bom_icons[i].descriptor)) {
                if (cloud)
                    *cloud = bom_icons[i].cloud;
                return bom_icons[i].symbol;
            }
    if (cloud)
        *cloud = -1;
    return "";
}
void home_geohash(double lat, double lon, char *out, unsigned chars)
{
    static const char alphabet[] = "0123456789bcdefghjkmnpqrstuvwxyz";
    double lo[2] = {-90, -180}, hi[2] = {90, 180}, v[2] = {lat, lon};
    unsigned bit = 0, idx = 0, n = 0;
    bool even = true; /* longitude first */
    while (n < chars) {
        int k = even ? 1 : 0;
        double mid = (lo[k] + hi[k]) / 2;
        idx <<= 1;
        if (v[k] >= mid) {
            idx |= 1;
            lo[k] = mid;
        } else
            hi[k] = mid;
        even = !even;
        if (++bit == 5) {
            out[n++] = alphabet[idx];
            bit = idx = 0;
        }
    }
    out[n] = 0;
}
/* A number, a JSON null or an absent key (all NAN); false for anything else or out of range. */
static bool num(const cJSON *object, const char *key, double min, double max, double *out)
{
    const cJSON *value = member(object, key);
    *out = NAN;
    if (!value || cJSON_IsNull(value))
        return true;
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < min ||
        value->valuedouble > max)
        return false;
    *out = value->valuedouble;
    return true;
}
/* Rain of a period from {"amount": {"min", "max"}}: the middle of the range; no amount is none. */
static bool rain_of(const cJSON *rain, double *out)
{
    const cJSON *amount = member(rain, "amount");
    double lo, hi;
    if (!num(amount, "min", 0, 1000, &lo) || !num(amount, "max", 0, 1000, &hi))
        return false;
    *out = isfinite(lo) && isfinite(hi) ? (lo + hi) / 2 : isfinite(lo) ? lo : isfinite(hi) ? hi : 0;
    return true;
}
static cJSON *parse_json(const char *json, size_t len)
{
    if (!valid_body(json, len) || !json_shape(json, len))
        return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &end, false);
    if (!root)
        return NULL;
    while (end < json + len && space((unsigned char)*end))
        ++end;
    if (end != json + len || !json_members(root, 0)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

bool home_parse_weather(const char *hourly, size_t hourly_len, const char *daily, size_t daily_len,
                        home_weather_t *out, int64_t now, const char *zone, char error[97])
{
    if (!out || now <= 0 || now > INT64_C(253402300799))
        return fail(error, "Invalid weather request");
    cJSON *hroot = parse_json(hourly, hourly_len), *droot = parse_json(daily, daily_len);
    bool ok = false;
    const char *reason = "Invalid weather schema";
    home_weather_t parsed = {0};
    parsed.low = parsed.high = parsed.precipitation = parsed.wind_speed = parsed.cloud_cover = NAN;
    for (unsigned i = 0; i < HOME_WEATHER_HOURS; ++i) {
        parsed.hourly_temperature[i] = parsed.hourly_rain[i] = NAN;
        parsed.hourly_wind[i] = NAN;
    }
    for (unsigned i = 0; i < HOME_WEATHER_DAYS; ++i)
        parsed.day[i].low = parsed.day[i].high = parsed.day[i].rain = parsed.day[i].wind = NAN;
    const cJSON *series = member(hroot, "data"), *days = member(droot, "data");
    if (!hroot || !droot) {
        reason = "Malformed weather JSON";
        goto done;
    }
    parsed.meta.issued_at = home_parse_time(string_value(member(member(hroot, "metadata"), "issue_time")));
    int count = cJSON_GetArraySize(series);
    if (!cJSON_IsArray(series) || !cJSON_IsArray(days) || count < 1 || count > 512 ||
        parsed.meta.issued_at < 0 || parsed.meta.issued_at > now + 300)
        goto done;
    int64_t previous = -1, selected_time = -1, today = local_day(zone, now, NULL);
    unsigned hours = 0;
    double min24 = INFINITY, max24 = -INFINITY;
    double day_low[HOME_WEATHER_DAYS], day_high[HOME_WEATHER_DAYS];
    for (unsigned i = 0; i < HOME_WEATHER_DAYS; ++i)
        day_low[i] = INFINITY, day_high[i] = -INFINITY;
    for (const cJSON *point = series->child; point; point = point->next) {
        int64_t at = home_parse_time(string_value(member(point, "time")));
        double temperature, wind, amount;
        if (at < 0 || at <= previous || !num(point, "temp", -100, 70, &temperature) ||
            !isfinite(temperature) || !num(member(point, "wind"), "speed_kilometre", 0, 500, &wind) ||
            !rain_of(member(point, "rain"), &amount)) {
            reason = "Invalid forecast time or value";
            goto done;
        }
        previous = at;
        const char *descriptor = string_value(member(point, "icon_descriptor"));
        int cloud;
        char code[49];
        snprintf(code, sizeof code, "%s%s", bom_symbol(descriptor, &cloud),
                 cJSON_IsTrue(member(point, "is_night")) ? "_night" : "");
        if (selected_time < 0 && at + 3600 > now) {
            if (at > now + 3600) {
                reason = "No current or next hourly forecast";
                goto done;
            }
            selected_time = at;
            parsed.forecast_at = at;
            parsed.temperature = temperature;
            parsed.wind_speed = wind;
            parsed.precipitation = amount;
            parsed.cloud_cover = cloud >= 0 ? cloud : NAN;
            snprintf(parsed.symbol, sizeof parsed.symbol, "%s", code);
        }
        if (selected_time >= 0 && hours < HOME_WEATHER_HOURS &&
            at == selected_time + (int64_t)hours * 3600) {
            parsed.hourly_temperature[hours] = temperature;
            parsed.hourly_rain[hours] = amount;
            parsed.hourly_wind[hours] = (float)wind;
            parsed.hourly_symbol[hours] = home_symbol_code(code);
            parsed.hourly_count = (uint8_t)(hours + 1);
            if (temperature < min24)
                min24 = temperature;
            if (temperature > max24)
                max24 = temperature;
            ++hours;
        }
        /* The day it falls in: wind, and a range to fall back on where the daily forecast has none. */
        int64_t idx = local_day(zone, at, NULL) - today;
        if (idx >= 0 && idx < HOME_WEATHER_DAYS) {
            home_day_t *d = &parsed.day[idx];
            d->date = (int32_t)(today + idx);
            if (temperature < day_low[idx])
                day_low[idx] = temperature;
            if (temperature > day_high[idx])
                day_high[idx] = temperature;
            if (isfinite(wind) && (!isfinite(d->wind) || wind > d->wind))
                d->wind = (float)wind;
        }
    }
    if (selected_time < 0) {
        reason = "Forecast has expired";
        goto done;
    }
    if (hours == HOME_WEATHER_HOURS) {
        parsed.low = min24;
        parsed.high = max24;
    }
    for (const cJSON *entry = days->child; entry; entry = entry->next) {
        int64_t start = home_parse_time(string_value(member(entry, "date")));
        double high, low, amount;
        if (start < 0 || !num(entry, "temp_max", -100, 70, &high) ||
            !num(entry, "temp_min", -100, 70, &low) || !rain_of(member(entry, "rain"), &amount)) {
            reason = "Invalid daily forecast";
            goto done;
        }
        /* "date" is the instant the local day starts. */
        int64_t idx = local_day(zone, start, NULL) - today;
        if (idx < 0 || idx >= HOME_WEATHER_DAYS)
            continue;
        home_day_t *d = &parsed.day[idx];
        d->date = (int32_t)(today + idx);
        d->high = isfinite(high) ? (float)high : NAN;
        d->low = isfinite(low) ? (float)low : NAN;
        d->rain = (float)amount;
        d->symbol = home_symbol_code(bom_symbol(string_value(member(entry, "icon_descriptor")), NULL));
        d->samples = 24;
    }
    for (unsigned i = 0; i < HOME_WEATHER_DAYS; ++i) {
        home_day_t *d = &parsed.day[i];
        if (!d->date)
            break;
        if (!isfinite(d->high) && day_high[i] > -INFINITY)
            d->high = (float)day_high[i];
        if (!isfinite(d->low) && day_low[i] < INFINITY)
            d->low = (float)day_low[i];
        if (!isfinite(d->high) || !isfinite(d->low))
            break;
        parsed.day_count = (uint8_t)(i + 1);
    }
    parsed.meta.valid = true;
    *out = parsed;
    if (error)
        error[0] = '\0';
    ok = true;
done:
    cJSON_Delete(hroot);
    cJSON_Delete(droot);
    return ok ? true : fail(error, reason);
}
