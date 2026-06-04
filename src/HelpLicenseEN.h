/**
 * @file HelpLicenseEN.h
 * @brief EN texts inline in PROGMEM — default fallback without LittleFS dependency.
 * @details Previously in data/help_en.txt and data/license_en.txt (LittleFS),
 * now embedded in firmware. Not accessible to user via /files.
 * PT versions come from /lang/language_<code>.lng (.lng parser).
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT
 */
#pragma once
#include <Arduino.h>

static const char HELP_TEXT_EN[] PROGMEM = R"raw(

===========================================
 SIMUT - COMMAND HELP
===========================================
 Destructive cmds need ' confirm' suffix
 (e.g., 'reload confirm').
 Commands themselves stay in English.

-- LANGUAGE / IDIOMA --
language pt
 Use Portuguese (Brazil)
language en
 Use English
language
 Show current language
 Note: 'write memory' to persist

-- 1. MONITORING --
show system info
 Device name, version, config
show system log
 Dump event log from flash
show storage stats
 Flash usage statistics
show net status
 IP, RSSI, time sync
show themes
 List available UI themes
show metrics
 Operational metrics (heap, net, tel, sensors)

-- 2. SENSOR DIAGNOSTICS --
show sensors
 List mapped sensors (database)
sensor scan
 Hardware scan for new sensors

-- 3. CONFIGURATION --
 (needs 'write memory' + 'reload')
conf system name <value>
 Set device friendly name
conf system ssid <name>
 WiFi SSID (case sensitive)
conf system pass <pass>
 WiFi password
conf system timezone <offset>
 UTC offset (e.g., -3)
conf system ntp <server>
 NTP server (empty = default)
conf system theme <id|index>
 Set UI theme
conf system admin reset [confirm]
 Reset admin password to default
conf system touch reset [confirm]
 Reset touch calibration
conf system factory [confirm]
 Factory reset (wipes ALL config) + reboot
conf system history_interval <min>
 History recording interval (1..1440 min, default 1)
conf ntp <on|off>
 Enable/disable NTP sync
conf time <YYYY-MM-DD> <HH:MM:SS>
 Set RTC manually (immediate; local time)
conf net dns auto
 DNS via DHCP (default)
conf net dns manual <ip1> [ip2]
 Manual DNS: primary and secondary (optional)
conf sensor ds18b20 resolution <9-12>
 DS18B20 global resolution

-- Telemetry --
conf tel server <url>
 Server address
conf tel port <port>
 Server port (80, 443, ...)
conf tel path <path>
 Endpoint path (/api/v1/data)
conf tel batch <n>
 Records per upload (max 50)
conf tel interval <ms>
 Auto-upload interval (0=off)
conf tel crypto <on|off>
 Enable SSL/HTTPS
conf tel mode <json|csv|custom>
 Payload format

-- 4. SENSOR MAPPING --
sensor define <gpio> <rom> <hwid> \"<name>\"
 Ex:
 sensor define 0 28AA.. S1 \"Oven_Top\"
 Note: GPIO 10 = Ambient Sensor

-- 5. MAINTENANCE --
sensor accept <gpio>
 Authorize new physical sensor
sensor wipe <gpio> [confirm]
 Reset graph history for slot
tel sync
 Force telemetry upload
tel dump
 Arm one-shot dump of next payload to console (USB+BT)
tel reset
 Reset telemetry cursor (cache RAM + flash file). Re-sends up to 30 days back.
clear log [confirm]
 Delete system log file
write memory
 Persist RAM config to flash
reload [confirm]
 Reboot system

-- 6. SESSION MODE --
debug on
 Stream logs to console (SIMUT#)
debug off
 Quiet console, cmds only (SIMUT>)
debug
 Show current mode
 Note: 'write memory' to persist
===========================================

-- 7. IP / SENSOR LIMITS / USERS / WEB --
conf ip <dhcp|static>
conf ip <addr|mask|gateway|dns> <ipv4>
conf sensor <tmin|tmax|hmin|hmax> <gpio> <n>
conf sensor alarm <gpio> <on|off>
conf user add <name> <pass>
conf user del <name>
conf user pass <name> <newpass>
conf web port <1..65535>
===========================================
)raw";

static const char LICENSE_TEXT_EN[] PROGMEM = R"raw(
MIT License

Copyright (c) 2026 Angelo Moises Alves

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

--- Acknowledgments ---

Arduino-Pico Core
 Earle F. Philhower III - LGPL-2.1

Raspberry Pi Pico SDK
 Raspberry Pi Ltd - BSD-3

Adafruit GFX Library
 Adafruit Industries - BSD-2

Adafruit ILI9341
 Adafruit Industries - BSD-2

XPT2046 Touchscreen
 Paul Stoffregen - MIT

LittleFS
 ARM Ltd / C. Haster - BSD-3

PubSubClient (MQTT)
 Nick O'Leary - MIT

BearSSL
 Thomas Pornin - MIT

GNU FreeFont (FreeSans)
 GNU Project - GPL-3 + Font Exception

OneWirePIO RP2040
 Angelo M. Alves - MIT

DHT22PIO RP2040
 Angelo M. Alves - MIT

BuzzerPIO RP2040
 Angelo M. Alves - MIT

SIMUT v3 - Made in Brazil)raw";
