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
 * GP0–GP15   → Sensor slots (runtime-configurable via CLI)
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
 * @author  Angelo Moises Alves
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
#define SIMUT_SENSOR_BME280 0   // BME280 T+H+P I2C sensor (disabled - PIO conflict)
#endif

/* =========================================================================
 * SECTION 6: COMMUNICATION FEATURES
 *
 * SIMUT_BLUETOOTH — Bluetooth Serial CLI (BLE UART).
 *                   Costs ~22 KB flash. Disabled by default.
 *
 * SIMUT_MDNS      — mDNS responder (SIMUT.local hostname).
 *                   Enabled by default. Negligible flash cost.
 * ========================================================================= */

#ifndef SIMUT_BLUETOOTH
#define SIMUT_BLUETOOTH 0       // Bluetooth BLE CLI (disabled)
#endif

#ifndef SIMUT_MDNS
#define SIMUT_MDNS 1            // mDNS hostname resolution (enabled)
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
#define MAX_SENSORS 16           // Configurable sensor slots (GPIO0–GPIO15)
#endif

#ifndef MAX_SENSOR_PINS
#define MAX_SENSOR_PINS 4        // Max GPIO pins per sensor (fits SPI)
#endif

#ifndef PIN_UNUSED
#define PIN_UNUSED 255           // Sentinel for unused GPIO pin slots
#endif
