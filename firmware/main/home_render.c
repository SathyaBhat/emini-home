/* Home native scene renderer. Original Home implementation, 2026.
 * Exactly one pigment per pixel; generated Atkinson masks retain OFL notice.
 * Pure, reentrant C: no heap, I/O, global mutable state or device dependency. */
#include "home_types.h"
#include "home_places.h"
#include "home_bins.h"
#include "home_air.h"
#include "home_qr.h"
#include "home_sky.h"
#include "home_parse.h"
#include "generated/home_font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { BLACK = 0, PAPER = 1, YELLOW = 2, RED = 3, W = 400, H = 300 };
#ifndef HOME_VERSION_TEXT
#define HOME_VERSION_TEXT "0.6.2"
#endif
/* Brushes: the user picks the tone structure in the
 * panel. The line-based screens (engraving, cross-hatch) were dropped after the device test:
 * on narrow strips and thin bars they read as broken stripes, not as texture. */
enum { RASTER_NOISE = 0, RASTER_DOTS = 1, RASTER_GRID = 2 };
typedef struct {
    uint8_t *frame;
    int cell, intensity;
    int lang; /* LANG_*: chooses the words, and the Polish one-letter line-break rule. */
    int raster; /* tone structure in force: grain, or a screen of dots/lines/cross/grid */
    int brush;  /* the user's brush (RASTER_*), painted on the large fields only */
} canvas_t;
#include "generated/home_noise.h"
/* Tones are ordered dither: through a 64x64 blue-noise mask by default (Renderer 2, 0.5.0: no
 * grid, no banding on ramps), or through one of the brushes below; the 4x4 Bayer matrix of
 * 0.3.1-0.4.4 stays as the "grid" brush. */
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static int imin(int a, int b)
{
    return a < b ? a : b;
}
static int imax(int a, int b)
{
    return a > b ? a : b;
}
static double clamp(double v, double a, double b)
{
    return !isfinite(v) ? a : v < a ? a : v > b ? b : v;
}
static float clampf(float v, float a, float b)
{
    return !isfinite(v) ? a : v < a ? a : v > b ? b : v;
}
/* Three display languages. English is the fallback: a missing translation shows English
 * words rather than boxes or an empty line. */
enum { LANG_EN = 0, LANG_PL = 1, LANG_ZH = 2 };
int home_language(const char *locale)
{
    if (!locale)
        return LANG_EN;
    if (locale[0] == 'p' && locale[1] == 'l')
        return LANG_PL;
    if (locale[0] == 'z' && locale[1] == 'h')
        return LANG_ZH;
    return LANG_EN;
}
static int lang_of(const home_config_t *c)
{
    return home_language(c->locale);
}
/* Chinese is kept as a dictionary from the English wording instead of a third argument at
 * every call: the screens stay readable, the whole translation can be reviewed in one place,
 * and the next language is a second table. Sorted by the English text, binary search. */
typedef struct {
    const char *en, *zh;
} phrase_t;
static const phrase_t chinese[] = {
    {" · sunscreen from %s", " · %s 起涂防晒"},
    {"%lu d", "%lu 天"},
    {"%lu h", "%lu 小时"},
    {"%s for %d h", "%s 持续 %d 小时"},
    {"%s from %s", "%s 从 %s 起"},
    {"%s until %s", "%s 到 %s"},
    {"1  JOIN THIS WI-FI NETWORK", "1  用手机连接此 WI-FI"},
    {"1  JOIN WI-FI", "1  连接 WI-FI"},
    {"2  OPEN HOME", "2  打开 HOME"},
    {"2  OPEN THIS ADDRESS IN YOUR BROWSER", "2  在浏览器中打开此地址"},
    {"24h", "24h"},
    {"3  Pairing code", "3  配对码"},
    {"A forecast for your place.", "你所在地的天气预报。"},
    {"A little room for the world.", "留一点空间给世界。"},
    {"A sky for your place.", "你所在地的天空。"},
    {"AIR", "空气"},
    {"AIR QUALITY", "空气质量"},
    {"AWAKE", "运行"},
    {"About %d days", "约 %d 天"},
    {"About %d h", "约 %d 小时"},
    {"Age unknown · check time", "时间未知 · 请检查时钟"},
    {"Air", "空气"},
    {"BATTERY", "电池"},
    {"BATTERY · LAST SEVEN DAYS", "电池 · 最近七天"},
    {"Cannot load data · check the phone panel", "无法获取数据 · 请查看手机面板"},
    {"Charged", "已充满"},
    {"Charging", "充电中"},
    {"Checked %lld days ago", "%lld 天前查询"},
    {"Checked %lld h ago", "%lld 小时前查询"},
    {"Checked just now", "刚刚查询"},
    {"Checked within the hour", "一小时内查询过"},
    {"Choose a screen.", "选择一个画面。"},
    {"Choose an RSS or Atom source in your phone panel. One story, without a stream to chase.",
     "在手机面板里选择 RSS 或 Atom 源。只看一条消息，不必追着信息流跑。"},
    {"Clear skies", "晴"},
    {"Cloud cover", "多云"},
    {"Computed on the device · nothing downloaded", "在设备上算出 · 不下载数据"},
    {"Connect your phone.", "连接你的手机。"},
    {"Connection failed · saved data", "连接失败 · 使用已存数据"},
    {"DAYS HERE", "在此天数"},
    {"DOWNLOADS", "下载次数"},
    {"DRAWN IN", "绘制用时"},
    {"Date unknown", "日期未知"},
    {"Dry until %s", "%s 前无降水"},
    {"EU index", "欧盟指数"},
    {"FORECAST", "预报"},
    {"First quarter", "上弦月"},
    {"Fog", "雾"},
    {"Full in %d days", "%d 天后满月"},
    {"Full moon", "满月"},
    {"Full story in phone panel", "全文见手机面板"},
    {"Full today", "今天满月"},
    {"Help · emini.ink/home", "帮助 · emini.ink/home"},
    {"Home reads the clock from the internet, and the sun and the moon appear here as soon as it has one.",
     "Home 从网络获取时间，一旦有了时间，日月就会出现在这里。"},
    {"Home, meet your phone.", "Home，认识一下你的手机。"},
    {"Hourly detail unavailable", "无逐小时预报"},
    {"Last quarter", "下弦月"},
    {"Learning how long a charge lasts", "正在学习一次充电能用多久"},
    {"Lit %s%%", "照亮 %s%%"},
    {"MAY SLEEP", "可休眠"},
    {"Make this space yours.", "这块地方留给你。"},
    {"Midnight sun", "极昼"},
    {"Mostly clear", "大致晴朗"},
    {"NETWORK PASSWORD", "网络密码"},
    {"NEXT 24 H · PM2.5, UV IN YELLOW", "未来 24 小时 · PM2.5，紫外线为黄色"},
    {"NEXT HOURS · °%s / mm", "未来几小时 · °%s / 毫米"},
    {"NOW", "现在"},
    {"New in %d days", "%d 天后新月"},
    {"New moon", "新月"},
    {"New today", "今天新月"},
    {"No index", "无指数"},
    {"No pollen forecast for this place", "此地无花粉预报"},
    {"No range", "无高低温"},
    {"ONE STORY", "一条消息"},
    {"Older data · waiting for update", "数据较旧 · 等待更新"},
    {"Open the panel on your phone and choose what Home shows.", "打开手机面板，选择 Home 显示的内容。"},
    {"Open the phone panel and use your location. Air quality, UV and pollen from Open-Meteo will appear here within the hour.",
     "打开手机面板并使用你的位置。来自 Open-Meteo 的空气质量、紫外线和花粉会在一小时内出现。"},
    {"Open the phone panel and use your location. The first forecast will appear here.",
     "打开手机面板并使用你的位置。第一份预报会出现在这里。"},
    {"Open the phone panel and use your location. The sun and the moon are then worked out here, with nothing downloaded.",
     "打开手机面板并使用你的位置。日月将在设备上算出，不下载任何数据。"},
    {"PAPER TIME", "纸屏用时"},
    {"PICTURES", "画面"},
    {"PICTURES DRAWN", "已绘制画面"},
    {"PM2.5 in µg per m3", "PM2.5 微克每立方米"},
    {"Panel open for 5 minutes", "面板开放 5 分钟"},
    {"Partly cloudy", "局部多云"},
    {"Password", "密码"},
    {"Polar night", "极夜"},
    {"Rain ahead", "有雨"},
    {"SKY", "天空"},
    {"Sky needs the time.", "天空需要时间。"},
    {"Sleet", "雨夹雪"},
    {"Snow", "雪"},
    {"Sunrise", "日出"},
    {"Sunrise %s · Sunset %s", "日出 %s · 日落 %s"},
    {"Sunrise and sunset unknown", "日出日落未知"},
    {"Sunset", "日落"},
    {"THE PROJECT", "项目"},
    {"TODAY", "今天"},
    {"The air, at a glance.", "一眼看懂空气。"},
    {"The sun does not rise today", "今天太阳不升"},
    {"The sun does not set today", "今天太阳不落"},
    {"This screen arrives with the next update.", "这个画面会在下次更新时出现。"},
    {"Thunderstorms", "雷雨"},
    {"WAKES / H", "唤醒 / 小时"},
    {"Waning crescent", "残月"},
    {"Waning gibbous", "亏凸月"},
    {"Waxing crescent", "蛾眉月"},
    {"Waxing gibbous", "盈凸月"},
    {"Weather", "天气"},
    {"Weather forecast", "天气预报"},
    {"Wind %s km/h", "风 %s 公里/时"},
    {"Wind —", "风 —"},
    {"With you for %lld days", "陪伴你 %lld 天"},
    {"Write a message in your phone panel. A reminder, a thought, something worth keeping in view.",
     "在手机面板里写一段话。提醒、想法，或者值得留在眼前的东西。"},
    {"YOUR NOTE", "你的便笺"},
    {"Your source", "你的信息源"},
    {"Yours to keep in view.", "留在眼前的话。"},
    {"day %d h %02d min", "昼长 %d 小时 %02d 分"},
    {"day %d h %02d min (%s%s min)", "昼长 %d 小时 %02d 分（%s%s 分）"},
    {"daylight all day", "全天有光"},
    {"no daylight today", "今天没有日光"},
    {"sunscreen now", "现在涂防晒"},
};
/* Checked by a build gate: every English phrase above is one the screens really pass to
 * tr(), and every Chinese character is in the font the device carries (GB 2312). */
static const char *chinese_for(const char *en)
{
    size_t low = 0, high = sizeof chinese / sizeof chinese[0];
    while (low < high) {
        size_t mid = (low + high) / 2;
        int d = strcmp(en, chinese[mid].en);
        if (!d)
            return chinese[mid].zh[0] ? chinese[mid].zh : NULL;
        if (d < 0)
            high = mid;
        else
            low = mid + 1;
    }
    return NULL;
}
static const char *tr(int lang, const char *en, const char *pol)
{
    if (lang == LANG_ZH) {
        const char *zh = chinese_for(en);
        return zh ? zh : en;
    }
    return lang == LANG_PL ? pol : en;
}
static void pixel(canvas_t *c, int x, int y, int p)
{
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    if (c->intensity == 0) {
        if (p == YELLOW)
            p = PAPER;
        else if (p == RED)
            p = BLACK;
    }
    unsigned i = (unsigned)y * 100u + (unsigned)x / 4u, shift = 6u - ((unsigned)x % 4u) * 2u;
    c->frame[i] = (uint8_t)((c->frame[i] & ~(3u << shift)) | ((unsigned)p << shift));
}
static void rect(canvas_t *c, int x, int y, int w, int h, int p)
{
    for (int yy = imax(y, 0); yy < imin(y + h, H); ++yy)
        for (int xx = imax(x, 0); xx < imin(x + w, W); ++xx)
            pixel(c, xx, yy, p);
}
/* Tone threshold in 0..1 at (x, y) for ink pigment `ink`, in cell space (x, y >> shift), so a
 * screen for colour is never finer than 2 px. Blue noise by default; the structured screens
 * (Renderer 2, 0.4) give each pigment its own angle (yellow 15, red 75, black 45 degrees) so
 * that pigments meeting on one field do not moire. Tone = line thickness or dot size. */
static float threshold(const canvas_t *c, int x, int y, int ink, int shift)
{
    int xs = x >> shift, ys = y >> shift;
    if (c->raster == RASTER_GRID)
        return (bayer[ys & 3][xs & 3] + 0.5f) * 0.0625f;
    if (c->raster == RASTER_NOISE)
        return (home_noise[(ys & 63) * 64 + (xs & 63)] + 0.5f) * (1.0f / 256.0f);
    /* Halftone: dots on a 6-cell grid (12 px for colour), each pigment at its own angle so
     * two of them meeting on one field do not moire. Tone = the area of the dot. */
    static const float angles[3][2] = {{0.258819f, 0.965926f}, {0.965926f, 0.258819f},
                                       {0.707107f, 0.707107f}};
    const float *ang = angles[ink == YELLOW ? 0 : ink == RED ? 1 : 2];
    const float inv = 1.0f / 6.0f;
    float u = (xs * ang[1] + ys * ang[0]) * inv, v = (-xs * ang[0] + ys * ang[1]) * inv;
    float du = (u - floorf(u)) - 0.5f, dv = (v - floorf(v)) - 0.5f;
    return (du * du + dv * dv) * 3.14159265f;
}
static int mix(canvas_t *c, int x, int y, int a, int b, float coverage)
{
    /* cell is 1/2/4; shifting avoids two runtime divisions per pigment pixel. */
    int shift = c->cell >> 1;
    if (c->intensity == 0) {
        if (a == YELLOW)
            a = PAPER;
        if (a == RED)
            a = BLACK;
        if (b == YELLOW)
            b = PAPER;
        if (b == RED)
            b = BLACK;
    }
    /* Colour is never finer than 2 px; black-and-paper patterns keep 1 px. */
    if (shift == 0 && (a == YELLOW || a == RED || b == YELLOW || b == RED))
        shift = 1;
    if (c->intensity == 1)
        coverage *= 0.55f;
    return coverage > threshold(c, x, y, b, shift) ? b : a;
}
/* Three pigments in one point (Renderer 2): shares wa, wb, wd of pigments a, b, d (any sum > 0;
 * they are normalised). One mask threshold picks the pigment by interval, so every share is
 * monotonic in its weight. Same cell and intensity rules as mix(): colour never finer than 2 px;
 * intensity 0 folds colour into black and paper; intensity 1 keeps 55 % of the colour shares. */
