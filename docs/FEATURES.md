# SIMUT features

[English](FEATURES.md) | [Português](FEATURES.pt-BR.md) | [Español](FEATURES.es-ES.md)

Everything the firmware does, in detail. The [README](../README.md) has the summary.

## Sensing and alarms
- **16 universal sensor slots** — GP0–GP15. Each slot takes a DS18B20, a DHT22 or a BMP280/BME280; a BMx280 is retyped automatically from its chip ID. Type and pins are assigned at runtime, with no recompile.
- **Temperature, humidity and pressure** as first-class channels.
- **Calibration** — per-sensor offsets and curves of up to 5 points per channel, linear or smooth.
- **Sensor validation:**
  - DS18B20 ROM verification, with a swapped probe quarantined until the right one returns;
  - error hysteresis: 3 failures to enter, 5 successes to leave;
  - out-of-range readings rejected.
- **Alarms on every channel:**
  - low and high limits per channel;
  - a fault alarm that fires even when a sensor's limits are off;
  - a 120 s silence and a global mute;
  - buzzer melodies and visual feedback on the display.
- **Maintenance windows** — per sensor, up to 30 days, set from the panel or by a server. While one is open, alarms are suppressed, and the window's start and end are reported as `maint_on` / `maint_off`.

## Touch panel (`release`)
- **320×240 ILI9341 touch panel** — dashboard, history graphs with a min/max band, statistics, calendar, settings.
- **Identity at the panel** — the operator picks an account, then types that account's PIN:
  - 32 accounts, each with its own PIN;
  - a configurable PIN policy: minimum length, 1–3 glyphs per key, digits or 0-9A-Z;
  - a keypad that is re-dealt after every tap;
  - a lockout per account: the sixth failure locks the account, and 20 failures in total lock the panel.
- **Administration on the glass:**
  - a Users item creates accounts and sets their permission bits and PINs;
  - the 13 settings rows are filtered by what the account may do;
  - Settings → 8 starts the setup access point;
  - Settings → 4 sets the date and time, and a unit with no network configured asks for them at the end of the boot.
- **Top-panel gestures** — a tap toggles min/max, a 3 s hold pins the selection.
- **DMA rendering fast path** — canvas compositing over 62.5 MHz SPI.
- **4 px safe area everywhere** — the screen-alignment offset (±4 px per axis) can never crop content.
- **Themes** — up to 8 loaded from LittleFS (11 ship in `data/themes/`); the editor in `tools/theme-editor/` previews on a live device.
- **Sound system** — Touch, Confirmation, Error, Alarm and Attention classes, 6 melodies each, with separate system and alarm volumes.

