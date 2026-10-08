#include "home_config.h"
#include "home_bins.h"
#include "home_places.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *screens[] = {"today", "weather", "note"};
/* The screens of schema 1 (0.4-0.6), in the canonical order its enabled[] array is indexed by. */
static const char *const legacy_screens[] = {"weather", "feed", "note", "sky", "air", "picture"};
#define LEGACY_SCREENS 6
static const char *styles[] = {"print", "rhythm", "atlas", "cycle"};
static const char *modes[] = {"fixed", "day", "rotate"};
const char *home_screen_name(int n)
{
    return n >= 0 && n < HOME_SCREEN_COUNT ? screens[n] : "today";
}
int home_screen_index(const char *s)
{
    if (s)
        for (int i = 0; i < HOME_SCREEN_COUNT; i++)
            if (!strcmp(s, screens[i]))
                return i;
    return -1;
}
const char *home_timezone(const char *name)
{
    return home_timezone_lookup(name);
}

bool home_utf8(const char *s, size_t max, bool multi)
{
    if (!s || strlen(s) > max)
        return false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p++;
        if (c < 32) {
            if (multi && (c == 10 || c == 9))
                continue;
            return false;
        }
        if (c == 127)
            return false;
        if (c < 128)
            continue;
        unsigned need = 0, cp = 0, min = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            need = 1;
            cp = c & 31;
            min = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            need = 2;
            cp = c & 15;
            min = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            need = 3;
            cp = c & 7;
            min = 65536;
        } else
            return false;
        while (need--) {
            if ((*p & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (*p++ & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
void home_config_defaults(home_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->revision = 1;
    strcpy(c->name, "Home");
    strcpy(c->locale, "en");
    strcpy(c->units, "C");
    strcpy(c->timezone, "UTC");
    strcpy(c->location, "My location");
    c->location_ready = false;
    for (int i = 0; i < HOME_SCREEN_COUNT; i++) {
        c->enabled[i] = true;
        c->order[i] = i;
        c->style[i] = HOME_PRINT;
    }
    c->style[HOME_NOTE] = HOME_ATLAS;
    c->texture = 1;
    c->intensity = 2;
    c->clock24 = true;
    /* Out of the box: one screen, Today, so the first hour on the wall is predictable.
     * The day rhythm and the rotation are a choice in the panel (QC first start, 16.09). */
    c->mode = HOME_FIXED;
    c->fixed_screen = HOME_TODAY;
    c->interval_min = 30;
    c->pause_min = 15;
    c->cycle_min = 30;
    c->ok_action = 0;
    c->brush = 0;
    c->power_mode = HOME_POWER_BREATH;
    c->quiet_enabled = true;
    c->quiet_start = 1350;
    c->quiet_end = 420;
    c->weekdays = 127;
    c->day_minute[0] = 420;
    c->day_minute[1] = 780;
    c->day_minute[2] = 1080;
    c->day_screen[0] = HOME_TODAY;
    c->day_screen[1] = HOME_NOTE;
    c->day_screen[2] = HOME_TODAY;
    /* Bins off; a Tuesday reference so the block is valid as soon as a list is added. */
    c->bin_weekday = 1;
    c->bin_reference = 5; /* 1970-01-06 */
    c->bins_from = 1020;
    c->bins_until = 1140;
    c->evening_minute = 1080;
}
static bool unique(const cJSON *n, int depth)
{
    if (depth > 12)
        return false;
    if (cJSON_IsObject(n))
        for (const cJSON *a = n->child; a; a = a->next) {
            if (!a->string)
                return false;
            for (const cJSON *b = a->next; b; b = b->next)
                if (!strcmp(a->string, b->string))
                    return false;
        }
    for (const cJSON *a = n->child; a; a = a->next)
        if (!unique(a, depth + 1))
            return false;
    return true;
}
static bool known(const cJSON *obj, const char *const *keys, size_t n)
{
    if (!cJSON_IsObject(obj))
        return false;
    for (const cJSON *v = obj->child; v; v = v->next) {
        bool found = false;
        for (size_t i = 0; i < n; i++)
            if (!strcmp(v->string, keys[i]))
                found = true;
        if (!found)
            return false;
    }
    return true;
}
static cJSON *get(const cJSON *j, const char *k)
{
    return cJSON_GetObjectItemCaseSensitive(j, k);
}
static bool strval(const cJSON *j, const char *k, char *out, size_t size, bool multiline)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsString(v) || !home_utf8(v->valuestring, size - 1, multiline))
        return false;
    strcpy(out, v->valuestring);
    return true;
}
static bool number(const cJSON *j, const char *k, double lo, double hi, double *out)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < lo ||
        v->valuedouble > hi)
        return false;
    *out = v->valuedouble;
    return true;
}
static bool integer(const cJSON *j, const char *k, int lo, int hi, int *out)
{
    double n;
    if (!number(j, k, lo, hi, &n) || floor(n) != n)
        return false;
    *out = (int)n;
    return true;
}
static bool boolean(const cJSON *j, const char *k, bool *out)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsBool(v))
        return false;
    *out = cJSON_IsTrue(v);
    return true;
}
/* Short OK/BOOT press: what it does (index = home_config_t.ok_action). Since 0.6 the default is
 * the "emini" card; the language moved to the phone panel alone, and a record that still asks for
 * the old language action is read as the card. */
