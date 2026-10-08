# Pushing values from your homeserver

Inifuss can show a few values that a computer on your own network sends to it: the charge of
a home battery, and up to three short lines of text. The device never asks for them and holds no
token or password for any home system. The sender pushes; the device displays.

**The examples on this page are examples.** "Car not charging" and the 62 % battery are made-up
sample data to show the shape of a request. Nothing in the firmware produces them. What you push
is up to your own automation.

## Request

`POST /api/home` with `Content-Type: application/json`, at most 8 KiB.

```json
{
  "battery": { "percent": 62.5, "kwh": 8.1, "ttl_min": 180 },
  "lines": [ { "level": "warn", "text": "Car not charging" } ],
  "lines_ttl_min": 60
}
```

| Key | Meaning |
| --- | --- |
| `battery.percent` | 0 to 100, required inside `battery` |
| `battery.kwh` | 0 to 1000, optional, shown under the gauge |
| `battery.ttl_min` | 1 to 1440, optional: minutes until the value counts as stale |
| `lines` | up to 3 objects `{ "level": "warn" \| "info" \| "outline", "text": "..." }`; level defaults to `info`; text is 1 to 24 bytes of UTF-8 |
| `lines_ttl_min` | 1 to 1440, optional, only together with `lines` |

- A section that is present replaces the stored one; an absent section is kept.
- `"battery": null` clears the battery. `"lines": []` (or `null`) clears the lines.
- Unknown keys, out-of-range numbers and bad text are refused with `400`.
- Until the device has a valid clock it answers `503 clock_not_set`; send again later.
- The reply is `202 {"accepted": true, "redraw": false}`. `redraw` says whether this push
  caused a new picture.

`GET /api/home` returns what is stored, with `received_at`, the age of each section
(`none`, `fresh`, `stale`, `gone`) and the same values appear in `GET /api/status` as `home`.

From a computer, `tools/home_cli.py` does the same:

```sh
python3 tools/home_cli.py --host inifuss.local push --battery 62 --kwh 8.1
python3 tools/home_cli.py --host inifuss.local push --line warn:"Car not charging"
python3 tools/home_cli.py --host inifuss.local push --clear-battery --clear-lines
```

## What the display does with it

- **Battery:** a four-segment gauge, the percent and the kWh at the bottom right of Today. Below
  the low limit (20 % by default) the gauge is red and a "Battery N%" chip appears in the alerts
  corner.
- **Lines:** each becomes a chip in the alerts corner: `warn` is red, `info` yellow, `outline`
  paper with an outline. Warnings come first, and the corner holds three chips in all.
- **Stale:** after `ttl_min` (180 minutes when not given, changed with `alerts.stale_min`) the
  battery is drawn as an outline with "· 3 h ago", and lines are not shown. After 24 hours the
  battery shows a dash.
- **Never pushed:** nothing is drawn and the bins line takes the full width.

## How often the picture changes

A paper refresh takes about 25 seconds, so a push redraws only when it matters:

- the lines changed, or
- the percent moved by 10 points or more, or crossed the low limit, or
- a section went from missing or stale back to fresh.

At most one push-caused redraw happens every 10 minutes. Smaller changes show with the next
scheduled redraw. The values are kept in memory at once and written to flash at most every 15
minutes, so a push survives a restart with its original age.

## Power mode and trust

- In **Open** mode the device always answers. In **Breath** its Wi-Fi is off between fetches, so
  a push may not arrive; a push does not wake the device or extend the five minutes the panel
  stays open.
- The endpoint has the same trust as the rest of the panel: anyone on your local network can use
  it (see [privacy](PRIVACY.md)). Do not expose the device to the internet.
