/**
 * @file HelpLicenseEN.h
 * @brief EN texts inline in PROGMEM — default fallback without LittleFS dependency.
 * @details Previously in data/help_en.txt and data/license_en.txt (LittleFS),
 * now embedded in firmware. Not accessible to user via /files.
 * PT versions come from /lang/language_<code>.lng (.lng parser).
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT
 */
#pragma once
#include <Arduino.h>
#include "SystemDefs_Cli.h" /* SIMUT_CLI_FULL — picks which help text is built */

#if !SIMUT_CLI_FULL
/* Emergency console (see SIMUT_CLI_FULL in SystemDefs_Cli.h). Every setting
 * moved to the web UI; what is left is the way back when the web cannot be
 * reached. Keeping the full 3.3 KB of help text for nine commands would cost
 * more flash than the commands themselves. */
static const char HELP_TEXT_EN[] PROGMEM = R"raw(

===========================================
 SIMUT - EMERGENCY CONSOLE
===========================================
 Settings live in the web interface.
 This console is the way back when the
 web cannot be reached.
 Destructive cmds need ' confirm'
 (e.g., 'reload confirm').

show net status
 IP, RSSI, time sync - find the device
show system info
 Name, firmware, serial, WiFi SSID
show system log
 Dump the event log from flash
debug on
 Stream logs to this console live
debug off
 Stop streaming
system admin reset [confirm]
 New random admin password, printed
 once on this console
system format [confirm]
 Reformat LittleFS, keep firmware
 (recovers a corrupt filesystem)
system factory [confirm]
 Wipe ALL config + reboot
system https off [confirm]
 Disable HTTPS (delete cert), back to HTTP
 (recovers a web locked by a bad TLS pair)
system ssid <name>
 WiFi network name (SSID)
system pass <pass>
 WiFi password
system cors <origin|off>
 Let the web fleet manager reach this
 device from a browser, e.g.
 system cors http://192.168.1.10:8080
reload [confirm]
 Reboot now
ap
 Start AP mode (access point) for setup
time <YYYY-MM-DD> <HH:MM:SS>
 Set the clock by hand (no network)
)raw"
/* The `air` commands exist only in the Air build. Advertising them from the
 * release and alpha images sent users after commands their firmware answers
 * with "unknown". Two adjacent raw literals concatenate at compile time, so the
 * block below costs those images nothing at all. */
#if SIMUT_AIR
R"raw(air stop
 Cancel hibernation and return to M0
air idle <sec>
 Set auto-hibernate idle timeout
air charger <gpio|off>
 Line that reads high while charging (stays awake)
air hibernate
 Enter hibernation now (SIMUT Air)
air status
 Show Air config + current phase
)raw"
#endif
R"raw(===========================================
)raw";
#else
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
 List configured slots with type, channels, alarms
show sensor types
 List sensor drivers compiled in firmware
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
sensor <slot> type <ds18b20|dht22|bme280>
 Set sensor driver type
sensor <slot> name <name>
 Set friendly name (max 31 chars)
sensor <slot> hwid <id>
 Set hardware ID (max 15 chars)
sensor <slot> active <on|off>
 Enable or disable slot
sensor <slot> pin <idx>,<gpio>
 Assign GPIO to pin index (0-3)
 Ex: sensor 5 pin 0,5  -> slot 5 uses GPIO5
 Ex: sensor 5 pin 1,6  -> adds GPIO6 as 2nd pin
sensor define <gpio> <rom> <hwid> \"<name>\"
 Legacy: full sensor definition with ROM
 Ex: sensor define 0 28AA.. S1 \"Oven_Top\"

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

-- 7. IP / SENSOR LIMITS / USERS / WEB --
conf ip <dhcp|static>
conf ip <addr|mask|gateway|dns> <ipv4>
sensor <slot> <tmin|tmax|hmin|hmax> <n>
 Set alarm thresholds per slot
sensor <slot> alarm <on|off>
 Toggle alarm per slot
conf sensor <tmin|tmax|hmin|hmax> <gpio> <n>
 Legacy: same as above with 'conf' prefix
conf user add <name> <pass>
conf user del <name>
conf user pass <name> <newpass>
conf web port <1..65535>
===========================================
)raw";
#endif /* SIMUT_CLI_FULL */

/* The License screen of the panel, since 2026-10-02: an opening in the
 * language of the installed pack, then the MIT text in its original English,
 * then every piece of third-party software in the images — the same three
 * parts, in the same order, as the /license web page. The opening of a pack
 * comes from its @WEBDICT, the very strings the page shows; without a pack it
 * is the English below. Both parts are UTF-8: the screen folds each word to
 * the classic CP437 font as it draws it, so this file spells the copyright
 * holder like every other copy does. */
/* BEGIN generated: licence opening — tools/gen_notices.py */
/* The English text of the /license page, which the screen shows when no
 * language pack is loaded. The page itself is the source: edit WebUI.h. */
