/**
 * @file simut_config.h
 * @brief Centralized hardware configuration for SIMUT.
 *
 * **THIS IS THE ONLY FILE YOU NEED TO EDIT** to customize your SIMUT device.
 * Every pin assignment, sensor enable/disable, and feature flag lives here.
 *
 * @section howto How to Customize
 * 1. Read the comments for each section below.
 * 2. Change the `#define` values as needed.
 * 3. Recompile and upload.
 *
 * @section pio PlatformIO Users
 * You may also override any flag via `platformio.ini` `build_flags`
 * (e.g. `-DSIMUT_SENSOR_DS18B20=0`). Those flags take **precedence**
 * over the defaults below.
 *
 * @section arduino Arduino IDE Users
 * This file is included automatically. If you use a release package,
 * edit `simut_arduino_config.h` to override values **before** this file
 * is processed (`#define` before `#include "simut_config.h"`).
 *
 * @section gpio GPIO Reference (Raspberry Pi Pico W)
 * ```
 * GP0–GP15   → Sensor slots (runtime-configurable via CLI); in a LAN build
 *              GP11–GP15 are the ENC28J60: its INT and an SPI made in PIO (10b)
 * GP16       → SPI0 MISO (TFT) / HD44780 RS (Alpha parallel)
 * GP17       → SPI0 CS (Touch) / HD44780 EN (Alpha parallel)
 * GP18       → SPI0 SCK / HD44780 D4 (Alpha parallel)
 * GP19       → SPI0 MOSI / HD44780 D5 (Alpha parallel)
 * GP20       → Touch IRQ / HD44780 D6 (Alpha parallel)
 * GP21       → HD44780 D7 (Alpha parallel)
 * GP22       → Buzzer (PIO-driven)
 * GP26–GP27  → I2C1 (HD44780 backpack in Alpha I2C mode)
 * GP28       → TFT CS
 * ```
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

// Arduino IDE: if simut_arduino_config.h exists, process it first.
// Its #defines take effect before the #ifndef guards below.
#if __has_include("simut_arduino_config.h")
#include "simut_arduino_config.h"
#endif

/* =========================================================================
 * SECTION 1: DISPLAY TYPE
 *
 * Choose ONE display type. Set the other to 0.
 *
 * SIMUT_DISPLAY_TFT   — ILI9341 320x240 TFT + XPT2046 resistive touch
 *                       Full GUI: dashboard, graphs, settings, themes, auth,
 *                       alarm, calendar, touch calibration.
 *                       Flash: ~909 KB (87% of 1 MB)
 *
 * SIMUT_DISPLAY_ALPHA — HD44780 16x2 alphanumeric LCD (character display)
 *                       Text-only: large digits, sensor cycling, boot
 *                       progress bar, WiFi status.
 *                       Flash: ~817 KB (78% of 1 MB)
 * ========================================================================= */

#ifndef SIMUT_DISPLAY_TFT
#define SIMUT_DISPLAY_TFT 1    // ILI9341 TFT + XPT2046 touch (default)
#endif

#ifndef SIMUT_DISPLAY_ALPHA
#define SIMUT_DISPLAY_ALPHA 0  // HD44780 16x2 alphanumeric LCD
#endif

// The touch panel's graph and calendar (DisplayManager_Graph, _Calendar and
// AppManager_Graph; 2026-09-30). Follows the TFT, so the alpha and the Air are 0
// without saying so, and a TFT build can drop it: the min/max strip then has no
// graph button and the dashboard cards no zone that opens it.
#ifndef SIMUT_TFT_GRAPH
#define SIMUT_TFT_GRAPH SIMUT_DISPLAY_TFT
#endif
#if SIMUT_TFT_GRAPH && !SIMUT_DISPLAY_TFT
#error "SIMUT_TFT_GRAPH draws on the touch panel: it needs SIMUT_DISPLAY_TFT"
#endif

/* =========================================================================
 * SECTION 2: TFT DISPLAY PINS
 *
 * Only used when SIMUT_DISPLAY_TFT=1.
 * SPI bus: MOSI=GP19, MISO=GP16, SCK=GP18 (fixed by SPI0 peripheral).
 * These pins must NOT conflict with sensor slots (GP0–GP15).
 * ========================================================================= */

#ifndef TFT_CS
#define TFT_CS 28     // TFT chip select (any free GPIO)
#endif

#ifndef TFT_DC
#define TFT_DC 27     // TFT data/command (any free GPIO)
#endif

#ifndef TFT_RST
#define TFT_RST 26    // TFT reset (any free GPIO)
#endif

#ifndef TOUCH_CS
#define TOUCH_CS 17   // XPT2046 touch controller chip select
#endif

#ifndef TOUCH_IRQ
#define TOUCH_IRQ 20  // XPT2046 touch controller IRQ (must support GPIO interrupts)
#endif

/* TFT SPI write clock, in Hz. One constant for BOTH write paths (the
 * Adafruit library begin( ) and the DMA blit transactions) — they must
 * agree or the DMA path silently runs at a different speed.
 *
 * The RP2040 PL022 divider only yields clk_peri / even ratios; with
 * clk_peri at 125 MHz the reachable ladder is 62.5 / 31.25 / 20.83 MHz —
 * there is nothing between the top two rungs. 62.5 MHz is well beyond the
 * ILI9341 datasheet figure (which real modules routinely exceed), so it
 * MUST be validated by reading pixels back (GET /api/screenshot reads the
 * panel GRAM): stray pixels in flat regions mean the wiring cannot carry
 * it — drop back to 31250000u. Touch and panel READS are unaffected
 * (they run their own transactions: the touch at 2 MHz, the read-back at
 * SIMUT_TFT_READ_HZ below). */
#ifndef SIMUT_TFT_SPI_HZ
#define SIMUT_TFT_SPI_HZ 62500000u
#endif