static int mix3(canvas_t *c, int x, int y, int a, int b, int d, float wa, float wb, float wd)
{
    int shift = c->cell >> 1;
    if (c->intensity == 0) {
        a = a == YELLOW ? PAPER : a == RED ? BLACK : a;
        b = b == YELLOW ? PAPER : b == RED ? BLACK : b;
        d = d == YELLOW ? PAPER : d == RED ? BLACK : d;
    }
    if (shift == 0 && (a == YELLOW || a == RED || b == YELLOW || b == RED || d == YELLOW || d == RED))
        shift = 1;
    if (c->intensity == 1) { /* less colour: every colour share, whichever slot holds it */
        if (a == YELLOW || a == RED)
            wa *= 0.55f;
        if (b == YELLOW || b == RED)
            wb *= 0.55f;
        if (d == YELLOW || d == RED)
            wd *= 0.55f;
    }
    wa = wa < 0 ? 0 : wa;
    wb = wb < 0 ? 0 : wb;
    wd = wd < 0 ? 0 : wd;
    float sum = wa + wb + wd;
    if (sum <= 0)
        return a;
    float t = threshold(c, x, y, wd >= wb ? d : b, shift) * sum;
    return t < wa ? a : t < wa + wb ? b : d;
}
static void line(canvas_t *c, int x0, int y0, int x1, int y1, int p)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
        err = dx + dy;
    for (;;) {
        pixel(c, x0, y0, p);
        if (x0 == x1 && y0 == y1)
            break;
        int e = 2 * err;
        if (e >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static size_t bounded(const char *s, size_t cap)
{
    size_t n = 0;
    if (s)
        while (n < cap && s[n])
            ++n;
    return n;
}
static bool contains(const char *s, size_t cap, const char *needle)
{
    size_t n = bounded(s, cap), m = strlen(needle);
    if (m > n)
        return false;
    for (size_t i = 0; i <= n - m; ++i)
        if (!memcmp(s + i, needle, m))
            return true;
    return false;
}
static bool time_valid(int64_t t)
{
    return t >= 1577836800LL && t <= 4102444800LL;
}
/* Consume malformed UTF-8 one byte at a time; never read beyond the span. */
static uint32_t next_cp(const char *s, size_t n, size_t *at)
{
    if (*at >= n)
        return 0;
    uint8_t a = (uint8_t)s[(*at)++];
    if (a < 0x80)
        return a;
    unsigned count = a >= 0xc2 && a <= 0xdf   ? 1
                     : a >= 0xe0 && a <= 0xef ? 2
                     : a >= 0xf0 && a <= 0xf4 ? 3
                                              : 0;
    if (!count || n - *at < count)
        return '?';
    uint32_t cp = a & ((1u << (6 - count)) - 1u);
    size_t start = *at;
    for (unsigned k = 0; k < count; ++k) {
        uint8_t b = (uint8_t)s[start + k];
        if ((b & 0xc0) != 0x80)
            return '?';
        cp = (cp << 6) | (b & 63);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return '?';
    *at += count;
    return cp;
}
static const home_glyph_t *find_glyph(const home_font_t *f, uint32_t cp)
{
    unsigned lo = f->first, hi = lo + f->count;
    while (lo < hi) {
        unsigned m = lo + (hi - lo) / 2;
        if (home_glyphs[m].codepoint < cp)
            lo = m + 1;
        else
            hi = m;
    }
    if (lo < (unsigned)f->first + f->count && home_glyphs[lo].codepoint == cp)
        return &home_glyphs[lo];
    return NULL;
}
/* Han, kana, CJK punctuation and full-width forms: no spaces, so each character is a
 * line-break opportunity (poster text), and every glyph comes from the CJK tables. */
static bool cjk(uint32_t cp)
{
    return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xf900 && cp <= 0xfaff) ||
           (cp >= 0xff00 && cp <= 0xffef);
}
/* Kinsoku: closing punctuation never starts a line, opening never ends one. */
static bool cjk_no_start(uint32_t cp)
{
    return cp == 0x3001 || cp == 0x3002 || cp == 0xff0c || cp == 0xff0e || cp == 0xff01 ||
           cp == 0xff1f || cp == 0xff1a || cp == 0xff1b || cp == 0x300d || cp == 0x300f ||
           cp == 0x3011 || cp == 0x3015 || cp == 0xff09 || cp == 0x300b || cp == 0x3009 ||
           cp == 0x2026 || cp == 0x2014;
}
static bool cjk_no_end(uint32_t cp)
{
    return cp == 0x300c || cp == 0x300e || cp == 0x3010 || cp == 0x3014 || cp == 0xff08 ||
           cp == 0x300a || cp == 0x3008;
}
/* The CJK table of the same pixel size as Atkinson font fi, if there is one. */
static const home_glyph_t *cjk_glyph(int fi, uint32_t cp)
{
    if (!cjk(cp))
        return NULL;
    for (unsigned i = 0; i < sizeof home_cjk_fonts / sizeof *home_cjk_fonts; ++i)
        if (home_cjk_fonts[i].size == home_fonts[fi].size)
            return find_glyph(&home_cjk_fonts[i], cp);
    return NULL;
}
static bool has_glyph(int fi, uint32_t cp)
{
    return cjk(cp) ? cjk_glyph(fi, cp) != NULL : find_glyph(&home_fonts[fi], cp) != NULL;
}
static const home_glyph_t *glyph(int fi, uint32_t cp)
{
    const home_glyph_t *g = cjk(cp) ? cjk_glyph(fi, cp) : find_glyph(&home_fonts[fi], cp);
    return g ? g : &home_glyphs[home_fonts[fi].first + ('?' - 32)];
}
static int width(int fi, const char *s, size_t cap)
{
    size_t at = 0, n = bounded(s, cap);
    int w = 0;
    while (at < n)
        w += glyph(fi, next_cp(s, n, &at))->advance;
    return w;
}
static void draw_glyph(canvas_t *c, int x, int baseline, int fi, uint32_t cp, int p, int bx, int by,
                       int bw, int bh)
{
    const home_glyph_t *g = glyph(fi, cp);
    int stride = (g->width + 7) / 8;
    for (int y = 0; y < g->height; ++y)
        for (int xx = 0; xx < g->width; ++xx) {
            int px = x + g->left + xx, py = baseline + g->top + y;
            if (px < bx || px >= bx + bw || py < by || py >= by + bh)
                continue;
            if (home_font_bits[g->offset + y * stride + xx / 8] & (0x80 >> (xx & 7)))
                pixel(c, px, py, p);
        }
}
/* True when cp[end] is the space after a one-letter Polish word (a i o u w z, any
 * case). Such a space is not a line break unless the line has no other space. */
static bool one_letter_word(const uint32_t *cp, int end)
{
    if (end < 1 || (end >= 2 && cp[end - 2] != ' '))
        return false;
    uint32_t ch = cp[end - 1] | 0x20u;
    return ch == 'a' || ch == 'i' || ch == 'o' || ch == 'u' || ch == 'w' || ch == 'z';
}
/* Word wrapping also breaks unspaced identifiers. Last line has a real glyph
 * ellipsis; all ink is bounded to its text region, including negative bearings. */
static void text(canvas_t *c, int x, int y, int w, int h, int fi, int p, const char *s, size_t cap)
{
    if (!s || w <= 0 || h <= 0)
        return;
    size_t n = bounded(s, cap), at = 0;
    int step = home_fonts[fi].size + 4, rows = imax(1, h / step);
    for (int row = 0; row < rows && at < n; ++row) {
        uint32_t cp[128];
        int count = 0, used = 0, space = -1, kept = -1;
        size_t space_at = 0, kept_at = 0;
        while (at < n && s[at] == ' ')
            ++at;
        size_t line_start = at;
        while (at < n && count < 128) {
            size_t before = at;
            uint32_t ch = next_cp(s, n, &at);
            if (ch == '\r')
                continue;
            if (ch == '\n')
                break;
            if (ch == '\t')
                ch = ' ';
            int advance = glyph(fi, ch)->advance;
            if (cjk(ch) && count && !cjk_no_start(ch) && !cjk_no_end(cp[count - 1])) {
                space = count; /* break before this character, keeping the previous one */
                space_at = before;
            }
            if (used + advance > w) {
                at = before;
                if (space < 0) {
                    space = kept;
                    space_at = kept_at;
                }
                if (space >= 0) {
                    count = space;
                    at = space_at;
                }
                break;
            }
            cp[count++] = ch;
            used += advance;
            if (cjk(ch) && !cjk_no_end(ch)) {
                space = count;
                space_at = at;
            } else if (ch == ' ' && c->lang == LANG_PL && one_letter_word(cp, count - 1)) {
                kept = count - 1;
                kept_at = at;
            } else if (ch == ' ') {
                space = count - 1;
                space_at = at;
            }
        }
        if (!count && at < n &&
            at == line_start) { /* A narrower box than one glyph must still progress. */
            size_t skip = at;
            next_cp(s, n, &skip);
            at = skip;
        }
        while (count && cp[count - 1] == ' ')
            --count;
        if (row == rows - 1 && at < n) {
            int avail = w - glyph(fi, 0x2026)->advance;
            used = 0;
            int keep = 0;
            while (keep < count && used + glyph(fi, cp[keep])->advance <= avail)
                used += glyph(fi, cp[keep++])->advance;
            count = keep;
            if (count < 128)
                cp[count++] = 0x2026;
        }
        int cursor = x;
        for (int k = 0; k < count; ++k) {
            draw_glyph(c, cursor, y + row * step + home_fonts[fi].size, fi, cp[k], p, x, y, w, h);
            cursor += glyph(fi, cp[k])->advance;
        }
    }
}
static void txt(canvas_t *c, int x, int y, int w, int h, int fi, const char *s)
{
    text(c, x, y, w, h, fi, BLACK, s, 512);
}
static void top(canvas_t *c, const home_config_t *cfg, const char *section)
{
    text(c, 14, 7, 192, 20, 1, BLACK, cfg->name[0] ? cfg->name : "emini HOME", sizeof cfg->name);
    int tw = width(0, section, 96);
    txt(c, imax(212, 386 - tw), 9, 174, 17, 0, section);
    rect(c, 14, 31, 372, 1, BLACK);
}
static void stamp(char *out, size_t len, int64_t epoch, int lang, bool clock24, const char *zone)
{
    struct tm tm;
    if (epoch <= 0 || !home_tz_localtime(zone, epoch, &tm)) {
        snprintf(out, len, "%s", tr(lang, "Date unknown", "Data nieznana"));
        return;
    }
    static const char *en[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    static const char *po[] = {"STY", "LUT", "MAR", "KWI", "MAJ", "CZE",
                               "LIP", "SIE", "WRZ", "PAŹ", "LIS", "GRU"};
    /* Chinese writes the date as 9 month 15 day and the half of the day before the hour. */
    if (lang == LANG_ZH) {
        if (clock24)
            snprintf(out, len, "%d月%d日 · %02d:%02d", tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min);
        else
            snprintf(out, len, "%d月%d日 · %s%d:%02d", tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour < 12 ? "上午" : "下午",
                     tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
        return;
    }
    if (clock24)
        snprintf(out, len, "%02d %s · %02d:%02d", tm.tm_mday, (lang == LANG_PL ? po : en)[tm.tm_mon],
                 tm.tm_hour, tm.tm_min);
    else
        snprintf(out, len, "%02d %s · %d:%02d %s", tm.tm_mday,
                 (lang == LANG_PL ? po : en)[tm.tm_mon],
                 tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}
static home_source_state_t state_at(const home_source_meta_t *m, int64_t now)
{
    if (!m->valid)
        return m->state == HOME_ERROR ? HOME_ERROR : HOME_EMPTY;
    if (m->state == HOME_ERROR)
        return HOME_ERROR;
    if (!time_valid(now) || !time_valid(m->fetched_at) || m->fetched_at > now + 300)
        return HOME_STALE;
    if (m->state == HOME_STALE || (m->expires_at > 0 && now >= m->expires_at))
        return HOME_STALE;
    return HOME_READY;
}
static void source_footer(canvas_t *c, const home_config_t *cfg, const home_source_meta_t *m,
                          int64_t now, const char *source, int64_t issue)
{
    int lang = lang_of(cfg);
    char date[64], status[96];
    stamp(date, sizeof date, issue, lang, cfg->clock24, cfg->timezone);
    home_source_state_t st = state_at(m, now);
    if (st == HOME_ERROR)
        snprintf(status, sizeof status, "%s",
                 tr(lang, "Connection failed · saved data", "Brak połączenia · zapisane dane"));
    else if (!time_valid(now) || !time_valid(m->fetched_at) || m->fetched_at > now + 300)
        snprintf(status, sizeof status, "%s",
                 tr(lang, "Age unknown · check time", "Wiek nieznany · sprawdź czas"));
    else if (st == HOME_STALE)
        snprintf(
            status, sizeof status, "%s",
            tr(lang, "Older data · waiting for update", "Starsze dane · czekają na aktualizację"));
    else {
        /* "Checked" is when we last asked the provider, not when the content last changed.
         * A conditional request usually comes back 304 Not Modified, which keeps fetched_at
         * where it was: counting from it made the line say "within the hour" right after a
         * fresh check and left the Refresh gesture with nothing to show (16.09). */
        int64_t checked = time_valid(m->checked_at) ? m->checked_at : m->fetched_at;
        int64_t hours = (now - checked) / 3600;
        if (now - checked < 120)
            snprintf(status, sizeof status, "%s", tr(lang, "Checked just now", "Sprawdzono teraz"));
        else if (hours < 1)
            snprintf(status, sizeof status, "%s",
                     tr(lang, "Checked within the hour", "Sprawdzono w ostatniej godzinie"));
        else if (hours < 48)
            snprintf(status, sizeof status,
                     tr(lang, "Checked %lld h ago", "Sprawdzono %lld godz. temu"), (long long)hours);
        else
            snprintf(status, sizeof status,
                     tr(lang, "Checked %lld days ago", "Sprawdzono %lld dni temu"),
                     (long long)(hours / 24));
    }
    rect(c, 14, 261, 372, 1, BLACK);
    txt(c, 14, 265, 215, 17, 0, source);
    txt(c, 231, 265, 155, 17, 0, date);
    txt(c, 14, 281, 372, 17, 0, status);
}
static void empty(canvas_t *c, const home_config_t *cfg, home_screen_t screen,
                  home_source_state_t st)
{
    int lang = lang_of(cfg);
    const char *title = screen == HOME_TODAY || screen == HOME_WEATHER
                            ? tr(lang, "A forecast for your place.", "Prognoza dla Twojego miejsca.")
                            : tr(lang, "Make this space yours.", "To miejsce jest dla Ciebie.");
    /* Sunset ramp paper -> yellow -> red (three pigments per point), in the user's brush. */
    c->raster = c->brush;
    for (int x = 280; x < 400; ++x) {
        float t = (x - 280) / 120.0f;
        for (int y = 43; y < 243; ++y) {
            pixel(c, x, y,
                  mix3(c, x, y, PAPER, YELLOW, RED, 1.0f - 0.8f * t, 0.8f * t * (1.0f - 0.5f * t),
                       0.4f * t * t));
            if ((x + y / 2) % 31 == 0)
                pixel(c, x, y, mix(c, x, y, YELLOW, RED, 0.4f));
        }
    }
    c->raster = RASTER_NOISE;
    txt(c, 14, 54, 268, 112, 3, title);
    const char *body =
        screen == HOME_TODAY || screen == HOME_WEATHER
            ? tr(lang,
                 "Open the phone panel and use your location. The first forecast will appear "
                 "here.",
                 "Otwórz panel w telefonie i użyj swojej lokalizacji. Pierwsza prognoza pojawi "
                 "się tutaj.")
            : tr(lang,
                 "Write a message in your phone panel. A reminder, a thought, something worth "
                 "keeping in view.",
                 "Wpisz wiadomość w panelu telefonu. Przypomnienie, myśl, coś, co warto mieć na "
                 "widoku.");
    txt(c, 14, 172, 260, 80, 1, body);
    rect(c, 14, 261, 372, 1, BLACK);
    txt(c, 14, 269, 372, 22, 1,
        st == HOME_ERROR ? tr(lang, "Cannot load data · check the phone panel",
                              "Nie można pobrać danych · sprawdź panel")
                         : "emini.ink/home");
}
static const char *condition(const home_weather_t *w, int lang)
{
    if (contains(w->symbol, sizeof w->symbol, "thunder"))
        return tr(lang, "Thunderstorms", "Burze");
    if (contains(w->symbol, sizeof w->symbol, "sleet"))
        return tr(lang, "Sleet", "Śnieg z deszczem");
    if (contains(w->symbol, sizeof w->symbol, "snow"))
        return tr(lang, "Snow", "Śnieg");
    if (contains(w->symbol, sizeof w->symbol, "rain"))
        return tr(lang, "Rain ahead", "Deszcz");
    if (contains(w->symbol, sizeof w->symbol, "fog"))
        return tr(lang, "Fog", "Mgła");
    if (contains(w->symbol, sizeof w->symbol, "clearsky"))
        return tr(lang, "Clear skies", "Pogodne niebo");
    if (contains(w->symbol, sizeof w->symbol, "fair"))
        return tr(lang, "Mostly clear", "Przeważnie pogodnie");
    if (contains(w->symbol, sizeof w->symbol, "partlycloudy"))
        return tr(lang, "Partly cloudy", "Częściowe zachmurzenie");
    if (contains(w->symbol, sizeof w->symbol, "cloudy"))
        return tr(lang, "Cloud cover", "Zachmurzenie");
    return tr(lang, "Weather forecast", "Prognoza pogody");
}
/* Large text sets the condition in 30 px only when no word is wider than its box, so
 * text() never splits a word or cuts it with an ellipsis; otherwise it stays 22 px. */
static int condition_font(const home_config_t *cfg, const char *s, int w)
{
    size_t n = strlen(s), at = 0;
    int word = 0;
    if (!cfg->large_text)
        return 2;
    while (at < n) {
        uint32_t ch = next_cp(s, n, &at);
        word = ch == ' ' ? 0 : word + glyph(3, ch)->advance;
        if (word > w)
            return 2;
    }
    return 3;
}
static double temp(double c, bool f)
{
    return f ? c * 1.8 + 32 : c;
}
/* A finite, clamped value: decimal comma in Polish, U+2212 minus, never "-0". */
static void number(char *out, size_t len, double v, int decimals, int lang)
{
    char digits[24];
    snprintf(digits, sizeof digits, "%.*f", decimals, fabs(v));
    bool zero = strspn(digits, "0.") == strlen(digits);
    char *dot = lang == LANG_PL ? strchr(digits, '.') : NULL;
    if (dot)
        *dot = ',';
    snprintf(out, len, "%s%s", v < 0 && !zero ? "−" : "", digits);
}
/* 24-hour "18:00" (an end at midnight reads "24:00") or 12-hour "6 PM"/"6:30 PM";
 * the AM/PM mark may be left to the other end of a range. */
static void clock_text(char *out, size_t len, const struct tm *tm, bool clock24, bool end,
                       bool mark)
{
    if (clock24) {
        snprintf(out, len, "%02d:%02d", end && !tm->tm_hour && !tm->tm_min ? 24 : tm->tm_hour,
                 tm->tm_min);
        return;
    }
    int hour = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
    const char *meridiem = !mark ? "" : tm->tm_hour < 12 ? " AM" : " PM";
    if (tm->tm_min)
        snprintf(out, len, "%d:%02d%s", hour, tm->tm_min, meridiem);
    else
        snprintf(out, len, "%d%s", hour, meridiem);
}
/* English names the precipitation after the current symbol, so the line never says
 * rain under "Snow or sleet"; Polish "Opady" covers every kind. Later hours have no
 * symbol of their own in the cache, so the current one stands in for them. */
static const char *precipitation(const home_weather_t *w, int lang)
{
    return lang                                               ? "Opady"
           : contains(w->symbol, sizeof w->symbol, "sleet") ? "Sleet"
           : contains(w->symbol, sizeof w->symbol, "snow")  ? "Snow"
                                                            : "Rain";
}
/* When the first rain of the parsed hourly window starts and stops. hourly_rain[k]
 * covers the hour from forecast_at + k h. The window ends at the first hour without
 * a rain amount, so nothing is claimed past the window or past missing data. */
static bool rain_outlook(char *out, size_t len, const home_config_t *cfg, const home_weather_t *w,
                         int64_t now)
{
    int lang = lang_of(cfg);
    const char *noun = precipitation(w, lang);
    int n = imin(w->hourly_count, 12), start = -1, stop = -1;
    for (int k = 0; k < n; ++k)
        if (!isfinite(w->hourly_rain[k]))
            n = k;
    /* Hours already over are skipped, so older data never names a time that has passed. */
    int64_t ago = time_valid(now) && now > w->forecast_at ? now - w->forecast_at : 0;
    int first = ago >= 12 * 3600 ? 12 : (int)(ago / 3600);
    if (n - first < 2 || !time_valid(w->forecast_at))
        return false;
    for (int k = first; k < n; ++k) {
        bool wet = w->hourly_rain[k] > 0.05; /* same threshold as rain_field() */
        if (wet && start < 0)
            start = k;
        else if (!wet && start >= 0 && stop < 0)
            stop = k;
    }
    struct tm from, until;
    if (!home_tz_localtime(cfg->timezone, w->forecast_at + (int64_t)imax(start, first) * 3600,
                           &from) ||
        !home_tz_localtime(cfg->timezone, w->forecast_at + (int64_t)(stop < 0 ? n : stop) * 3600,
                           &until))
        return false;
    char a[16], b[16];
    clock_text(a, sizeof a, &from, cfg->clock24, false,
               stop < 0 || (from.tm_hour < 12) != (until.tm_hour < 12));
    clock_text(b, sizeof b, &until, cfg->clock24, true, true);
    if (start < 0)
        snprintf(out, len, tr(lang, "Dry until %s", "Bez opadów do %s"), b);
    else if (start == first && stop < 0)
        snprintf(out, len, tr(lang, "%s for %d h", "%s przez %d h"), noun, n - first);
    else if (start == first)
        snprintf(out, len, tr(lang, "%s until %s", "%s do %s"), noun, b);
    else if (stop < 0)
        snprintf(out, len, tr(lang, "%s from %s", "%s od %s"), noun, a);
    else
        snprintf(out, len, "%s %s–%s", noun, a, b);
    return true;
}
/* Fallback: the precipitation amount of the current hour. */
static void rain_amount(char *out, size_t len, const home_weather_t *w, int lang)
{
    char mm[24];
    if (isfinite(w->precipitation)) {
        number(mm, sizeof mm, clamp(w->precipitation, 0, 999), 1, lang);
        snprintf(out, len, "%s %s mm", precipitation(w, lang), mm);
    } else
        snprintf(out, len, "%s —", precipitation(w, lang));
}
static void disc(canvas_t *c, int cx, int cy, int rx, int ry, const home_weather_t *w)
{
    float cloud = (float)clamp(w->cloud_cover / 100, 0, 1),
          warm = (float)clamp((w->temperature + 15) / 55, 0, 1);
    bool night = contains(w->symbol, sizeof w->symbol, "night");
    float inv_rx = 1.0f / rx, inv_ry = 1.0f / ry;
    /* Cloud edge and horizontal shade are constant down each column. */
    for (int x = imax(0, cx - rx); x < imin(W, cx + rx + 1); ++x) {
        float dx = (x - cx) * inv_rx;
        float shade = clampf((dx + 1.0f) * 0.35f + warm * 0.3f, 0.04f, 0.96f);
        float night_shade = 0.12f + 0.35f * (dx + 1.0f);
        float edge = 0.54f - cloud * 1.05f + 0.10f * sinf((x - cx) / 15.0f);
        float obscured_shade = cloud * 0.36f;
        for (int y = imax(34, cy - ry); y < imin(255, cy + ry + 1); ++y) {
            float dy = (y - cy) * inv_ry, r = dx * dx + dy * dy;
            if (r > 1.0f)
                continue;
            int p =
                night ? mix(c, x, y, PAPER, BLACK, night_shade) : mix(c, x, y, YELLOW, RED, shade);
            if (dx < -0.35f && r > 0.36f && ((int)(sqrtf(r) * 40.0f) % 7) == 0)
                p = night ? PAPER : YELLOW;
            if (dy > edge)
                p = mix(c, x, y, PAPER, BLACK, obscured_shade);
            pixel(c, x, y, p);
        }
    }
}
static void rain_field(canvas_t *c, const home_weather_t *w, int y0, int y1)
{
    /* The sky is drawn in m/s. */
    double rain = clamp(w->precipitation, 0, 20), wind = clamp(w->wind_speed / 3.6, 0, 40);
    int16_t wind_shift[H]; /* 600B, no heap; preserve exact integer rain positions. */
    for (int y = y0; y < y1; ++y)
        wind_shift[y] = (int16_t)(wind * y / 12);
    int rain_rows = (int)clamp(rain * 1.8, 1, 7);
    bool wet = rain > 0.05;
    float wave_scale = 1.0f / (46.0f + (float)wind * 2.0f);
    for (int x = 0; x < W; ++x) {
        int horizon = y0 + 5 + (int)(4.0f * sinf(x * wave_scale));
        float inv_depth = 1.0f / imax(1, y1 - horizon), warmth = (x / 399.0f) * 0.6f;
        for (int y = y0; y < y1; ++y) {
            if (y >= horizon) {
                float d = (y - horizon) * inv_depth;
                int p = mix(c, x, y, PAPER, YELLOW, d * 0.95f);
                if (p == YELLOW)
                    p = mix(c, x, y, YELLOW, RED, d * warmth);
                pixel(c, x, y, p);
            }
            if (wet && (x + wind_shift[y]) % 13 == 0 && (y - y0) % 9 < rain_rows)
                pixel(c, x, y, BLACK);
        }
    }
}
static bool graph_valid(const home_weather_t *w)
{
    int n = imin(w->hourly_count, 12);
    if (n < 2)
        return false;
    for (int k = 0; k < n; ++k)
        if (!isfinite(w->hourly_temperature[k]))
            return false;
    return true;
}
static void forecast_graph(canvas_t *c, const home_weather_t *w, int x, int y, int ww, int hh)
{
    int n = imin(w->hourly_count, 12);
    if (!graph_valid(w))
        return;
    double low = 1000, high = -1000;
    for (int k = 0; k < n; ++k) {
        double t = clamp(w->hourly_temperature[k], -100, 100);
        if (t < low)
            low = t;
        if (t > high)
            high = t;
    }
    if (high - low < 2) {
        low -= 1;
        high += 1;
    }
    int lastx = x, lasty = y + hh / 2;
    for (int xx = 0; xx < ww; ++xx) {
        double index = (double)xx * (n - 1) / (ww - 1);
        int k = imin((int)index, n - 2);
        double f = index - k;
        double value = clamp(w->hourly_temperature[k], -100, 100) * (1 - f) +
                       clamp(w->hourly_temperature[k + 1], -100, 100) * f;
        int py = y + hh - 1 - (int)((value - low) / (high - low) * (hh - 1));
        float shade_step = 0.78f / imax(1, y + hh - py);
        for (int yy = py; yy < y + hh; ++yy)
            pixel(c, x + xx, yy, mix(c, x + xx, yy, YELLOW, RED, (yy - py) * shade_step));
        if (xx)
            line(c, lastx, lasty, x + xx, py, BLACK);
        lastx = x + xx;
        lasty = py;
    }
    for (int k = 0; k < n; ++k) {
        int xx = x + k * (ww - 1) / (n - 1);
        int bars = (int)clamp(w->hourly_rain[k] * 3, 0, hh / 2);
        for (int yy = y + hh - bars; yy < y + hh; ++yy)
            for (int dx = -3; dx <= 3; ++dx)
                pixel(c, xx + dx, yy, mix(c, xx + dx, yy, PAPER, BLACK, 0.72f));
    }
}
static void weather(canvas_t *c, const home_config_t *cfg, const home_weather_t *w, int64_t now)
{
    int lang = lang_of(cfg);
    bool f = cfg->units[0] == 'F';
    int style = cfg->style[HOME_WEATHER] <= HOME_ATLAS ? cfg->style[HOME_WEATHER] : HOME_PRINT;
    char label[160], value[32], range[80], metrics[128], rain[64], wind[32], a[24], b[24];
    snprintf(label, sizeof label, "%.64s",
             cfg->location[0] ? cfg->location : tr(lang, "Weather", "Pogoda"));
    top(c, cfg, label);
    if (!w->meta.valid) {
        empty(c, cfg, HOME_WEATHER, w->meta.state);
        return;
    }
    if (!isfinite(w->temperature)) {
        empty(c, cfg, HOME_WEATHER, HOME_ERROR);
        return;
    }
    number(a, sizeof a, temp(clamp(w->temperature, -100, 100), f), 0, lang);
    snprintf(value, sizeof value, "%s°", a);
    if (isfinite(w->low) && isfinite(w->high)) {
        number(a, sizeof a, temp(clamp(w->low, -100, 100), f), 0, lang);
        number(b, sizeof b, temp(clamp(w->high, -100, 100), f), 0, lang);
        /* A spaced dash keeps "−7 – −2" apart from the minus signs. */
        bool negative = !strncmp(a, "−", 3) || !strncmp(b, "−", 3);
        snprintf(range, sizeof range, "%s%s%s °%s · %s", a, negative ? " – " : "–", b,
                 f ? "F" : "C", tr(lang, "24h", "24 h"));
    } else
        snprintf(range, sizeof range, "°%s · %s", f ? "F" : "C",
                 tr(lang, "No range", "Brak zakresu"));
    if (!rain_outlook(rain, sizeof rain, cfg, w, now))
        rain_amount(rain, sizeof rain, w, lang);
    if (isfinite(w->wind_speed)) {
        number(a, sizeof a, clamp(w->wind_speed, 0, 540), 0, lang);
        snprintf(wind, sizeof wind, tr(lang, "Wind %s km/h", "Wiatr %s km/h"), a);
    } else
        snprintf(wind, sizeof wind, "%s", tr(lang, "Wind —", "Wiatr —"));
    snprintf(metrics, sizeof metrics, "%s · %s", rain, wind);
    if (width(1, metrics, sizeof metrics) > 372) {
        rain_amount(rain, sizeof rain, w, lang);
        snprintf(metrics, sizeof metrics, "%s · %s", rain, wind);
    }
    int raster = c->brush;
    if (style == HOME_RHYTHM) {
        txt(c, 14, 40, 178, 19, 0, tr(lang, "FORECAST", "PROGNOZA"));
        txt(c, 12, 56, 174, 82, width(4, value, 32) > 174 ? 3 : 4, value);
        /* Two 30 px lines with descenders need 72 px: large text starts the
         * condition level with the FORECAST label, still above the range. */
        const char *sky = condition(w, lang);
        if (condition_font(cfg, sky, 191) == 3)
            txt(c, 195, 39, 191, 74, 3, sky);
        else
            txt(c, 195, 53, 191, 60, 2, sky);
        txt(c, 195, 113, 191, 23, 1, range);
        float cloud = (float)clamp(w->cloud_cover / 100, 0, 1);
        /* The cloud texture stops where the graph ends, above the label strip. */
        c->raster = raster;
        for (int x = 0; x < W; ++x) {
            float coverage = cloud * (x / 400.0f) * 0.5f;
            for (int y = 141; y < 216; ++y)
                pixel(c, x, y, mix(c, x, y, PAPER, YELLOW, coverage));
        }
        forecast_graph(c, w, 14, 146, 372, 70);
        c->raster = RASTER_NOISE;
        if (!graph_valid(w))
            rain_field(c, w, 146, 216);
        rect(c, 14, 219, 372, 18, PAPER);
        snprintf(label, sizeof label, tr(lang, "NEXT HOURS · °%s / mm", "KOLEJNE GODZINY · °%s / mm"),
                 f ? "F" : "C");
        txt(c, 14, 219, 372, 17, 0,
            graph_valid(w) ? label
                           : tr(lang, "Hourly detail unavailable", "Brak prognozy godzinowej"));
        txt(c, 14, 239, 372, 21, 1, metrics);
    } else if (style == HOME_ATLAS) {
        for (int y = 38; y < 250; ++y)
            for (int x = 0; x < 196; ++x)
                if ((x + y) % 17 < 2)
                    pixel(c, x, y, YELLOW);
        c->raster = raster;
        disc(c, 90, 132, 102, 99, w);
        c->raster = RASTER_NOISE;
        rain_field(c, w, 218, 256);
        rect(c, 196, 35, 204, 182, PAPER);
        txt(c, 207, 44, 180, 18, 0, tr(lang, "FORECAST", "PROGNOZA"));
        /* The range sits under the reading in the paper column, so the rain
         * field under the disc runs without a hole cut for a label. Two 30 px
         * condition lines need 72 px above the reading strip at y 210, so large
         * text lifts the reading and the range by 8 px. */
        const char *sky = condition(w, lang);
        int lift = condition_font(cfg, sky, 179) == 3 ? 8 : 0;
        txt(c, 203, 53 - lift, 183, 66, width(4, value, 32) > 183 ? 3 : 4, value);
        txt(c, 208, 121 - lift, 179, 22, 1, range);
        txt(c, 208, 145 - lift, 179, lift ? 72 : 60, lift ? 3 : 2, sky);
        // Weather quantities occupy a clean reading strip above the footer. It
        // meets the range box and the rain field's last row, so nothing shows through.
        rect(c, 194, 210, 206, 46, PAPER);
        /* The 178 px column takes a long 12-hour sentence at 12 px on the same
         * baseline; only a sentence too long for that falls back to the amount. */
        if (width(1, rain, sizeof rain) <= 178)
            txt(c, 208, 214, 178, 20, 1, rain);
        else if (width(0, rain, sizeof rain) <= 178)
            txt(c, 208, 218, 178, 17, 0, rain);
        else {
            rain_amount(rain, sizeof rain, w, lang);
            txt(c, 208, 214, 178, 20, 1, rain);
        }
        txt(c, 208, 236, 178, 20, 1, wind);
    } else {
        float cloud = (float)clamp(w->cloud_cover / 100, 0, 1);
        double wind = clamp(w->wind_speed / 3.6, 0, 50); /* the sky is drawn in m/s */
        c->raster = raster;
        for (int x = 184; x < W; ++x) {
            float coverage = (x - 184) / 216.0f * (0.12f + cloud * 0.4f);
            /* Keep exact engraved-line positions; only216 double sin calls
             * remain, instead of38016 per full Print weather scene. */
            int wave = (int)(wind * 2 * sin(x / 61.0));
            for (int y = 35; y < 211; ++y) {
                int p = mix(c, x, y, PAPER, YELLOW, coverage);
                if (((y + wave) % 13) == 0)
                    p = mix(c, x, y, PAPER, YELLOW, 0.72f);
                pixel(c, x, y, p);
            }
        }
        disc(c, 300, 119, 89, 78, w);
        c->raster = RASTER_NOISE;
        rain_field(c, w, 218, 233);
        txt(c, 14, 40, 172, 18, 0, tr(lang, "FORECAST", "PROGNOZA"));
        /* Range under the reading, condition below it: two lines of either
         * size still end above the rain field, which keeps its whole width. */
        /* Large text needs 74 px for two 30 px lines with descenders. */
        const char *sky = condition(w, lang);
        int lift = condition_font(cfg, sky, 171) == 3 ? 6 : 0;
        txt(c, 12, 56 - lift, 174, 66, width(4, value, 32) > 174 ? 3 : 4, value);
        txt(c, 14, 124 - lift, 170, 22, 1, range);
        txt(c, 14, 148 - lift, 171, 68 + lift, lift ? 3 : 2, sky);
        txt(c, 14, 236, 372, 23, 1, metrics);
    }
    source_footer(c, cfg, &w->meta, now, "Bureau of Meteorology", w->forecast_at);
}
static uint32_t fingerprint(const char *s, size_t cap)
{
    uint32_t hash = 2166136261u;
    size_t n = bounded(s, cap);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ (uint8_t)s[i]) * 16777619u;
    return hash;
}
/* A reproducible printed signature of this particular text, not a score or a
 * data chart. It changes when the story/message changes and exports exactly. */
typedef struct {
    int x, y, width, height;
} paper_window_t;
static void signature(canvas_t *c, const char *s, size_t cap, int style, int topy, int bottom,
                      const paper_window_t *paper)
{
    uint32_t hash = fingerprint(s, cap);
    int extent = bottom - topy;
    float phase = (hash % 97) / 15.0f, inv_height = 1.0f / imax(1, extent - 1);
    for (int y = topy; y < bottom; ++y) {
        float a = (y - topy) * inv_height, dy = y - topy + 38.0f;
        float row_phase = phase * (13.0f / 23.0f) + a * 5.0f;
        for (int x = 0; x < W; ++x) {
            // The caller immediately paints this rectangle opaque paper.
            // Skipping its texture is bit-exact and avoids hidden trig work.
            if (paper && x >= paper->x && x < paper->x + paper->width && y >= paper->y &&
                y < paper->y + paper->height)
                continue;
            float dx = x - 200.0f;
            float pattern = style == HOME_RHYTHM ? (0.5f + 0.5f * sinf(x / 23.0f + row_phase))
                            : style == HOME_ATLAS
                                ? (0.5f + 0.5f * cosf(sqrtf(dx * dx + dy * dy) / 13.0f + phase))
                                : (x / 399.0f);
            pixel(c, x, y, mix(c, x, y, YELLOW, RED, pattern * a * 0.88f));
            if (style == HOME_PRINT && ((x + (hash % 11)) % 21 == 0))
                pixel(c, x, y, mix(c, x, y, PAPER, YELLOW, 0.6f));
        }
    }
}
/* Poster-only layout. Weather, setup, source labels and the existing text()
 * path are unchanged. Measure and paint share exactly the same line breaker. */
static bool poster_layout(canvas_t *c, int x, int y, int w, int h, int fi, const char *s,
                          size_t cap, bool compact, int lang, int *height)
{
    size_t n = bounded(s, cap), at = 0;
    int row = 0, step = home_fonts[fi].size + 4, needed = 0;
    while (at < n) {
        while (at < n &&
               (s[at] == ' ' || (compact && (s[at] == '\n' || s[at] == '\r' || s[at] == '\t'))))
            ++at;
        if (at == n)
            break;
        if (row >= 24)
            return false;
        uint32_t cp[128];
        int count = 0, used = 0, space = -1, kept = -1;
        size_t space_at = 0, kept_at = 0;
        while (at < n && count < 128) {
            size_t before = at;
            uint32_t ch = next_cp(s, n, &at);
            if (ch == '\r')
                continue;
            if (ch == '\n' && !compact)
                break;
            if (ch == '\n' || ch == '\t')
                ch = ' ';
            if (compact && ch == ' ' && (!count || cp[count - 1] == ' '))
                continue;
            if (!has_glyph(fi, ch) && ch != ' ')
                return false; /* try the next size; CJK tables cover fewer sizes */
            const home_glyph_t *g = glyph(fi, ch);
            if (cjk(ch) && count && !cjk_no_start(ch) && !cjk_no_end(cp[count - 1])) {
                space = count;
                space_at = before;
            }
            if (used + g->advance > w - 8 || used + g->left + g->width > w - 6) {
                at = before;
                if (!count)
                    return false;
                if (space < 0) {
                    space = kept;
                    space_at = kept_at;
                }
                if (!compact && space >= 0) {
                    count = space;
                    at = space_at;
                }
                break;
            }
            cp[count++] = ch;
            used += g->advance;
            if (cjk(ch) && !cjk_no_end(ch)) {
                space = count;
                space_at = at;
            } else if (ch == ' ' && lang == LANG_PL && one_letter_word(cp, count - 1)) {
                kept = count - 1;
                kept_at = at;
            } else if (ch == ' ') {
                space = count - 1;
                space_at = at;
            }
        }
        while (count && cp[count - 1] == ' ')
            --count;
        int cursor = x + 6; /* includes the64px lowercase-j negative bearing */
        for (int i = 0; i < count; ++i) {
            const home_glyph_t *g = glyph(fi, cp[i]);
            int top = row * step + home_fonts[fi].size + g->top;
            int bottom = top + g->height;
            if (cursor + g->left < x || top < 0 || bottom > h)
                return false;
            needed = imax(needed, bottom);
            if (c)
                draw_glyph(c, cursor, y + row * step + home_fonts[fi].size, fi, cp[i], BLACK, x, y,
                           w, h);
            cursor += g->advance;
        }
        ++row;
        needed = imax(needed, row * step);
        if (needed > h)
            return false;
    }
    *height = needed;
    return true;
}
static void poster_text(canvas_t *c, int x, int y, int w, int h, const char *s, size_t cap,
                        bool large)
{
    const int candidates[] = {4, 5, 7, 3, 2, 1, 0, 6}; /* 64,48,44,30,22,16,12,10 */
    /* Each size is tried with the Polish one-letter rule first, then without it,
     * before a smaller size: a poster keeps its size rather than a perfect break. */
    for (int compact = 0; compact < 2; ++compact)
        for (unsigned i = large ? 0 : 1; i < sizeof(candidates) / sizeof(*candidates); ++i) {
            int height, fi = candidates[i];
            for (int rule = c->lang == LANG_PL; rule >= 0; --rule) {
                if (!poster_layout(NULL, x, y, w, h, fi, s, cap, compact != 0, rule != 0, &height))
                    continue;
                int offset = (h - height) / 2;
                (void)poster_layout(c, x, y + offset, w, h - offset, fi, s, cap, compact != 0,
                                    rule != 0, &height);
                return;
            }
        }
    /* Validated EN/PL title/note byte limits fit at10px with compact wrapping.
     * Keep an explicit fallback for out-of-contract calls, never an overrun. */
    text(c, x, y, w, h, 6, BLACK, s, cap);
}
/* ---- Bins. A bin is drawn from primitives (the Latin font has no symbols): a lid, a body that
 * narrows towards the bottom, two grooves and two wheels. The body takes the bin's ink; a white
 * bin is outline only. (x, y) is the top-left of a w x h box. */
static void bin_icon(canvas_t *c, int x, int y, int w, int h, int ink)
{
    int lid = imax(3, h / 9), wheel = imax(2, w / 9);
    int body_y = y + lid, body_h = h - lid - wheel;
    int narrow = imax(2, w * 4 / 52); /* per side, top to bottom */
    int edge = w >= 30 ? 2 : 1;
    int groove = ink == BLACK ? PAPER : BLACK;
    rect(c, x, y, w, lid, BLACK);
    for (int row = 0; row < body_h; ++row) {
        int dt = narrow * row / imax(body_h - 1, 1);
        int l = x + 2 + dt, r = x + w - 2 - dt;
        rect(c, l, body_y + row, r - l, 1, ink);
        rect(c, l, body_y + row, edge, 1, BLACK);
        rect(c, r - edge, body_y + row, edge, 1, BLACK);
        if (row >= body_h - edge)
            rect(c, l, body_y + row, r - l, 1, BLACK);
    }
    if (w >= 30)
        for (int g = 1; g <= 2; ++g) {
            int gx = x + w * g / 3 - 1 + (g == 1 ? narrow / 4 : -narrow / 4);
            rect(c, gx, body_y + 5, 2, body_h - 10, groove);
        }
    for (int g = 0; g < 2; ++g) {
        int cx = x + (g ? w - w / 4 : w / 4), cy = y + h - wheel;
        for (int yy = -wheel; yy <= wheel; ++yy)
            for (int xx = -wheel; xx <= wheel; ++xx)
                if (xx * xx + yy * yy <= wheel * wheel)
                    pixel(c, cx + xx, cy + yy, BLACK);
    }
}
static const char *bin_name(const home_bin_t *b, int lang)
{
    if (b->label[0])
        return b->label;
    switch (b->colour & 3) {
    case HOME_INK_BLACK:
        return "Black";
    case HOME_INK_WHITE:
        return "White";
    case HOME_INK_YELLOW:
        return "Yellow";
    default:
        return "Red";
    }
}
static void note(canvas_t *c, const home_config_t *cfg, int64_t now)
{
    int lang = lang_of(cfg);
    top(c, cfg, tr(lang, "YOUR NOTE", "TWOJA KARTKA"));
    if (!cfg->note[0]) {
        empty(c, cfg, HOME_NOTE, HOME_EMPTY);
        return;
    }
    int style = cfg->style[HOME_NOTE] <= HOME_ATLAS ? cfg->style[HOME_NOTE] : HOME_PRINT;
    if (style == HOME_PRINT) {
        signature(c, cfg->note, sizeof cfg->note, style, 36, 55, NULL);
        poster_text(c, 14, 68, 372, 178, cfg->note, sizeof cfg->note, cfg->large_text);
    } else if (style == HOME_RHYTHM) {
        signature(c, cfg->note, sizeof cfg->note, style, 208, 261, NULL);
        poster_text(c, 14, 52, 372, 150, cfg->note, sizeof cfg->note, cfg->large_text);
    } else {
        signature(c, cfg->note, sizeof cfg->note, style, 37, 260,
                  &(paper_window_t){14, 52, 372, 193});
        rect(c, 14, 52, 372, 193, PAPER);
        poster_text(c, 26, 61, 348, 176, cfg->note, sizeof cfg->note, cfg->large_text);
    }
    rect(c, 14, 267, 372, 1, BLACK);
    txt(c, 14, 277, 220, 19, 0, tr(lang, "Yours to keep in view.", "Warto mieć to na widoku."));
    txt(c, 270, 277, 116, 19, 0, "emini.ink/home");
    (void)now;
}
/* Card for a screen index outside weather/feed/note: the device name like every
 * other screen ("emini HOME" without one), a title, one line of help. */

static void status(canvas_t *c, const char *name, size_t cap, const char *title, const char *body)
{
    if (name && bounded(name, cap))
        text(c, 14, 8, 372, 22, 1, BLACK, name, cap);
    else
        txt(c, 14, 8, 372, 22, 1, "emini HOME");
    rect(c, 14, 35, 372, 1, BLACK);
    text(c, 14, 54, 372, 100, 3, BLACK, title ? title : "Home", 256);
    text(c, 14, 163, 372, 80, 1, BLACK, body ? body : "", 512);
    signature(c, title ? title : "Home", 256, HOME_PRINT, 251, 270, NULL);
    /* The panel is local; the web address is where help lives. */
    txt(c, 14, 279, 372, 18, 0, tr(c->lang, "Help · emini.ink/home", "Pomoc · emini.ink/home"));
}
/* ---- Kept from the Sky screen for the sunrise and sunset line on Today. ---- */
enum { SKY_DAY_S = 86400 };
/* UTC instant at which the local calendar day holding `now` begins. Found by
 * bisection on the local date rather than by subtracting an offset: a day that
 * starts with a spring-forward jump has no 00:00 local at all, and the search
 * still returns its first second. */
static long day_key(const char *zone, int64_t utc, bool *ok)
{
    struct tm tm;
    *ok = home_tz_localtime(zone, utc, &tm);
    return *ok ? (((long)tm.tm_year * 12 + tm.tm_mon) * 32L + tm.tm_mday) : 0;
}
static bool sky_midnight(const char *zone, int64_t now, int64_t *out)
{
    bool ok;
    long today = day_key(zone, now, &ok);
    if (!ok)
        return false;
    int64_t lo = now - 2 * SKY_DAY_S, hi = now;
    if (day_key(zone, lo, &ok) >= today || !ok)
        return false;
    while (hi - lo > 1) {
        int64_t mid = lo + (hi - lo) / 2;
        long key = day_key(zone, mid, &ok);
        if (!ok)
            return false;
        if (key >= today)
            hi = mid;
        else
            lo = mid;
    }
    *out = hi;
    return true;
}
/* An event time to the nearest minute, the resolution every almanac prints. */
static bool event_clock(char *out, size_t len, const home_config_t *cfg, int64_t t, bool mark)
{
    struct tm tm;
    if (t <= 0 || !home_tz_localtime(cfg->timezone, t + 30, &tm))
        return false;
    tm.tm_sec = 0;
    clock_text(out, len, &tm, cfg->clock24, false, mark);
    return true;
}
/* The largest of the offered fonts whose single line fits the width. */
/* A size the font cannot draw is not a candidate: the CJK tables exist at 30, 22, 16 and 12 px
 * only, so a Chinese word offered a 64 px slot would come out as rows of '?'. Whoever picks a
 * size has to ask whether that size can draw this text (0.5.1, the third language). */
static bool drawable(int fi, const char *s, size_t cap)
{
    size_t at = 0, n = bounded(s, cap);
    while (at < n) {
        uint32_t cp = next_cp(s, n, &at);
        if (cp != ' ' && !has_glyph(fi, cp))
            return false;
    }
    return true;
}
/* The last size of the list is the floor; if even that cannot draw the text, the smallest
 * size that can wins, so a fallback is always a readable one. */
static int fit_floor(const char *s, const int *order, int n)
{
    if (drawable(order[n - 1], s, 256))
        return order[n - 1];
    for (int fi = 0; fi < 8; ++fi)
        if (drawable(fi, s, 256))
            return fi;
    return order[n - 1];
}
static int fit_font(const char *s, int w, const int *order, int n)
{
    for (int i = 0; i < n; ++i)
        if (drawable(order[i], s, 256) && width(order[i], s, 256) <= w)
            return order[i];
    return fit_floor(s, order, n);
}
/* ---- Today. Weather icons are drawn from primitives (the Latin font has no symbols); an icon
 * fills an s x s box whose top-left is (x, y). ---- */
static void fdisc(canvas_t *c, int cx, int cy, int r, int p)
{
    for (int yy = -r; yy <= r; ++yy)
        for (int xx = -r; xx <= r; ++xx)
            if (xx * xx + yy * yy <= r * r)
                pixel(c, cx + xx, cy + yy, p);
}
static void stroke(canvas_t *c, int x0, int y0, int x1, int y1, int t, int p)
{
    for (int d = 0; d < t; ++d)
        line(c, x0 + d - t / 2, y0, x1 + d - t / 2, y1, p);
}
/* A yellow disc with a black ring and, from 28 px up, eight rays. */
static void sun_icon(canvas_t *c, int cx, int cy, int r, int s)
{
    int e = s >= 40 ? 2 : 1;
    if (s >= 28) {
        static const int8_t dir[8][2] = {{1, 0},  {1, 1},   {0, 1},  {-1, 1},
                                         {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        int a = r + imax(3, s / 14), b = r + imax(5, s / 6);
        for (int i = 0; i < 8; ++i) {
            float k = dir[i][0] && dir[i][1] ? 0.7071f : 1.0f;
            stroke(c, cx + (int)(dir[i][0] * k * a), cy + (int)(dir[i][1] * k * a),
                   cx + (int)(dir[i][0] * k * b), cy + (int)(dir[i][1] * k * b), e, BLACK);
        }
    }
    fdisc(c, cx, cy, r, BLACK);
    fdisc(c, cx, cy, r - e, YELLOW);
}
static void moon_icon(canvas_t *c, int cx, int cy, int r)
{
    fdisc(c, cx, cy, r, BLACK);
    fdisc(c, cx + r * 2 / 5, cy - r / 4, r * 4 / 5, PAPER);
}
/* Paper circles and a base with a black outline; dy lifts it (percent of s). */
static void cloud_icon(canvas_t *c, int x, int y, int s, int dy)
{
    static const int8_t blob[3][3] = {{34, 58, 18}, {54, 46, 24}, {74, 60, 16}};
    int e = s >= 40 ? 2 : 1, bx = x + 30 * s / 100, by = y + (60 + dy) * s / 100,
        bw = 46 * s / 100, bh = 16 * s / 100;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 3; ++i)
            fdisc(c, x + blob[i][0] * s / 100, y + (blob[i][1] + dy) * s / 100,
                  blob[i][2] * s / 100 + (pass ? 0 : e), pass ? PAPER : BLACK);
        if (pass)
            rect(c, bx, by, bw, bh, PAPER);
        else
            rect(c, bx - e, by - e, bw + 2 * e, bh + 2 * e, BLACK);
    }
}
static void weather_icon(canvas_t *c, int x, int y, int s, int sym)
{
#define PX(v) (x + (v) * s / 100)
#define PY(v) (y + (v) * s / 100)
    bool night = sym & HOME_SYMBOL_NIGHT;
    int k = sym & 0x7f, t = s >= 40 ? 2 : 1, drops = 0;
    switch (k) {
    case HOME_SYMBOL_PARTLY:
        if (night)
            moon_icon(c, PX(34), PY(34), s * 20 / 100);
        else
            sun_icon(c, PX(34), PY(34), s * 17 / 100, s);
        cloud_icon(c, x, y, s, 4);
        break;
    case HOME_SYMBOL_CLOUDY:
        cloud_icon(c, x, y, s, 8);
        break;
    case HOME_SYMBOL_FOG:
        for (int i = 0; i < 3; ++i)
            rect(c, PX(14 + (i & 1) * 6), PY(30 + 18 * i), 72 * s / 100, t + 1, BLACK);
        break;
    case HOME_SYMBOL_LIGHTRAIN:
    case HOME_SYMBOL_RAIN:
    case HOME_SYMBOL_HEAVYRAIN:
    case HOME_SYMBOL_SHOWERS:
    case HOME_SYMBOL_SLEET:
        if (k == HOME_SYMBOL_SHOWERS && !night)
            sun_icon(c, PX(30), PY(26), s * 14 / 100, s);
        cloud_icon(c, x, y, s, -10);
        drops = k == HOME_SYMBOL_LIGHTRAIN ? 2 : 3;
        for (int i = 0; i < drops; ++i) {
            int dx = (drops == 2 ? 44 : 36) + i * (drops == 2 ? 18 : 16);
            if (k == HOME_SYMBOL_SLEET && i == 1)
                rect(c, PX(dx), PY(80), t + 1, t + 1, BLACK);
            else
                stroke(c, PX(dx + 4), PY(74), PX(dx - 3), PY(92), t,
                       k == HOME_SYMBOL_HEAVYRAIN ? RED : BLACK);
        }
        break;
    case HOME_SYMBOL_SNOW:
        cloud_icon(c, x, y, s, -10);
        for (int i = 0; i < 4; ++i)
            rect(c, PX(34 + i * 12), PY(76 + (i & 1) * 12), t + 1, t + 1, BLACK);
        break;
    case HOME_SYMBOL_THUNDER:
        cloud_icon(c, x, y, s, -10);
        stroke(c, PX(58), PY(68), PX(44), PY(84), t + 1, RED);
        stroke(c, PX(44), PY(84), PX(58), PY(84), t + 1, RED);
        stroke(c, PX(58), PY(84), PX(46), PY(98), t + 1, RED);
        break;
    default: /* clear, fair, unknown */
        if (night)
            moon_icon(c, PX(50), PY(50), s * 30 / 100);
        else
            sun_icon(c, PX(50), PY(50), s * 26 / 100, s);
    }
#undef PX
#undef PY
}
static void centre(canvas_t *c, int x, int y, int w, int h, int fi, const char *s)
{
    int tw = width(fi, s, 96);
    txt(c, x + imax(0, (w - tw) / 2), y, imin(w, imax(tw, 1) + 2), h, fi, s);
}
static const char *const day_short[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
/* "WED 7 OCTOBER"; the evening says "TOMORROW · THU 8 OCT". */
static void date_line(char *out, size_t len, int32_t day, int lang, bool tomorrow)
{
    static const char *const month[12] = {"JANUARY",   "FEBRUARY", "MARCH",    "APRIL",
                                          "MAY",       "JUNE",     "JULY",     "AUGUST",
                                          "SEPTEMBER", "OCTOBER",  "NOVEMBER", "DECEMBER"};
    int y, m, d;
    home_civil_from_days(day, &y, &m, &d);
    const char *wd = day_short[home_weekday(day)];
    const char *name = month[m - 1];
    char abbr[16];
    if (tomorrow) {
        snprintf(abbr, sizeof abbr, "%.3s", name);
        name = abbr;
    }
    snprintf(out, len, "%s%s %d %s", tomorrow ? "TOMORROW · " : "", wd, d, name);
}
/* Sunrise or sunset: today's next event, or tomorrow's sunrise. Local midnight comes from
 * sky_midnight(); tomorrow's is searched from the middle of tomorrow, so a day with a clock
 * change in it still lands on the right date. */
static bool sun_line(char *out, size_t len, const home_config_t *cfg, int64_t now, bool tomorrow)
{
    int64_t mid;
    home_sky_t sk;
    if (!sky_midnight(cfg->timezone, now, &mid) ||
        (tomorrow && !sky_midnight(cfg->timezone, mid + 36 * 3600, &mid)) ||
        !home_sky_day(cfg->latitude, cfg->longitude, mid, &sk))
        return false;
    char clk[16];
    if (tomorrow || (sk.sunrise && now < sk.sunrise)) {
        if (!sk.sunrise || !event_clock(clk, sizeof clk, cfg, sk.sunrise, true))
            return false;
        snprintf(out, len, "%s %s", "Sunrise", clk);
        return true;
    }
    if (!sk.sunset || now >= sk.sunset || !event_clock(clk, sizeof clk, cfg, sk.sunset, true))
        return false;
    snprintf(out, len, "%s %s", "Sunset", clk);
    return true;
}
#define RAIN_HOUR_MM 4.0
/* The next six hours: time, temperature, a rain bar (red from RAIN_HOUR_MM). The strip starts at
 * the first hour not yet over, as rain_outlook() does. */
static void hour_strip(canvas_t *c, const home_config_t *cfg, const home_weather_t *w, int64_t now)
{
    if (!time_valid(w->forecast_at))
        return;
    bool f = cfg->units[0] == 'F';
    int64_t ago = time_valid(now) && now > w->forecast_at ? now - w->forecast_at : 0;
    int first = ago >= 24 * 3600 ? 24 : (int)((ago + 3599) / 3600);
    int cols = imin(6, imin(w->hourly_count, HOME_WEATHER_HOURS) - first);
    if (cols < 3)
        return;
    for (int i = 0; i < cols; ++i) {
        int k = first + i, x = 14 + i * 62;
        struct tm tm;
        char hh[16], v[24];
        if (!home_tz_localtime(cfg->timezone, w->forecast_at + (int64_t)k * 3600, &tm))
            continue;
        clock_text(hh, sizeof hh, &tm, cfg->clock24, false, true);
        centre(c, x, 170, 62, 14, 0, hh);
        if (isfinite(w->hourly_temperature[k])) {
            number(v, sizeof v, temp(clamp(w->hourly_temperature[k], -100, 100), f), 0,
                   lang_of(cfg));
            strcat(v, "°");
        } else
            snprintf(v, sizeof v, "–");
        centre(c, x, 186, 62, 24, 2, v);
        double mm = w->hourly_rain[k];
        if (isfinite(mm) && mm > 0.05) {
            int h = (int)clamp(mm / RAIN_HOUR_MM * 26 + 0.5, 3, 26);
            rect(c, x + 21, 238 - h, 20, h, mm >= RAIN_HOUR_MM ? RED : BLACK);
        }
    }
}
/* Tomorrow and after: a weekday, an icon, the rain and the range, in columns. */
static void day_columns(canvas_t *c, const home_config_t *cfg, const home_weather_t *w,
                        int32_t from, int count)
{
    bool f = cfg->units[0] == 'F';
    int lang = lang_of(cfg), cw = 372 / count, col = 0;
    for (int i = 0; i < w->day_count && col < count; ++i) {
        const home_day_t *d = &w->day[i];
        if (d->date < from || !isfinite(d->high) || !isfinite(d->low))
            continue;
        int x = 14 + col++ * cw;
        char a[24], b[24], v[64];
        centre(c, x, 170, cw, 20, 1, day_short[home_weekday(d->date)]);
        weather_icon(c, x + 6, 192, 28, d->symbol);
        if (isfinite(d->rain) && d->rain >= 0.5f) {
            number(a, sizeof a, d->rain, 0, lang);
            snprintf(v, sizeof v, "%s mm", a);
            txt(c, x + 38, 198, cw - 38, 16, 0, v);
            if (d->rain >= RAIN_HOUR_MM * 2.5f)
                rect(c, x + 38, 214, imin(cw - 42, 24), 3, RED);
        } else if (isfinite(d->rain))
            txt(c, x + 38, 198, cw - 38, 16, 0, "dry");
        number(a, sizeof a, temp(clamp(d->high, -100, 100), f), 0, lang);
        number(b, sizeof b, temp(clamp(d->low, -100, 100), f), 0, lang);
        snprintf(v, sizeof v, "%s° %s°", a, b);
        centre(c, x, 222, cw, 18, 1, v);
    }
}
/* One small line in the bottom band: "Next bins: Tue 13 · Red", an icon for each bin due. */
static void bins_line(canvas_t *c, const home_config_t *cfg, int32_t today_day, int lang, int y)
{
    unsigned mask;
    int32_t day = home_bins_next(cfg, today_day, &mask);
    if (day < 0)
        return;
    int d, x = 14, m, yy;
    home_civil_from_days(day, &yy, &m, &d);
    char head[48];
    snprintf(head, sizeof head, "%s %s %d%s", "Next bins:",
             day_short[home_weekday(day)], d,
             day == today_day ? " · today" : "");
    txt(c, x, y + 2, 190, 20, 1, head);
    x += width(1, head, 96) + 8;
    for (int i = 0; i < cfg->bin_count; ++i)
        if (mask >> i & 1) {
            bin_icon(c, x, y, 16, 22, cfg->bins[i].colour);
            x += 20;
        }
}
/* The day before a collection, in the reminder window: the bins take the hero area. */
static void bins_hero(canvas_t *c, const home_config_t *cfg, unsigned mask, int lang)
{
    const char *title = "Bins out tonight";
    int fi = width(3, title, 96) <= 242 ? 3 : 2, n = 0, cell = 80;
    centre(c, 14, 38, 242, 38, fi, title);
    for (int i = 0; i < cfg->bin_count; ++i)
        n += (mask >> i) & 1;
    int x = 14 + (242 - n * cell) / 2;
    for (int i = 0; i < cfg->bin_count; ++i) {
        if (!(mask >> i & 1))
            continue;
        bin_icon(c, x + (cell - 52) / 2, 78, 52, 70, cfg->bins[i].colour);
        centre(c, x, 148, cell, 18, 1, bin_name(&cfg->bins[i], lang));
        x += cell;
    }
}
static void today_footer(canvas_t *c, const home_config_t *cfg, const home_weather_t *w,
                         int64_t now)
{
    char line[96], clk[16];
    struct tm tm;
    home_source_state_t st = state_at(&w->meta, now);
    int64_t at = time_valid(w->meta.checked_at) ? w->meta.checked_at : w->meta.fetched_at;
    if (st == HOME_ERROR || st == HOME_STALE || !w->meta.valid)
        snprintf(line, sizeof line, "Bureau of Meteorology · %s",
                 "older data");
    else if (time_valid(at) && home_tz_localtime(cfg->timezone, at, &tm)) {
        clock_text(clk, sizeof clk, &tm, cfg->clock24, false, true);
        snprintf(line, sizeof line, "Bureau of Meteorology · %s %s",
                 "checked", clk);
    } else
        snprintf(line, sizeof line, "Bureau of Meteorology");
    txt(c, 14, 286, 372, 14, 6, line);
}
/* Today, in two layouts. By day: the date, the weather now, when it rains, the next six hours.
 * From evening_minute: tomorrow, and the days after it. In the bins reminder window the bins take
 * the hero and the weather moves down into the band. The alerts corner (top right) and the pushed
 * home values of docs/plan-0.7.md come later. */
static void today(canvas_t *c, const home_config_t *cfg, const home_weather_t *w, int64_t now)
{
    int lang = lang_of(cfg);
    bool f = cfg->units[0] == 'F';
    char buf[160], value[32], rain[64], a[24], b[24];
    struct tm lt;
    bool known = time_valid(now) && home_tz_localtime(cfg->timezone, now, &lt);
    int32_t day = known ? home_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) : 0;
    int minute = known ? lt.tm_hour * 60 + lt.tm_min : 0;
    unsigned due = 0;
    bool bins = known && cfg->bin_count,
         reminder = bins && home_bins_reminder(cfg, day, minute, &due),
         have = w->meta.valid && isfinite(w->temperature);
    const home_day_t *tom = NULL;
    if (known && minute >= cfg->evening_minute)
        for (int i = 0; i < w->day_count; ++i)
            if (w->day[i].date == day + 1 && isfinite(w->day[i].high) && isfinite(w->day[i].low))
                tom = &w->day[i];
    if (!have && !tom && !bins) {
        snprintf(buf, sizeof buf, "%.64s",
                 cfg->location[0] ? cfg->location : "Today");
        top(c, cfg, buf);
        empty(c, cfg, HOME_TODAY, w->meta.valid ? HOME_ERROR : w->meta.state);
        return;
    }
    if (known) {
        date_line(buf, sizeof buf, tom ? day + 1 : day, lang, tom != NULL);
        txt(c, 14, 6, 250, 28, fit_font(buf, 250, (int[]){2, 1}, 2), buf);
    }
    rect(c, 14, 34, 372, 1, BLACK);
    rain[0] = a[0] = 0;
    if (have) {
        number(a, sizeof a, temp(clamp(w->temperature, -100, 100), f), 0, lang);
        if (!rain_outlook(rain, sizeof rain, cfg, w, now))
            rain_amount(rain, sizeof rain, w, lang);
    }
    if (reminder)
        bins_hero(c, cfg, due, lang);
    else if (tom) {
        number(a, sizeof a, temp(clamp(tom->high, -100, 100), f), 0, lang);
        number(b, sizeof b, temp(clamp(tom->low, -100, 100), f), 0, lang);
        weather_icon(c, 14, 40, 64, tom->symbol);
        snprintf(value, sizeof value, "%s°", a);
        txt(c, 88, 40, 172, 70, width(4, value, 32) <= 130 ? 4 : 7, value);
        snprintf(value, sizeof value, "/ %s°", b);
        txt(c, 88 + imin(width(4, a, 24) + 40, 150), 76, 100, 36, 3, value);
        char mm[24], ms[24];
        number(mm, sizeof mm, clamp(tom->rain, 0, 999), tom->rain < 10 ? 1 : 0, lang);
        number(ms, sizeof ms, clamp(tom->wind, 0, 360), 0, lang);
        if (tom->rain < 0.05f)
            snprintf(buf, sizeof buf, "Dry · wind %s km/h", ms);
        else
            snprintf(buf, sizeof buf, "Rain %s mm · wind %s km/h", mm, ms);
        txt(c, 14, 114, 372, 24, 2, buf);
        if (sun_line(buf, sizeof buf, cfg, now, true))
            txt(c, 14, 142, 372, 20, 1, buf);
    } else if (have) {
        weather_icon(c, 14, 40, 64, home_symbol_code(w->symbol));
        snprintf(value, sizeof value, "%s°", a);
        txt(c, 88, 40, 172, 70, width(4, value, 32) <= 130 ? 4 : 7, value);
        const char *cond = condition(w, lang);
        txt(c, 14, 108, 372, 28, fit_font(cond, 372, (int[]){2, 1}, 2), cond);
        txt(c, 14, 138, 280, 24, fit_font(rain, 280, (int[]){2, 1}, 2), rain);
        if (sun_line(buf, sizeof buf, cfg, now, false))
            txt(c, 300, 142, 86, 20, 1, buf);
    }
    rect(c, 14, 166, 372, 1, BLACK);
    if (tom)
        day_columns(c, cfg, w, reminder ? day + 1 : day + 2, reminder ? 5 : 4);
    else if (have)
        hour_strip(c, cfg, w, now);
    rect(c, 14, 240, 372, 1, BLACK);
    if (reminder && have) { /* the weather takes the band the bins line uses */
        weather_icon(c, 14, 246, 28, home_symbol_code(w->symbol));
        snprintf(buf, sizeof buf, "%s° %.64s", a, condition(w, lang));
        txt(c, 50, 246, 336, 24, 2, buf);
        txt(c, 50, 267, 336, 20, 1, rain);
    } else if (bins && !reminder)
        bins_line(c, cfg, day, lang, 248);
    today_footer(c, cfg, w, now);
}
void home_render(const home_config_t *cfg, const home_data_t *data, home_screen_t screen,
                 int64_t now, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    if (!cfg || !data)
        return;
    canvas_t c = {frame, (cfg->texture == 2 || cfg->texture == 4) ? cfg->texture : 1,
                  imin(cfg->intensity, 2), lang_of(cfg), RASTER_NOISE,
                  cfg->brush <= RASTER_GRID ? cfg->brush : RASTER_NOISE};
    if ((screen == HOME_TODAY || screen == HOME_WEATHER) && !cfg->location_ready) {
        top(&c, cfg, screen == HOME_TODAY ? tr(c.lang, "Today", "Dziś") : tr(c.lang, "Weather", "Pogoda"));
        empty(&c, cfg, screen, HOME_EMPTY);
    } else if (screen == HOME_TODAY)
        today(&c, cfg, &data->weather, now);
    else if (screen == HOME_WEATHER)
        weather(&c, cfg, &data->weather, now);
    else if (screen == HOME_NOTE)
        note(&c, cfg, now);
    else {
        /* Status keeps its fixed texture and intensity, as home_render_status() does. */
        canvas_t card = {frame, 1, 2, c.lang, RASTER_NOISE, RASTER_NOISE};
        status(&card, cfg->name, sizeof cfg->name, tr(c.lang, "Choose a screen.", "Wybierz ekran."),
               tr(c.lang, "Open the panel on your phone and choose what Home shows.",
                  "Otwórz panel w telefonie i wybierz, co ma pokazywać Home."));
    }
}
/* ---- The "emini" card (0.6): what the device knows about itself. One screen you reach with
 * the button: battery with an estimate the device measured on itself, a few counters, a week of
 * battery, and a code that leads to the site. Colour carries meaning here too: the battery ramp
 * runs paper -> yellow -> red as it empties. */
static int battery_pigment(canvas_t *c, int x, int y, int percent)
{
    float t = 1.0f - (float)clamp(percent, 0, 100) / 100.0f; /* 0 full, 1 empty */
    return mix3(c, x, y, PAPER, YELLOW, RED, (1 - t) * (1 - t) * 1.2f, 2 * t * (1 - t) + 0.25f,
                t * t * 1.4f);
}
static void battery_bar(canvas_t *c, int x, int y, int w, int h, int percent, bool charging)
{
    rect(c, x, y, w, h, BLACK);
    rect(c, x + 2, y + 2, w - 4, h - 4, PAPER);
    rect(c, x + w, y + h / 3, 4, h / 3, BLACK); /* the cap of a battery */
    int fill = percent < 0 ? 0 : (w - 6) * clamp(percent, 0, 100) / 100;
    for (int yy = y + 3; yy < y + h - 3; ++yy)
        for (int xx = x + 3; xx < x + 3 + fill; ++xx)
            pixel(c, xx, yy, battery_pigment(c, xx, yy, percent));
    if (charging) /* a bolt in the empty part, drawn in the ink of the fill */
        for (int k = 0; k < 14; ++k) {
            int bx = x + w / 2 - 4 + (k < 7 ? k : 13 - k) / 2, by = y + 5 + k;
            rect(c, bx, by, 3, 1, BLACK);
        }
}
static void info_number(canvas_t *c, int x, int y, int w, const char *label, const char *value)
{
    txt(c, x, y, w, 15, 0, label);
    txt(c, x, y + 15, w, 26, 2, value);
}
static void info_week(canvas_t *c, const home_stats_t *s, int x, int y, int w, int h, int lang)
{
    txt(c, x, y, w, 15, 0, tr(lang, "BATTERY · LAST SEVEN DAYS", "BATERIA · OSTATNIE SIEDEM DNI"));
    int top = y + 18, hh = h - 22, pitch = w / HOME_BATTERY_DAYS;
    rect(c, x, top + hh, w, 1, BLACK);
    for (int k = 0; k < HOME_BATTERY_DAYS; ++k) {
        int day = s->battery_day[HOME_BATTERY_DAYS - 1 - k];
        int bx = x + k * pitch + 2, bw = (pitch - 6) & ~1;
        if (day < 0) {
            for (int yy = top + hh - 4; yy < top + hh; yy += 2)
                for (int xx = bx; xx < bx + bw; xx += 2)
                    pixel(c, xx, yy, BLACK);
            continue;
        }
        int bh = imax(4, (hh - 2) * clamp(day, 0, 100) / 100) & ~1;
        for (int yy = top + hh - bh; yy < top + hh; ++yy)
            for (int xx = bx; xx < bx + bw; ++xx)
                pixel(c, xx, yy, battery_pigment(c, xx, yy, day));
    }
    txt(c, x + w - 46, y, 46, 15, 0, tr(lang, "TODAY", "DZIŚ"));
}
/* Read a pigment back from the frame. Needed only by the wordmark, which is drawn first in full
 * colour and then punched through the dot raster. */
static int readpx(const canvas_t *c, int x, int y)
{
    if ((unsigned)x >= W || (unsigned)y >= H)
        return PAPER;
    unsigned i = (unsigned)y * 100u + (unsigned)x / 4u, shift = 6u - ((unsigned)x % 4u) * 2u;
    return (c->frame[i] >> shift) & 3u;
}
/* EMINI.INK in large dotted type behind the first face of the card. This card is what people
 * photograph and post, so the name has to survive a handheld photo. The letters are drawn in
 * solid yellow first, then punched through the same dot raster the brushes use - so the colour
 * is never finer than 2 px, the same rule every other screen keeps. */
static void wordmark(canvas_t *c, int x, int y, int w, int h)
{
    static const int order[3] = {5, 7, 3}; /* 48, 44, 30 px */
    const char *name = "EMINI.INK";
    int fi = order[2], tw = 0;
    for (int i = 0; i < 3; ++i) {
        int candidate = width(order[i], name, 16);
        if (candidate <= w) {
            fi = order[i];
            tw = candidate;
            break;
        }
    }
    if (!tw)
        tw = width(fi, name, 16);
    text(c, x + imax(0, (w - tw) / 2), y, w, h, fi, YELLOW, name, 16);
    int raster = c->raster;
    c->raster = RASTER_DOTS;
    for (int yy = imax(y, 0); yy < imin(y + h, H); ++yy)
        for (int xx = imax(x, 0); xx < imin(x + w, W); ++xx)
            if (readpx(c, xx, yy) == YELLOW)
                pixel(c, xx, yy, mix(c, xx, yy, PAPER, YELLOW, 0.66f));
    c->raster = raster;
}
static void info_battery_line(const home_stats_t *s, char *line, size_t cap, int lang)
{
    if (s->charging)
        snprintf(line, cap, "%s",
                 s->full ? tr(lang, "Charged", "Naładowana") : tr(lang, "Charging", "Ładuje się"));
    else if (s->estimate_hours >= 48)
        snprintf(line, cap, tr(lang, "About %d days", "Około %d dni"), s->estimate_hours / 24);
    else if (s->estimate_hours >= 0)
        snprintf(line, cap, tr(lang, "About %d h", "Około %d h"), s->estimate_hours);
    else
        snprintf(line, cap, "%s",
                 tr(lang, "Learning how long a charge lasts", "Uczy się, na jak długo starcza"));
}
static void info_footer(canvas_t *c, const home_config_t *cfg, const home_stats_t *s, int64_t now,
                        int lang)
{
    char line[96], a[32]; /* a Chinese 12-hour time takes 26 bytes */
    rect(c, 14, 253, 372, 1, BLACK);
    if (cfg->power_mode == HOME_POWER_BREATH)
        /* In Breath the press that shows this card also opens the phone panel (HOME_AWAKE_US in
         * home_wake.h): the one place on the device that says so. The days are on the front. */
        snprintf(line, sizeof line, "%s",
                 tr(lang, "Panel open for 5 minutes", "Panel otwarty przez 5 minut"));
    else if (s->first_start > 0 && time_valid(now)) {
        long long days = (now - s->first_start) / 86400;
        snprintf(line, sizeof line, tr(lang, "With you for %lld days", "Z Tobą od %lld dni"), days);
    } else
        snprintf(line, sizeof line, "emini Home");
    txt(c, 14, 257, 232, 17, 0, line);
    stamp(a, sizeof a, now, lang, cfg->clock24, cfg->timezone);
    txt(c, 250, 257, 136, 17, 0, a);
    snprintf(line, sizeof line, "%s · %s · emini.ink/home", HOME_VERSION_TEXT,
             s->address[0] ? s->address : "home.local");
    txt(c, 14, 273, 372, 17, 0, line);
}
/* First face: the battery, the wordmark and the QR code. Nothing that needs translating. */
static void info_front(canvas_t *c, const home_config_t *cfg, const home_stats_t *s, int64_t now)
{
    int lang = c->lang;
    char value[32], line[96];
    txt(c, 14, 40, 176, 15, 0, tr(lang, "BATTERY", "BATERIA"));
    if (s->percent >= 0)
        snprintf(value, sizeof value, "%d%%", s->percent);
    else
        snprintf(value, sizeof value, "—");
    txt(c, 12, 54, 178, 62, 4, value);
    info_battery_line(s, line, sizeof line, lang);
    txt(c, 14, 118, 176, 24, 1, line);
    battery_bar(c, 14, 146, 168, 26, s->percent, s->charging);
    snprintf(value, sizeof value, "%lu", (unsigned long)s->pictures);
    info_number(c, 200, 40, 88, tr(lang, "PICTURES", "OBRAZÓW"), value);
    if (s->first_start > 0 && time_valid(now))
        snprintf(value, sizeof value, "%lld", (long long)((now - s->first_start) / 86400));
    else
        snprintf(value, sizeof value, "—");
    info_number(c, 200, 96, 88, tr(lang, "DAYS HERE", "DNI TUTAJ"), value);
    home_qr_paint(c->frame, "https://emini.ink/home", 298, 40, 88, 88, NULL);
    txt(c, 298, 132, 88, 15, 0, tr(lang, "THE PROJECT", "PROJEKT"));
    wordmark(c, 14, 182, 372, 56);
    info_footer(c, cfg, s, now, lang);
}
/* Second face: everything the device knows about itself. For the curious, not for the photo. */
static void info_nerd(canvas_t *c, const home_config_t *cfg, const home_stats_t *s, int64_t now)
{
    int lang = c->lang;
    char value[32], line[96];
    /* A warm field under the counters: otherwise the right half is black text alone, and the
     * screen has four pigments. */
    for (int y = 32; y < 188; ++y) {
        float fade = 0.13f * (1.0f - (float)((y & ~1) - 32) / 156.0f);
        for (int x = 198; x < 390; ++x)
            pixel(c, x, y, mix(c, x, y, PAPER, YELLOW, fade));
    }
    txt(c, 14, 36, 176, 15, 0, tr(lang, "BATTERY", "BATERIA"));
    if (s->percent >= 0)
        snprintf(value, sizeof value, "%d%%", s->percent);
    else
        snprintf(value, sizeof value, "—");
    txt(c, 12, 50, 178, 48, 7, value);
    info_battery_line(s, line, sizeof line, lang);
    txt(c, 14, 102, 176, 24, 1, line);
    battery_bar(c, 14, 132, 168, 26, s->percent, s->charging);
    snprintf(value, sizeof value, "%lu", (unsigned long)s->pictures);
    info_number(c, 208, 36, 88, tr(lang, "PICTURES", "OBRAZÓW"), value);
    if (s->awake_hours >= 48) /* days only: "10 d 10 h" already overflows the 88 px cell */
        snprintf(value, sizeof value, tr(lang, "%lu d", "%lu d"),
                 (unsigned long)(s->awake_hours / 24));
    else
        snprintf(value, sizeof value, tr(lang, "%lu h", "%lu h"), (unsigned long)s->awake_hours);
    info_number(c, 298, 36, 88, tr(lang, "AWAKE", "CZUWA"), value);
    snprintf(value, sizeof value, "%lu", (unsigned long)s->fetches);
    info_number(c, 208, 90, 88, tr(lang, "DOWNLOADS", "POBRAŃ"), value);
    snprintf(value, sizeof value, "%lu.%lu s", (unsigned long)(s->refresh_ms / 1000),
             (unsigned long)((s->refresh_ms % 1000) / 100));
    info_number(c, 298, 90, 88, tr(lang, "PAPER TIME", "PAPIER"), value);
    if (s->wakes_per_hour)
        snprintf(value, sizeof value, "%lu", (unsigned long)s->wakes_per_hour);
    else
        snprintf(value, sizeof value, "—");
    info_number(c, 208, 144, 88, tr(lang, "WAKES / H", "WYBUDZEŃ"), value);
    if (s->sleep_percent >= 0)
        snprintf(value, sizeof value, "%d%%", s->sleep_percent);
    else
        snprintf(value, sizeof value, "—");
    info_number(c, 298, 144, 88, tr(lang, "MAY SLEEP", "MOŻE SPAĆ"), value);
    info_week(c, s, 14, 192, 372, 58, lang);
    info_footer(c, cfg, s, now, lang);
}
/* Which face is on screen - two dots in the top bar, like pages. No words, so nothing to
 * translate and no risk of a label not fitting its column. */
static void face_dots(canvas_t *c, int face)
{
    for (int i = 0; i < HOME_INFO_FACES; ++i) {
        int cx = 316 + i * 14, cy = 17;
        for (int y = -4; y <= 4; ++y)
            for (int x = -4; x <= 4; ++x)
                if (x * x + y * y <= (i == face ? 16 : 16))
                    pixel(c, cx + x, cy + y,
                          (i == face || x * x + y * y > 6) ? BLACK : PAPER);
    }
}
void home_render_info(const home_config_t *cfg, const home_stats_t *s, int64_t now, int face,
                      uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame || !cfg || !s)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, (cfg->texture == 2 || cfg->texture == 4) ? cfg->texture : 1,
                  imin(cfg->intensity, 2), lang_of(cfg), RASTER_NOISE,
                  cfg->brush <= RASTER_GRID ? cfg->brush : RASTER_NOISE};
    top(&c, cfg, "EMINI");
    face_dots(&c, face);
    if (face == HOME_INFO_NERD)
        info_nerd(&c, cfg, s, now);
    else
        info_front(&c, cfg, s, now);
}
void home_render_setup(const char *ssid, const char *password, const char *code,
                       const char *address, int lang, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, lang, RASTER_NOISE, RASTER_NOISE};
    char wifi_payload[256], password_line[96];
    bool bounded_inputs = ssid && password && code && address && bounded(ssid, 33) <= 32 &&
                          bounded(password, 64) <= 63 && bounded(code, 7) == 6 &&
                          bounded(address, 128) < 128;
    if (bounded_inputs) {
        snprintf(password_line, sizeof(password_line), "%s: %s", tr(lang, "Password", "Hasło"),
                 password);
        bool fit = width(0, ssid, 33) <= 372 &&
                   width(0, password_line, sizeof(password_line)) <= 372 &&
                   width(1, address, 128) <= 372;
        bool panel_url = !strncmp(address, "http://", 7) && !strpbrk(address, "?#@\r\n");
        if (fit && panel_url &&
            home_qr_wifi_text(ssid, password, wifi_payload, sizeof(wifi_payload)) &&
            home_qr_paint(frame, wifi_payload, 14, 48, 172, 136, NULL) &&
            home_qr_paint(frame, address, 214, 48, 172, 136, NULL)) {
            /* 26 px box: the 22 px font descends 25 rows below the box top. */
            txt(&c, 14, 3, 372, 26, 2, tr(lang, "Connect your phone.", "Połącz telefon."));
            txt(&c, 14, 29, 180, 17, 0, tr(lang, "1  JOIN WI-FI", "1  POŁĄCZ WI-FI"));
            txt(&c, 214, 29, 172, 17, 0, tr(lang, "2  OPEN HOME", "2  OTWÓRZ HOME"));
            // These values remain legible as a complete manual fallback.
            text(&c, 14, 184, 372, 17, 0, BLACK, ssid, 33);
            txt(&c, 14, 201, 372, 18, 0, password_line);
            text(&c, 14, 220, 372, 24, 1, BLACK, address, 128);
            txt(&c, 14, 253, 176, 21, 1, tr(lang, "3  Pairing code", "3  Kod parowania"));
            text(&c, 206, 246, 180, 37, 3, BLACK, code, 7);
            rect(&c, 14, 278, 372, 1, BLACK);
            txt(&c, 14, 282, 372, 17, 0, "emini.ink/home");
            // QR payload contains the private setup AP password; no token is
            // ever added to the panel URL. Frame access stays parent-private.
            memset(wifi_payload, 0, sizeof(wifi_payload));
            memset(password_line, 0, sizeof(password_line));
            return;
        }
    }
    // Unusual long fields or an encoding failure keep the complete established
    // manual setup instead of drawing a tiny/partial QR or hiding credentials.
    memset(frame, 0x55, HOME_FRAME_BYTES);
    memset(wifi_payload, 0, sizeof(wifi_payload));
    memset(password_line, 0, sizeof(password_line));
    txt(&c, 14, 7, 372, 26, 2, tr(lang, "Home, meet your phone.", "Home, poznaj swój telefon."));
    rect(&c, 14, 37, 372, 1, BLACK);
    txt(&c, 14, 42, 372, 18, 0,
        tr(lang, "1  JOIN THIS WI-FI NETWORK", "1  POŁĄCZ TELEFON Z TĄ SIECIĄ WI-FI"));
    text(&c, 14, 61, 372, 44, 1, BLACK, ssid ? ssid : "", 64);
    txt(&c, 14, 104, 372, 17, 0, tr(lang, "NETWORK PASSWORD", "HASŁO SIECI"));
    text(&c, 14, 122, 372, 60, 1, BLACK, password ? password : "", 128);
    txt(&c, 14, 185, 372, 18, 0,
        tr(lang, "2  OPEN THIS ADDRESS IN YOUR BROWSER", "2  OTWÓRZ TEN ADRES W PRZEGLĄDARCE"));
    text(&c, 14, 204, 372, 26, 2, BLACK, address ? address : "", 128);
    txt(&c, 14, 237, 160, 20, 1, tr(lang, "3  Pairing code", "3  Kod parowania"));
    text(&c, 202, 230, 184, 37, 3, BLACK, code ? code : "", 32);
    rect(&c, 14, 273, 372, 1, BLACK);
    txt(&c, 14, 279, 372, 18, 0, "emini.ink/home");
}
void home_render_status(const char *title, const char *body, int lang,
                        uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, lang, RASTER_NOISE, RASTER_NOISE};
    status(&c, NULL, 0, title, body);
}
#ifdef HOME_TESTCARD
/* Panel measurement cards. Compiled only with
 * -DHOME_TESTCARD=1, never part of a release image. Cell 1 px colour, cell 3
 * and blue noise are deliberate here, outside the 2 px colour rule, to measure
 * what the pigments do before the renderer picks its minimum colour cluster. */
