# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Inifuss is ESP32-S3 firmware for the ZECTRIX NOTE4C, a four-colour (black/white/red/yellow)
e-paper devkit. It draws a daily poster (weather, one headline, a personal note, sun/moon, air
quality) and is configured entirely from a phone browser panel served by the device itself — no
app, no account, no server of ours in the loop after setup. Tested and released against exactly
one physical unit; see the README "Status" section before assuming behaviour is verified broadly.

## Build

```sh
cd firmware
export IDF_COMPONENT_MANAGER=0   # keeps the build offline, using only components already vendored here
idf.py -DIDF_TARGET=esp32s3 reconfigure
idf.py build
```

Requires ESP-IDF **v6.0** exactly (tag `v6.0`, commit `662a3be354759d9487bf4b1a629fadb766cb1800`),
activated in the shell. Nothing else needs installing — fonts, the noise mask and the time-zone
table are pre-generated into `firmware/main/generated/`.

Outputs: `build/inifuss.bin` (app, flashed at `0x20000`) and
`build/partition_table/partition-table.bin` (flashed at `0x8000`). The phone panel
(`firmware/ui/*`) is embedded into the binary via `EMBED_FILES` in `firmware/main/CMakeLists.txt`;
edit the UI source there, not a built copy.

Two debug-only build flags, never used in a release:
- `idf.py -DHOME_PM_PROFILE=1` — logs power-management locks once a minute, lets the device sleep
  on a cable so sleep can be watched over serial.
- `idf.py -DHOME_TESTCARD=1` — shows fixed test cards on boot and stops before touching the
  network, for measuring the panel/dither on real hardware.

To compare a local build against a release binary (accounting for the embedded build timestamp):
```sh
python3 tools/compare_image.py firmware/build/inifuss.bin inifuss-<ver>-note4c.bin
```

## No test suite

There is no automated test runner in this repository, and no on-device test framework —
comments in `home_main.c`/`home_runtime.h` refer to a "host simulation" driving `home_loop_begin`/
`home_loop_step` with fake time/keys, but that harness is not part of this repo. Verification is
manual, on real hardware, and the README/CONTRIBUTING/PR conventions expect contributors to state
plainly whether a change was tried on a real NOTE4C. Changes to the partition layout, the NVS
storage format (`home_store.c`), or the display driver (`home_panel.c`) can brick or reset a real
device's settings and need a hardware test before merging.

## Formatting

- C: clang-format using `firmware/.clang-format` (LLVM base, 4-space indent, 100 columns, no
  string/comment reflow). Format only the lines you touch.
- Panel JS/CSS (`firmware/ui/`): `npx prettier@3 --print-width 100`, same rule — format only
  changed lines.

## Code architecture

All firmware logic lives in `firmware/main/`, one `home_*` module per concern, wired together
through a single global `home_runtime_t home_runtime` (`home_runtime.h`) guarded by one mutex
(`home_lock()`/`home_unlock()`). A second, separate mutex (`render_lock`, internal to
`home_main.c`) serialises access to the frame buffer during rendering — the two locks are
intentionally distinct so a slow render doesn't block state updates from the network/button tasks.

**Concurrency model** — a handful of FreeRTOS tasks share `home_runtime` under the lock:
- `app_main` → the main loop (`home_loop_begin`/`home_loop_wait`/`home_loop_step` in
  `home_main.c`): decides what screen to show, renders it, hands the frame to the panel driver,
  and does the once-a-minute bookkeeping (counters, hourly power log, staleness of cached data).
  It sleeps on a semaphore given by a GPIO ISR (button edges) or times out after 20 ms (mid-gesture)
  or 500 ms (idle) — see `home_loop_wait_ms()`.
- `home_sources_task` (`home_fetch.c`/`home_parse.c`) — fetches and parses weather/feed/air-quality
  on its own task, writes results back into `home_runtime.data` under the lock.
- `home_battery_task` (`home_battery.c`) — samples ADC voltage periodically.
- `home_server_start()` (`home_server.c`) — the HTTP server task(s) serving the phone panel and
  `/api/*`.
- `home_panel_show()` (`home_panel.c`) owns the SPI bus and GPIO 6/8–13 exclusively; it calls back
  into `buttons_tick()` via `home_panel_set_idle_hook()` roughly every 50 ms during the ~25-second
  refresh so button presses during a redraw are never lost.