/* TFT SPI READ clock, in Hz — the GRAM read-back, which is a different and much
 * slower bus transaction than the write above. It was 2 MHz written inline in
 * readRow with nothing saying why; 2026-09-18 measured the ladder and made it a
 * constant at 6 MHz, and the sweep below moved it again.
 *
 * 2026-09-19: 6 MHz -> 12 MHz, after sweeping the ladder on the rig with every
 * frame compared pixel by pixel against a 6 MHz reference of a screen held
 * still, AND against /api/screenshot, which is a different reader:
 *
 *      6 MHz   wire 387.9 ms   0 wrong pixels
 *      8 MHz   wire 293.7 ms   0
 *     10 MHz   wire 292.9 ms   0   <- same PL022 rung as 8
 *     12 MHz   wire 201.5 ms   0   <- this default
 *     16 MHz   wire 204.6 ms   0   <- same rung as 12
 *     20 MHz   wire 201.3 ms   0   <- same rung as 12
 *
 * The rungs are what the PL022 divider can reach, so 16 and 20 buy NOTHING over
 * 12 and only spend margin on a part whose datasheet read cycle works out near
 * 6.6 MHz. 12 is the last rung that pays, and it is where this stops.
 *
 * Plus a 32-frame soak at 12 MHz with DMA: every frame identical, 0 differing
 * pixels, and 3 of 3 frames equal to the three-vote forensic BMP. If a
 * different module or longer wiring shows stray pixels, drop to 6000000u.
 *
 * Check a clock against colours the firmware WROTE, not only capture against
 * capture: in simut_def any menu holds (240,244,240) and (0,148,248). Every
 * comparison above is reader against reader, and that proves only that an
 * error is deterministic — on 2026-09-24 the PicoHand holding TOUCH_CS low
 * zeroed bits 6, 5 and 2 of every byte, in every capture alike (AGENTS.md §1).
 * With the hand released, 12 MHz read the theme back exactly.
 * A wrong clock here does not corrupt the panel; it corrupts what the mirror
 * reports, which looks like a display fault. */
#ifndef SIMUT_TFT_READ_HZ
#define SIMUT_TFT_READ_HZ 12000000u
#endif

/* Bench instrument for the panel mirror: lets /api/screen_stream take its Core-1
 * pause a different way per request (?pm=, ?g=) and prints the frame's
 * decomposition in `show metrics`.
 *
 * OFF in every shipped environment. It exists so that an A/B of pause
 * strategies runs in ONE image — this project has been burned before by
 * comparing two builds and measuring the build (ESPELHO_DELTA.md §2, and
 * validate-the-instrument). Build the bench image with
 *
 *     PLATFORMIO_BUILD_FLAGS=-DSIMUT_MIRROR_PROBE=1 pio run -e pico_w_test
 *
 * and note that this variable wipes .pio/build for every environment. */
#ifndef SIMUT_MIRROR_PROBE
#define SIMUT_MIRROR_PROBE 0
#endif

/* =========================================================================
 * SECTION 3: ALPHA DISPLAY — HD44780 16x2
 *
 * Only used when SIMUT_DISPLAY_ALPHA=1.
 * Two interface modes are supported. Choose ONE.
 * ========================================================================= */

// --- Interface mode selection (choose ONE) ---
#ifndef HD44780_MODE_I2C
#ifndef HD44780_MODE_PARALLEL
#define HD44780_MODE_I2C 1   // Default: I2C backpack (PCF8574 on I2C1)
#endif
#endif

/* -------------------------------------------------------------------------
 * 3a. I2C mode pins (only when HD44780_MODE_I2C is defined)
 *     Uses I2C1 bus. SDA/SCL can be any free GPIO pair.
 * ------------------------------------------------------------------------- */
#if HD44780_MODE_I2C
#ifndef HD44780_I2C_ADDR
#define HD44780_I2C_ADDR 0x27 // PCF8574 backpack address (0x27 or 0x3F)
#endif
#ifndef HD44780_I2C_SDA
#define HD44780_I2C_SDA 26    // I2C1 SDA
#endif
#ifndef HD44780_I2C_SCL
#define HD44780_I2C_SCL 27    // I2C1 SCL
#endif
#endif

/* -------------------------------------------------------------------------
 * 3b. Parallel 4-bit mode pins (only when HD44780_MODE_PARALLEL is defined)
 *     Uses GP16–GP21 (these are free when TFT SPI is not used).
 * ------------------------------------------------------------------------- */
#if HD44780_MODE_PARALLEL
#ifndef HD44780_RS
#define HD44780_RS 16         // Register Select
#endif
#ifndef HD44780_EN
#define HD44780_EN 17         // Enable
#endif
#ifndef HD44780_D4
#define HD44780_D4 18         // Data bit 4
#endif
#ifndef HD44780_D5
#define HD44780_D5 19         // Data bit 5
#endif
#ifndef HD44780_D6
#define HD44780_D6 20         // Data bit 6
#endif
#ifndef HD44780_D7
#define HD44780_D7 21         // Data bit 7
#endif
#endif

/* =========================================================================
 * SECTION 4: AUDIO — BUZZER
 *
 * Passive buzzer driven by PIO (BuzzerPIO_RP2040 library).
 * Uses PIO block 1 (pio1). Can be any free GPIO.
 * ========================================================================= */

#ifndef BUZZER_PIN
#define BUZZER_PIN 22         // Buzzer GPIO
#endif

// Set to 0 to drop the buzzer: SoundManager compiles to a no-op and
// SoundManager.cpp leaves the build (features.toml off_exclude). SIMUT_AIR forces
// the same no-op; giving the buzzer its own switch (2026-09-26) lets a TFT/normal
// profile drop it too, not only the headless Air build. Measured cost in
// docs/analysis/ECONOMIA_DE_RECURSOS.md.
#ifndef SIMUT_SOUND_BUZZER
#define SIMUT_SOUND_BUZZER 1  // Buzzer sound engine (enabled)
#endif

