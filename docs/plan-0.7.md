# Plan: Inifuss 0.7 (formerly emini Home) — a fridge-first lineup

Working plan, not user documentation. Nothing here is built or verified on hardware yet. Line
numbers refer to commit 71d8131.

## Progress

Updated after every change. Device: the NOTE4C at `home-ff2e.local` (the old name until step 8).
Builds use ESP-IDF v6.0 from `~/esp/esp-idf-v6.0`. Nothing is committed yet. The 0.6.2 baseline
(`config`, `status`, `power`) is saved outside the repo in `~/code/emini-baseline/`.

| Step | State | Notes |
|---|---|---|
| 0 Baseline | Done, late | Saved after step 1 was flashed, so it comes from a unit already running the step 1 build. The config and power log were unchanged, so it still serves as the 0.6.2 reference. |
| 1 Weather parser and struct | **Done, flashed, checked** | Hourly arrays 24, `day[6]`, symbols, `zone` argument, cache keyed by zone. Host-tested with a synthetic forecast. On the unit: weather ready, 168 power-log hours kept. |
| 2 Lineup and schema 2 | **Done, flashed, checked** | See below. |
| 3 Bins | **Done, flashed, checked** (panel editor untried in a browser) | See below. |
| 4 Today layouts | **Flashed, evening seen in preview** | App-only flash, hash verified. Evening layout looked right in `/api/preview`; day layout, bins window, paper and -12 degrees not yet seen. See below. |
| 5 Alerts corner | Not started | `uv_level`/`pollen_level` were deleted with Air; recreate them in `home_alerts.c`. |
| 6 Push | Not started | |
| 7 Clean-up | Not started | Version still reads 0.6.2. |
| 8 Rename to Inifuss | Not started | |

**Step 2, what is in.**
- **Screens:** `HOME_TODAY/WEATHER/NOTE`, `HOME_SCREEN_COUNT 3`, `HOME_SCHEMA 2`.
- **Removed:** Feed (parser, `home_fetch_feed`, `home_feed_t`, `feed_url`, server and network blocks),
  Picture (`home_picture.*`, `/api/picture`), and the Sky and Air renderers. The Sky helpers `day_key`,
  `sky_midnight`, `event_clock`, `sky_disc`, `fit_font`, `fit_wrapped`, `drawable` stay (unused for now,
  so they give "defined but not used" warnings).
- **Config:** schema 1 records migrate in `home_config_decode()`. `alerts: {air}` is the only new
  block so far. `feed_url` and `air_main` are accepted and ignored. `alerts_air` replaced `air_main`.
- **Wake:** `home_wake.c` bits are 1 = weather, 4 = air.
- **Server:** `sources.weather.day_count` is in the status. A zone change clears cached weather;
  `alerts_air` off clears air. `/api/refresh` "all" is mask 5.
- **Panel and CLI:** screen list, schema-1 recipe import (`migrateLegacy`), Today editor with the
  UV and pollen checkbox, `convert`/`picture` removed, `enable()` refuses schema 1.
- **Checked on the unit:** the real 0.6.2 config migrated (Today first and on, Note keeps Atlas, fixed
  screen Today, 18:00 slot is Today); `day_count` is 6; all 168 power-log hours survived; the new
  panel code is served.

**Step 3, what is in.**
- **Module:** `home_bins.c/.h`, pure. Host-checked, and `core.js` `binsOn/binsNext` gives the same dates.
- **Config:** optional `bins` block (not a recipe key); `reference` must fall on `weekday`, `from` < `until`,
  at most 3 bins, `every` 1..4, `week` < `every`. Defaults: off, Tuesday, window 17:00-19:00.
- **Status/CLI:** `status.bins_next` (next 4, label or colour name); `home_cli.py bins`.
- **Render:** `bin_icon`, `bins_hero` ("Bins out tonight", 52x70 icons, labels), `bins_line` (bottom band,
  16x22 icons), in the skeleton `today()`. In the window the weather moves into the bottom band.
- **Main loop:** once a minute a key (local day, window open) sets `dirty` when it changes.
- **Panel:** bins editor in the Today editor (weekday, reference date, window, up to 3 rows, next 4 list), en/pl.
- **Checked on the unit (app-only flash, NVS untouched):** the 0.6.2-era settings survived; with the owner's
  schedule `bins_next` is 10-13 red, 10-20 red + yellow, 10-27 red, 11-03 red + yellow; line and hero both
  looked right on paper (title clipped at first, fixed); a window set 2 min ahead opened at 22:31 and closed at
  22:34 by itself (generation 3 -> 4 -> 5, no input); five bad `bins` bodies each got a 400 with a reason.
- **Left as it was set:** the owner's real schedule (Tuesday, red weekly, yellow fortnightly, 17:00-19:00),
  quiet hours on again. Backups of the configs before the tests are in `~/code/emini-baseline/`.
- **Open:** the panel editor has only been syntax-checked (`node --check`), not clicked through in a
  browser; no Chinese strings for the bins text yet (they fall back to English, step 7); icons not judged
  from 3 m; `/api/power` not compared with the baseline yet.