**Screens and rendering** — five screens (`home_screen_t`: Weather, Feed/News, Note, Sky, Air),
each with three "compositions" (Print/Rhythm/Atlas, `home_style_t`) plus a synthetic `HOME_CYCLE`
style that walks through the three on a timer ("In turn"). `home_render.c` (the largest file, pure
C, no I/O) takes `home_config_t` + `home_data_t` + a screen/style and writes a packed 2-bit/pixel
400×300 frame (`HOME_FRAME_BYTES` = 30000, B0/W1/Y2/R3, MSB-first). Dithering uses a precomputed
64×64 blue-noise mask (`generated/home_noise.h`, built by `tools/build_noise.py`, which depends on
a `dither` module that lives *outside* this repo — the generated header is committed, so nothing
needs regenerating for a normal build) plus two alternative brushes (halftone, grid) chosen in
Settings → Appearance. Yellow/red never dither finer than 2 px; black/paper can use single pixels.

**Config vs. data vs. secrets** — three persisted blobs, each with its own NVS-backed store
function in `home_store.c`:
- `home_config_t` (`home_types.h`) — everything the user sets in the panel (screens, order, style,
  location, note, feed URL, power mode, quiet hours, day rhythm). Has a `revision` counter bumped
  on every change and a `HOME_SCHEMA` version; `home_config_decode()` is tolerant of older/shorter
  saved records (e.g. an old 3-screen config gets the two new screens appended, disabled).
- `home_data_t` — cached results from the three network sources (weather, feed, air), each wrapped
  in a `home_source_meta_t` carrying HTTP freshness (ETag, Last-Modified, expiry, error) so a
  provider's "not modified" answer costs nothing.
- `home_secrets_t` — Wi-Fi credentials, the setup AP's own SSID/password, and hashed pairing
  tokens for up to 4 paired browsers. Never put through `home_config_json()`.
Counters (`home_counters_t`) and the rolling one-week power log (`home_power_log_t`,
`HOME_POWER_HOURS` = 168 entries) are separate NVS records again, deliberately never folded into
`home_config_t`/struct layouts that could change size, because that would invalidate the NVS
record and wipe history the device has been collecting since first boot.

**Power management** (`home_power.c`, `home_wake.c`) — two modes (`home_power_mode_t`): **Breath**
(default) turns Wi-Fi off between fetches and only opens the panel for 5 minutes after a button
press or an explicit request; **Open** keeps Wi-Fi connected like pre-0.6 releases. `home_wake.c`
is deliberately pure/host-testable (no locks, no clock reads) — `home_radio_wanted()` decides
whether the radio should be on, `home_sources_wanted()`/`home_sources_due()` decide whether a fetch
is owed. `home_power.c` holds/releases ESP-IDF power-management locks based on cable presence,
charging, setup/maintenance state.

**Button handling** (`home_main.c`) — a single ISR arms a semaphore on any edge across the three
GPIOs; `buttons_tick()` (polled by the loop and by the panel's idle hook) debounces and classifies
presses into short (`action()`), long-hold (`long_action()`), and hold-then-release
(`refresh_action()`) gestures with per-key thresholds (see `key_hold_us()`). This state machine is
timing-sensitive and has picked up several bug-fix comments referencing specific field reports
(dated `dd.mm` in comments) — read the comments around `key_state` before changing timings.

**HTTP API and panel** — `home_server.c` implements `/api/*` (status, config, recipe, pairing,
wifi scan/join, location, frame/preview, show/pause/refresh, unpair) plus a wildcard `/*` static
asset handler for the embedded `firmware/ui/` files. `home_config.c` provides the JSON codec
shared by the config API and the "recipe" export/import feature, plus schedule logic
(`home_auto_screen`, quiet hours, day-rhythm slots). Pairing is token-based (hashed, up to 4
browsers) rather than a persistent session.

**Location and time** — `home_location.c` manages an async IP-based location lookup (FreeIPAPI)
with its own tiny state machine so the network task never blocks the main loop; `home_places.c`
holds the pinned IANA time-zone database (`generated/home_zones.c`) and offers lookup only —
callers must never feed its result into `setenv(TZ, ...)`. `home_sky.c` is a pure, dependency-free
sun/moon calculator (no device, no I/O) for the Sky screen.

**Third-party components** — `firmware/components/` vendors `home_json` (cJSON), `home_qr`
(QR code generation for the setup screen), and `mdns` (Espressif's mDNS, for `<name>.local`
discovery via `home_discovery.c`), all built offline with `IDF_COMPONENT_MANAGER=0`.

**`tools/`** — standalone Python scripts, not part of the firmware build: `preflight.py` (compares
flash backups against the tested unit before installing, no network/device I/O),
`compare_image.py` (byte-for-byte release verification), `build_fonts_pixel.py`/
`build_fonts_cjk.py` (regenerate glyph slices in `generated/home_font.c` from external font
files), `build_noise.py` (regenerates the dither mask, needs the external `dither` module).
