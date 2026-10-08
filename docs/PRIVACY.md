# Privacy

Inifuss runs entirely on the NOTE4C. The phone panel, your settings, the
schedule, the rendering and the saved data all live on the device. There is no
account, no cloud service and no computer that has to stay on. The
firmware contains no analytics or telemetry.

## What leaves the device

Home and the panel talk directly to a few public services. Each of them sees an
ordinary internet request, including an IP address, and its own terms apply.
Nobody behind Inifuss receives your searches, your location or your IP
address from any of these requests.

| Service | Sent by | When | What it receives |
| --- | --- | --- | --- |
| Australian [Bureau of Meteorology](http://www.bom.gov.au/) forecast service (`api.weather.bom.gov.au`) | Home | only while the Today or Weather screen is switched on, when the last forecast expires (see below) | a 6-character geohash of the saved forecast location (a cell of about 1.2 km by 0.6 km, not exact coordinates), plus your home IP address |
| [FreeIPAPI](https://freeipapi.com/) | Home | when the panel asks Home for an approximate location | your home IP address, which it uses to estimate a location |
| [Open-Meteo Geocoding API](https://open-meteo.com/en/terms) | your phone's browser, from the panel | only when you search for a town | the text you typed, your phone's IP address and ordinary browser request data |
| `pool.ntp.org` time servers | Home | at start and then about hourly; in Breath, when no screen downloads anything, every six hours | time requests, plus your home IP address |

Requests from Home identify the software with the User-Agent
`inifuss/0.7.0 (+https://github.com/SathyaBhat/emini-home)`: its name and
version, followed by the project page as a contact address, which weather
services ask clients to include. The same text is sent from every device and
does not identify you.

Home also looks up these names through your network's DNS server, gives your
router the name `inifuss` when it joins, and follows up to three HTTPS
redirects from the weather service, which can lead to other
servers.

## How Home finds its location

There is no GPS. You can set the place for the weather in two ways.

1. **Search for a town.** The panel sends what you type from your phone
   directly to Open-Meteo and lists matching places. Home saves only the place
   you pick: its name, coordinates and time zone. Open-Meteo says it may keep
   IP addresses in its web server logs for technical reasons and deletes those
   logs after 90 days. Place search by [Open-Meteo.com](https://open-meteo.com/),
   using location data from [GeoNames](https://www.geonames.org/), licensed
   under CC BY 4.0.
2. **Approximate location.** Home asks FreeIPAPI where its internet connection
   appears to be. This can point to your internet provider's city rather than
   yours. Home does not replace a town you picked in the search with this
   estimate.

**How often Home asks.** Home asks the weather service again once the copy it holds expires, and
it never asks sooner than **25 minutes** after an answer, unless you ask for an update yourself.
That is half an hour, less the few minutes by which Home may bring a request forward so that two
share one trip of the radio. The Bureau sends no cache validators, so every request fetches the
whole forecast (two files, about 50 KB before compression). After a failed request Home waits
fifteen minutes, and after each failure that follows twice as long as before, up to two hours.
If neither Today nor Weather is switched on, nothing is fetched.

In **Breath**, the default power mode since 0.6.0, the radio is off between these requests.
That changes when they are sent, not what is sent or to whom.

Whichever way you choose, the saved location becomes the forecast location that Home sends to
the Bureau (as a geohash) while Today or Weather is switched on. The Bureau's service covers
Australia only. Its terms of use restrict use of its data, and its forecast API is not documented
for third parties: this build is for personal use on one household's device.

## What the homeserver may push

Home can receive a few values from a computer on your own network, such as a home battery's
charge or a short warning line, at `POST /api/home` (see [pushing values](PUSH.md)). They are
held in RAM and saved to flash at most every 15 minutes, shown on the Today screen, and go stale
after the time the sender gives (3 hours without one) and disappear after a day. Home does not
send them anywhere, and it does not need an account or token for them: see the next section.

## On your local network

- The panel is served by the device over **HTTP**, not HTTPS. Use it on a home
  network you trust and never expose the device to the internet.
- There is **no pairing**. Home checks that a request is addressed to the device's own
  name or address, and that a browser's `Origin` header matches, but any device on the same
  network can read and change its settings, note, location and Wi-Fi details, push home
  values and read the battery and power log. Anyone on the setup network (while it is open) can too.
- The device announces itself on the local network as `inifuss.local`, with a service named after the unit
  (`Inifuss Home-XXXX`) that carries its model and firmware version.
- Polled repeatedly, the status answer (the screen shown, whether a picture is being drawn,
  the last button pressed and when the weather was last checked) shows when someone uses the device.

## On the device

Settings, including the location you saved, the home Wi-Fi password, the setup
network password and cached data are stored in flash **without encryption**.
Anyone with the device and a USB cable can read them. Treat a NOTE4C like a
router you own: keep it in your home, and
[erase Home's settings over USB](INSTALL.md#starting-over) before giving it
away. Installing Home and starting over leave the factory firmware's own
settings area untouched; if the factory firmware was ever connected to Wi-Fi,
that area can still hold its Wi-Fi details.

Home also keeps a few numbers about **itself**, shown on the Inifuss card and readable on the
local network:

- **Counters** since the first start: pictures drawn, hours awake, downloads, and one battery
  reading per day for the last seven days.
- **A power log**: one line per hour for the last week, each with the battery voltage, whether
  it was charging, how many pictures and downloads there were, how many seconds the chip was
  not allowed to sleep, and how many seconds the display spent drawing and the radio was on.
  Taken together this is a record of **when the device was busy and when it sat on a
  charger** — a rough trace of the rhythm of the room it stands in. It never leaves the device
  on its own: `GET /api/power` is answered to the local network, and nothing is sent anywhere. It is stored unencrypted, like everything else here, and
  [erasing Home's settings](INSTALL.md#starting-over) erases it too.

The flash backup you make during installation contains the same kind of data.
Keep it private.

## Sharing

A recipe exported from the panel contains compositions, appearance, screen
order, language, units, clock format and rhythm choices only, including quiet
hours and day-rhythm times. It does not include your note, location, time
zone, alert limits, bins, Wi-Fi details or access token.
Before sharing a photo of the display, check that it does not show the setup
screen, which contains the setup network password.