**Step 4, what is in (built only).**
- **Config:** `evening_minute` (default 18:00), JSON key `evening` (optional, not a recipe key); panel field
  "Show tomorrow from" in the Today editor (en/pl), `validate()` checks it.
- **Render:** `today()` now has the day layout (date, 64 px icon and temperature, condition, rain sentence,
  sunrise/sunset, 6-hour strip with rain bars red from 4 mm/h), the evening layout (tomorrow's icon, high/low,
  rain and wind, sunrise, 4 day columns) and the bins window in both (bins hero, weather in the bottom band;
  evening columns become 5). Icons from primitives: `weather_icon()` (sun, moon, cloud, rain, thunder, snow,
  sleet, fog). `bins_hero`/`bins_line` were re-laid out. No `top()` header on Today any more, except the empty card.
- **Main loop:** the minute key now includes "evening" and no longer needs bins to be on.
- **Removed:** `fit_wrapped` and `sky_disc` (unused).
- **Polish and Chinese:** the new Today and bins strings are English only (Polish/Chinese removed from the new
  render code and panel keys; older screens keep theirs).
- **Not done / to check:** day layout and paper not looked at (the preview was taken in the evening, 19:47); -12 degrees and long Polish strings untried; the
  alert corner (x 264+) is left blank for step 5, so the top-right of the date line is the only thing near it;
  rain threshold is a constant (`RAIN_HOUR_MM`) until step 5 adds the config; footer is one line now;
  Chinese strings fall back to English, `明天` untested in the 22 px CJK table.

**Weather source: Bureau of Meteorology instead of met.no (done after step 4).**
- `home_fetch_weather()` now fetches `api.weather.bom.gov.au/v1/locations/<geohash>/forecasts/hourly` and
  `/daily` (geohash of 6 characters computed on the device by `home_geohash()`); `home_parse_weather()` takes
  both bodies. BOM sends no ETag, so every poll is a full pair (about 40 KB + 8 KB before gzip).
- BOM icon descriptors map to the met.no symbol codes the renderer already uses (`bom_icons[]`); haze and dust
  show as fair. Rain is the middle of BOM's min..max range, wind is kept in km/h, as BOM sends it, everywhere (store, screens, thresholds); only the Weather screen's sky animation divides by 3.6. Cloud cover is guessed from the
  icon. Day low/high come from the daily forecast, with the hourly range where BOM has none (today after the
  maximum has passed).
- Footer now says "Bureau of Meteorology". Checked on the unit (Acacia Gardens, `r65245`): status ready,
  `day_count` 6, Today preview shows BOM values.
- **Caveats:** the API is undocumented and BOM's copyright line says it must not be used or shared, so this is
  for personal use only; it covers Australia only. `docs/PRIVACY.md`, README attribution and `HOME_UA` still
  say met.no or emini (step 7). The Weather screen and `rain_outlook()` are untested with BOM data.

**Open items from step 2.**
- The Today skeleton on paper has not been looked at.
- Importing a 0.6 recipe in the real panel is untried (only checked in node).
- Panel files are formatted at width 80, not 100; prettier was skipped to avoid whole-file rewrites.
- `fit_wrapped` is unused; drop it if step 4 does not need it.
- The stored config stays schema 1 until the next save, so a downgrade works until then.
- The Picture data in the `assets` partition is left untouched.

## Goal

A device on a fridge door, read in passing from 1–3 m by the household. It runs in **Open** power
mode (Wi-Fi always on). Breath must keep working, but gets no extra machinery: anything that
depends on pushed data degrades gracefully when the radio was off.

## Lineup

| Screen | Decision |
|---|---|
| **Today** (new, default) | One composition. Daytime layout; evening layout from `evening_minute` (18:00). |
| **Weather** | Kept with its three compositions; reads the extended weather struct. |
| **Note** | Kept. `signature()` and `poster_text()` stay. |
| News/Feed | Deleted: XML scanner `home_parse.c:433-1159`, `home_parse_feed*`, `home_fetch_feed()` (`home_fetch.c:1020-1058`), `feed()` (`home_render.c:1238-1264`), `home_feed_t`, `feed_url`, feed blocks in `home_network.c`/`home_server.c`, feed UI. |
| Sky | Screen deleted (`home_render.c:1721-2278`). `home_sky.c` and the helpers `day_key`, `sky_midnight`, `event_clock`, `sky_disc`, `fit_font`, `drawable` stay for sunrise/sunset. |
| Air | Screen deleted (`home_render.c:1294-1703`). `home_fetch_air()` + `home_parse_air.c` stay as an optional UV/pollen alert source (`alerts_air`); `uv_level`/`pollen_level` move to `home_alerts.c`. |
| Picture | Deleted: `home_picture.c/.h`, `/api/picture`, its branch in `home_render_locked()`, `home_picture_init()` in `app_main`, panel text, CLI `convert`/`picture`. Can return later from git history. |
| Calendar | Out of scope. |
| Countdowns | Later. |