/* =========================================================================
 * SECTION 5: SENSORS
 *
 * Set to 0 to exclude a sensor driver from the firmware.
 * Disabling unused sensors saves flash and RAM.
 *
 * DS18B20 — 1-Wire temperature sensor (1 GPIO pin)
 * DHT22   — Temperature + humidity sensor (1 GPIO pin)
 * BME280  — Temperature + humidity + pressure (I2C, 2 GPIO pins)
 *
 * Sensor GPIOs are assigned at RUNTIME via CLI (`sensor <n> pin …`),
 * NOT here. GP0–GP15 are the 16 configurable sensor slots.
 * ========================================================================= */

#ifndef SIMUT_SENSOR_DS18B20
#define SIMUT_SENSOR_DS18B20 1  // DS18B20 1-Wire temperature (enabled)
#endif

#ifndef SIMUT_SENSOR_DHT22
#define SIMUT_SENSOR_DHT22 1    // DHT22 temperature + humidity (enabled)
#endif

#ifndef SIMUT_SENSOR_BME280
#define SIMUT_SENSOR_BME280 1   // BME280 T+H+P I2C sensor (HW I2C on native pin pairs since stability wave 2; PIO bit-bang only as fallback)
#endif

/* =========================================================================
 * SECTION 6: COMMUNICATION FEATURES
 *
 * SIMUT_BLUETOOTH — Bluetooth Serial CLI over classic SPP (SerialBT/RFCOMM, not
 *                   BLE). Costs 64,732 B of flash and 16,416 B of .bss, measured
 *                   — see the note in platformio.ini's [pico_base]. Off by default.
 *
 * SIMUT_MDNS      — mDNS responder (<deviceName>.local hostname, `_simut._tcp` service).
 *                   Enabled by default. Costs ~15 KB of flash (15,376 B measured,
 *                   LEAmDNS); off in test/alpha/air.
 *
 * SIMUT_TEL_TLS   — the telemetry's TLS client: HTTPS and MQTTS to the collector
 *                   (WiFiClientSecure over BearSSL), the /cert.pem it validates
 *                   against, and the alarm line's HTTPS path. Enabled by default.
 *                   In an image without the HTTPS web server it is the only thing
 *                   that links BearSSL's TLS engine; measured cost per product in
 *                   docs/analysis/ECONOMIA_DE_RECURSOS.md.
 *
 * SIMUT_TEL_MQTT  — the MQTT transport (MQTT and MQTTS, PubSubClient), the alarm
 *                   line's MQTT path with its ack topic, and Home Assistant MQTT
 *                   Discovery. Enabled by default; without it telemetry is HTTP only.
 * ========================================================================= */

#ifndef SIMUT_BLUETOOTH
#define SIMUT_BLUETOOTH 0       // Bluetooth BLE CLI (disabled)
#endif

#ifndef SIMUT_MDNS
#define SIMUT_MDNS 1            // mDNS hostname resolution (enabled)
#endif

// Set to 0 to drop the telemetry's TLS client. Telemetry keeps running over plain
// HTTP and MQTT; a config that asks for encryption is refused at the commit and
// never sent in the clear at the socket — the API key and the MQTT password
// travel in what would be sent (TelemetryManager.cpp, SIMUT_TEL_TLS blocks).
#ifndef SIMUT_TEL_TLS
#define SIMUT_TEL_TLS 1         // HTTPS / MQTTS telemetry client (enabled)
#endif

// Set to 0 to drop the MQTT transport and Home Assistant Discovery. A config that
// names MQTT is refused at the commit and at every send: every other branch of the
// transport choice is HTTP, and it would POST the batch to the broker's port.
#ifndef SIMUT_TEL_MQTT
#define SIMUT_TEL_MQTT 1        // MQTT / MQTTS transport + HA Discovery (enabled)
#endif

/* =========================================================================
 * SECTION 6b: WEB INTERFACE — pieces a build can leave out (2026-09-30)
 *
 * Each one is a route (or a page and its routes) guarded where it is
 * registered in WebManager_Core.cpp; with the registration gone, the linker
 * drops the handler. A page comes out of the image through the web builder's
 * @IF blocks (custom_web_omit, derived from tools/features.toml).
 *
 * SIMUT_WEB_HISTORY    — the history page (/history: charts, calendar, CSV,
 *                        event viewer) with /api/logcodes and /api/clear_logs,
 *                        which only it calls. The history itself is recorded
 *                        either way; /api/logs, /api/history/open and
 *                        /api/history_days stay (the bench tools and the
 *                        server-integration guide read them).
 * SIMUT_WEB_EXPORT_API — /api/history_multi and the .simx export routes
 *                        (/api/export/history.bin, /api/export/logs.bin): no
 *                        page calls them; bench tools and integrations do.
 * SIMUT_WEB_METRICS    — GET /metrics for Prometheus.
 * SIMUT_SYSLOG         — the RFC 5424 / UDP forwarder (SyslogManager) and the
 *                        Remote Syslog section of the config page.
 * ========================================================================= */
#ifndef SIMUT_WEB_HISTORY
#define SIMUT_WEB_HISTORY 1     // history page + its two routes (enabled)
#endif
#ifndef SIMUT_WEB_EXPORT_API
#define SIMUT_WEB_EXPORT_API 1  // /api/history_multi + .simx export (enabled)
#endif
#ifndef SIMUT_WEB_METRICS
#define SIMUT_WEB_METRICS 1     // Prometheus /metrics (enabled)
#endif
#ifndef SIMUT_SYSLOG
#define SIMUT_SYSLOG 1          // RFC 5424 syslog forwarder (enabled)
#endif

/* Setup access point without a key. 0 = WPA2 with a per-device key derived
 * from the board id and shown on the console and the display (see ApPsk.h and
 * NetworkManager::beginAP) — finding V-05, where an open AP handed anyone in
 * radio range the captive portal of a device whose Wi-Fi had just failed.
 *
 * 1 is for bench work only: it makes joining scriptable without reading a key
 * off a console. Never in a shipped image; it is the exact configuration the
 * finding describes. */
#ifndef SIMUT_AP_OPEN
#define SIMUT_AP_OPEN 0
#endif