static const char *const ok_actions[] = {"info", "refresh", "hold", "setup"};
/* Brush: tone structure of the large fields (index = home_config_t.brush).
 * The line-based screens of the 0.5.0 pre-releases are still accepted and read as grain, so a
 * record written by one of those builds still loads. */
static const char *const brushes[] = {"grain", "halftone", "grid"};
static const char *const brushes_legacy[] = {"engraving", "crosshatch", "auto"};
/* Power mode (index = home_power_mode_t). A record without it - every one written before 0.6.0 -
 * reads as Breath, the default. */
static const char *const power_modes[] = {"breath", "open"};
static int choice(const cJSON *j, const char *k, const char *const *values, int count)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsString(v))
        return -1;
    for (int i = 0; i < count; i++)
        if (!strcmp(v->valuestring, values[i]))
            return i;
    return -1;
}
static bool hm(const cJSON *j, const char *k, uint16_t *out)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsString(v))
        return false;
    const char *s = v->valuestring;
    if (strlen(s) != 5 || s[2] != ':' || !isdigit((unsigned char)s[0]) ||
        !isdigit((unsigned char)s[1]) || !isdigit((unsigned char)s[3]) ||
        !isdigit((unsigned char)s[4]))
        return false;
    int h = (s[0] - 48) * 10 + s[1] - 48, m = (s[3] - 48) * 10 + s[4] - 48;
    if (h > 23 || m > 59)
        return false;
    *out = h * 60 + m;
    return true;
}

/* Bin colours, indexed by HOME_INK_*. */
static const char *const bin_colours[] = {"black", "white", "yellow", "red"};
/* "YYYY-MM-DD" as days since 1970-01-01; rejects dates that do not exist. */
static bool date_value(const cJSON *j, const char *k, int32_t *out)
{
    cJSON *v = get(j, k);
    if (!cJSON_IsString(v))
        return false;
    const char *s = v->valuestring;
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)s[i]))
            return false;
    int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    int32_t day = home_days_from_civil(y, m, d);
    int y2, m2, d2;
    home_civil_from_days(day, &y2, &m2, &d2);
    if (y2 != y || m2 != m || d2 != d)
        return false;
    *out = day;
    return true;
}

/* A screen reference in a record of the given schema. Schema 1 knew six screens; everything that
 * showed data (weather, feed, sky, air, picture) is Today now, which contains the weather, and only
 * the note stays the note. */
static int legacy_to_screen(int legacy)
{
    return legacy == 2 ? HOME_NOTE : HOME_TODAY;
}
static int screen_choice(const cJSON *j, const char *k, int schema)
{
    if (schema == 2)
        return choice(j, k, screens, HOME_SCREEN_COUNT);
    int n = choice(j, k, legacy_screens, LEGACY_SCREENS);
    return n < 0 ? -1 : legacy_to_screen(n);
}