static int card_threshold(int x, int y, int cell, bool noise)
{
    int cx = x / cell, cy = y / cell;
    return noise ? home_noise[(cy & 63) * 64 + (cx & 63)] : bayer[cy & 3][cx & 3] * 16 + 8;
}
static void card_patch(canvas_t *c, int x, int y, int w, int h, int a, int b, int cell, bool noise,
                       int percent)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(c, xx, yy, percent * 256 / 100 > card_threshold(xx, yy, cell, noise) ? b : a);
}
static bool card_hatch(int x, int y, int angle, int period, int width)
{
    double rad = angle * 3.14159265358979323846 / 180.0;
    double m = fmod(-x * sin(rad) + y * cos(rad), (double)period);
    if (m < 0)
        m += period;
    return m < width;
}
typedef struct {
    const char *label;
    int a, b, cell;
    bool noise;
} card_row_t;
/* Card A: 0..100 % in steps of 10 %, one pigment pair per row, cell 2 px. */
static void card_ramps(canvas_t *c)
{
    static const card_row_t rows[10] = {
        {"P/Y B", PAPER, YELLOW, 2, false}, {"P/Y N", PAPER, YELLOW, 2, true},
        {"P/R B", PAPER, RED, 2, false},    {"P/R N", PAPER, RED, 2, true},
        {"Y/R B", YELLOW, RED, 2, false},   {"Y/R N", YELLOW, RED, 2, true},
        {"K/Y B", BLACK, YELLOW, 2, false}, {"K/Y N", BLACK, YELLOW, 2, true},
        {"K/R B", BLACK, RED, 2, false},    {"K/R N", BLACK, RED, 2, true},
    };
    txt(c, 2, 0, 396, 16, 0, "A · RAMPY 0–100 % · B BAYER 4×4 · N SZUM · KOMÓRKA 2 PX");
    for (int r = 0; r < 10; ++r) {
        int y = 16 + r * 26;
        txt(c, 2, y + 5, 44, 16, 0, rows[r].label);
        for (int i = 0; i <= 10; ++i)
            card_patch(c, 46 + i * 32, y, 32, 24, rows[r].a, rows[r].b, rows[r].cell,
                       rows[r].noise, i * 10);
    }
    for (int i = 0; i <= 10; ++i) {
        char s[8];
        snprintf(s, sizeof s, "%d", i * 10);
        txt(c, 46 + i * 32 + 2, 280, 30, 16, 0, s);
    }
}
/* Card B: cells 1..4 px, each at 25/50/75 %, Bayer and blue noise. */
static void card_cells(canvas_t *c)
{
    static const card_row_t rows[9] = {
        {"P/Y B", PAPER, YELLOW, 0, false}, {"P/Y N", PAPER, YELLOW, 0, true},
        {"P/R B", PAPER, RED, 0, false},    {"P/R N", PAPER, RED, 0, true},
        {"K/Y B", BLACK, YELLOW, 0, false}, {"K/R B", BLACK, RED, 0, false},
        {"Y/R B", YELLOW, RED, 0, false},   {"P/K B", PAPER, BLACK, 0, false},
        {"P/K N", PAPER, BLACK, 0, true},
    };
    txt(c, 2, 0, 396, 16, 0, "B · KOMÓRKA 1·2·3·4 PX, W GRUPIE 25 · 50 · 75 %");
    for (int g = 0; g < 4; ++g) {
        char s[8];
        snprintf(s, sizeof s, "%d PX", g + 1);
        txt(c, 46 + g * 87 + 2, 14, 80, 16, 0, s);
    }
    for (int r = 0; r < 9; ++r) {
        int y = 28 + r * 26;
        txt(c, 2, y + 5, 44, 16, 0, rows[r].label);
        for (int g = 0; g < 4; ++g)
            for (int k = 0; k < 3; ++k)
                card_patch(c, 46 + g * 87 + k * 29, y, 29, 24, rows[r].a, rows[r].b, g + 1,
                           rows[r].noise, 25 * (k + 1));
    }
    txt(c, 2, 280, 396, 16, 0, "B BAYER · N SZUM NIEBIESKI 64×64 · P PAPIER, K CZERŃ");
}
/* Card C: hatching, isolated dots, text on colour, solid edges. */
static void card_structure(canvas_t *c)
{
    static const int angles[4] = {0, 15, 45, 75}, widths[3] = {1, 2, 3};
    static const struct {
        const char *label;
        int a, b;
    } bands[3] = {{"Y/P", PAPER, YELLOW}, {"R/P", PAPER, RED}, {"K/P", PAPER, BLACK}};
    txt(c, 2, 0, 396, 16, 0, "C · KRESKI OKRES 8 PX: KĄT 0·15·45·75, GRUBOŚĆ 1·2·3 PX");
    for (int b = 0; b < 3; ++b) {
        int y = 16 + b * 32;
        txt(c, 2, y + 8, 44, 16, 0, bands[b].label);
        for (int a = 0; a < 4; ++a)
            for (int w = 0; w < 3; ++w) {
                int x0 = 46 + (a * 3 + w) * 29;
                for (int yy = y; yy < y + 30; ++yy)
                    for (int xx = x0; xx < x0 + 28; ++xx)
                        pixel(c, xx, yy,
                              card_hatch(xx, yy, angles[a], 8, widths[w]) ? bands[b].b : bands[b].a);
            }
    }
    txt(c, 2, 112, 396, 16, 0, "KROPKI 1·2·3·4 PX: Y NA P · R NA P · Y NA K · R NA K · K NA P");
    static const struct {
        int a, b;
    } dots[5] = {{PAPER, YELLOW}, {PAPER, RED}, {BLACK, YELLOW}, {BLACK, RED}, {PAPER, BLACK}};
    for (int g = 0; g < 5; ++g)
        for (int s = 1; s <= 4; ++s) {
            int x0 = 46 + g * 68 + (s - 1) * 17;
            rect(c, x0, 128, 16, 30, dots[g].a);
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 2; ++i)
                    rect(c, x0 + 2 + i * 8, 130 + j * 9, s, s, dots[g].b);
        }
    rect(c, 14, 164, 186, 66, YELLOW);
    text(c, 18, 166, 178, 16, 0, BLACK, "Deszcz od 18:00 · 14–17 °C", 64);
    text(c, 18, 182, 178, 20, 1, BLACK, "Deszcz od 18:00 · 14°", 64);
    text(c, 18, 202, 178, 26, 2, BLACK, "Deszcz od 18:00", 64);
    rect(c, 206, 164, 180, 66, RED);
    text(c, 210, 166, 172, 16, 0, PAPER, "Deszcz od 18:00 · 14–17 °C", 64);
    text(c, 210, 182, 172, 20, 1, PAPER, "Deszcz od 18:00 · 14°", 64);
    text(c, 210, 202, 172, 26, 2, PAPER, "Deszcz od 18:00", 64);
    static const int fields[5] = {RED, YELLOW, BLACK, YELLOW, RED};
    for (int i = 0; i < 5; ++i)
        rect(c, 14 + i * 62, 236, 62, 40, fields[i]);
    rect(c, 324, 236, 62, 40, BLACK);
    rect(c, 325, 237, 60, 38, PAPER);
    txt(c, 2, 282, 396, 16, 0, "TEKST 12/16/22 PX NA KOLORZE · PEŁNE POLA I KRAWĘDZIE");
}
void home_render_testcard(int card, uint8_t frame[HOME_FRAME_BYTES])
{
    if (!frame)
        return;
    memset(frame, 0x55, HOME_FRAME_BYTES);
    canvas_t c = {frame, 1, 2, LANG_EN, RASTER_NOISE, RASTER_NOISE};
    if (card == 0)
        card_ramps(&c);
    else if (card == 1)
        card_cells(&c);
    else
        card_structure(&c);
}
#endif