```c
typedef enum { HOME_TODAY=0, HOME_WEATHER=1, HOME_NOTE=2 } home_screen_t;
#define HOME_SCREEN_COUNT 3
#define HOME_SCHEMA 2
```

Flash: the app is 3.81 MB in a 4.13 MB slot (~319 KB free). Deleting the feed parser, Sky, Air
and Picture should more than pay for the new code.

## Bins

Owner's schedule: collection on **Tuesday**; red every week, yellow every other week; yellow was
collected Tue 2026-10-06. So 10-13 red, 10-20 red + yellow, 10-27 red, 11-03 red + yellow.

**Config**

```c
#define HOME_BINS 3
enum { HOME_INK_BLACK=0, HOME_INK_WHITE=1, HOME_INK_YELLOW=2, HOME_INK_RED=3 };
typedef struct { uint8_t colour, every /* 1..4 weeks */, week /* 0..every-1 */; char label[17]; } home_bin_t;

uint8_t  bin_weekday;              /* collection day, Monday 0 (same bit order as `weekdays`) */
int32_t  bin_reference;            /* days since 1970-01-01; a collection day of week 0 */
uint16_t bins_from, bins_until;    /* reminder window on the day before collection; 1020, 1140 */
uint8_t  bin_count;                /* 0 = bins off */
home_bin_t bins[HOME_BINS];
```

Owner's values: weekday Tuesday, reference 2026-10-06, red `every 1`, yellow `every 2, week 0`,
window 17:00–19:00.

**Rule.** Bin *i* is collected on day D when `weekday(D) == bin_weekday` and
`floor_div(D - bin_reference, 7) mod every == week` (floor modulo, so dates before the reference
work too).

**Display.**
- **Reminder window** — on the day before a collection, from `bins_from` to `bins_until`: the bins
  take over Today's hero area. "Bins out tonight", one 52×70 icon per bin due.
- **Any other time** — one small line in the bottom band: "Next bins: Tue 13 · red", with 16×22
  icons.
- The window's start and end force a redraw (see Main loop), so it doesn't wait for the hourly
  one.

**Icon.**
- Body: a trapezoid narrowing from 52 to 44 px, filled with the bin's ink, 2 px black outline, two
  2 px black grooves.