/* =========================================================================
 * SECTION 7: THEME PACKS (TFT only)
 *
 * The core theme (simut_def) is always compiled.
 * Uncomment any SIMUT_THEMES_* line below to include that theme pack.
 * Each theme costs ~70 bytes of flash.
 *
 * Theme packs are only relevant when SIMUT_DISPLAY_TFT=1.
 * ========================================================================= */

// #define SIMUT_THEMES_HEALTH   // 12 themes — monthly health campaigns (jan_branco..dez_laranja)
// #define SIMUT_THEMES_PRO      //  5 themes — dark_pro, monochrome, clinical, corporate, minimal
// #define SIMUT_THEMES_MEDICAL  //  6 themes — unimed, unimed_dark, xray, uti_monitor, scrubs, biohazard
// #define SIMUT_THEMES_SAFETY   //  3 themes — danger, safety, fire
// #define SIMUT_THEMES_RETRO    //  8 themes — matrix, cyberpunk, pipboy, nes, cmd, synth, gameboy, sith
// #define SIMUT_THEMES_NATURE   //  8 themes — amber, vampire, magma, ocean, nature, outrun, midnight, forest
// #define SIMUT_THEMES_UTILITY  //  7 themes — paper, blocks, blueprint, solarized, luxury, ubuntu, whiteboard

/* =========================================================================
 * SECTION 8: ONE-WIRE DEFAULT PIN
 *
 * Default GPIO used for DS18B20 bus scan when no sensor slots are
 * configured. Set to PIN_UNUSED (255) to disable default scan.
 * ========================================================================= */

#ifndef PIN_ONEWIRE_DEFAULT
#if SIMUT_DISPLAY_TFT
#define PIN_ONEWIRE_DEFAULT 0    // GP0 (TFT build: GP0–GP15 free for sensors)
#else
#define PIN_ONEWIRE_DEFAULT 255  // PIN_UNUSED (alpha build)
#endif
#endif

/* =========================================================================
 * SECTION 9: ADVANCED — System Limits
 *
 * Change only if you know what you are doing.
 * Incorrect values may cause memory corruption or boot failure.
 * ========================================================================= */

#ifndef MAX_SENSORS
#define MAX_SENSORS 16           // Sensor slots, GPIO0–GPIO15 (the lan switch: 11, GPIO0–GPIO10)
#endif

#ifndef MAX_SENSOR_PINS
#define MAX_SENSOR_PINS 4        // Max GPIO pins per sensor (fits SPI)
#endif

#ifndef PIN_UNUSED
#define PIN_UNUSED 255           // Sentinel for unused GPIO pin slots
#endif

/* =========================================================================
 * SECTION 10: SIMUT AIR — headless hibernating build
 *
 * SIMUT_AIR=1 builds the headless variant (no display, no buzzer) that runs
 * as an Alpha-like device on cold boot and enters a dormant hibernation
 * cycle on command or after an inactivity timeout. The wake period IS the
 * history save interval (h_int, set from the web/CLI); AIR_WAKE_INTERVAL_MIN
 * is only the fallback when that interval reads as zero. Air-only settings
 * (idle timeout, sensor power pin, phase timeouts) live in /config/air.bin —
 * NOT in SystemConfig, so CONFIG_VERSION is untouched.
 *
 * Override any default here or via -D build_flags.
 * ========================================================================= */

#ifndef SIMUT_AIR
#define SIMUT_AIR 0              // 1 = SIMUT Air (headless hibernating build)
#endif

/* =========================================================================
 * SECTION 10b: THE NETWORK — the driver the image has, and what the rest of
 * the firmware knows of it
 *
 * Two vocabularies, apart on purpose (2026-10-13). Until then SIMUT_NET_LAN
 * meant "the link is a cable" and "the chip is the ENC28J60" at once, and a
 * second wired board would have inherited the first one's pins.
 *
 * IDENTITY  the driver, the chip. Exactly one macro is 1:
 *   SIMUT_NET_CYW43     the radio of the Pico W and the Pico 2 W, as in every
 *                       image up to v2.11.1. The default: an image that names
 *                       no driver has it.
 *   SIMUT_NET_ENC28J60  a plain Raspberry Pi Pico, the RP2040 without the
 *                       radio (PlatformIO `board = rpipico`), with the network
 *                       on the cable through an ENC28J60 module. The manifest
 *                       (`net = "enc28j60"` in tools/features.toml) passes
 *                       -DSIMUT_NET_ENC28J60=1, picks that board and takes
 *                       -DPICO_CYW43_SUPPORTED=1 out of the flags (build_unflags).
 *   Only src/net/, this file (the module's pins, below) and src/net/NetCaps.h
 *   read an identity macro (tools/check_net_boundary.py, with no allowance
 *   since the radio's state machine moved into src/net/ on 2026-10-13).
 *
 * CLASS  what the link is, derived from the identity in src/net/NetCaps.h
 * (included just below) and never passed with -D:
 *   SIMUT_NET_WIFI      joins by SSID and password (scan, setup AP, signal
 *                       level): the CYW43.
 *   SIMUT_NET_LAN       a cable: the ENC28J60.
 *   Every other file reads the class only, and includes this header or
 *   net/NetCaps.h itself (the reason is in NetCaps.h).
 *
 * Adding a board: its identity macro and its class in NetCaps.h (the recipe is
 * in the comment there), a [net.<id>] in the manifest, its pins and its PIO
 * program under an `#if SIMUT_NET_<ID>` of its own here, as the ENC28J60's
 * below, and its OTA variant names in the SIMUT_ENV_NAME block at the end of
 * this section (the #error there stops a wired driver that has none).
 *
 * The wired side is src/net/WiredLink.cpp over the chip's device header (the
 * ENC28J60: src/net/Enc28j60Device.h) and src/net/NetworkManager_Wired.cpp
 * (NetworkManager's wired unit), since 2026-10-12. The radio's side is
 * src/net/NetworkManager_Cyw43.cpp (NetworkManager's radio unit, since
 * 2026-10-13); NetworkManager.cpp is the host both units share (NTP, the
 * clocks, mDNS), and NetworkManager.h has the contract a unit implements. The 2026-09-26 study
 * (docs/analysis/SIMUT_WIRE_VIABILIDADE.md, study/simut-wire branch) found the
 * release linking for board = rpipico once the one cyw43_arch_enable_sta_mode( )
 * call was out.
 * ========================================================================= */