bool home_config_decode(const char *text, size_t len, home_config_t *out, const home_config_t *base,
                        bool recipe, char err[128])
{
    const char *reason = "Invalid configuration";
    cJSON *j = NULL;
    home_config_t c;
    int n;
    static const char *const normal[] = {
        "schema",   "revision", "name",         "locale",       "units",          "timezone",
        "location", "latitude", "longitude",    "note",         "location_ready", "feed_url",
        "enabled",  "order",    "styles",       "texture",      "intensity",      "large_text",
        "clock24",  "mode",     "fixed_screen", "interval_min", "pause_min",      "quiet",
        "weekdays", "day",      "cycle_min",    "ok_action",    "air_main",       "brush",
        "power_mode", "alerts",  "bins",     "evening"};
    static const char *const public_keys[] = {
        "schema",   "locale",       "units",        "enabled",    "order",
        "styles",   "texture",      "intensity",    "large_text", "clock24",
        "mode",     "fixed_screen", "interval_min", "pause_min",  "quiet",
        "weekdays", "day",          "cycle_min",    "ok_action",  "air_main",
        "brush"};
#define REQUIRE(condition, why)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            reason = (why);                                                                        \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)
    REQUIRE(text && len > 0 && len <= 8192 && strlen(text) == len && !strstr(text, "\\u0000"),
            "Invalid body length or NUL");
    j = cJSON_ParseWithLengthOpts(text, len + 1, NULL, true);
    REQUIRE(j && unique(j, 0), "Invalid or duplicate JSON fields");
    REQUIRE(known(j, recipe ? public_keys : normal,
                  recipe ? sizeof(public_keys) / sizeof(*public_keys)
                         : sizeof(normal) / sizeof(*normal)),
            "Unknown fields");
    if (base)
        c = *base;
    else
        home_config_defaults(&c);
    REQUIRE(integer(j, "schema", 1, HOME_SCHEMA, &n), "Unsupported schema");
    const int schema = n;
    if (!recipe) {
        REQUIRE(integer(j, "revision", 1, 2147483646, &n), "Invalid revision");
        c.revision = (uint32_t)n;
        REQUIRE(strval(j, "name", c.name, sizeof(c.name), false) && c.name[0],
                "Invalid device name");
        REQUIRE(strval(j, "timezone", c.timezone, sizeof(c.timezone), false) &&
                    home_timezone(c.timezone),
                "Unsupported timezone");
        REQUIRE(strval(j, "location", c.location, sizeof(c.location), false) && c.location[0],
                "Invalid place name");
        REQUIRE(number(j, "latitude", -90, 90, &c.latitude) &&
                    number(j, "longitude", -180, 180, &c.longitude),
                "Invalid coordinates");
        cJSON *location_ready = get(j, "location_ready");
        REQUIRE(!location_ready || cJSON_IsBool(location_ready), "Invalid location state");
        /* Older saved configurations already had a selected place. */
        c.location_ready = !location_ready || cJSON_IsTrue(location_ready);
        REQUIRE(strval(j, "note", c.note, sizeof(c.note), true), "Note exceeds 240 UTF-8 bytes");
        /* feed_url went with the News screen in 0.7; a 0.6 record still carries it. */
        REQUIRE(!get(j, "feed_url") || cJSON_IsString(get(j, "feed_url")), "Invalid feed URL");
        if (get(j, "alerts")) { /* optional since 0.7; a household fact, not a recipe key */
            cJSON *al = get(j, "alerts");
            static const char *const akeys[] = {"air"};
            REQUIRE(known(al, akeys, 1) && boolean(al, "air", &c.alerts_air), "Invalid alerts");
        }
        if (get(j, "evening")) /* optional since 0.7; a household fact, not a recipe key */
            REQUIRE(hm(j, "evening", &c.evening_minute), "Invalid evening time");
        if (get(j, "bins")) { /* optional since 0.7; a household fact, not a recipe key */
            cJSON *b = get(j, "bins");
            static const char *const bkeys[] = {"weekday", "reference", "from", "until", "list"};
            REQUIRE(known(b, bkeys, 5) && integer(b, "weekday", 0, 6, &n), "Invalid bins");
            c.bin_weekday = (uint8_t)n;
            REQUIRE(date_value(b, "reference", &c.bin_reference), "Invalid bins reference date");
            REQUIRE(home_weekday(c.bin_reference) == c.bin_weekday,
                    "Bins reference date is not on the collection weekday");
            REQUIRE(hm(b, "from", &c.bins_from) && hm(b, "until", &c.bins_until) &&
                        c.bins_from < c.bins_until,
                    "Invalid bins reminder window");
            cJSON *l = get(b, "list");
            REQUIRE(cJSON_IsArray(l) && cJSON_GetArraySize(l) <= HOME_BINS, "Invalid bins list");
            static const char *const lkeys[] = {"colour", "every", "week", "label"};
            c.bin_count = (uint8_t)cJSON_GetArraySize(l);
            memset(c.bins, 0, sizeof c.bins);
            for (int i = 0; i < c.bin_count; i++) {
                cJSON *v = cJSON_GetArrayItem(l, i);
                int every, week;
                REQUIRE(known(v, lkeys, 4), "Invalid bin");
                n = choice(v, "colour", bin_colours, 4);
                REQUIRE(n >= 0, "Invalid bin colour");
                REQUIRE(integer(v, "every", 1, 4, &every) && integer(v, "week", 0, 3, &week) &&
                            week < every,
                        "Invalid bin frequency");
                REQUIRE(!get(v, "label") ||
                            strval(v, "label", c.bins[i].label, sizeof c.bins[i].label, false),
                        "Invalid bin label");
                c.bins[i].colour = (uint8_t)n;
                c.bins[i].every = (uint8_t)every;
                c.bins[i].week = (uint8_t)week;
            }
        }
        if (get(j, "power_mode")) { /* optional since 0.6.0; not in a recipe: it is not a look */
            n = choice(j, "power_mode", power_modes, 2);
            REQUIRE(n >= 0, "Invalid power mode");
            c.power_mode = (uint8_t)n;
        }
    }
    REQUIRE(strval(j, "locale", c.locale, sizeof(c.locale), false) &&
                (!strcmp(c.locale, "en") || !strcmp(c.locale, "pl") || !strcmp(c.locale, "zh")),
            "Unsupported language");
    REQUIRE(strval(j, "units", c.units, sizeof(c.units), false) &&
                (!strcmp(c.units, "C") || !strcmp(c.units, "F")),
            "Unsupported units");
    cJSON *a = get(j, "enabled"), *o = get(j, "order");
    bool any = false;
    unsigned seen = 0;
    int listed = cJSON_IsArray(a) ? cJSON_GetArraySize(a) : 0;
    cJSON *st = get(j, "styles");
    if (schema == 2) {
        REQUIRE(cJSON_IsArray(a) && cJSON_IsArray(o) && listed == HOME_SCREEN_COUNT &&
                    cJSON_GetArraySize(o) == listed,
                "Expected three screens");
        for (int i = 0; i < listed; i++) {
            cJSON *v = cJSON_GetArrayItem(a, i);
            REQUIRE(cJSON_IsBool(v), "Invalid enabled screens");
            c.enabled[i] = cJSON_IsTrue(v);
            any |= c.enabled[i];
            v = cJSON_GetArrayItem(o, i);
            n = cJSON_IsString(v) ? home_screen_index(v->valuestring) : -1;
            REQUIRE(n >= 0 && !(seen & (1U << n)), "Invalid screen order");
            seen |= 1U << n;
            c.order[i] = n;
        }
        REQUIRE(known(st, screens, HOME_SCREEN_COUNT), "Invalid styles");
        for (int i = 0; i < HOME_SCREEN_COUNT; i++) {
            n = choice(st, screens[i], styles, 4);
            REQUIRE(n >= 0, "Unknown style");
            c.style[i] = n;
        }
    } else {
        /* Schema 1 listed three (before 0.5.0), five (before Picture) or six screens against the
         * canonical list weather, feed, note, sky, air, picture. enabled[i] is indexed by that
         * list, not by position in order[]. Today comes first and on; Weather and Note keep what
         * the record said; the screens that are gone drop out of enabled and order. */
        REQUIRE(cJSON_IsArray(a) && cJSON_IsArray(o) &&
                    (listed == 3 || listed == 5 || listed == LEGACY_SCREENS) &&
                    cJSON_GetArraySize(o) == listed,
                "Expected three, five or six screens");
        bool legacy_on[LEGACY_SCREENS] = {false};
        int at = 1;
        unsigned legacy_seen = 0;
        c.order[0] = HOME_TODAY;
        seen = 1U << HOME_TODAY;
        for (int i = 0; i < listed; i++) {
            cJSON *v = cJSON_GetArrayItem(a, i);
            REQUIRE(cJSON_IsBool(v), "Invalid enabled screens");
            legacy_on[i] = cJSON_IsTrue(v);
            v = cJSON_GetArrayItem(o, i);
            int named = -1;
            if (cJSON_IsString(v))
                for (int k = 0; k < LEGACY_SCREENS; k++)
                    if (!strcmp(v->valuestring, legacy_screens[k]))
                        named = k;
            REQUIRE(named >= 0 && !(legacy_seen & (1U << named)), "Invalid screen order");
            legacy_seen |= 1U << named;
            if (named == 0 || named == 2) { /* weather, note */
                int now = named == 2 ? HOME_NOTE : HOME_WEATHER;
                seen |= 1U << now;
                c.order[at++] = now;
            }
        }
        for (int now = 0; now < HOME_SCREEN_COUNT; now++)
            if (!(seen & (1U << now))) { /* a screen the record does not mention: off, last */
                seen |= 1U << now;
                c.order[at++] = now;
            }
        c.enabled[HOME_TODAY] = true;
        c.enabled[HOME_WEATHER] = legacy_on[0];
        c.enabled[HOME_NOTE] = listed > 2 && legacy_on[2];
        any = true;
        REQUIRE(known(st, legacy_screens, LEGACY_SCREENS), "Invalid styles");
        c.style[HOME_TODAY] = HOME_PRINT; /* Today has one composition */
        static const struct {
            int legacy, now;
        } kept[] = {{0, HOME_WEATHER}, {2, HOME_NOTE}};
        for (int i = 0; i < 2; i++) {
            if (!get(st, legacy_screens[kept[i].legacy]))
                continue;
            n = choice(st, legacy_screens[kept[i].legacy], styles, 4);
            REQUIRE(n >= 0, "Unknown style");
            c.style[kept[i].now] = n;
        }
    }
    REQUIRE(any, "Enable at least one screen");
    REQUIRE(integer(j, "texture", 1, 4, &n) && (n == 1 || n == 2 || n == 4),
            "Texture must be 1, 2 or 4");
    c.texture = n;
    REQUIRE(integer(j, "intensity", 0, 2, &n), "Invalid intensity");
    c.intensity = n;
    REQUIRE(boolean(j, "large_text", &c.large_text) && boolean(j, "clock24", &c.clock24),
            "Invalid display options");
    n = choice(j, "mode", modes, 3);
    REQUIRE(n >= 0, "Invalid mode");
    c.mode = n;
    n = screen_choice(j, "fixed_screen", schema);
    REQUIRE(n >= 0, "Invalid fixed screen");
    c.fixed_screen = n;
    REQUIRE(integer(j, "interval_min", 5, 1440, &n), "Rotation minimum is five minutes");
    c.interval_min = n;
    REQUIRE(integer(j, "pause_min", 0, 240, &n), "Invalid manual pause");
    c.pause_min = n;
    /* Optional: settings saved before 0.4.0 keep the default. */
    if (get(j, "cycle_min")) {
        REQUIRE(integer(j, "cycle_min", 5, 1440, &n), "Composition minimum is five minutes");
        c.cycle_min = n;
    }
    if (get(j, "ok_action")) { /* optional since 0.4.4 */
        n = choice(j, "ok_action", ok_actions, 4);
        if (n < 0) {
            static const char *const legacy[] = {"language"};
            if (choice(j, "ok_action", legacy, 1) >= 0)
                n = 0; /* the language button of 0.4.4-0.5.0: now the card */
        }
        REQUIRE(n >= 0, "Invalid OK button action");
        c.ok_action = (uint8_t)n;
    }
    /* air_main went with the Air screen in 0.7; a 0.6 record or recipe still carries it. */
    REQUIRE(!get(j, "air_main") || cJSON_IsString(get(j, "air_main")), "Invalid Air headline number");
    if (get(j, "brush")) { /* optional since 0.5.0 */
        n = choice(j, "brush", brushes, 3);
        if (n < 0 && choice(j, "brush", brushes_legacy, 3) >= 0)
            n = 0; /* a pre-release line brush: grain */
        REQUIRE(n >= 0, "Invalid brush");
        c.brush = (uint8_t)n;
    }
    REQUIRE(integer(j, "weekdays", 1, 127, &n), "Select at least one weekday");
    c.weekdays = n;
    cJSON *q = get(j, "quiet");
    static const char *const qkeys[] = {"enabled", "start", "end"};
    REQUIRE(known(q, qkeys, 3) && boolean(q, "enabled", &c.quiet_enabled) &&
                hm(q, "start", &c.quiet_start) && hm(q, "end", &c.quiet_end),
            "Invalid quiet hours");
    REQUIRE(!c.quiet_enabled || c.quiet_start != c.quiet_end,
            "Quiet hours must have different endpoints");
    cJSON *d = get(j, "day");
    REQUIRE(cJSON_IsArray(d) && cJSON_GetArraySize(d) == HOME_DAY_SLOTS,
            "Expected three day slots");
    static const char *const dkeys[] = {"time", "screen"};
    for (int i = 0; i < HOME_DAY_SLOTS; i++) {
        cJSON *v = cJSON_GetArrayItem(d, i);
        REQUIRE(known(v, dkeys, 2) && hm(v, "time", &c.day_minute[i]), "Invalid day slot");
        n = screen_choice(v, "screen", schema);
        REQUIRE(n >= 0, "Invalid day screen");
        c.day_screen[i] = n;
        if (i)
            REQUIRE(c.day_minute[i] > c.day_minute[i - 1], "Day times must increase");
    }
    cJSON_Delete(j);
    *out = c;
    err[0] = 0;
    return true;
fail:
    if (j)
        cJSON_Delete(j);
    snprintf(err, 128, "%s", reason);
    return false;
#undef REQUIRE
}
static void addhm(cJSON *j, const char *k, int m)
{
    if (m < 0 || m >= 1440)
        m = 0;
    int hour = m / 60, minute = m % 60;
    char b[6] = {(char)('0' + hour / 10),   (char)('0' + hour % 10),   ':',
                 (char)('0' + minute / 10), (char)('0' + minute % 10), 0};
    cJSON_AddStringToObject(j, k, b);
}
cJSON *home_config_json(const home_config_t *c, bool recipe)
{
    if (!c)
        return NULL;
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    /* A partial object must never be persisted as a valid configuration. */
#define JSON_NEED(value)                                                                           \
    do {                                                                                           \
        if (!(value))                                                                              \
            goto oom;                                                                              \
    } while (0)
#define JSON_APPEND(array, value)                                                                  \
    do {                                                                                           \
        cJSON *child_ = (value);                                                                   \
        if (!child_)                                                                               \
            goto oom;                                                                              \
        if (!cJSON_AddItemToArray((array), child_)) {                                              \
            cJSON_Delete(child_);                                                                  \
            goto oom;                                                                              \
        }                                                                                          \
    } while (0)
    JSON_NEED(cJSON_AddNumberToObject(j, "schema", HOME_SCHEMA));
    if (!recipe) {
        JSON_NEED(cJSON_AddNumberToObject(j, "revision", c->revision));
        JSON_NEED(cJSON_AddStringToObject(j, "name", c->name));
        JSON_NEED(cJSON_AddStringToObject(j, "timezone", c->timezone));
        JSON_NEED(cJSON_AddStringToObject(j, "location", c->location));
        JSON_NEED(cJSON_AddNumberToObject(j, "latitude", c->latitude));
        JSON_NEED(cJSON_AddNumberToObject(j, "longitude", c->longitude));
        JSON_NEED(cJSON_AddBoolToObject(j, "location_ready", c->location_ready));
        JSON_NEED(cJSON_AddStringToObject(j, "note", c->note));
        cJSON *alerts = cJSON_AddObjectToObject(j, "alerts");
        JSON_NEED(alerts);
        JSON_NEED(cJSON_AddBoolToObject(alerts, "air", c->alerts_air));
        addhm(j, "evening", c->evening_minute);
        JSON_NEED(get(j, "evening"));
        cJSON *bins = cJSON_AddObjectToObject(j, "bins");
        JSON_NEED(bins);
        JSON_NEED(cJSON_AddNumberToObject(bins, "weekday", c->bin_weekday));
        int ry, rm, rd;
        home_civil_from_days(c->bin_reference, &ry, &rm, &rd);
        char ref[16];
        snprintf(ref, sizeof ref, "%04d-%02d-%02d", ry, rm, rd);
        JSON_NEED(cJSON_AddStringToObject(bins, "reference", ref));
        addhm(bins, "from", c->bins_from);
        JSON_NEED(get(bins, "from"));
        addhm(bins, "until", c->bins_until);
        JSON_NEED(get(bins, "until"));
        cJSON *list = cJSON_AddArrayToObject(bins, "list");
        JSON_NEED(list);
        for (int i = 0; i < c->bin_count && i < HOME_BINS; i++) {
            cJSON *v = cJSON_CreateObject();
            JSON_APPEND(list, v);
            JSON_NEED(cJSON_AddStringToObject(v, "colour", bin_colours[c->bins[i].colour & 3]));
            JSON_NEED(cJSON_AddNumberToObject(v, "every", c->bins[i].every));
            JSON_NEED(cJSON_AddNumberToObject(v, "week", c->bins[i].week));
            JSON_NEED(cJSON_AddStringToObject(v, "label", c->bins[i].label));
        }
        JSON_NEED(cJSON_AddStringToObject(j, "power_mode",
                                          power_modes[c->power_mode < 2 ? c->power_mode : 0]));
    }
    JSON_NEED(cJSON_AddStringToObject(j, "locale", c->locale));
    JSON_NEED(cJSON_AddStringToObject(j, "units", c->units));
    cJSON *a = cJSON_AddArrayToObject(j, "enabled");
    JSON_NEED(a);
    cJSON *o = cJSON_AddArrayToObject(j, "order");
    JSON_NEED(o);
    cJSON *s = cJSON_AddObjectToObject(j, "styles");
    JSON_NEED(s);
    for (int i = 0; i < HOME_SCREEN_COUNT; i++) {
        JSON_APPEND(a, cJSON_CreateBool(c->enabled[i]));
        JSON_APPEND(o, cJSON_CreateString(screens[c->order[i]]));
        JSON_NEED(cJSON_AddStringToObject(s, screens[i], styles[c->style[i]]));
    }
    JSON_NEED(cJSON_AddNumberToObject(j, "texture", c->texture));
    JSON_NEED(cJSON_AddNumberToObject(j, "intensity", c->intensity));
    JSON_NEED(cJSON_AddBoolToObject(j, "large_text", c->large_text));
    JSON_NEED(cJSON_AddBoolToObject(j, "clock24", c->clock24));
    JSON_NEED(cJSON_AddStringToObject(j, "mode", modes[c->mode]));
    JSON_NEED(cJSON_AddStringToObject(j, "fixed_screen", screens[c->fixed_screen]));
    JSON_NEED(cJSON_AddNumberToObject(j, "interval_min", c->interval_min));
    JSON_NEED(cJSON_AddNumberToObject(j, "pause_min", c->pause_min));
    JSON_NEED(cJSON_AddNumberToObject(j, "cycle_min", c->cycle_min));
    JSON_NEED(
        cJSON_AddStringToObject(j, "ok_action", ok_actions[c->ok_action < 4 ? c->ok_action : 0]));
    JSON_NEED(cJSON_AddStringToObject(j, "brush", brushes[c->brush < 3 ? c->brush : 0]));
    JSON_NEED(cJSON_AddNumberToObject(j, "weekdays", c->weekdays));
    cJSON *q = cJSON_AddObjectToObject(j, "quiet");
    JSON_NEED(q);
    JSON_NEED(cJSON_AddBoolToObject(q, "enabled", c->quiet_enabled));
    addhm(q, "start", c->quiet_start);
    JSON_NEED(get(q, "start"));
    addhm(q, "end", c->quiet_end);
    JSON_NEED(get(q, "end"));
    cJSON *d = cJSON_AddArrayToObject(j, "day");
    JSON_NEED(d);
    for (int i = 0; i < HOME_DAY_SLOTS; i++) {
        cJSON *v = cJSON_CreateObject();
        JSON_APPEND(d, v);
        addhm(v, "time", c->day_minute[i]);
        JSON_NEED(get(v, "time"));
        JSON_NEED(cJSON_AddStringToObject(v, "screen", screens[c->day_screen[i]]));
    }
    return j;
oom:
    cJSON_Delete(j);
    return NULL;
#undef JSON_APPEND
#undef JSON_NEED
}
bool home_is_quiet(const home_config_t *c, const struct tm *t)
{
    if (!c->quiet_enabled || !t)
        return false;
    int m = t->tm_hour * 60 + t->tm_min;
    return c->quiet_start < c->quiet_end ? (m >= c->quiet_start && m < c->quiet_end)
                                         : (m >= c->quiet_start || m < c->quiet_end);
}
int home_schedule_screen(const home_config_t *c, const struct tm *t, int current, int64_t elapsed)
{
    if (c->mode != HOME_FIXED && t && !(c->weekdays & (1 << ((t->tm_wday + 6) % 7))))
        return current;
    int desired = current;
    if (c->mode == HOME_FIXED)
        desired = c->fixed_screen;
    else if (c->mode == HOME_DAY && t) {
        int dow = (t->tm_wday + 6) % 7;
        if (!(c->weekdays & (1 << dow)))
            return current;
        int m = t->tm_hour * 60 + t->tm_min;
        desired = c->day_screen[HOME_DAY_SLOTS - 1];
        for (int i = 0; i < HOME_DAY_SLOTS; i++)
            if (m >= c->day_minute[i])
                desired = c->day_screen[i];
    } else if (c->mode == HOME_ROTATE && elapsed >= c->interval_min * 60) {
        for (int i = 0; i < HOME_SCREEN_COUNT; i++)
            if (c->order[i] == current) {
                for (int n = 1; n <= HOME_SCREEN_COUNT; n++) {
                    int j = c->order[(i + n) % HOME_SCREEN_COUNT];
                    if (c->enabled[j])
                        return j;
                }
            }
    }
    if (desired >= 0 && desired < HOME_SCREEN_COUNT && c->enabled[desired])
        return desired;
    for (int i = 0; i < HOME_SCREEN_COUNT; i++)
        if (c->enabled[c->order[i]])
            return c->order[i];
    return current;
}

