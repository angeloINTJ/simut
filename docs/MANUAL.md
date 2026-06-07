---
title: "SIMUT — Complete User Manual [DRAFT — NOT REVIEWED]"
subtitle: "Cold Chain Monitoring System"
author: "Ângelo Moisés Alves"
version: "v1.0.0"
date: ""
documentclass: report
geometry: margin=2.5cm
header-includes: |
    \usepackage{xcolor}
---

# SIMUT — Complete User Manual

> ⚠️ **STATUS: DRAFT — NOT REVIEWED**
>
> This document was automatically generated as a pipeline experiment
> (auto-capture of TFT screens via `screen <code>` + web pages via Selenium
> + markdown→HTML→PDF render via pandoc + chromium). **Content has NOT been
> reviewed by a human**. It may contain:
>
> - Incorrect UI behavior descriptions
> - Outdated endpoints and permissions
> - Imprecise configuration recommendations
> - Hardware details that don't match your kit
>
> Use as a starting point only. For authoritative information consult
> the source code, `SECURITY.md`, and `STABILITY_PLAN.md` in the
> repository.

---

**Firmware version:** v1.4.4-beta
**Target hardware:** Raspberry Pi Pico W (RP2040 + CYW43439)
**Repository:** https://github.com/angeloINTJ/SIMUT
**License:** MIT

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware](#2-hardware)
3. [First Access](#3-first-access)
4. [TFT Display — Screens and Touches](#4-tft-display--screens-and-touches)
5. [Web Interface](#5-web-interface)
6. [CLI via USB Serial](#6-cli-via-usb-serial)
7. [CLI via Bluetooth](#7-cli-via-bluetooth)
8. [OTA — Firmware Update](#8-ota--firmware-update)
9. [Backup and Restore](#9-backup-and-restore)
10. [Security](#10-security)
11. [Troubleshooting](#11-troubleshooting)
12. [Appendices](#12-appendices)

---

## 1. Overview

SIMUT is a professional datalogger for **temperature and humidity
monitoring** in regulated environments (laboratories, pharmacies, blood
banks, vaccine freezers, water baths, microbiological incubators).

### 1.1 Key Capabilities

- **Up to 16 simultaneous sensors** on configurable slots: DS18B20 (1-Wire, T),
  DHT22 (Data, T+RH), BME280 (I2C, T+RH+Pressure)
- **16 configurable GPIO slots** — each slot accepts any sensor type;
  configure via CLI before physical wiring (`gpio`, `sensor create`, `sensor pin`)
- **320×240 TFT display** with resistive touch — operation without a PC
- **Complete web interface** (dashboard, history, alarms, configuration)
- **Binary history** (1 point/min, compact format, ~1 year of data in
  1 MB)
- **HTTP/MQTT telemetry** for SCADA / cloud integration
- **Bluetooth Classic SPP** — remote CLI without a cable
- **OTA** auto-flash via web interface (admin-only)
- **Full backup/restore** via API (BKP1 with CRC32 + chip_id matching)
- **Multi-user** (up to 5 accounts with granular permissions)
- **i18n**: PT-BR + EN dynamic

### 1.2 Typical Scenarios

| Setpoint | Environment | Typical Range |
|---|---|---|
| -80 °C | Biobank ultra-freezer | ≤ -65 °C |
| -30 °C | Plasma freezer | -35 to -25 |
| -20 °C | Vaccine freezer | -25 to -15 |
| +5 °C | Vaccine/medication refrigerator | +2 to +8 |
| +25 °C | BOD incubator | +24 to +26 |
| +37 °C | Microbiological culture incubator | +36 to +38 |
| +37 °C | Inactivation water bath | +36 to +38 |
| +45 °C | Coagulation water bath | +44 to +46 |

### 1.3 Alarm Management

Each sensor has **independent min/max** (T and RH). When a value goes
out of range:

- Audible beep on the SIMUT buzzer
- Colored banner on the dashboard
- Full-screen alarm with timestamp + value
- Permanent record in history (`alarm_open`/`alarm_close`)
- Optional trigger via HTTP/MQTT telemetry

---

## 2. Hardware

### 2.1 Official Components

| Component | Model | Function |
|---|---|---|
| MCU | Raspberry Pi Pico W (RP2040 + CYW43439) | Processing + WiFi/BT |
| Display | TFT 320×240 ILI9341 SPI | Main UI |
| Touch | XPT2046 SPI | Display interaction |
| T Sensor | DS18B20 1-Wire | Individual points (up to 16) |
| T+RH Sensor | DHT22 Data | Ambient / individual points (up to 16) |
| T+RH+Pressure Sensor | BME280 I2C | Environmental (up to 8, 2 GPIOs each) |
| Buzzer | PIO active buzzer | Audible alarms |
| Storage | LittleFS on internal flash (1.5 MB usable) | Config + history |

### 2.2 Pinout (Pico W)

| GPIO | Function |
|---|---|
| 0–15 | **SLOT 0–15** — configurable via CLI. Any sensor type: DS18B20 (1-Wire), DHT22 (Data), or BME280 (I2C SDA+SCL). Assign with `sensor create` + `sensor pin`. |
| 16 | TFT MISO |
| 17 | TFT CS |
| 18 | TFT SCK |
| 19 | TFT MOSI |
| 20 | TFT DC |
| 21 | TFT RST |
| 22 | Touch CS |
| 26 | Buzzer (PIO) |
| 27 | Touch IRQ |
| 28 | Reserved |

### 2.3 Power Supply

- **USB-C** (5 V, sufficient for Pico W + display + sensors)
- **Average consumption**: 80–120 mA @ 5 V (display active, WiFi
  connected)
- **Peak**: ~250 mA (telemetry burst + TFT render)

Recommendations:

- Use a quality USB cable (≥22 AWG) — voltage drops cause reboots
- In production, connect to a UPS or no-break to survive short outages
- Don't use USB hubs without external power

---

## 3. First Access

### 3.1 Initial Boot

1. **Connect the USB cable** to the Pico W. The board's green LED
   blinks; after ~30 s the display shows the dashboard.
2. **Without WiFi configured**: TFT shows `IP: (no network)` in the
   header. Go directly to AP mode (next step).
3. **AP Mode**: hold your finger on the screen during boot for **3
   seconds** when the progress bar appears. SIMUT becomes an Access Point
   `SIMUT-config-XXXX`. Connect with your phone/laptop. AP password:
   shown on the display.
4. **Configure WiFi**: navigate to `http://192.168.4.1`, go to
   "Network", fill in your network SSID and password, save.
5. **Reboot**: the device reboots in client mode. The display shows the
   assigned DHCP IP (e.g., `192.168.3.195`).

### 3.2 First Password

The factory password is a **one-time OTP** shown on the display after
`factory reset` (or on first boot if LFS is pristine). Write it down.

1. Access `http://<SIMUT-IP>` in the browser
2. Login with `admin` + OTP
3. System **forces password change** on the first session
4. Set a strong password (min. 8 characters recommended)

> **Forgot password?** Reset via serial CLI: `conf system admin reset
> confirm` → capture new OTP on the terminal.

### 3.3 mDNS

After WiFi association, access via `http://simut.local` instead of the
IP — works on macOS, Linux with `avahi-daemon`, Windows with Bonjour
Print Services. Useful when the IP is DHCP and may change.

---

## 4. TFT Display — Screens and Touches

SIMUT has **9 main screens** accessible via touch or via CLI command
`screen <code>`. Each screen has specific touchable buttons/areas.

### 4.1 Dashboard (Home Screen)

![Dashboard TFT](screenshots_v4/tft_01_dashboard.png){ width=70% }

Main screen shown at boot. Displays:

- **Header**: current time, IP, WiFi icon (RSSI), BT icon, firmware
  version
- **Ambient panel**: T (°C) + RH (%) from SHT31 — updates every 2 s
- **Active slot panel**: T from the selected DS18B20 sensor, customizable
  name, status (OK / out-of-range / no reading)
- **Bottom buttons**: slot navigation (◄ / ►), graph icon, menu/settings
  icon

**Available touches:**

| Area | Action |
|---|---|
| Ambient panel (left) | Toggle ambient min/max |
| Slot panel (right) | Toggle active slot min/max |
| ◄ button (bottom-left) | Previous slot |
| ► button (bottom-right) | Next slot |
| Graph icon (bottom center) | Opens active slot graph |
| Menu icon (top-right) | Goes to authentication → Settings |
| Hold 3s on screen during boot | AP Mode (to reconfigure WiFi) |

### 4.2 Settings (Main Menu)

![Settings TFT](screenshots_v4/tft_02_settings_main.png){ width=70% }

Accessible via touch on the menu icon on the dashboard (after PIN/
password authentication). Lists options:

- **Sounds** — volume, melodies, mute
- **Languages** — PT-BR / EN
- **Password** — change local admin PIN (see §4.5)
- **Touch Cal** — recalibrate resistive touch (8 points)
- **Touch Sens** — touch sensitivity
- **Themes** — display color scheme
- **Alarms** — manage setpoints and activation
- **System Status** — technical info (heap, uptime, etc.)
- **License** — product key
- **Reboot** — restart the device

**Touches:**

| Area | Action |
|---|---|
| Menu item | Opens sub-screen |
| ▼ button (scroll) | Next page of options |
| ◄ button (bottom-left) | Returns to dashboard |

### 4.3 Settings → Themes

![Themes TFT](screenshots_v4/tft_03_settings_themes.png){ width=70% }

Lists pre-installed themes + custom themes (uploadable via web). Each
theme is a `.thm` in `/themes/` on LFS.

- Current theme marked with ✓
- Touch any theme to apply immediately
- "Applying theme..." appears for ~200 ms while the display redraws

### 4.4 Settings → Language

![Language TFT](screenshots_v4/tft_04_settings_language.png){ width=70% }

Selection between **Português (Brasil)** and **English**. Immediate
change + persistent after reboot. Lang packs in
`/lang/language_pt-BR.lng` (~22 KB) and `/lang/language_en.lng` (not
included by default — upload via web).

### 4.5 Settings → Password

![Password TFT](screenshots_v4/tft_05_settings_password.png){ width=70% }

Change the PIN for accessing Settings via touch. Different from the web
admin password (this one is a short numeric PIN for convenience on the
display). Default: `1234`.

This password **only protects the Settings menu via touch**. Web and CLI
use separate credentials.

### 4.6 Settings → License

![License TFT](screenshots_v4/tft_06_settings_license.png){ width=70% }

Shows the installed **license key** and status (valid, expired, demo
mode). The key is a unique identifier tied to the RP2040 chip_id — for
installation control in regulated environments.

### 4.7 System Status

![System Status TFT](screenshots_v4/tft_07_system_status.png){ width=70% }

Real-time technical snapshot:

- Firmware version
- Uptime
- Free / total / largest block heap
- IP, RSSI, MAC
- Active sensors
- Telemetry (sent, failures, retries)
- Storage (FS used/total)

Useful screen for quick debugging — if RSSI < -80 dBm, WiFi signal is
weak; if heap < 20 KB, there's a leak or fragmentation.

### 4.8 Settings → Alarms

![Alarms TFT](screenshots_v4/tft_08_settings_alarms.png){ width=70% }

Lists all sensors with:

- Tmin / Tmax range
- RHmin / RHmax range (SHT31 only)
- Alarm status (ON/OFF)

**Touch the sensor** to edit limits:

- + / − buttons increment ±0.5 °C (T) or ±1% (RH)
- Hold on + or − = repeat (accelerates after 1 s)
- "Save" button persists to flash (implicit write memory)
- "Cancel" button discards changes

### 4.9 Graph

![Graph TFT](screenshots_v4/tft_09_graph.png){ width=70% }

Time plot of the last N samples (default 200 points = ~3.3 h at 1 min
interval).

**Touches:**

| Area | Action |
|---|---|
| Touch and drag horizontally | Pan in time |
| Double tap | Zoom in (next level: 1d, 7d, 30d) |
| ⊞ button (top-right) | Stats (min/max/avg/deviation) |
| ◄ button (bottom-left) | Returns to dashboard |

---

## 5. Web Interface

Access via browser at `http://<SIMUT-IP>` or `http://simut.local`.
Compatible with Chrome, Firefox, Safari, Edge.

### 5.1 Login

![Web Login](screenshots_v4/web_01_login.png){ width=85% }

- **User** field: username (default admin)
- **Password** field: web password (≠ display PIN)
- **Sign In** button: authenticates
- **Reset password** link: generates OTP via CLI (not via web — security)

After 5 consecutive failed attempts from the same IP, 30 s lockout
(exponential). Trying from another IP/computer resets that IP's counter.

> **Technical gotcha:** the browser hashes the password with hexadecimal
> SHA-256 **before** sending via POST. If accessing via raw curl, you
> need to hash on the client first — otherwise you get HTTP 401 even
> with the correct password.

### 5.2 Web Dashboard

![Web Dashboard](screenshots_v4/web_02_dashboard.png){ width=85% }

Real-time overview:

- Header with device name, time, WiFi/BT status
- Cards for each sensor: current T, RH (if SHT31), colored status bar
  (green=OK, yellow=warning, red=alarm)
- Mini-graph of recent samples
- **Force sync telemetry** button (admin): forces immediate upload

### 5.3 History

![Web History](screenshots_v4/web_03_history.png){ width=85% }

Table of stored readings:

- Filters: start date, end date, sensor, alarms only
- Pagination (50 rows per page)
- **Export CSV** button: downloads the filtered range
- **Export JSON** button: same in structured format
- "Alarm" column marked when the point triggered an alarm

### 5.4 Alarms

![Web Alarms](screenshots_v4/web_04_alarms.png){ width=85% }

- Table with all historical `alarm_open` / `alarm_close` events
- Filters by sensor, time range
- **Duration** column calculated automatically
- **Acknowledge** button (admin): marks as acknowledged (doesn't silence,
  only records)

### 5.5 Configuration

![Web Config](screenshots_v4/web_05_config.png){ width=85% }

Screen with ALL persistent device configs:

- **System**: name, timezone, NTP, language, theme, history interval
- **Sensors**: friendly name per slot, ROM↔slot mapping, T/RH ranges
- **Telemetry**: server, port, path, batch size, interval, format
  (JSON/CSV/custom)
- **Calibration**: per-sensor offset (reading correction)

Save persists in RAM. **"Apply & Reboot"** button does `write memory` +
`reload`. Some changes (e.g., web port) only take effect after reboot.

### 5.6 Network

![Web Network](screenshots_v4/web_06_network.png){ width=85% }

- WiFi: SSID, password, mode (DHCP/static)
- Static IP (if selected): IP, mask, gateway, DNS
- mDNS: hostname (default `simut`)
- Reset → AP mode (emergency button)

### 5.7 Users

![Web Users](screenshots_v4/web_07_users.png){ width=85% }

Manages up to 5 users with granular permissions (admin only):

| Permission | Bit | Allows |
|---|---|---|
| `PERM_DASHBOARD` | 0x01 | View dashboard |
| `PERM_HISTORY` | 0x02 | View/export history |
| `PERM_LOGS` | 0x04 | View system logs |
| `PERM_SYS_CONFIG` | 0x08 | Change system configs |
| `PERM_NET_CONFIG` | 0x10 | Change network configs |
| `PERM_FILE_READ` | 0x20 | List/download LFS files |
| `PERM_FILE_UPLOAD` | 0x40 | Upload LFS files |
| `PERM_FILE_DELETE` | 0x80 | Delete LFS files |
| `PERM_USER_MGR` | 0x100 | Create/remove users |
| `PERM_CALIB` | 0x200 | Sensor calibration |
| `PERM_FULL_ADMIN` | 0xFFFF | EVERYTHING + destructive OTA |

**OTA is restricted to `PERM_FULL_ADMIN`** (slot 0 admin only) since
v1.0.0. Other users with `PERM_FILE_UPLOAD` can backup/restore but not
update firmware.

### 5.8 Files

![Web Files](screenshots_v4/web_08_files.png){ width=85% }

Manages the internal LittleFS filesystem:

- List of folders + files with size
- **Upload** (PERM_FILE_UPLOAD): send .lng, .thm, .csv, etc.
- **Download** (PERM_FILE_READ): download any file
- **Delete** (PERM_FILE_DELETE)
- **Backup** (any auth): download complete `.bkp` of the ENTIRE LFS
- **Restore** (PERM_FILE_UPLOAD): apply saved `.bkp`
- **Firmware** (admin only): OTA — replaces the app `.bin`

### 5.9 License

![Web License](screenshots_v4/web_09_license.png){ width=85% }

View license key + register/upgrade (admin only). For use in regulated
environments requiring installation auditing.

---

## 6. CLI via USB Serial

The most robust way to configure SIMUT — **independent of WiFi and web
UI**. Works even in recovery mode.

### 6.1 Connection

Connect the Pico W USB cable to the computer. `/dev/ttyACM0` (Linux/
macOS) or `COMx` (Windows) appears. Use any serial terminal:

| System | Recommended Terminal |
|---|---|
| Linux | `picocom -b 115200 /dev/ttyACM0`, `screen`, `minicom` |
| macOS | `screen /dev/cu.usbmodemXXXX 115200` |
| Windows | PuTTY (Connection type=Serial, Speed=115200, COMx) |

Parameters: **115200 8N1, no flow control**.

### 6.2 Prompt and Modes

- `SIMUT>` — silent mode (debug off)
- `SIMUT#` — verbose mode (debug on, shows real-time logs)

Toggle: `debug on` / `debug off`.

### 6.3 Commands by Category

#### Language

```
language pt          Portuguese (Brazil)
language en          English
language             Show current
```

#### Monitoring

```
show system info     Name, version, config
show system log      Dump binary log from flash
show storage stats   FS used/total
show net status      IP, RSSI, NTP
show themes          List themes
show metrics         Heap, uptime, telemetry, sensors, storage
show sensors         List configured slots with type, channels, alarms
show sensor types    List compiled-in sensor drivers (DS18B20, DHT22, BME280)
gpio                 GPIO resource map (16 pins, free/used by slot)
show gpio            Same as above
```

#### Sensor Diagnostics

```
sensor scan          Scan for new sensors (1-Wire + I2C)
sensor accept <gpio> Authorize newly detected sensor
sensor wipe <gpio> [confirm]  Wipe slot history
```

#### Slot Configuration (GPIO Resource Management)

```
sensor <n> create <type>   Create slot — sets type, shows pin requirements
sensor <n> type <type>     Change sensor driver (ds18b20|dht22|bme280)
sensor <n> pin <idx>,<gpio>  Assign GPIO to pin (with conflict detection)
sensor <n> name "<name>"   Set friendly name (max 31 chars)
sensor <n> hwid <id>       Set hardware ID (max 15 chars)
sensor <n> active <on|off> Enable/disable slot (validates prerequisites)
```

#### System Configuration

```
conf system name <value>           Friendly name
conf system ssid <name>            WiFi SSID
conf system pass <password>        WiFi password
conf system timezone <offset>      UTC offset (e.g., -3)
conf system ntp <server>           NTP server (empty = default)
conf system theme <id|index>       UI theme
conf system admin reset [confirm]  Generate new admin OTP
conf system touch reset [confirm]  Reset touch cal
conf system factory [confirm]      TOTAL WIPE + reboot
conf system history_interval <min> 1..1440 (default 1)
```

#### Telemetry

```
conf tel server <url>              Server address
conf tel port <n>                  Port
conf tel path <path>               Endpoint
conf tel batch <n>                 Records per upload (max 50)
conf tel interval <ms>             Interval (0=off)
conf tel crypto <on|off>           HTTPS/TLS
conf tel mode <json|csv|custom>    Payload format
tel sync                           Force upload now
tel dump                           Capture next payload on console
```

#### Sensor Mapping (Legacy)

```
sensor define <gpio> <rom> <hwid> "<name>"
  Ex: sensor define 4 28AABB.. STM0001 "Vaccine_Fridge"

Prefer the slot-based commands above for new deployments.
```

#### Static IP Configuration

```
conf ip dhcp                       Return to DHCP
conf ip static                     Enable static mode
conf ip addr <ipv4>                Device IP
conf ip mask <ipv4>                Mask
conf ip gateway <ipv4>             Gateway
conf ip dns <ipv4>                 Primary DNS
conf net dns auto                  DNS via DHCP
conf net dns manual <ip1> [ip2]    Manual DNS
```

#### Sensor Limits

```
conf sensor tmin <gpio> <n>        Minimum T limit
conf sensor tmax <gpio> <n>        Maximum limit
conf sensor hmin <gpio> <n>        Minimum RH (SHT31)
conf sensor hmax <gpio> <n>        Maximum RH
conf sensor alarm <gpio> <on|off>  Enable sensor alarm
conf sensor ds18b20 resolution <9-12>  Global resolution (9..12 bits)
```

#### Users

```
conf user add <name> <pass>        Create new user
conf user del <name>               Remove
conf user pass <name> <newpass>    Change password
```

#### NTP / Time

```
conf ntp on|off                    Enable/disable sync
conf time YYYY-MM-DD HH:MM:SS      Set RTC manually (local time)
```

#### Maintenance

```
write memory                       Persist config RAM → flash
reload [confirm]                   Reboot
clear log [confirm]                Wipe binary log
```

#### Web

```
conf web port <1..65535>           Web server port
```

#### Manual/Automation Debug

```
screen <code>                      Force TFT screen directly (bypass touch).
                                   Codes: dash, set, thm, lng, pwd, lic,
                                          sts, alm, gra
touch sim X Y                      Inject simulated touch at (X, Y).
                                   X 0..319, Y 0..239. Auto-clear ~100ms.
```

### 6.4 Typical Workflow

1. `show system info` — confirm version
2. `show net status` — confirm WiFi
3. `conf system ssid ...` + `conf system pass ...` — configure network
4. `write memory` — persist
5. `reload confirm` — reboot to apply

---

## 7. CLI via Bluetooth

SIMUT exposes the same CLI via **Bluetooth Classic (RFCOMM-SPP)** when
BT is enabled. Useful for configuring the device without a USB cable or
WiFi network.

### 7.1 Pairing

SIMUT appears as `SIMUT-BT-XXXX` (XXXX = last 4 chars of chip_id) in
your device's BT scan.

#### 7.1.1 Linux (bluetoothctl)

```bash
bluetoothctl
> power on
> agent on
> scan on
# Wait until you see SIMUT-BT-XXXX and copy the MAC (e.g., 28:CD:C1:15:4E:99)
> pair 28:CD:C1:15:4E:99
> trust 28:CD:C1:15:4E:99
> exit

# Bind RFCOMM channel
sudo rfcomm bind 0 28:CD:C1:15:4E:99 1

# Connect to terminal
picocom -b 115200 /dev/rfcomm0
```

#### 7.1.2 Windows

1. Settings → Devices → Bluetooth & other devices
2. "Add Bluetooth or other device" → Bluetooth
3. Select `SIMUT-BT-XXXX` and pair
4. After paired, open "More Bluetooth options" → "COM Ports"
5. Note the assigned COM port (e.g., COM5)
6. Open PuTTY → Serial → COM5, 115200 baud

#### 7.1.3 macOS

No native Bluetooth Classic SPP support. Use:

- **CoolTerm** (free) — choose the paired device
- Or third-party apps like **Serial** or **Bluetooth SPP Pro**

#### 7.1.4 Android

Recommended app: **Serial Bluetooth Terminal** (Kai Morich, free):

1. Pair via system Bluetooth (Settings → Bluetooth)
2. Open the app, tap ⋮ → Devices → Bluetooth Classic
3. Select SIMUT-BT-XXXX
4. Tap ▶ to connect
5. Use the CLI normally

#### 7.1.5 iOS

iOS **does not allow** apps to access Bluetooth Classic SPP due to
Apple restrictions. Use macOS or Android.

### 7.2 Authentication

After connecting, SIMUT requires authentication on the first command
(except `help`):

```
SIMUT-BT> auth admin <password>
```

An authenticated BT session remains valid while the connection is open.
After `disconnect`, the next connect requires a new `auth`.

### 7.3 Limitations

- Only **1 simultaneous BT connection** (default BTstack limit)
- Throughput: ~5 KB/s — OK for CLI but slow for large log dumps
- BT Classic has ~10 m range (no LE — cannot use BLE scan/advertising)

---

## 8. OTA — Firmware Update

Update via web interface. **Restricted to admin** (perms = 0xFFFF) since
v1.0.0.

### 8.1 Workflow

1. Web → **Files** → **Firmware** button
2. Warning modal: "OTA reformats LittleFS. Configs preserved;
   history/themes/calib DELETED." Continue if OK.
3. Automatic backup: SIMUT downloads a `.bkp` of the current LFS via
   browser. **Save it!** It's your post-OTA insurance.
4. Select the new firmware `.bin` (generated by your PIO/CLI/CI build)
5. Client-side validation: valid size + boot2 CRC-32/MPEG-2 OK +
   SIMUT_VERSION found
6. Final confirmation with summary (current version → new, size)
7. Upload (~30 s for 1 MB of firmware)
8. Apply: device flashes display "Applying firmware..." and reboots
9. ~60 s later, web returns — login + manual restore of the backup
   downloaded in (3) if you want to recover themes/lang/history

### 8.2 Route Security

- **POST `/api/restore?op=stage` endpoint**: admin pre-check BEFORE
  erasing 1 MB of flash
- **POST `/api/ota/apply` endpoint**: admin-only (response 403
  "Forbidden — admin only" otherwise)
- **GET `/api/ota/staging_test` endpoint**: destructive self-test,
  admin-only

### 8.3 Recovery in Case of Brick

If post-OTA boot fails (rare, but possible):

1. Press and hold **BOOTSEL** on the Pico W
2. Plug in USB (keeping BOOTSEL pressed)
3. `RPI-RP2` volume appears
4. Drag the `.uf2` of the PREVIOUS firmware (v1.0.0 release downloadable
   from GitHub) **OR** use `picotool load -x firmware.uf2`
5. Boot recovers automatically. LFS preserved (OTA erase pads don't
   touch config).

### 8.4 Validated Reliability

- **F-RESTORE CLOSED** (v1.0.0): 98/100 PASS in a 100-iteration OTA +
  restore loop with real config preserved (canonical 455 KB, 32 files,
  29 critical)
- **0 ConnResets**, 0 forced recoveries, 4.2 s avg/restore
- Auto-reboot via `LogManager::safeReboot()` post-apply

---

## 9. Backup and Restore

### 9.1 BKP1 Format

All SIMUT backups are `.bkp` in the custom **BKP1** format:

```
[Header 40 bytes]
  magic         u32     "BKP1" (0x31504B42)
  schema_version u16    1
  reserved      u16
  chip_id       u8[8]   Unique RP2040 ID
  firmware_ver  u32     vM.m.p encoded
  timestamp     u32     Unix epoch UTC
  payload_size  u32
  payload_crc32 u32
  header_crc32  u32
[Payload TLV]
  For each file:
    path_length u16
    content_length u32
    path        char[path_length]
    content     u8[content_length]
```

CRC32 = EDB88320 polynomial (gzip-compatible). Standalone Python
validator: `tools/verify_backup.py`.

### 9.2 Web Operations

| Button (Files) | Endpoint | Permission |
|---|---|---|
| Backup | `GET /api/backup` | any auth |
| Restore | `POST /api/restore?op=apply` | `PERM_FILE_UPLOAD` |
| Firmware (OTA) | `POST /api/restore?op=stage` + `/api/ota/apply` | **admin only** |

### 9.3 curl Operations

```bash
# Login (pre-hash)
PWHASH=$(echo -n "$ADMIN_PASS" | sha256sum | awk '{print $1}')
NONCE=$(curl -s http://192.168.3.195/api/login_init | jq -r '.nonce')
curl -s -c cookies.txt -d "user=admin&pass=$PWHASH&nonce=$NONCE" \
     http://192.168.3.195/api/login

# Backup
curl -s -b cookies.txt -o backup.bkp http://192.168.3.195/api/backup

# Restore
curl -s -b cookies.txt -F "file=@backup.bkp" \
     "http://192.168.3.195/api/restore?op=apply"
```

### 9.4 Best Practice Recommendations

- **Monthly backup** (minimum) per device in production
- Backup **before any critical change** (OTA, factory reset, hardware
  swap)
- Store on **separate media** (not on the same computer that admins
  SIMUT)
- Periodically test restore on a test device
- chip_id in backup ensures restore won't work on a different device —
  for cloning, generate an equivalent config from scratch

---

## 10. Security

Full document: `SECURITY.md` in the repository.

### 10.1 Auth Model

- **Web**: SHA-256(password) hex on client → POST → server compares with
  stored hash (HMAC + salt + 2500 rounds, pepper = unique chip_id)
- **Bluetooth**: `auth admin <password>` command on BT CLI (plaintext
  under BT encryption)
- **USB Serial**: no auth (physical access = trust)
- **Touch display**: short PIN (default 1234) gateway to the Settings
  menu

### 10.2 Lockout

5 failed web login attempts → 30 s lockout (exponential up to 1 h). Per
IP. CLI/BT have no lockout.

### 10.3 OTA Admin-Only

Since v1.0.0: stage + apply require `PERM_FULL_ADMIN`. Other users (with
PERM_FILE_UPLOAD) can backup/restore but **not update firmware**.

### 10.4 Audit Log

All security events recorded in `system.blog` (binary, 800-entry
rotation):

- `LOGIN_OK`, `LOGIN_FAIL`, `LOGOUT`
- `CONFIG_CHANGED` (with user ID + description)
- `ALARM_OPEN`, `ALARM_CLOSE`
- `OTA_STAGE`, `OTA_APPLY`
- `FACTORY_RESET`, `LOG_CLEAR`

View: web → Dashboard → "Logs" tab. Or CLI: `show system log`.

### 10.5 Operational Best Practices

1. **Change admin password** immediately after first boot
2. **Don't share** passwords via WhatsApp/email — use a password manager
   (Bitwarden, 1Password)
3. **Create additional users** with minimum necessary permissions
   (principle of least privilege)
4. **Place SIMUT on an isolated VLAN/subnet** if possible — limits
   exposure
5. **Backup before any destructive action**
6. **Monthly audit log review** — look for repeated `LOGIN_FAIL` events
   (brute force indicator)

---

## 11. Troubleshooting

### 11.1 Display Won't Turn On

- Bad USB cable? Try another one
- Pico W fried? Does the board's green LED blink?
- Corrupted firmware? Recover via BOOTSEL (§8.3)

### 11.2 WiFi Won't Connect

1. `show net status` via serial → shows `IP: (IP unset)` + `RSSI: -100`?
2. Check SSID + password (case sensitive): `conf system ssid` / `conf
   system pass`
3. `write memory` + `reload confirm`
4. If persistent: try AP mode (hold touch 3s on boot) and reconfigure
   via web

### 11.3 mDNS (`simut.local`) Not Responding

- `avahi-daemon` running (Linux)? `systemctl status avahi-daemon`
- Bonjour Print Services installed (Windows)?
- Router blocking multicast (some isolate SSIDs)? Test on another WiFi
- Reboot SIMUT — it can take up to 30 s for mDNS to respond after boot

### 11.4 Forgot Password

- **Web admin**: serial → `conf system admin reset confirm` → capture
  new OTP on terminal → web `/login_chpass` with OTP
- **Display PIN**: serial → `conf system touch reset confirm` (reverts
  to default 1234)
- **Everything**: `conf system factory confirm` (wipes EVERYTHING +
  reboot, new OTP on display)

### 11.5 Sensor Not Appearing

1. `show sensors` — is it mapped?
2. `sensor scan` — is hardware detected?
3. 1-Wire cable OK? DS18B20 needs 4.7 kΩ pull-up on data line
4. Correct ROM address? `sensor define <gpio> <rom_hex> <slot> "<name>"`

### 11.6 Telemetry Not Sending

- `show metrics` shows `Failures` > 0? See CLI debug logs
- Server reachable? `ping <server>` on host
- Correct endpoint? `conf tel server <url>` + `conf tel path <path>`
- Invalid SSL cert? Try `conf tel crypto off` (plain HTTP) to test
- `tel sync` — forces immediate attempt and shows detailed error

### 11.7 OTA Failed

1. Symptom: device offline post-flash
2. Recovery via BOOTSEL (§8.3)
3. Restore backup (Files → Restore with .bkp saved pre-OTA)

### 11.8 Low Heap / OOM

- `show metrics` → Heap < 20 KB?
- Reboot resolves short-term
- Root cause: leak (report via GitHub Issues with `show system log`)

### 11.9 Erratic Touch

- Recalibrate: Settings → Touch Cal → follow instructions (8 points)
- If persistent: `conf system touch reset confirm` (sane default) + cal
  again
- Hardware: surface protective film may cause offset

### 11.10 Advanced Debug via UART1 (GP8/GP9)

If SIMUT's USB-CDC freezes (`F-USB-CDC-DEAD`), use UART1 via another
Pico (PicoHand) or USB-UART adapter:

- Wiring: SIMUT GP8 (TX) → adapter RX, common GND
- Baud: 115200
- Useful for capturing post-watchdog boot logs when USB CDC goes mute

---

## 12. Appendices

### A. Quick Reference — CLI

```
# Status
help                      List commands
show system info          Version + config
show net status           IP + RSSI
show metrics              Heap + telemetry + sensors
show sensors              List sensors
show storage stats        FS used/total

# Basic config
conf system name "X"      Friendly name
conf system ssid "net"    WiFi SSID
conf system pass "1234"   WiFi password
conf system timezone -3   UTC offset
conf system theme 2       Theme idx
write memory              Persist
reload confirm            Reboot

# Sensors
sensor scan               Detect new
sensor accept <gpio>      Authorize
conf sensor tmin 4 -25    Tmin for GPIO 4 sensor
conf sensor tmax 4 -15    Tmax
conf sensor alarm 4 on    Enable alarm

# Reset
conf system admin reset confirm   New OTP
conf system touch reset confirm   Default PIN
conf system factory confirm       WIPE EVERYTHING

# Automation (debug)
screen <code>             Force TFT screen
touch sim X Y             Simulate touch

# Screen codes
dash  = Dashboard
set   = Settings menu
thm   = Themes
lng   = Language
pwd   = Password (PIN)
lic   = License
sts   = System status
alm   = Alarms
gra   = Graph
```

### B. Permission Bitmask

```
PERM_DASHBOARD    0x0001  View dashboard
PERM_HISTORY      0x0002  View/export history
PERM_LOGS         0x0004  View logs
PERM_SYS_CONFIG   0x0008  Change configs
PERM_NET_CONFIG   0x0010  Change network
PERM_FILE_READ    0x0020  List/download files
PERM_FILE_UPLOAD  0x0040  Upload files
PERM_FILE_DELETE  0x0080  Delete files
PERM_USER_MGR     0x0100  Manage users
PERM_CALIB        0x0200  Calibration
PERM_FULL_ADMIN   0xFFFF  Full admin + OTA
```

### C. BKP1 Status Codes

```
0  OK
1  Bad magic
2  Unsupported schema
3  Header CRC mismatch
4  Payload truncated
5  Payload CRC mismatch
6  chip_id mismatch (backup from another device)
7  Invalid path
8  Path too long
9  I/O error
10 Internal error
```

### D. Main HTTP Endpoints

```
GET  /api/login_init          Request nonce (auth)
POST /api/login               Login (user, pass=sha256, nonce)
POST /api/login_chpass        Change password (oldpass, newpass)
GET  /api/perms               Current session permissions
GET  /api/info                Device snapshot (json)
GET  /api/screenshot          BMP 320x240 of display
GET  /api/screenshot_chunk?n  16-row chunk with CRC32
GET  /api/backup              Full LFS .bkp
POST /api/restore?op=validate Validate .bkp without applying
POST /api/restore?op=apply    Restore .bkp (PERM_FILE_UPLOAD)
POST /api/restore?op=stage    Firmware staging (admin only)
POST /api/ota/apply           Apply firmware (admin only)
GET  /api/files               List LFS files
POST /api/upload?uploadDir=   File upload
GET  /download?file=          Download
POST /api/delete?file=        Delete
POST /api/mkdir               mkdir
POST /api/commit_all          Save config (sys, alarms)
POST /api/set_time            Set RTC manually
```

### E. Online Resources

- **Repository**: https://github.com/angeloINTJ/SIMUT
- **Issues + support**: GitHub Issues
- **Releases (binaries)**: https://github.com/angeloINTJ/SIMUT/releases
- **Technical documentation**: `STABILITY_PLAN.md`, `SECURITY.md` in the
  repo

---

**End of manual — v1.0.0 —**