#include "net/NetCaps.h"

// The CYW43 driver and the others are told apart by whether the toolchain was handed
// the CYW43: the Pico W / Pico 2 W boards pass -DPICO_CYW43_SUPPORTED=1 (boards.txt,
// the board JSONs, and pico_base in platformio.ini), the plain Pico does not. A build
// with the CYW43 driver and without it would compile against the framework's no-radio
// stub and boot with a network that cannot exist; a build with another driver and with
// it would still link the CYW43 control code and its firmware blob (225,240 B,
// 2026-09-26 study). Both are refused where the macro is looked at, and the test is
// the CYW43 driver (SIMUT_NET_CYW43), the chip, not the class. Only on the firmware's
// toolchain (ARDUINO_ARCH_RP2040 comes with every arduino-pico board): the host test
// builds compile this header with neither macro.
#if defined(ARDUINO_ARCH_RP2040)
#if SIMUT_NET_CYW43 && !defined(PICO_CYW43_SUPPORTED)
#error "The CYW43 network driver needs the CYW43: PICO_CYW43_SUPPORTED is not defined (a plain Pico builds with another driver, -DSIMUT_NET_ENC28J60=1)"
#endif
#if !SIMUT_NET_CYW43 && defined(PICO_CYW43_SUPPORTED)
#error "A network driver other than the CYW43 is a build without the CYW43, and PICO_CYW43_SUPPORTED is still defined (build_unflags it)"
#endif
#endif

// Bluetooth and the Air cycle both run on the CYW43 (tools/features.toml rules
// bluetooth-needs-cyw43 and air-needs-cyw43). What they need is the chip, not "no
// cable": a Wi-Fi board added later without a CYW43 could not run them either.
#if SIMUT_BLUETOOTH && !SIMUT_NET_CYW43
#error "SIMUT_BLUETOOTH cannot be combined with a network driver other than the CYW43: Bluetooth runs on the CYW43, which this build does not have"
#endif
#if SIMUT_AIR && !SIMUT_NET_CYW43
#error "SIMUT_AIR cannot be combined with a network driver other than the CYW43: the Air cycle is built around the CYW43, which this build does not have"
#endif

#if SIMUT_NET_ENC28J60
/* The ENC28J60's pins. The PicoENC28J60 library talks to it over an SPI made in
 * PIO (tools/pio_budget.py counts its 17 instructions), so any GPIO would do; the
 * four chosen are also SPI1's (RX GP12, SCK GP14, TX GP15), which leaves room for a
 * hardware SPI without rewiring.
 *
 * INT is GP11 with every display (2026-10-12). Outside the sensor slots, GP21 is
 * free beside the TFT (which takes GP16–GP20 and GP26–GP28) and GP28 beside the
 * 16x2 LCD (GP16–GP21 in parallel mode, GP26/GP27 in I2C mode; GP22 is the
 * buzzer), but no GPIO is free beside both: one wiring for both displays costs a
 * slot. GP11–GP15 were five of the sixteen sensor slots, so a LAN build has
 * eleven, GP0–GP10 (the driver passes -DMAX_SENSORS=11, [net.enc28j60].flags, and the
 * check below keeps every line of the module out of the slots' range). -1 = no INT
 * pin: the driver polls the module. Nothing of this exists in a build with another
 * driver (2026-10-13): the radio images used to carry these macros for nobody. */
#ifndef SIMUT_ENC_MISO
#define SIMUT_ENC_MISO 12
#endif
#ifndef SIMUT_ENC_CS
#define SIMUT_ENC_CS 13
#endif
#ifndef SIMUT_ENC_SCK
#define SIMUT_ENC_SCK 14
#endif
#ifndef SIMUT_ENC_MOSI
#define SIMUT_ENC_MOSI 15
#endif
#ifndef SIMUT_ENC_INT_PIN
#define SIMUT_ENC_INT_PIN 11
#endif
/* The PIO block of the module's SPI, where tools/pio_budget.py's place( ) puts
 * it (tools/test_pio_budget.py holds this line to it). The module starts before
 * the sensors (AppManager_Boot.cpp), so it takes the block that leaves room for
 * what comes after it: the DS18B20's 1-Wire fits only pio0 (27 instructions), so
 * with it the module goes to pio1, beside the buzzer; without it, pio0, and the
 * buzzer and the DHT22 share pio1. Never "any block": the driver would take the
 * first, a sensor family's. */
#ifndef SIMUT_ENC_PIO
#if SIMUT_SENSOR_DS18B20
#define SIMUT_ENC_PIO 1
#else
#define SIMUT_ENC_PIO 0
#endif
#endif

// Who owns which GPIO, checked by the preprocessor so a layout that cannot work
// stops the build instead of the board: the display, the module and the buzzer
// may share none. `#if (A & B) != 0` is all it takes; the masks are 64 bits wide
// because the RP2350B has pins past 31. A mask lives only inside this check.
#if SIMUT_DISPLAY_TFT
// SPI0 is fixed by the peripheral (MISO GP16, SCK GP18, MOSI GP19); the rest are the
// TFT_* and TOUCH_* pins of section 2.
#define SIMUT_PINS_DISPLAY ((1ull << 16) | (1ull << 18) | (1ull << 19) | (1ull << TFT_CS) | \
                            (1ull << TFT_DC) | (1ull << TFT_RST) | (1ull << TOUCH_CS) | (1ull << TOUCH_IRQ))