- Lid: a black 8 px band, 4 px wider than the body.
- Wheels: black discs, r = 6.
- A white bin is paper with the outline only. Labels ("Red", or the user's label) in 16 px under
  each icon.

**Pure module `home_bins.c/.h`** (no I/O, host-testable):

```c
int32_t  home_days_from_civil(int y, int m, int d);
int      home_weekday(int32_t day);                          /* Monday 0 */
unsigned home_bins_on(const home_config_t *c, int32_t day);  /* bit i = bins[i] collected */
int32_t  home_bins_next(const home_config_t *c, int32_t from, unsigned *mask); /* ≤ 35 days, -1 */
bool     home_bins_reminder(const home_config_t *c, int32_t today, int minute, unsigned *mask);
```

No holiday shifts. The panel lists the next 4 collections, so you can check the schedule after
saving.

## Weather

Stay on api.met.no locationforecast **compact**.
- **Payload:** measured at 39.6 KB (~3 KB gzipped) for 89 steps: hourly for ~62 h, then 6-hourly
  to ~9 days.
- **Gusts:** compact has none. `complete` has gusts only inside the Nordic MEPS area, so wind
  alerts use sustained `wind_speed`.

**`home_weather_t`** (`home_types.h:69-76`) — existing fields unchanged; hourly arrays grow from 12
to 24; add:

```c
typedef enum { HOME_SYMBOL_UNKNOWN, CLEAR, FAIR, PARTLY, CLOUDY, FOG, LIGHTRAIN, RAIN, HEAVYRAIN,
               SHOWERS, THUNDER, SLEET, SNOW, HOME_SYMBOL_NIGHT = 0x80 } home_symbol_t;
#define HOME_WEATHER_HOURS 24
#define HOME_WEATHER_DAYS 6   /* today + 5 */
typedef struct {
    int32_t date;             /* local civil day; 0 = empty */
    float low, high, rain, wind;
    uint8_t symbol, samples;  /* samples < 4 = partial day */
} home_day_t;
float   hourly_wind[24];
uint8_t hourly_symbol[24];
home_day_t day[6];
uint8_t day_count;
```

Existing renderers cap at `imin(hourly_count, 12)`, so the Weather screen draws as before.

**`home_parse_weather()`** (`home_parse.c:329`) gains a `const char *zone` argument. Every
existing rule is kept. On top of them:
- **Hourly:** fill 24 slots (temperature, `next_1_hours` rain, wind, symbol).
- **Local day of an instant:** `local_day(at) = floor((at + offset) / 86400)`, taking `offset` from
  `home_tz_offset_at()` per instant, so DST nights come out right. An unknown zone falls back to
  UTC. The zone is only looked up, never set as the process TZ.
- **Day index:** `idx = local_day(at) - local_day(now)`, for 0 ≤ idx < 6.
- **Low, high, wind:** low/high from the instant temperatures of that day, wind as the highest
  sustained speed.
- **Rain:** the interval [at, next_at) goes to the day of `at`. A 1 h gap uses `next_1_hours`, a
  6 h gap `next_6_hours`; other gaps are skipped, so no block is counted twice.
- **Day symbol:** the `next_6_hours` symbol (falling back to `next_1_hours`) of the step closest to
  local noon.
- **`home_symbol_code(const char *)`:** maps met.no codes, in this order:
  - thunder → THUNDER
  - sleet → SLEET
  - snow → SNOW
  - showers → SHOWERS
  - heavyrain → HEAVYRAIN
  - lightrain → LIGHTRAIN
  - rain → RAIN
  - fog → FOG
  - partlycloudy → PARTLY
  - cloudy → CLOUDY
  - fair → FAIR
  - clearsky → CLEAR
  - `_night` suffix → NIGHT bit

Known limitation: days 3–5 come from about four instants each, so their low/high range is 1–2 °C
narrower than the real one.

**`home_fetch_weather()`** (`home_fetch.c:972`) passes `c->timezone`. URL unchanged.

## Alerts corner (top right of Today)

**Blank when all is fine** — the fixed spot teaches the household that an empty corner means
nothing needs doing. Otherwise up to 3 chips (122×28 at x 264, y 4/36/68) plus "+N":
- **warn:** red fill, paper 16 px text.
- **info:** yellow fill, 2 px black border, black text.
- **outline:** a paper chip with a black outline, used for old weather.

| Alert | Condition | Level |
|---|---|---|
| Rain | any hour ≥ `rain_hour` (4 mm/h) or 12 h sum ≥ `rain_sum` (10 mm); evening: tomorrow's sum | warn |
| Wind | highest sustained in the next 12 h (evening: tomorrow) ≥ `wind` (36 km/h) | warn |
| Home battery low | pushed percent ≤ `home_low_pct` (20), only while fresh | warn |
| Pushed lines | only while fresh; stale lines are dropped | as sent |
| UV | only with `alerts_air`; today's max ≥ 6 | info |
| Pollen | only with `alerts_air`; level ≥ high | info |
| Old weather | source in error, or > 6 h since the last good fetch | outline chip |

Config: `uint16_t rain_sum_x10, rain_hour_x10, wind_kmh; bool alerts_air;`.

Pure module `home_alerts.c/.h`:

```c
typedef struct { uint8_t kind, level; char text[25]; } home_alert_t;
int home_alerts(const home_config_t *, const home_data_t *, int64_t now, bool evening,
                home_alert_t out[3], int *more);
```

## Pushed home values

The owner's homeserver pushes to the device. The device holds no HA token and makes no plain-HTTP
fetch, and `home_secrets_t` is unchanged. The other options considered were the device pulling
HA's REST API with a token, or pulling a JSON file from the homeserver. Both are unnecessary in
Open mode and would need a plain-HTTP LAN path in `home_fetch.c`.

**Endpoint** `POST /api/home`, JSON, ≤ 8 KiB, parsed with `small_json()`:

```json
{ "battery": { "percent": 62.5, "kwh": 8.1, "ttl_min": 180 },
  "lines":   [ { "level": "warn", "text": "Car not charging" } ] }
```

- **Strict decode, like the config codec:** unknown keys → 400. Limits:
  - `percent` 0..100, `kwh` 0..1000, `ttl_min` 1..1440.
  - Up to 3 lines of ≤ 24 UTF-8 bytes each, checked with `home_utf8()`.
- **Section semantics:** a present section replaces the stored one; an absent one is kept; `null`
  clears it; `"lines": []` clears the lines.
- **Timestamps:** the device stamps the receipt time. Until the clock is valid it answers
  `503 clock_not_set`, and the sender retries.
- **Staleness:** a section is stale after `ttl_min`, or `home_stale_min` (180) when not given.
- **Response:** `202 {"accepted":true,"redraw":bool}`.
- **Read-back:** `GET /api/home`, and a `home` object in `GET /api/status`. Both show values,
  `received_at` and `stale`.
- **Trust:** the same as `PUT /api/config` (`origin_ok()`, LAN-trusted). No separate key; if one is
  wanted later, its hash fits the unused `token_hash[0]` slot without a layout change.
- **Breath:** `/api/home` is exempt from the 5-minute awake extension in `api_inner()`
  (`home_server.c:502-507`).
- **Redraw policy, so a chatty homeserver doesn't cost a 25 s refresh each time:** a push sets
  `dirty` only when one of these happens:
  - the lines changed;
  - the percent moved 10 points or more;
  - the percent crossed `home_low_pct`;
  - a section went from missing or stale to fresh.

  At most one push-triggered redraw every 10 min. Smaller changes appear with the next scheduled
  redraw.
- **Flash wear:** RAM is updated at once. `home_store_data()` runs at most every 15 min; a deferred
  store is picked up by the minute bookkeeping in `home_main.c`.
- **Degrading:**
  - Never pushed: the battery block is hidden and the bins line takes the full width.
  - Stale: an outline-only gauge with "· 3 h ago".
  - After 24 h: "—".

**Types**:

```c
#define HOME_PUSH_LINES 3
typedef struct { uint8_t level; char text[25]; } home_line_t;
typedef struct {
    int64_t battery_at, lines_at;            /* UTC received; 0 = never */
    uint16_t battery_ttl_min, lines_ttl_min; /* 0 = home_stale_min */
    float battery_percent, battery_kwh;      /* NAN when unknown */
    uint8_t line_count;
    home_line_t line[HOME_PUSH_LINES];
} home_home_t;
typedef struct { home_weather_t weather; home_air_t air; home_home_t home; } home_data_t;
```

Pure decoder `home_push.c`: `home_push_apply(const cJSON *, home_home_t *, int64_t now,
char err[64])`. If Breath ever matters, the same document could later be pulled from the LAN.

Config: `uint8_t home_low_pct; uint16_t home_stale_min;`.

## Today layouts (400×300)

Fonts (`home_fonts[]`): 0 = 12 px, 1 = 16, 2 = 22, 3 = 30, 4 = 64, 5 = 48, 6 = 10, 7 = 44. The
Latin slices have no arrows or symbols, so all icons are drawn from primitives.

**Daytime** (local minute < `evening_minute`):

```
y 0 ┌───────────────────────────────────────────────────┬──────────────────┐
  6 │ WED 7 OCTOBER                    22px             │ ALERT CORNER     │
 34 │───────────────────────────────────────────────────│ (blank when fine)│
 40 │ [icon 64×64]  17°  64px (44px if it doesn't fit)   │                  │
108 │               Partly cloudy  22px                  │                  │
138 │ Dry until 21:00 · rain 15:00   22px     ◒ 18:21 16px                 │
166 ├────────────────────────────────────────────────────────────────────────┤
170 │ 15:00  16:00  17:00  18:00  19:00  20:00     12px, 6 columns × 62 px  │
186 │  17°    16°    15°    13°    12°    11°      22px                     │
212 │  ▆▆     ▂▂                   ▆▆▆▆           rain bars, red ≥ threshold│
240 ├────────────────────────────────────────────────────────────────────────┤
248 │ ▯▯ Next bins: Tue 13 · red   16px     │ [▮▮▮▯] 62%  30px   8.1 kWh 16px│
288 │ MET Norway CC BY 4.0 · checked 14:05   10px                            │
300 └────────────────────────────────────────────────────────────────────────┘
```

**Evening** (from `evening_minute`, 18:00):

```
  6 │ TOMORROW · THU 8 OCT             22px             │ ALERT CORNER     │
 40 │ [icon 64]  16° 64px  / 9° 30px                     │ (tomorrow and    │
112 │ Rain 4 mm · wind 25 km/h           22px              │  overnight)      │
140 │ Sunrise 07:04                    16px                                 │
166 ├────────────────────────────────────────────────────────────────────────┤
170 │  FRI        SAT        SUN        MON       16px, 4 columns × 93 px  │
188 │ [icon 28] 4mm  …                            mm 12px, red ≥ threshold  │
218 │ 14° 7°     12° 6°     …                     16px                      │
240 ├────────────────────────────────────────────────────────────────────────┤
248 │ Next bins: Tue 13 · red + yellow     │ home battery, as daytime        │
```

**Bins reminder window** (Monday 17:00–19:00 by default):
- The hero area (14..256, 36..162) shows "Bins out tonight" in 30 px over the bin icons.
- The bottom-left band shows current weather instead of the bins line: a 24 px icon, "17° Partly
  cloudy" and "Dry until 21:00".
- From 18:00 the evening layout keeps the bins hero until the window ends. The day row then shows
  5 columns × 74 px, tomorrow to +4.

**Readability:**
- 64 px temperatures, 30 px titles and solid-ink bins are meant for 3 m.
- 22 px reads at about 2 m.
- 16 px and smaller are for someone at the fridge.

**Ink rules:**
- Yellow only as a fill, always with a 2 px black outline; never yellow text.
- Red as fills, chips, and strokes of 2 px or more.
- Text is black, or paper on red/black chips.

**Weather icons** (64, 28, 24 px; about 120 lines, reusing `sky_disc()`):
- Sun: a yellow disc with a 2 px black ring and 8 rays.
- Cloud: paper circles with a 2 px black outline.
- Rain: 2 px black slanted strokes, red for heavy rain.
- Thunder: a red bolt.
- Fog: three 2 px bars.
- Night: a black crescent.

**New statics in `home_render.c`:**
- `today()` chooses `today_day()` or `today_evening()`.
- Shared parts: `alert_corner()`, `bins_hero()`, `bins_line()`, `bin_icon()`, `weather_icon()`,
  `hour_strip()`, `day_columns()`, `home_battery()`.
- Wired into `home_render()` (2279) as `HOME_TODAY`.
- `empty()` (654) gets a Today card for when there is no place, no bins and no push.
- The strip starts at the first hour not yet passed, as `rain_outlook()` (797) does, and reuses
  that function's sentence.

## Settings migration

Config is stored as JSON (`home_store_config()` → `home_config_json()`), so screen enum values are
never persisted and migration lives entirely in `home_config_decode()` (`home_config.c:220-415`).

- **Schema:** accept 1 and 2 (line 259 forces 1 today).
- **Schema 1, `enabled[]`:** read it against the 0.6 canonical list `weather, feed, note, sky, air,
  picture` (3, 5 or 6 entries).
  - Drop feed, sky, air and picture from `enabled` and `order`; map the rest by name.
  - Insert Today first, enabled; append unseen screens disabled (existing loop at 319-327).
  - Note: `enabled[i]` is indexed by the writer's canonical screen index, not by order position.
- **Schema 2:** exactly `HOME_SCREEN_COUNT` entries.
- **Screen references** (`fixed_screen` 349-351, `day[i].screen` 399-401): weather, feed, sky, air
  and picture become **today**; note stays note. Today contains the weather, and the Weather screen
  stays enabled as before.
- **Styles:** old names accepted, feed/sky/air/picture ignored, `style[HOME_TODAY]` forced to
  Print.
- **Old keys:** `feed_url` and `air_main` are still accepted (in `normal[]` and `public_keys[]`)
  and ignored, so 0.6 records and recipes load.
- **New optional blocks**, defaults when absent; bins, alerts, home and evening are household facts,
  not a look, so they aren't recipe keys:

  ```json
  "evening": "18:00",
  "bins": {"weekday": 1, "reference": "2026-10-06", "from": "17:00", "until": "19:00",
           "list": [{"colour": "red", "every": 1, "week": 0, "label": ""},
                    {"colour": "yellow", "every": 2, "week": 0, "label": ""}]},
  "alerts": {"rain_mm": 10, "rain_mm_h": 4, "wind_kmh": 36, "air": false},
  "home": {"stale_min": 180, "low_pct": 20}
  ```

  `reference` must fall on `weekday`; `from` < `until`.
- **Encoder** (425-511): emits schema 2 and the new blocks; stops emitting `feed_url`/`air_main`.
- **Defaults** (69-110):
  - today, weather and note enabled; `fixed_screen` today; `day_screen` {today, note, today};
  - `evening_minute` 1080; bins off;
  - thresholds 10 mm / 4 mm/h / 36 km/h; `home_stale_min` 180; `home_low_pct` 20.
- **Readiness** (`home_auto_screen()`, 563-584): Today is ready when `location_ready || bin_count
  || home.battery_at`.
- **Data cache** (`home_store.c:16-20, 129-148, 190-208`):
  - `cache_t` becomes `{latitude, longitude, timezone[49], home_data_t}`.
  - The size change drops the 0.6 record once at line 137, which is fine for a cache; in Open mode
    weather is back within seconds.
  - Weather/air are restored only when lat/lon/zone match; `home` is always restored.
- **Untouched:** counters, the power log (NVS kinds 3/4, `stats*`/`power*`) and secrets.
- **Downgrade:** 0.6.x rejects schema 2, so settings reset. Wi-Fi, counters and the power log
  survive. Goes in the release notes.

## Main loop, wake, network

- **`home_wake.c` (16-34):** sources bit 1 = weather, 4 = air (feed bit 2 retired).
  - weather when `(enabled[TODAY] || enabled[WEATHER]) && location_ready`;
  - air when `enabled[TODAY] && alerts_air && location_ready`.
- **`home_network.c` `home_sources_task` (614-761):**
  - Delete the feed block (720-732), the `refresh_requested & 2U` lines (638-639) and the
    `editorial` hack.
  - The batch becomes `{weather, air}`; line 739 uses `alerts_air`.
- **`home_main.c`:**
  - `screen_has_data()` (132-152): Today in; Feed/Sky/Air/Picture out.
  - The staleness meta array (684-693) becomes `{weather, air}`; the deferred push store goes in
    the same block.
  - **Phase-change redraw:** the hourly `now % 3600 < 60` (696) misses 17:00–19:00 edges at
    arbitrary minutes and an 18:30 evening. Once a minute, compute a key from (evening?, in bins
    window?, local day) and set `dirty` when it changes. The frame-hash dedupe (768) makes this
    free on other screens.
  - Remove Picture from `home_render_locked()` and `app_main`.
- **`home_server.c`:**
  - Remove `feed_json()`, `sources.feed` (394), `"feed"` in `/api/refresh` (748-767; "all" becomes
    mask 5) and `/api/picture`.
  - Add `POST`/`GET /api/home`, the awake exemption, and in `/api/status`: `sources.weather.day_count`,
    `home`, and `bins_next` (next 4 collections, `[{"date":"2026-10-13","bins":["red"]},…]`).
  - `PUT /api/config` (586-600) and `preview()` (455-465) also clear weather when the zone changes,
    and air when `alerts_air` turns off.

## Panel UI (`firmware/ui/`)

**`core.js`:**
- `screens = ["today","weather","note"]`.
- `recipeKeys` loses `air_main`.
- `migrateLegacy(c)` mirrors the C mapping for schema-1 recipes.
- `validate()` checks the new blocks.
- `sourcesKey()` = weather, air plus `home.received_at`.
- `profile("showcase")` enables today + weather.
- A pure `binsOn(config, date)` mirror gives live feedback on a draft.

**`device.js`:**
- **Delete:**
  - `safeStoryURL`, `updateSelectedStory`;
  - the feed/sky/air/picture branches of `editor()`, `sourceLabel()`, `styleReady()` and
    `sourceBlock()`;
  - their icons and en/pl strings.
- **Add a Today editor:**
  - **Evening from** (time).
  - **Bins:**
    - collection weekday; reference date (warns if not on that weekday); reminder window
      from/until;
    - up to 3 rows: colour, frequency (weekly / fortnightly this week / fortnightly other week)
      and an optional label;
    - the next 4 collections.
  - **Alerts:** the three thresholds; "UV & pollen from Open-Meteo (sends your place)".
  - **Home values:**
    - stale-after and low-% settings;
    - a read-only card with the last push ("received 12 min ago" / stale);
    - a copyable `curl` and Home Assistant `rest_command` example.

Format changed lines only, with prettier at width 100.

## `tools/home_cli.py`

- `SCREENS = ["today","weather","note"]`; remove `convert` and `picture`.
- `enable()` refuses clearly against 0.6 firmware.
- **`push`** — reference client for the homeserver; POSTs `/api/home` with `X-Home-Auto: 1` and
  prints `redraw`:

  ```
  push --battery-percent 62 --battery-kwh 8.1 [--ttl 180] [--line warn:"Car not charging"]… [--clear-lines]
  ```
- **`bins`** prints `status.bins_next`.
- **`home`** prints `GET /api/home`.

## Rename: emini Home → Inifuss

Named after the Scroll of Inifuss (Diablo II): the old scroll that tells you where to go next.
This renames the product; the `home_*` code prefix, the NVS namespace and keys stay as they are,
so a renamed build still reads 0.6 settings, counters and the power log.

| Where | Now | After |
|---|---|---|
| Project / binary (`firmware/CMakeLists.txt:5`) | `emini_home_g3` → `emini_home_g3.bin` | `inifuss` → `inifuss.bin`; release asset `inifuss-<ver>-note4c.bin` |
| DHCP hostname seen by the router (`home_network.c:370`) | `emini-home` | `inifuss` |
| Setup access point name (`home_main.c:892`) | `emini.ink` | `Inifuss` |
| Panel address / mDNS hostname (`home_discovery.c`, `home_main.c:926-933`) | `home-xxxx.local`, taken from `secrets.ap_ssid` | `inifuss.local` |
| mDNS `_http._tcp` instance name | NULL (= hostname) | `Inifuss Home-XXXX`, from `secrets.ap_ssid`, so units stay distinguishable in a service browser |
| User agent (`home_fetch.c:30`) | `emini-home/0.6 (+…fiedoruk/emini-home)` | `inifuss/0.7 (+<this repo's URL>)` — met.no requires a contactable UA |
| Screen wordmark (`home_render.c:565, 1709`) | `emini HOME` | `INIFUSS` |
| Help lines and QR (`home_render.c:128, 712, 1288, 1715, 2445, 2470, 2584, 2610`) | `emini.ink/home` | this repo's URL (shortened), or dropped where space is tight |
| The "emini" card (`home_render.c:2326-2470`, comments in `home_main.c`, `home_types.h`, `home_store.c`, `home_config.c`) | "emini" card | "Inifuss" card |
| Boot log (`home_main.c:948`) | `BOOT emini_home_g3` | `BOOT inifuss` |
| Panel (`firmware/ui/index.html`, `core.js`, `device.js`) | title, brand link, "e." mark, `emini-home-recipe.json` | Inifuss title/mark; recipe file `inifuss-recipe.json` (import still accepts old files, which have no name check) |
| CLI (`tools/home_cli.py`) | docstring, `~/.config/emini-home/<host>.token` | new path `~/.config/inifuss/`, reading the old path as a fallback |
| `tools/preflight.py`, `tools/compare_image.py` | emini file names in help/examples | Inifuss names |
| Docs (README, CONTRIBUTING, SECURITY, SUPPORT, `docs/*`, `.github/ISSUE_TEMPLATE/*`, CLAUDE.md) | emini Home, emini.ink links | Inifuss; links to this repo |

**Keep:**
- **Attribution:** in README, LICENSE and THIRD_PARTY_NOTICES, say plainly that Inifuss is a fork of
  emini Home (fiedoruk/emini-home), and keep the original copyright lines.
- **Release history:** keep the emini release history as it is.

**Upgrade effect:**
- **Setup network:** the setup access point gets a new name.
- **Router:** the router lists the device as `inifuss`.
- **Panel address:** the panel moves to `http://inifuss.local`. `home-xxxx.local` stops answering, so
  bookmarks and anything scripted against the old name (the homeserver's push target,
  `$EMINI_HOST`) need updating; the IP address is unchanged.
- **Settings:** unaffected.

**mDNS changes:**
- **`home_discovery_start()`:**
  - Takes a fixed hostname `inifuss` instead of normalising `home-xxxx`.
  - `normalized_name()` is replaced by a check that the per-unit ID matches `Home-XXXX`, used only
    for the instance name.
- **`home_runtime.hostname`:** becomes `inifuss.local`. `origin_ok()` (`home_server.c:86-114`) keeps
  accepting exactly that name, the IP and 192.168.4.1.
- **`secrets.ap_ssid` keeps its value and layout:** it is still the per-unit ID, so the secrets
  record and Wi-Fi survive.
- **Two units on one network:** ESP-IDF's mDNS probes, and on a conflict renames the second unit
  (e.g. `inifuss-2.local`). Store the name actually registered in `home_runtime.hostname` (read it
  back with `mdns_hostname_get()`), so the panel, the card and `origin_ok()` all show and accept
  the real one. Check on hardware with a second mDNS responder claiming `inifuss`.
- **Docs:** `docs/PANEL.md:36`, `docs/PRIVACY.md:81`, `docs/HARDWARE.md:104` and `tools/home_cli.py`
  (`$EMINI_HOST` → `$INIFUSS_HOST`, still reading the old variable) are updated.

"Diablo" and "Inifuss" belong to Blizzard. For a personal, non-commercial fork that's low risk; if
it is ever distributed widely, keep the name out of any logo or store listing.

Do this as its own step (step 8), after the feature work, so feature diffs stay readable.

## Build order

Each step builds on its own and is checked on the NOTE4C before the next.

0. **Baseline.** Save `/api/config`, `/api/status` and `/api/power` from the 0.6.2 unit.
1. **Weather parser and struct.** Hourly 24, days 6, symbols, zone argument, cache key.
   - The Weather screen must look identical.
   - The status shows `day_count`.
   - The power-log week survives the flash.
2. **Lineup and schema-2 migration.** New enum; Feed and Picture removed; Sky/Air rendering removed
   (Air fetch gated by `alerts_air`); skeleton Today; panel screen list; CLI; `home_wake.c` bits.
   - Flash over 0.6.2 with real settings: name, note, place, quiet hours, day rhythm, units,
     language and Wi-Fi survive; Today first and enabled; counters and power log intact.
   - Import a 0.6 recipe.
3. **Bins.** Module, config, `status.bins_next`, CLI `bins`, panel editor, line + hero, icons,
   phase-change redraw.
   - `bins_next` gives 10-13 red, 10-20 red + yellow.
   - Move the window to now + 2 min: it appears and disappears by itself.
   - Icons read from 3 m.
4. **Full Today and Evening layouts.** Hour strip, day columns, icons, sunrise/sunset.
   - Check `/api/preview?screen=today`, then on paper.
   - Try −12° and long Polish strings.
5. **Alerts corner.** Module, config, panel fields, UV/pollen.
   - Force chips with tiny thresholds.
   - The corner stays empty on a calm day with real thresholds.
6. **Push.** `/api/home`, battery block, pushed lines, rate limits, CLI `push`.
   - 62% → 61% → no redraw; 15% → redraw with a red gauge and chip.
   - With a 2 min TTL it goes stale and the lines vanish.
   - The value survives a reboot with its age.
   - Bad bodies get 400.
   - Compare `/api/power` with the baseline.
7. **Clean-up.** README privacy section and `docs/PRIVACY.md` (feed gone, Open-Meteo optional, push
   endpoint); release notes (downgrade reset); Chinese strings; 0.7.0 (`HOME_VERSION_TEXT`,
   `HOME_UA`); clang-format/prettier on touched lines.
8. **Rename to Inifuss** (section above).
   - Check that the binary is `inifuss.bin`, the setup network is called Inifuss, the router shows
     `inifuss`, `http://inifuss.local` opens the panel (and saving works there), and settings,
     counters and the power log survive the flash.
   - `grep -ri emini` should leave only attribution and history.

Optional: `home_bins.c`, `home_alerts.c`, `home_push.c` and `home_parse_weather()` (with a saved
met.no JSON) compile on a host with `cc` + cJSON. A small check script would catch date and
bucketing mistakes before flashing; the repo has no harness for this today.

## Risks

- **Migration bugs** fall back to defaults. Test against real 0.6.0/0.6.2 records and a 3-screen
  record, and watch the canonical-index subtlety in `enabled[]`.
- **Downgrade** to 0.6.x resets settings.
- **Redraw and battery cost** from the hourly strip, the phase-change key and pushes. Check with
  `/api/power`.
- **Bins schedule has no holiday shifts**; the panel's next-4 list is the safeguard.
- **Wind alerts use sustained wind**, not gusts.
- **Push is LAN-trusted**: any host on the LAN can set values. Low stakes, stated plainly.
- **Pushes in Breath** are best effort only.