static const char LIC_OPEN_SUB[] PROGMEM = "Free software. Below, what that means in practice.";
static const char LIC_OPEN_TITLE[] PROGMEM = "In short";
static const char LIC_OPEN_SUMMARY[] PROGMEM = "You may use, copy, modify, merge, publish, distribute, sublicense and sell copies of this software, free of charge and without restriction.\n\nThe only obligation is to keep the copyright notice and the permission notice in every copy.\n\nThe software is provided \"as is\", without warranty of any kind. The author is not liable for any damage arising from its use.";
static const char LIC_OPEN_NOTE[] PROGMEM = "A plain-language summary, written to be understood. It does not replace the licence text.";
static const char LIC_OPEN_LEGAL[] PROGMEM = "The text below stays in English because it is the original — that is the version with legal force.";
static const char* const LICENSE_OPENING_EN[] = {
	LIC_OPEN_SUB, LIC_OPEN_TITLE, LIC_OPEN_SUMMARY, LIC_OPEN_NOTE, LIC_OPEN_LEGAL,
};
static const char* const LICENSE_OPENING_KEYS[] = {
	"lic_sub", "lic_summary_title", "lic_summary", "lic_summary_note", "lic_legal_note",
};
/* END generated: licence opening */

#if defined(SIMUT_LICENSE_STUB)
/* Profiles that set license_stub in tools/features.toml (the two test images,
 * and SIMUT Air). The only reader is the touch display's License screen, which
 * draws it after the opening above: there it saves 1,728 B against the full
 * text below (SIMUT, measured 2026-09-26 by tools/measure_savings.py --matrix).
 * Without the touch display nothing reads the string, so on Air the switch
 * changes nothing. The release image always carries the complete text below;
 * the /license web page is separate and complete in every image. */
static const char LICENSE_TEXT_EN[] PROGMEM =
"MIT License - the full text ships in the release image,\n"
"and with the third-party notices in the source repository.\n";
#else
/* The list after the MIT text is generated from tools/third_party.toml, the
 * same list THIRD_PARTY_NOTICES.md and the /license page carry; the script
 * checks in CI that the three agree. */
static const char LICENSE_TEXT_EN[] PROGMEM = R"raw(MIT License

Copyright (c) 2026 Ângelo Moisés Alves

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

)raw"
/* BEGIN generated: third-party list — tools/gen_notices.py */
R"raw(--- Third-party software ---

Arduino-Pico core
  Earle F. Philhower, III - LGPL-2.1
Raspberry Pi Pico SDK
  Raspberry Pi (Trading) Ltd. - BSD-3-Clause
TinyUSB
  Ha Thach (tinyusb.org) - MIT
cyw43-driver
  George Robotics Pty Ltd - Raspberry Pi licence (LICENSE.RP)
CYW43439 radio firmware
  Infineon Technologies (Cypress) - binary, shipped in cyw43-driver
lwIP
  Swedish Institute of Computer Science - BSD-3-Clause
BearSSL
  Thomas Pornin - MIT
LittleFS
  Arm Limited and the littlefs authors - BSD-3-Clause
MicroPython DHCP server
  Damien P. George - MIT
LEAmDNS
  LaborEtArs - MIT
newlib
  Red Hat, Inc. and others - BSD-style (COPYING.NEWLIB)
GCC runtime libraries
  Free Software Foundation, Inc. - GPL-3.0 with the GCC Runtime Library Exception
Adafruit GFX Library
  Adafruit Industries - BSD-2-Clause
Adafruit ILI9341
  Adafruit Industries (Limor Fried) - BSD
XPT2046_Touchscreen
  Paul Stoffregen - MIT
PubSubClient
  Nicholas O'Leary - MIT
OneWirePIO_RP2040
  Ângelo Moisés Alves - MIT
DHT22PIO_RP2040
  Ângelo Moisés Alves - MIT
BuzzerPIO_RP2040
  Ângelo Moisés Alves - MIT
TwoWirePIO_RP2040
  Ângelo Moisés Alves - MIT
BMx280PIO_RP2040
  Ângelo Moisés Alves - MIT
BTstack
  BlueKitchen GmbH - Raspberry Pi licence (LICENSE.RP)
GNU FreeFont (FreeSans Bold)
  GNU FreeFont contributors - GPL-3.0-or-later with the font exception
Liberation Sans Bold
  Red Hat, Inc. (digitized data: Google, Arimo) - SIL Open Font License 1.1
SHA-256 for JavaScript
  Geraint Luff - public domain

Licence texts: THIRD_PARTY_NOTICES.md and
LICENSES/ in the source repository.
)raw"
/* END generated: third-party list */
R"raw(
SIMUT v3 - Made in Brazil)raw";
#endif /* SIMUT_LICENSE_STUB */