#elif SIMUT_DISPLAY_ALPHA && HD44780_MODE_PARALLEL
#define SIMUT_PINS_DISPLAY ((1ull << HD44780_RS) | (1ull << HD44780_EN) | (1ull << HD44780_D4) | \
                            (1ull << HD44780_D5) | (1ull << HD44780_D6) | (1ull << HD44780_D7))
#elif SIMUT_DISPLAY_ALPHA && HD44780_MODE_I2C
#define SIMUT_PINS_DISPLAY ((1ull << HD44780_I2C_SDA) | (1ull << HD44780_I2C_SCL))
#else
#define SIMUT_PINS_DISPLAY 0ull
#endif

#define SIMUT_PINS_ENC_SPI ((1ull << SIMUT_ENC_MISO) | (1ull << SIMUT_ENC_CS) | \
                            (1ull << SIMUT_ENC_SCK) | (1ull << SIMUT_ENC_MOSI))
#if SIMUT_ENC_INT_PIN >= 0
#define SIMUT_PINS_ENC (SIMUT_PINS_ENC_SPI | (1ull << SIMUT_ENC_INT_PIN))
#else
#define SIMUT_PINS_ENC SIMUT_PINS_ENC_SPI
#endif

#if SIMUT_SOUND_BUZZER
#define SIMUT_PINS_BUZZER (1ull << BUZZER_PIN)
#else
#define SIMUT_PINS_BUZZER 0ull
#endif

#if (SIMUT_PINS_ENC & SIMUT_PINS_DISPLAY) != 0
#error "SIMUT_NET_ENC28J60: a GPIO of the ENC28J60 (SIMUT_ENC_MISO, _CS, _SCK, _MOSI or _INT_PIN) is also a pin of the display"
#endif
#if (SIMUT_PINS_ENC & SIMUT_PINS_BUZZER) != 0
#error "SIMUT_NET_ENC28J60: a GPIO of the ENC28J60 (SIMUT_ENC_MISO, _CS, _SCK, _MOSI or _INT_PIN) is also BUZZER_PIN"
#endif
#if (SIMUT_PINS_DISPLAY & SIMUT_PINS_BUZZER) != 0
#error "SIMUT_NET_ENC28J60: BUZZER_PIN is also a pin of the display"
#endif
// The masks cannot see two of the module's own lines on one GPIO: OR-ed together
// they only lose a bit. Each pair is compared instead.
#if SIMUT_ENC_MISO == SIMUT_ENC_CS || SIMUT_ENC_MISO == SIMUT_ENC_SCK || \
    SIMUT_ENC_MISO == SIMUT_ENC_MOSI || SIMUT_ENC_CS == SIMUT_ENC_SCK || \
    SIMUT_ENC_CS == SIMUT_ENC_MOSI || SIMUT_ENC_SCK == SIMUT_ENC_MOSI || \
    (SIMUT_ENC_INT_PIN >= 0 && (SIMUT_ENC_INT_PIN == SIMUT_ENC_MISO || SIMUT_ENC_INT_PIN == SIMUT_ENC_CS || \
                                SIMUT_ENC_INT_PIN == SIMUT_ENC_SCK || SIMUT_ENC_INT_PIN == SIMUT_ENC_MOSI))
#error "SIMUT_NET_ENC28J60: two lines of the ENC28J60 (SIMUT_ENC_MISO, _CS, _SCK, _MOSI, _INT_PIN) are on the same GPIO"
#endif
// A sensor slot can take any GPIO below MAX_SENSORS, so no line of the module may
// be one: GP11–GP15 stop being slots only because MAX_SENSORS is 11 here.
#if SIMUT_ENC_MISO < MAX_SENSORS || SIMUT_ENC_CS < MAX_SENSORS || SIMUT_ENC_SCK < MAX_SENSORS || \
    SIMUT_ENC_MOSI < MAX_SENSORS || (SIMUT_ENC_INT_PIN >= 0 && SIMUT_ENC_INT_PIN < MAX_SENSORS)
#error "SIMUT_NET_ENC28J60: a GPIO of the ENC28J60 is also a sensor slot's (below MAX_SENSORS); the driver passes -DMAX_SENSORS=11"
#endif

#undef SIMUT_PINS_DISPLAY
#undef SIMUT_PINS_ENC_SPI
#undef SIMUT_PINS_ENC
#undef SIMUT_PINS_BUZZER
#endif /* SIMUT_NET_ENC28J60 */

/* -------------------------------------------------------------------------
 * SIMUT_ENV_NAME — the hardware variant this image was built for, as a
 * string the image itself carries: "release" (TFT), "alpha" (16x2 LCD) or
 * "air" (headless, hibernating).
 *
 * Until now nothing in a .bin said which variant it was. /api/status
 * exposed `cap` (1 = TFT, 0 = anything else), so a fleet manager could not
 * tell an alpha from an Air — and staging the wrong one formats the file
 * system and leaves the device with an image for hardware it does not have.
 * The name is exposed in /api/perms ("env"), /api/status ("env"), the mDNS
 * TXT record, and the .rodata tag in BuildIdentity.cpp that
 * ota_validate_staging( ) compares against before a stage is accepted.
 *
 * The Pico 2 W's images add "two": "releasetwo" is the release on the
 * RP2350. The variant alone said nothing of the chip, so a Pico 2 W took a
 * Pico W's release for its own, and its signature too, which binds the env
 * (docs/analysis/OTA_AB_RP2350.md, step 4). A suffix of letters, because the
 * tag reads only a to z (BuildIdentity.cpp), and short, because the longest,
 * "headlesstwo", fills the 11 characters the validator keeps.
 *
 * A LAN build is "wire" with the 16x2 LCD and "wiretft" with the touch display
 * (SIMUT Wire, 2026-10-12): it shares no variant with the Pico W's images, so a
 * Wi-Fi image cannot be staged on a board with no radio, nor the other way round
 * (the staging check compares the tag, and the signature binds it). "wiretfttwo"
 * would be 10 characters, inside the 11. A LAN build with no display at all has
 * no name, and stops here: no product is one, and a name chosen now would be one
 * more OTA identity to keep apart.
 *
 * Those two names are the ENC28J60's, not the cable's (2026-10-13): the staging
 * check compares only the tag, so an image of one wired module installed over the
 * air on a board with another would pass it and start the wrong driver. A second
 * wired driver names its own variants here before it builds.
 * ---------------------------------------------------------------------- */
