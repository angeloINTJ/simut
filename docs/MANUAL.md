# SIMUT — User Manual

**Firmware:** v1.6.2-beta · **Hardware:** Raspberry Pi Pico W (RP2040 + CYW43439) · **License:** MIT
**Repository:** https://github.com/angeloINTJ/simut

> **This is beta software.** It is tested on real hardware, but it is not a
> certified metrological instrument. Do not make it the only control on
> regulated storage without validating it against your own reference.

Everything below was checked against a running v1.6.2-beta device. Where a
number is quoted it was measured rather than estimated; where behaviour is
untested or known to be incomplete, the text says so rather than going quiet.

---

## Contents

1. [What SIMUT is](#1-what-simut-is)
2. [Hardware](#2-hardware)
3. [First boot](#3-first-boot)
4. [Sensors and the slot model](#4-sensors-and-the-slot-model)
5. [The device display](#5-the-device-display)
6. [The web interface](#6-the-web-interface)
7. [Alarms](#7-alarms)
8. [History and logs](#8-history-and-logs)
9. [Users and permissions](#9-users-and-permissions)
10. [Telemetry](#10-telemetry)
11. [Backup and restore](#11-backup-and-restore)
12. [Firmware updates](#12-firmware-updates)
13. [The serial console](#13-the-serial-console)
14. [Recovery](#14-recovery)
15. [Specifications](#15-specifications)
16. [HTTP API reference](#16-http-api-reference)

---

## 1. What SIMUT is

A datalogger for temperature, humidity and pressure that runs entirely on one
Raspberry Pi Pico W. It reads up to sixteen sensors, draws them on a touch
display, serves its own web interface on your LAN, keeps an audit trail, and
can update its own firmware over the air.

There is no cloud component and no account. Telemetry to an external endpoint
exists but is off by default, and the device is fully usable having never been
given one.

**What it is not.** It is not certified for regulated storage, it has no
redundant sensing, and it holds no second firmware slot to fall back on. The
sections below are explicit about each of those limits where they matter.

### Design in one paragraph

Two cores with a strict division. **Core 0** runs sensors, Wi-Fi, the web
server, telemetry, history and the serial console. **Core 1** does nothing but
drive the display, reading lock-free snapshots of shared state. That split is
why a busy network does not stutter the screen — and it is also the source of
the trickiest class of bug in the project, since a flash write must stop Core 1
before erasing anything it might be executing from.

---

## 2. Hardware

| Part | Specification |
|---|---|
| Microcontroller | Raspberry Pi Pico W — RP2040, dual Cortex-M0+, 264 KB SRAM, 2 MB flash |
| Wireless | CYW43439 (2.4 GHz Wi-Fi) — the radio blob occupies ~232 KB of the application slot |
| Display | ILI9341 320×240 TFT over SPI |
| Touch | XPT2046 resistive panel |
| Sensors | 16 slots on GPIO0–GPIO15 |
| Buzzer | Passive piezo, driven from PIO |
| Storage | On-chip flash: 1020 KB application, 1 MB filesystem, 4 KB metadata |

**GPIO allocation.** GPIO0–GPIO15 are available to sensors. GPIO16 and above
belong to the display, touch panel and buzzer, and the pin picker in the web
interface will not offer them.

Full pinout and assembly notes: [WIRING.md](WIRING.md).

---

## 3. First boot

1. **Flash the firmware.** Hold BOOTSEL while connecting the Pico over USB,
   then copy `simut_v1.6.2-beta.uf2` onto the `RPI-RP2` drive that appears. The
   board reboots into SIMUT by itself.

2. **Read the admin password.** On the first boot with no stored
   configuration, a random 8-character admin password is generated and printed
   **once** over USB serial at 115200 baud. Write it down — it is stored only
   as a salted hash, and nothing recovers it later except a reset.

3. **Join a network.** Configure Wi-Fi from the touch display, then note the
   address the device reports. `show net status` over serial prints it, and so
   does the dashboard once you are in.

   > **There is no `simut.local`.** mDNS exists in the source but is compiled
   > out by default — it costs roughly 196 KB of flash, which the application
   > slot does not have to spare. Reach the device by IP, and give it a static
   > lease on your router if you want the address to stay put.

4. **Change the password.** The first web login is forced through a password
   change before any page will load.

5. **Add sensors.** In the web interface under **System Config → Sensors &
   GPIO**, either add slots manually or use **Scan for probes** to discover
   1-Wire devices on a pin.

**A factory device provisions no sensors at all.** All sixteen slots come up
empty and claim no GPIO. This changed in v1.6.0-beta: earlier firmware
pre-activated slot 10 as a DHT22 on GP10, which made that pin unassignable on a
board that had no sensor there, and a factory reset put it back.

---

## 4. Sensors and the slot model

### One model, sixteen interchangeable slots

A slot is a position, not a role. Any slot takes any supported sensor, in any
combination, and none of them is special. What identifies a sensor is its own
**hardware ID** — so calibration offsets, alarm thresholds and history records
follow the physical device you wired, not the position you wired it into.

| Type | Bus | Channels | Pins per slot |
|---|---|---|---|
| DS18B20 | 1-Wire | temperature | 1 |
| DHT22 | single-wire | temperature, humidity | 1 |
| BME280 | I²C | temperature, humidity, pressure | 2 (SDA, SCL) |
| BMP280 | I²C | temperature, pressure | 2 (SDA, SCL) |

The BMP280 became a type of its own in v1.6.0-beta. Before that it shared
`TYPE_BME280`, which declares a humidity channel the part does not have — so
whichever chip you owned, the firmware was wrong about one of the two.

### Calibration

Each sensor carries its own offset, stored in `/calib.csv` and keyed by
hardware ID. Two identical DHT22s on one board calibrate independently, which
was not true before v1.6.0-beta: offsets for ROM-less parts used to be a single
device-wide row pair applied to whichever such sensor came first in the runtime
list.

Calibration requires the `CALIB` permission and is applied from the web
dashboard.

> **`/calib.csv` does not survive a firmware update.** See
> [§12](#12-firmware-updates) for what an update preserves and what it does not.

### Reading pipeline

Readings pass through a trimmed-mean sliding window of 10 samples before they
reach the display, history or telemetry. The DS18B20 resolution (9–12 bit) and
the sampling interval are set under **System Config → Hardware & Sampling**.

---

## 5. The device display

The panel is 320×240 with a resistive touch overlay. The firmware has 21
distinct UI modes. A full visual map — every screen, with the exact route to
reach it — is generated from a real device by
[`tools/screen_mapper.py`](../tools/screen_mapper.py) and published at
[docs/images/screens/screens.md](images/screens/screens.md).

### Dashboard

Two sensor cards — an upper panel and a lower panel — above a footer of up to
five buttons. Footer buttons select slots, page through them when more than
four are active, and open settings (**CFG**).

- **Tap a sensor card** to toggle its min/max view.
- **Tap the graph icon** in the min/max view to open that sensor's history.
- **Tap CFG** to reach settings — this asks for the display PIN if one is set.

### Settings

Reached through CFG. Covers visual themes, alarm limits, alarm sounds,
interface language, the display PIN, touch calibration, touch sensitivity,
display alignment, system status and the license text.

**System status** is the screen worth knowing: device name, firmware version,
board serial, uptime, free heap, flash usage and board temperature — the
fastest way to confirm what a device is actually running.

### History

The graph view plots one sensor over a selectable range, with navigation
backwards and forwards in time, a calendar picker and zoom. A numeric detail
screen gives maximum, minimum, average and standard deviation for the range on
screen.

### While the web holds the device

When a web client is performing a long operation — streaming history, exporting
logs — the top bar shows the user holding it and **touch is rejected on the
dashboard** until it finishes. The banner is deliberate: it tells you why
before you touch rather than after.

---

## 6. The web interface

Served from the device itself. Log in at `http://<device-ip>` — see the note in
§3 about why there is no `.local` name.

| Page | What it does |
|---|---|
| `/` | Dashboard: system statistics, memory and flash usage, live sensor table, and a display capture panel that reads the physical screen |
| `/config` | Device identity, date and time, hardware and sampling, the GPIO map and sensor slots, telemetry |
| `/network` | Wi-Fi, static addressing, mDNS, NTP |
| `/alarms` | Per-sensor thresholds and actions |
| `/users` | Accounts and permissions |
| `/files` | Filesystem browser: upload, download, delete, create directories |
| `/history` | History graphs, CSV export, and the system event log viewer |
| `/license` | License text |

### Authentication

Login is a two-step exchange: the browser fetches a nonce from
`/api/login_init`, hashes the password client-side, and posts the hash with the
nonce. The session is a `SIMUTSESS` cookie.

Two details matter if you are scripting against it:

- The page hashes **each UTF-16 code unit as one byte** — that is latin-1, not
  UTF-8. A password containing characters above U+00FF cannot be reproduced by
  a UTF-8 hash.
- Repeated failures trigger an exponential lockout measured in seconds.

### Display capture

`GET /api/screenshot` returns a 320×240 24-bit BMP read back from the panel's
framebuffer over SPI. It is the real screen rather than a re-rendering, and it
is what the screen map in §5 is built from.

---

## 7. Alarms

Each sensor slot carries its own thresholds and is enabled independently.
Thresholds are set from the web interface under `/alarms`, or on the device
under **Settings → Alarm Limits** — select a row, then tap its ON/OFF zone to
open the editor.

An alarm in progress raises the buzzer unless muted, marks the sensor on the
dashboard, and writes a record to the audit log.

**Global mute** lives on the device under **Settings → Alarm Sounds** and asks
for confirmation, because it silences every alarm channel at once.

---

## 8. History and logs

### History records

Readings are written to `/history/YYYY-MM-DD.sim4` in a compact binary format
(**V4**). The recording interval defaults to one minute and is configurable
from 1 to 1440.

Records are keyed by the sensor's hardware ID. That has a consequence worth
knowing before rewiring anything: **changing a slot's hardware ID makes the
existing file stop matching it**, and the history for that sensor appears to
stop. `/api/history_rebind` exists to re-point records at a new ID.

Export is available as CSV from `/history`, or as raw binary through
`/api/export/history.bin`.

### Event log

The audit trail is a persistent binary log of 12-byte records:

| Field | Bytes | Notes |
|---|---|---|
| epoch | 4 | absolute timestamp |
| uptime | 3 | **seconds**, split across two fields, saturating at ~194 days |
| code | 2 | numeric event code |
| context | 2 | code-specific |
| flags | 1 | level and module |

The uptime column held whole hours until v1.6.2-beta, which meant any device
rebooting more than once an hour wrote zero into every record it ever made.
**Records written by older firmware read their old hours field as seconds** —
in practice zero, which is what that field already contained.

The log is viewable from `/history`, exportable as CSV, and dumpable over the
serial console with `show system log`. Note that the serial dump prints the
numeric code and context, **not free text**: the descriptive message for an
event exists only in the live serial output at the moment it happens.

---

## 9. Users and permissions

Five accounts maximum. Three sessions may be active at once. Passwords are
hashed with a per-user random salt.

Ten permission bits, granted independently:

| Bit | Permission | Grants |
|---|---|---|
| `0x0001` | DASHBOARD | View live readings |
| `0x0002` | HISTORY | View and export history |
| `0x0004` | LOGS | View the event log |
| `0x0008` | SYS_CONFIG | Device and sampling configuration |
| `0x0010` | NET_CONFIG | Network configuration |
| `0x0020` | FILE_READ | Browse and download files |
| `0x0040` | FILE_UPLOAD | Upload files |
| `0x0080` | FILE_DELETE | Delete files |
| `0x0100` | USER_MGR | Manage accounts |
| `0x0200` | CALIB | Calibrate sensors |

**Admin is all bits set.** Three operations demand full admin rather than a
single bit, because each erases a large region of flash: staging a firmware
image, applying an update, and the staging selftest.

---

## 10. Telemetry

Off by default. When enabled, the device posts readings to an endpoint you
specify.

| Setting | Options |
|---|---|
| Transport | HTTP POST, or MQTT |
| Payload | JSON, CSV, or a custom template |
| Security | TLS supported |
| Interval | Configurable, with a batch limit per upload |

### Template tokens

| Token | Resolves to |
|---|---|
| `{TS}` | Timestamp |
| `{DEV}` | Device name |
| `{t0}`…`{t15}` | Temperature of slot N |
| `{u0}`…`{u15}` | Humidity of slot N |
| `{p0}`…`{p15}` | Pressure of slot N |
| `{DHT_ID}` | Hardware ID of the DHT sensor |

The tokens `{tAMB}`, `{uAMB}` and `{pAMB}` were removed in v1.6.0-beta along
with the privileged ambient slot they resolved through. Use the numbered slot
tokens instead.

Records that cannot be delivered are queued; the dashboard shows the pending
count.

---

## 11. Backup and restore

`GET /api/backup` downloads the whole filesystem as a single `.bkp`. The format
carries a CRC32 over the payload and is **bound to the chip ID**, so an image
cannot be restored onto a different board by accident.

Restore is `POST /api/restore` — `op=validate` checks an image without writing,
`op=apply` writes it. A successful apply reboots the device so nothing keeps a
stale cache of what was on flash.

**Take a backup before every firmware update.** §12 explains why.

---

## 12. Firmware updates

### Read this first

**Over-the-air updates work from v1.6.2-beta onward, and only from there.**
Every earlier build shipped an applier whose watchdog feed wrote the reset bit
instead of reloading the counter: it rebooted the chip before copying a single
sector, while every layer above it reported success. The symptom was a device
that announced a successful update and kept running the old firmware.

That applier is the one every existing device is running, so **there is no
over-the-air path onto this release**. Flash v1.6.2-beta over USB once; updates
work normally after that.

### What an update destroys

Staging shares the flash partition with the filesystem, so an update
**reformats it**. A snapshot carries `/config/system.bin` across — Wi-Fi
credentials, users and sensor slots survive automatically, and the device
rejoins the network unattended.

Nothing else does. **Language packs, `/calib.csv` and all stored history are
lost.** Download a backup first.

### There is no rollback

The application slot is single. The image is validated before it is committed
and verified again on the next boot, but if a bad image boots badly there is no
second slot to fall back to — recovery is the BOOTSEL button and a USB cable.
See [RECOVERY.md](RECOVERY.md).

### The procedure

From the web interface: **System Config → Firmware**. Or directly:

```bash
# 1. Stage — uploads and validates. ~29 s for a 957 KB image.
curl -b cookies.txt -F "file=@simut_v1.6.2-beta.bin" \
     "http://$DEVICE_IP/api/restore?op=stage&commit=1"
# -> {"st":5,"bytes":957696,"crc32":"...","v":0,"dsize":957500,"dcrc":"...","committed":1}

# 2. Apply — answers 202 immediately, then tears down and reboots.
curl -b cookies.txt -X POST "http://$DEVICE_IP/api/ota/apply"
# -> {"accepted":true,"mode":"apply"}
```

Staging must report `committed: 1` and `v: 0` before apply will do anything.
`/api/ota/apply` answers **409** when no validated update is pending.

Note that `bytes` and `dsize` differ, and should: `bytes` counts the 0xFF
padding that closes the final 256-byte page, which is what the applier copies,
while `dsize` and `dcrc` describe the bytes that actually arrived.

### What is checked

| Stage | Check |
|---|---|
| Upload | Size between 100 KB and the 1020 KB application slot |
| Upload | CRC32/MPEG-2 over the first 252 bytes against the 4 bytes that follow — the same check the RP2040 boot ROM performs, so a file that is not a valid RP2040 image is rejected before anything is erased |
| Apply | The applier copies staging into the application slot from SRAM, with interrupts off |
| Next boot | The installed image is CRC-checked against the metadata and the verdict logged |

The post-apply verdict appears on the serial console as
`[INF][OTA] image verified, NNNNNN B`. It exists there and nowhere else — the
persistent log stores only the numeric code, so after the fact the level
(`INF` versus `ERR`) is what distinguishes success from a mismatch.

### Measured behaviour

21 consecutive updates on the bench, all successful:

| Stage | Time |
|---|---|
| Upload and stage (957,500 B) | 29.2 s ± 0.07 (32.1 KiB/s) |
| `/api/ota/apply` → 202 | 0.1 s |
| Applier window — erase and program | 25.1 s ± 0.10 |
| Reboot → image verified | 9.4 s ± 0.06 |
| **Web interface unreachable** | **48.4 s** |

Roughly two thirds of the downtime is the applier; the rest is Wi-Fi
re-associating. Free heap moved 24 bytes across the whole run, and no boot
produced a panic.

---

## 13. The serial console

USB CDC at **115200 baud, 8N1**, DTR asserted. The console exists in two
profiles, and which one you have depends on the firmware build.

### Release firmware — nine commands

The image users run ships a recovery console, not a configuration interface.
Configuration lives in the web UI.

| Command | Purpose |
|---|---|
| `show net status` | IP, signal, buffer pool, send aborts |
| `show system info` | Device, firmware, serial, Wi-Fi, timezone, NTP |
| `show system log` | Dump the event log |
| `debug on` / `debug off` | Verbose logging for this session |
| `system admin reset` | Reset the admin password to a random one |
| `system format` | Erase the filesystem |
| `system factory` | Restore factory defaults |
| `reload` | Reboot |
| `help` | List these |

Destructive commands require `confirm` as a final word.

> **Changes here do not persist.** The emergency console has no `write memory`,
> so anything it changes applies to the running session and is gone at the next
> reboot. `system admin reset` in particular yields a password for *this boot
> only* — long enough to log in and set a real one through the web UI.

This console replaced a 55-command one in v1.5.6-beta. The commands that were
cut had web equivalents already, and removing them returned 44.5 KB of flash.

### Test firmware — the full console

`pico_w_test` builds ship the 55 commands with Cisco-style modes
(`enable` → `configure terminal` → `write memory`), plus `touch sim` and
`screen` for driving the display from a script. It is the build the automated
suites under `tools/` require. It is not what belongs on a device someone uses.

Full reference: [CLI-Manual.md](CLI-Manual.md) *(in Portuguese)*.

### Bluetooth

Earlier manuals documented a Bluetooth console. **It is not compiled into the
release firmware** — `BluetoothManager.cpp` is excluded from the build.

---

## 14. Recovery

| Symptom | What to do |
|---|---|
| Forgot the admin password | `system admin reset confirm` over serial, then log in with the printed password and set a new one through the web UI |
| Answers on serial but not on the network | `show net status` — with no IP, reconfigure Wi-Fi from the display |
| Blank screen after adjusting the display offset | Fixed in v1.6.2-beta. On older firmware a factory reset clears the stored offset |
| Update reported success but the version did not change | The applier defect described in §12. Flash v1.6.2-beta over USB |
| Does not enumerate over USB at all | BOOTSEL rescue — see [RECOVERY.md](RECOVERY.md) |

---

## 15. Specifications

### Limits

| | |
|---|---|
| Sensor slots | 16 (GPIO0–GPIO15) |
| Channels per sensor | 4 (temperature, humidity, pressure, lux) |
| Pins per sensor | up to 4 |
| User accounts | 5 |
| Concurrent web sessions | 3 |
| Permission bits | 10 |
| Averaging window | 10 samples, trimmed mean |
| TFT graph points | 200 |
| History interval | 1–1440 minutes, default 1 |

### Flash layout

| Region | Offset | Size |
|---|---|---|
| Application | `0x000000` | 1020 KB |
| Staging / LittleFS | `0x0FF000` | 1024 KB |
| Config snapshot | last 4 KB of staging | 4 KB |
| OTA metadata | `0x1FF000` | 4 KB |

The staging area and the filesystem are the same physical region. That is why
an update reformats the filesystem, and why the configuration snapshot lives in
the metadata sector instead.

### Build

| | |
|---|---|
| Firmware size | 945,464 B — 90.5% of the application slot |
| RAM at link | 122,540 B of 262,144 B |
| Free heap in service | ~55 KB |
| Radio firmware | ~232 KB of the application slot |

---

## 16. HTTP API reference

All routes require an authenticated session unless noted. Permissions in
brackets.

### Session

| Route | Method | Notes |
|---|---|---|
| `/api/login_init` | GET | Returns a nonce. **Open, no session required** |
| `/api/login` | POST | `user`, `pass` (sha256, latin-1), `nonce` |
| `/api/login_chpass` | POST | Change password at login |
| `/api/force_chpass` | POST | Complete a forced password change |
| `/logout` | GET | End the session |

### Reading state

| Route | Method | Notes |
|---|---|---|
| `/api/status` | GET | Uptime, heap, flash usage, RSSI |
| `/api/sensors` | GET | Live readings per slot |
| `/api/config` | GET | Device configuration |
| `/api/network` | GET | Network configuration |
| `/api/alarms` | GET | Thresholds |
| `/api/users` | GET | Accounts [USER_MGR] |
| `/api/perms` | GET | Permission bits of the session |
| `/api/sec_status` | GET | Lockout and security state |
| `/api/themes` | GET | Available themes |
| `/api/lang` | GET | Language dictionary |

### History and logs

| Route | Method | Notes |
|---|---|---|
| `/api/history_multi` | GET | Records for a range [HISTORY] |
| `/api/history_days` | GET | Which days hold data |
| `/api/history_rebind` | POST | Re-point records at a new hardware ID |
| `/api/export/history.bin` | GET | Raw binary export |
| `/api/logs` | GET | Event log [LOGS] |
| `/api/export/logs.bin` | GET | Raw binary export |
| `/api/clear_logs` | POST | Erase the log |

### Files

| Route | Method | Notes |
|---|---|---|
| `/api/ls` | GET | List a directory — the parameter is `dir` |
| `/api/upload` | POST | Upload [FILE_UPLOAD] |
| `/api/delete` | POST | Delete — the parameter is `file` [FILE_DELETE] |
| `/api/mkdir` | POST | Create a directory |
| `/download` | GET | Download a file [FILE_READ] |

### Configuration

| Route | Method | Notes |
|---|---|---|
| `/api/save_sys` | POST | Save system configuration [SYS_CONFIG] |
| `/api/commit_all` | POST | Apply a batch of changes |
| `/api/set_time` | POST | Set the clock |
| `/api/calib` | GET/POST | Calibration offsets [CALIB] |
| `/api/action` | POST | Multiplexed actions — reboot, telemetry reset |
| `/api/reset_touch_cal` | POST | Clear touch calibration |

### Firmware and backup

| Route | Method | Notes |
|---|---|---|
| `/api/backup` | GET | Download the filesystem as `.bkp` |
| `/api/restore` | POST | `op=validate` \| `op=apply` \| `op=stage&commit=1` — **stage is admin only** |
| `/api/ota/apply` | POST | Apply a staged update — **admin only**, answers 202 |

### Display

| Route | Method | Notes |
|---|---|---|
| `/api/screenshot` | GET | 320×240 24-bit BMP off the panel |
| `/api/screenshot_chunk` | GET | One 16-row chunk with a CRC32, for verifiable transfer |

---

## Getting help

- [Wiring and pinout](WIRING.md)
- [Recovery](RECOVERY.md)
- [Over-the-air updates](OTA_USAGE.md)
- [Glossary](GLOSSARY.md)
- [Serial console reference](CLI-Manual.md) *(Portuguese)*
- [Screen map](images/screens/screens.md)
- [Report a bug](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- [Security policy](https://github.com/angeloINTJ/simut/blob/main/SECURITY.md)
