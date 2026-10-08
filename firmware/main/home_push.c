#include "home_push.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "home_config.h"

#define PERCENT_STEP 10.0f

static bool refuse(char err[64], const char *why)
{
    snprintf(err, 64, "%s", why);
    return false;
}
static const cJSON *item(const cJSON *o, const char *k)
{
    return cJSON_GetObjectItemCaseSensitive((cJSON *)o, k);
}
static bool keys_in(const cJSON *o, const char *const *keys, int n)
{
    for (const cJSON *v = o->child; v; v = v->next) {
        bool found = false;
        for (int i = 0; i < n; ++i)
            found = found || !strcmp(v->string, keys[i]);
        if (!found)
            return false;
        for (const cJSON *w = v->next; w; w = w->next)
            if (!strcmp(w->string, v->string))
                return false;
    }
    return true;
}
static bool in_range(const cJSON *v, double lo, double hi)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= lo &&
           v->valuedouble <= hi;
}
home_push_age_t home_push_age(const home_home_t *h, int section, const home_config_t *cfg,
                              int64_t now)
{
    bool lines = section == HOME_PUSH_LINES_SECTION;
    int64_t at = lines ? h->lines_at : h->battery_at;
    if (at <= 0)
        return HOME_PUSH_NONE;
    if (lines && !h->line_count)
        return HOME_PUSH_NONE;
    if (!lines && !isfinite(h->battery_percent))
        return HOME_PUSH_NONE;
    int ttl = lines ? h->lines_ttl_min : h->battery_ttl_min;
    int64_t age = now - at, limit = (int64_t)(ttl ? ttl : cfg->home_stale_min) * 60;
    if (age < 0)
        age = 0;
    if (age > HOME_PUSH_GONE_S)
        return HOME_PUSH_GONE;
    return age > limit ? HOME_PUSH_STALE : HOME_PUSH_FRESH;
}
static bool battery_apply(const cJSON *b, home_home_t *h, int64_t now, char err[64])
{
    static const char *const keys[] = {"percent", "kwh", "ttl_min"};
    if (cJSON_IsNull(b)) {
        h->battery_at = 0;
        h->battery_ttl_min = 0;
        h->battery_percent = h->battery_kwh = NAN;
        return true;
    }
    if (!cJSON_IsObject(b) || !keys_in(b, keys, 3))
        return refuse(err, "Invalid battery");
    const cJSON *pc = item(b, "percent"), *kwh = item(b, "kwh"), *ttl = item(b, "ttl_min");
    if (!in_range(pc, 0, 100))
        return refuse(err, "Invalid battery percent");
    if (kwh && !in_range(kwh, 0, 1000))
        return refuse(err, "Invalid battery kWh");
    if (ttl && (!in_range(ttl, 1, 1440) || ttl->valuedouble != floor(ttl->valuedouble)))
        return refuse(err, "Invalid battery ttl_min");
    h->battery_percent = (float)pc->valuedouble;
    h->battery_kwh = kwh ? (float)kwh->valuedouble : NAN;
    h->battery_ttl_min = ttl ? (uint16_t)ttl->valuedouble : 0;
    h->battery_at = now;
    return true;
}
static bool lines_apply(const cJSON *a, home_home_t *h, int64_t now, uint16_t ttl, char err[64])
{
    static const char *const keys[] = {"level", "text"};
    if (cJSON_IsNull(a) || (cJSON_IsArray(a) && !cJSON_GetArraySize(a))) {
        h->line_count = 0;
        h->lines_at = 0;
        h->lines_ttl_min = 0;
        return true;
    }
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) > HOME_PUSH_LINES)
        return refuse(err, "At most 3 lines");
    int n = 0;
    home_line_t out[HOME_PUSH_LINES];
    memset(out, 0, sizeof out);
    for (const cJSON *l = a->child; l; l = l->next, ++n) {
        const cJSON *level = item(l, "level"), *text = item(l, "text");
        if (!cJSON_IsObject(l) || !keys_in(l, keys, 2) || !cJSON_IsString(text) ||
            !text->valuestring[0] || !home_utf8(text->valuestring, sizeof out[0].text - 1, false))
            return refuse(err, "Invalid line");
        if (!level)
            out[n].level = HOME_LINE_INFO;
        else if (!cJSON_IsString(level))
            return refuse(err, "Invalid line level");
        else if (!strcmp(level->valuestring, "warn"))
            out[n].level = HOME_LINE_WARN;
        else if (!strcmp(level->valuestring, "info"))
            out[n].level = HOME_LINE_INFO;
        else if (!strcmp(level->valuestring, "outline"))
            out[n].level = HOME_LINE_OUTLINE;
        else
            return refuse(err, "Invalid line level");
        snprintf(out[n].text, sizeof out[n].text, "%s", text->valuestring);
    }
    memcpy(h->line, out, sizeof out);
    h->line_count = (uint8_t)n;
    h->lines_at = now;
    h->lines_ttl_min = ttl;
    return true;
}
bool home_push_apply(const cJSON *body, home_home_t *h, int64_t now, char err[64])
{
    static const char *const keys[] = {"battery", "lines", "lines_ttl_min"};
    if (!cJSON_IsObject(body) || !keys_in(body, keys, 3))
        return refuse(err, "Unknown key");
    const cJSON *battery = item(body, "battery"), *lines = item(body, "lines"),
                *ttl = item(body, "lines_ttl_min");
    if (!battery && !lines)
        return refuse(err, "Nothing to store");
    if (ttl && (!lines || !in_range(ttl, 1, 1440) || ttl->valuedouble != floor(ttl->valuedouble)))
        return refuse(err, "Invalid lines_ttl_min");
    home_home_t next = *h;
    if (battery && !battery_apply(battery, &next, now, err))
        return false;
    if (lines && !lines_apply(lines, &next, now, ttl ? (uint16_t)ttl->valuedouble : 0, err))
        return false;
    *h = next;
    return true;
}
static bool lines_equal(const home_home_t *a, const home_home_t *b)
{
    if (a->line_count != b->line_count)
        return false;
    for (int i = 0; i < a->line_count; ++i)
        if (a->line[i].level != b->line[i].level || strcmp(a->line[i].text, b->line[i].text))
            return false;
    return true;
}
bool home_push_redraw(const home_home_t *before, const home_home_t *after, const home_config_t *cfg,
                      int64_t now)
{
    bool b0 = home_push_age(before, HOME_PUSH_BATTERY, cfg, now) == HOME_PUSH_FRESH,
         b1 = home_push_age(after, HOME_PUSH_BATTERY, cfg, now) == HOME_PUSH_FRESH,
         l0 = home_push_age(before, HOME_PUSH_LINES_SECTION, cfg, now) == HOME_PUSH_FRESH,
         l1 = home_push_age(after, HOME_PUSH_LINES_SECTION, cfg, now) == HOME_PUSH_FRESH;
    /* What the picture would show: the lines only while fresh. */
    if (l0 != l1 || (l1 && !lines_equal(before, after)))
        return true;
    if (b0 != b1)
        return true;
    if (b1) {
        if (fabsf(after->battery_percent - before->battery_percent) >= PERCENT_STEP)
            return true;
        if ((before->battery_percent < cfg->home_low_pct) !=
            (after->battery_percent < cfg->home_low_pct))
            return true;
    }
    return false;
}
void home_push_sanitise(home_home_t *h)
{
    if (h->line_count > HOME_PUSH_LINES)
        h->line_count = HOME_PUSH_LINES;
    for (int i = 0; i < HOME_PUSH_LINES; ++i) {
        h->line[i].text[sizeof h->line[i].text - 1] = 0;
        if (h->line[i].level > HOME_LINE_OUTLINE)
            h->line[i].level = HOME_LINE_INFO;
    }
    if (!isfinite(h->battery_percent) || h->battery_percent < 0 || h->battery_percent > 100)
        h->battery_at = 0, h->battery_percent = NAN;
    if (!isfinite(h->battery_kwh))
        h->battery_kwh = NAN;
}