#if defined(PICO_RP2350) && PICO_RP2350
#define SIMUT_ENV_CHIP "two"
#else
#define SIMUT_ENV_CHIP ""
#endif
#if SIMUT_NET_LAN && !SIMUT_NET_ENC28J60
#error "a new wired driver needs its own OTA variant names: an image of one module installed over the air on a board with another would start the wrong driver"
#endif
#if SIMUT_NET_LAN
#if SIMUT_DISPLAY_TFT
#define SIMUT_ENV_NAME "wiretft" SIMUT_ENV_CHIP
#elif SIMUT_DISPLAY_ALPHA
#define SIMUT_ENV_NAME "wire" SIMUT_ENV_CHIP
#else
#error "SIMUT_NET_LAN has a variant name for the TFT and for the 16x2 LCD only: a wired build with no display has none"
#endif
#elif SIMUT_AIR
#define SIMUT_ENV_NAME "air" SIMUT_ENV_CHIP
#elif SIMUT_DISPLAY_ALPHA
#define SIMUT_ENV_NAME "alpha" SIMUT_ENV_CHIP
#elif SIMUT_DISPLAY_TFT
#define SIMUT_ENV_NAME "release" SIMUT_ENV_CHIP
#else
#define SIMUT_ENV_NAME "headless" SIMUT_ENV_CHIP
#endif

/* -------------------------------------------------------------------------
 * SIMUT_OTA_TRUST_BENCH — 1 = over the air, this image also takes what the
 * BENCH key signs (keys/ota_root_bench.pub), besides what the release key
 * signs. It is how a local build reaches the rig through the usual OTA.
 * Only an unpublished profile sets it (`ota_trust_bench` in
 * tools/features.toml): tools/gen_features.py refuses it on a published one,
 * and a configurator build drops it whatever its base. On a field device the
 * less guarded key would be a way in (docs/analysis/OTA_ASSINADA.md).
 * ---------------------------------------------------------------------- */
#ifndef SIMUT_OTA_TRUST_BENCH
#define SIMUT_OTA_TRUST_BENCH 0
#endif

#if SIMUT_AIR
#ifndef AIR_WAKE_INTERVAL_MIN
#define AIR_WAKE_INTERVAL_MIN 5  // Fallback wake period (min) when the history interval reads 0
#endif
#ifndef AIR_IDLE_TIMEOUT_SEC
#define AIR_IDLE_TIMEOUT_SEC 300 // M0 inactivity -> auto-hibernate (5 min)
#endif
#ifndef AIR_STAB_TIMEOUT_MS
#define AIR_STAB_TIMEOUT_MS 30000 // Sensor stabilization cap
#endif
#ifndef AIR_WIFI_SCAN_TIMEOUT_MS
#define AIR_WIFI_SCAN_TIMEOUT_MS 4000
#endif
#ifndef AIR_CONNECT_TIMEOUT_MS
#define AIR_CONNECT_TIMEOUT_MS 30000
#endif
#ifndef AIR_FLUSH_TIMEOUT_MS
#define AIR_FLUSH_TIMEOUT_MS 30000
#endif
#ifndef AIR_FLUSH_TAIL_MS
// What the wake still needs after the FLUSH ends: cursor to flash, air.bin if
// the crash-loop count moved, CYW43 teardown, USB detach, RTC arm. The flush
// budget is the configured cap OR whatever is left of the reading interval
// minus this tail, whichever is smaller — so a telemetry wake can no longer
// outlast the interval it is supposed to fit in (measured: a 1-minute reading
// interval with the flat 30 s cap gave a 57 s wake and an OVERRUN).
#define AIR_FLUSH_TAIL_MS 5000
#endif
#ifndef AIR_FLUSH_NTP_WAIT_MS
// How long the FLUSH waits for NTP on a link that already has its address. The
// upload waits for NET_READY, which needs the NTP answer, and the phase starts
// as soon as the address is there: NTP answered 0 to 5 s after it in five
// boots on the bench (2026-10-13). A network that never answers NTP would hold the radio
// on for the phase's whole budget, sending nothing. 10 s, the maintainer's
// choice of 2026-10-14 (airFlushNetLost( ), src/air/AirConfig.h).
#define AIR_FLUSH_NTP_WAIT_MS 10000
#endif
#ifndef AIR_SENSOR_POWER_PIN
#define AIR_SENSOR_POWER_PIN 16 // GPIO power-gating for sensors (also the awake/sleep probe)
#endif
#ifndef AIR_CHARGER_PIN
// GPIO that reads HIGH while the charger is connected, through a divider off
// the 5 V rail. On the charger the device is not on a battery budget, so it
// stops hibernating and stays awake and reachable; unplug it and the cycle
// resumes on the next idle timeout. PIN_UNUSED disables the whole behaviour.
#define AIR_CHARGER_PIN 17
#endif
#ifndef AIR_CHARGER_ACTIVE_HIGH
// 1 = charging reads HIGH (a divider off 5 V). 0 inverts it, for a board that
// pulls the line down while charging.
#define AIR_CHARGER_ACTIVE_HIGH 1
#endif
#ifndef AIR_MAX_CONNECT_ATTEMPTS
// How many failed WiFi connection attempts a single wake may spend before it
// stops trying. With the SSID out of range the wake still does its real job —
// read the sensors, write the history — and then hibernates; the next wake
// tries again from scratch, because every wake is a fresh boot. Each attempt
// costs up to 20 s inside NetworkManager, so in practice one attempt fits in a
// wake and this is the ceiling rather than the usual case.
#define AIR_MAX_CONNECT_ATTEMPTS 2
#endif
#ifndef AIR_MIN_SLEEP_SEC
// Floor for the compensated wake alarm. The alarm is set to
// (history interval - time this wake spent awake) so the PERIOD equals the
// configured interval; if a wake ever outlasts the interval the subtraction
// would ask for zero, and the device would spin boot-sleep-boot. Sleeping this
// long instead degrades to "as fast as it can" and the log line says OVERRUN.
#define AIR_MIN_SLEEP_SEC 5
#endif
#ifndef AIR_WAKE_BOOT_MS
// The part of a wake that happens before millis( ) exists: from the RTC alarm
// to the moment runtime_init releases the timer — boot ROM, boot2 and crt0
// copying and zeroing ~120 KB of RAM on the ring oscillator, all uncounted.
// The clock carried across the sleep (AIR_CLOCK_MAGIC) is "alarm instant +
// millis( )", so without this term every wake started that much behind and
// the provisional clock drifted by it. Measured on the bench 2026-09-23 (Air,
// clock printed at DECIDE against an NTP-synced host): 117 to 182 ms per wake
// over four wakes, mean 140. It depends on the image's RAM footprint and on
// the ROSC, which varies by chip and temperature, so it is a calibration, not
// a law: the telemetry wake's NTP still settles whatever it leaves.
#define AIR_WAKE_BOOT_MS 140
#endif
#ifndef AIR_RESUME_GRACE_SEC
// How long M0 waits before resuming a cycle that a reset interrupted (plan
// F25). The hibernation marker is cleared on every boot on purpose, so a device
// that crashes inside the cycle stays reachable; the armed flag in air.bin is
// what says the operator wanted the cycle, and this is the window they get to
// countermand it with 'air stop'. Short, because the device is awake with the
// radio on the whole time and that is the state the Air build exists to avoid.
// The wide window is earned instead: after AIR_MAX_DIRTY_BOOTS unclean resumes
// the grace becomes the full idle timeout, which is when a human actually needs
// to get in.
#define AIR_RESUME_GRACE_SEC 10
#endif
#ifndef AIR_TEL_FAIL_SKIP_WAKES
// Reading wakes of silence after a telemetry wake whose send failed. Without
// it, a collector that stops answering arms the "enough records are waiting"
// trigger on every wake — the queue only grows — and the radio runs flat
// answering nobody. Five wakes is one radio wake in six.
#define AIR_TEL_FAIL_SKIP_WAKES 5
#endif
#ifndef AIR_MAX_DIRTY_BOOTS
// Consecutive resumes after an UNCLEAN reset before the device stops rushing
// back into the cycle. Counted in air.bin (flags bits 4..7) and zeroed by the
// first healthy sleep, so an isolated glitch costs nothing and a real crash
// loop still parks the device where an operator can reach it.
#define AIR_MAX_DIRTY_BOOTS 3
#endif
#endif /* SIMUT_AIR */