int home_style_for(const home_config_t *c, int screen, uint32_t showing)
{
    if (screen < 0 || screen >= HOME_SCREEN_COUNT)
        return HOME_PRINT;
    int style = c->style[screen];
    if (style != HOME_CYCLE)
        return style <= HOME_ATLAS ? style : HOME_PRINT;
    return showing ? (int)((showing - 1) % 3) : HOME_PRINT;
}
int home_auto_screen(const home_config_t *c, const home_data_t *d, const struct tm *t, int current,
                     int64_t elapsed)
{
    /* Without local time, weekday and quiet-hour decisions are unknown.
     * Manual requests bypass this automatic selector in the main loop. */
    if (!t && c->mode != HOME_FIXED)
        return current;
    home_config_t ready = *c;
    /* Today is useful with just a place; the bins and pushed values that also make it ready
     * arrive with those features. */
    ready.enabled[HOME_TODAY] &= c->location_ready;
    ready.enabled[HOME_WEATHER] &= c->location_ready && d->weather.meta.valid;
    ready.enabled[HOME_NOTE] &= c->note[0] != 0;
    bool ready_any = false;
    for (int i = 0; i < HOME_SCREEN_COUNT; i++)
        ready_any |= ready.enabled[i];
    if (!ready_any)
        return current;
    return home_schedule_screen(&ready, t, current, elapsed);
}