## Character LCD (`alpha`)
- **Readings** — cycles every active slot and channel every 3 s, with big digits for temperature and humidity and an `S<n>` tag naming the slot.
- **Setup access point** — shows the address, the SSID and the key, scrolling long values.
- **Bluetooth console** — see the security note under [Environments](../README.md#environments).
- **Pending telemetry** — with a single sensor, the bottom-left corner shows the pending telemetry count (`N`, or `Nk` from a thousand up), and the Wi-Fi icon fills left to right.

## Web interface
- **11 pages** — gzip-compressed (zopfli) in flash, with light and dark themes that follow the system preference, a file manager, and multi-user sessions that expire after 15 minutes idle.
- **Live panel mirror** (`release`) — the panel's current frame in the browser, 213 ms per frame. A click on it is a touch on the glass.
- **Changes say what they cost** — three buttons:
  - *Test*: applied, not saved;
  - *Apply now*: saved, no reboot;
  - *Save and restart*.

  The device classifies each change with a dry run before the page offers them.
- **Restart without saving** — a button at the bottom of the Configuration page restarts the device and drops whatever the page has not saved; the saved configuration is what comes back.
- **Version on the login page** — the firmware version shows under the name before anyone signs in.
- **Wi-Fi scan** — pick the network from a list, even from inside the setup access point.
- **History graphs and CSV export in the browser** — the page downloads the raw binary day files, then decodes, buckets (min/max/mean) and exports them itself. The recent, unsealed hour comes from `/api/history/open`. The chart renderer is embedded — no CDN.
- **HTTP API** — 62 routes. Each one is either gated by a permission or public by design, and CI checks it.

## Telemetry and integrations
- **Four transports** — HTTP, HTTPS, MQTT and MQTTS:
  - payloads in JSON, CSV or a custom template;
  - TLS 1.2 (ECDHE with AES-GCM), with the server certificate checked against an uploaded `/cert.pem`.
- **Batching by quantity:**
  - `t_int` is the minimum batch: the radio stays off until that many records wait (0 = off);
  - `t_bat` is the maximum per request, a ceiling that free memory can lower;
  - the batch size adapts to successes and failures, and the server's response time paces the next one.
- **A second line for alarms:**
  - alarm, fault and maintenance events travel in their own queue (32 by default, up to 64);
  - each event leaves the queue only when the server acknowledges it (HTTP 2xx or an MQTT ack);
  - each carries the name of the account that acted.
- **Integrations** — Home Assistant MQTT Discovery (opt-in), Prometheus `/metrics` (session or HTTP Basic), and remote syslog (RFC 5424 over UDP).
- **Fleet hooks:**
  - `X-SIMUT-*` identity headers on uploads;
  - on the `release` image, an mDNS `_simut._tcp` service with id, version, image and TLS in its TXT record;
  - Bearer tokens and a configurable CORS origin.

## Network and time
- **Wi-Fi that reconnects itself:**
  - a retry ladder: 5 s, doubling to 120 s, then dormancy and a new round;
  - hidden SSIDs and signal-quality checks;
  - static IP, two DNS servers, a custom NTP server or a manual clock, and a configurable web port.
- **Setup access point:**
  - named `<device name>_SETUP` — `simut_SETUP` from the factory;
  - WPA2, with a per-device key shown on the USB console and the TFT boot screen;
  - a captive portal at `http://192.168.4.1`.

  It opens only when someone asks. A unit whose network is away keeps measuring and keeps retrying it. Three ways in:
  - Settings → 8 on the panel;
  - the `ap` console command (USB, or Bluetooth on the alpha and the Air);
  - a 3 s hold on the panel during boot.
- **NTP** — the retry backoff grows from 20 s to 15 min, with a fallback to `pool.ntp.org`. Until NTP syncs or someone sets the clock, a provisional clock is seeded from the newest stored record. The panel marks it with `?` between the date and the time, and the first set after the boot, by NTP or by hand, corrects the history blocks that boot started.

## Storage and history
- **Compact binary history (V5)** — delta + anchor encoding at 5.38 bytes/record, about 116 days in the 1 MB filesystem (11 channels at a 1-minute cadence, measured on bench files on 2026-07-31):
  - blocks of 60 records, each with its own CRC;
  - the open block is saved after every record;
  - past 86 % full, the oldest day is deleted.
- **Configuration** — CRC32-checked, written to a temporary file and renamed, with a `.bak` fallback. Secrets are obfuscated at rest, not encrypted: physical access to the flash is outside the threat model ([SECURITY.md §3](../SECURITY.md#3-secret-storage)).
- **Event log** — 2 × 800 records and 155 event codes:
  - routine events are persisted on state changes, with an hourly heartbeat and a count of what was suppressed;
  - security, configuration and fatal records are never filtered.

## Security
- **Accounts and permissions:**
  - 32 accounts, 13 permission bits;
  - nobody can grant a bit they do not hold;
  - backup, restore, OTA and certificate install require the full-admin mask.
- **Passwords:**
  - HMAC-SHA256, 5000 rounds, an 8-byte hardware-random salt per user and a board-bound pepper;
  - a factory-fresh unit generates a random 8-character admin password, prints it once on the USB console and forces a change at the first login.
- **Brute-force limits:**
  - login lockout from 2 s to 300 s per client, with `429` once every lockout slot is taken;
  - per-IP throttling on the heavy routes;
  - the Bluetooth console has its own exponential lockout and stops advertising 5 minutes after boot.
- **Sessions** — an `HttpOnly; SameSite=Strict` cookie (`Secure` over HTTPS), or a Bearer token.
- **Uploads** — path traversal, percent-encoding, control bytes and reserved names are refused, and `/config` is out of the file manager's reach.
- **Optional HTTPS** (`release`) — install the certificate pair with `POST /api/tls`. TLS 1.2, ECDHE with AES-GCM.
- **Audits** — the audits of 2026-08-16, of v2.3.6-beta and of 2026-09-07 are closed. The last finding, V-09 (a restricted account could create one with more bits than it held), was fixed in v2.7.0, and both it and the 2026-09-07 fixes were verified on hardware. See **[SECURITY.md](../SECURITY.md)**.

## Resilience and forensics
- **Crash autopsy on every boot** — the watchdog scratch registers name the stalled module on each core. Since v2.7.0, three more records also persist Core 1's module, the free heap and the uptime at the stall.
- **Dual-core flash discipline** — Core 1 is paused around every flash write (measured, not assumed).
- **Watchdog discipline** — the watchdog is fed around every filesystem operation, so slow HTTP clients cannot starve the loop.

## Updates, backup and recovery
- **OTA from the web page:**
  - admin only;
  - the image is checked before it is committed (size, boot2 CRC, image variant) and again on the next boot;
  - Wi-Fi, accounts and sensor slots are carried across, and the rest of the filesystem is reformatted, so the page downloads a backup first;
  - the device is back in under a minute: 52–56 s in the v2.7.0 campaign.
- **Backup & restore** — the whole filesystem in one file, CRC32-checked and bound to the chip.
- **[Recovery guide](RECOVERY.md)** — BOOTSEL, picotool and 1200 bps paths for every failure mode.

## SIMUT Air (experimental)
- **Two modes, no display, no buzzer:**
  - **M0** is awake: web, console, Bluetooth and sensors;
  - **M1** is the cycle: sleep on the RTC alarm, wake, read, write history, and sleep again.
- **The radio only when it pays** — it comes up only when `t_int` records are waiting. A reading wake takes 9.31 s with a DS18B20, and with a 60 s interval the device is awake about 13 % of the time.
- **Charger pin** — a charger-detect pin (GP17 by default) keeps it awake while powered.
- **Full console** — the only published image with the full console.

## Internationalization
- **3 interface languages** — English built in; Portuguese (pt-BR) and Spanish (es-ES) come as `.lng` packs on the filesystem. A device runs English plus the one pack installed.