// ── Automatic cadence and batch size (measured 2026-09-07, see
// docs/analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md) ────────────────────────
// telInterval stopped being a floor between batches and became the period
// between DRAINS. Inside a drain the pace comes from the server: a cycle that
// finishes under TEL_FAST_MS earns the next batch at once, a slower one earns
// a gap that doubles per slow batch up to TEL_GAP_MAX_MS, and any success
// under the fast mark clears it.
//
// The fast mark is per transport because it is the device's own cost that
// defines "fast": the slowest measured plain cycle was 281 ms (batch 250) and
// the fastest with a TLS handshake was ~1.4 s. A server slower than that is
// slower than the whole of the device's work, which is the criterion.
#ifndef TEL_FAST_MS_PLAIN
#define TEL_FAST_MS_PLAIN 400
#endif
#ifndef TEL_FAST_MS_TLS
#define TEL_FAST_MS_TLS 2500
#endif
#ifndef TEL_GAP_MAX_MS
#define TEL_GAP_MAX_MS 10000
#endif
// Batch AIMD, always inside safeBatchLimit( )'s heap ceiling: grow by half on
// any success, halve on failure. It starts at the configured maximum, because
// that is what the operator asked for — the controller exists to back away
// from a server that cannot take it, not to make the device earn its own
// setting back on every boot.
// Ceiling for the minimum-batch trigger (SystemConfig::telInterval as a COUNT).
// Above the 30-day floor collectBatch applies there is nothing left to send, so
// a minimum larger than what fits inside it would simply never fire: 20,000
// records is a fortnight at one reading a minute, well inside that floor.
#ifndef TEL_MIN_BATCH_MAX
#define TEL_MIN_BATCH_MAX 20000
#endif
#ifndef TEL_BATCH_MIN
#define TEL_BATCH_MIN 10
#endif
// A drain that starts the instant the device boots competes with the rest of
// setup( ), and a TLS handshake plus a POST in that window used to reach the
// watchdog. Applies to the mains path only: an Air wake is a boot whose whole
// purpose is to send, and it bypasses this through drain mode.
#ifndef TEL_FIRST_SEND_DELAY_MS
#define TEL_FIRST_SEND_DELAY_MS 8000
#endif
#ifndef TEL_BATCH_MAX
#define TEL_BATCH_MAX 250
#endif
#ifndef TEL_TLS_KEEPALIVE_EXPERIMENT
// Bench experiment (2026-09-07): keep the telemetry TLS session open between
// consecutive successful batches instead of stopping it after every one. Every
// HTTPS batch currently pays a full handshake because attemptHttpUpload( ) calls
// _httpSecurePtr->stop( ) unconditionally on the way out — a defence measured
// against the `drip` fault. This flag keeps the stop for every non-success and
// adds an idle stop, so the defence stays for the failure path while the
// success path amortises the handshake. Default 0. Measured 2026-09-07 (HTTPS,
// 45 s windows): 5.2x at batch 25 and 3.4x at batch 100 against a server that
// keeps the connection; identical to the default when the server closes it.
#define TEL_TLS_KEEPALIVE_EXPERIMENT 0
#endif

