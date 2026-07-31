/**
 * @file SensorHelpers.h
 * @brief Compile-time sensor type helpers — inline, zero-cost.
 * @details Centralizes all SensorType-dependent logic so that consumer code
 * never needs #if SIMUT_SENSOR_* guards. The compiler constant-folds the
 * flag checks and dead-code-eliminates unreachable branches.
 *
 * Include this header to get sensorHasHumidity(), sensorTypeName(),
 * sensorValueCount(), sensorTypeEnabled(), and sensorDefaultIntervalMs().
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include "SensorConfig.h"
#include "SensorChannelTable.h"    /* SensorChannel enum, ChannelInfo, the table */
#include "../SystemDefs_Records.h" /* SensorType enum */

/* ===========================================================================
 * SENSOR CHANNELS — universal measurement axes
 *
 * The enum and the per-channel metadata moved to SensorChannels.h: what a
 * channel MEANS is a property of the quantity, not of any driver. What stays
 * here is what genuinely varies per driver — which channels it reports
 * (channelMask) and which pins it needs.
 * =========================================================================== */

/* ===========================================================================
 * PIN ROLES — what each GPIO does in a sensor
 *
 * Each sensor type declares its pin requirements via SensorFormat::forType().
 * The role is NOT stored in flash — it is derived from the driver (sensorType
 * + pin index). Adding a new sensor only requires the driver + format entry.
 *
 * Slot semantics: each slot = 1 GPIO pin with a declared role.
 * A sensor is an entity that consumes pinCount slots.
 *   DHT22  → 1 slot (PIN_DATA)
 *   BME280 → 2 slots (PIN_I2C_SDA, PIN_I2C_SCL)
 *   BMP388 → 4 slots (PIN_SPI_CS, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK)
 * =========================================================================== */

enum PinRole : uint8_t {
    ROLE_DATA      = 0,   /**< 1-Wire / DHT22 single data line */
    ROLE_I2C_SDA   = 1,   /**< I2C Serial Data */
    ROLE_I2C_SCL   = 2,   /**< I2C Serial Clock */
    ROLE_SPI_MOSI  = 3,   /**< SPI Master Out Slave In */
    ROLE_SPI_MISO  = 4,   /**< SPI Master In Slave Out */
    ROLE_SPI_SCK   = 5,   /**< SPI Serial Clock */
    ROLE_SPI_CS    = 6,   /**< SPI Chip Select (unique per device) */
    ROLE_UART_TX   = 7,   /**< UART Transmit */
    ROLE_UART_RX   = 8,   /**< UART Receive */
    ROLE_ANALOG    = 9,   /**< ADC analog input */
    ROLE_POWER     = 10,  /**< Switchable VCC control */
    ROLE_UNUSED    = 255  /**< Sentinel */
};

/** Describes one pin slot requirement for a sensor driver. */
struct PinRequirement {
    PinRole role;         /**< What this pin does (SDA, SCL, DATA, CS...) */
    const char* label;    /**< Human label: "SDA", "SCL", "CS", "DATA" */
    uint8_t flags;        /**< FLAG_PULLUP, FLAG_OPENDRAIN, etc. */
};

#define FLAG_PULLUP    0x01
#define FLAG_PULLDOWN  0x02
#define FLAG_OPENDRAIN 0x04

/** @return human-readable pin role name */
inline const char* pinRoleName(PinRole r) {
    switch (r) {
    case ROLE_DATA:      return "Data";
    case ROLE_I2C_SDA:   return "SDA";
    case ROLE_I2C_SCL:   return "SCL";
    case ROLE_SPI_MOSI:  return "MOSI";
    case ROLE_SPI_MISO:  return "MISO";
    case ROLE_SPI_SCK:   return "SCK";
    case ROLE_SPI_CS:    return "CS";
    case ROLE_UART_TX:   return "TX";
    case ROLE_UART_RX:   return "RX";
    case ROLE_ANALOG:    return "ADC";
    case ROLE_POWER:     return "VCC";
    default:             return "?";
    }
}

/** @return human-readable channel name, from the channel table. */
inline const char* sensorChannelName(uint8_t ch) { return channelInfo(ch).name; }

/** Forward declaration — implementation after SensorFormat definition below. */
inline bool sensorHasChannel(SensorType t, uint8_t channel);

/** @return human-readable sensor type name (e.g. "DS18B20", "DHT22"). */
inline const char* sensorTypeName(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return "DS18B20";
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return "DHT22";
#endif
#if SIMUT_SENSOR_BME280
 /* This used to answer "BMP280" for TYPE_BME280 — the one type covered both
  * parts and the label picked the wrong one for anybody holding a BME280. */
 case TYPE_BME280:  return "BME280";
 case TYPE_BMP280:  return "BMP280";
#endif
 default:           return "Unknown";
 }
}

/** @return true if this sensor type is compiled-in and available. */
inline bool sensorTypeEnabled(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return true;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return true;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return true;
 case TYPE_BMP280:  return true;  /* same driver, one channel fewer */
#endif
 default:           return false;
 }
}

/** @return the default read interval in ms for this sensor type. */
inline uint32_t sensorDefaultIntervalMs(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return 1000;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return 2000;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return 5000;
 case TYPE_BMP280:  return 5000;
#endif
 default:           return 5000;
 }
}

/* ===========================================================================
 * Sensor display format — SensorValueFormat now lives in SensorChannels.h,
 * and the canonical presentation of each quantity in the channel table. A
 * driver only fills values[] when it deviates from the channel default.
 * =========================================================================== */

/** Complete driver metadata for a sensor type — channels + pin requirements.
 *
 * The driver says WHICH channels it reports and which pins it needs. What each
 * channel MEANS — unit, decimals, icon, storage letter, packing — comes from
 * the channel table, so a new driver reporting an existing quantity needs no
 * metadata at all beyond its mask.
 *
 * Adding a new sensor type requires ONLY a new entry in forType() + a driver file.
 */
struct SensorFormat {
 /** Bit N set = this sensor reports channel N (1 << CH_TEMP, 1 << CH_HUM, ...).
  *
  * Replaces the old `valueCount`. That was a COUNT, and every consumer read it
  * as "channels 0..count-1" — channels had to be a contiguous prefix of the
  * enum. A BMP280 measures temperature and pressure and no humidity, i.e.
  * channels {CH_TEMP, CH_PRESS} with a hole at CH_HUM, and a count simply
  * cannot say that. Which is why both 280s shared one type that claimed
  * humidity. */
 uint8_t          channelMask;
 /** Indexed BY CHANNEL, not by position: values[CH_PRESS] is the pressure
  *  format whether or not CH_HUM is present. Entries for absent channels are
  *  zeroed and must not be read — test channelMask first. */
 SensorValueFormat values[MAX_SENSOR_CHANNELS];
 uint8_t          pinCount;         /**< How many GPIO slots this sensor needs */
 PinRequirement   pins[MAX_SENSOR_PINS]; /**< Role + label per slot (unused are zeroed) */

 bool hasChannel(uint8_t ch) const {
 return ch < MAX_SENSOR_CHANNELS && (channelMask & (uint8_t)(1u << ch)) != 0;
 }
 /** How many channels this sensor actually reports. */
 uint8_t channelCount( ) const {
 uint8_t n = 0;
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) if (hasChannel(c)) n++;
 return n;
 }

 /** Factory: returns the complete driver metadata for a given sensor type. */
 static SensorFormat forType(SensorType t) {
 SensorFormat f = {};
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20:
 f.channelMask = (1u << CH_TEMP);
 f.pinCount  = 1;
 f.pins[0]   = {ROLE_DATA, "1-Wire", FLAG_PULLUP};
 break;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:
 f.channelMask = (1u << CH_TEMP) | (1u << CH_HUM);
 f.pinCount  = 1;
 f.pins[0]   = {ROLE_DATA, "Data", FLAG_PULLUP};
 break;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:
 f.channelMask = (1u << CH_TEMP) | (1u << CH_HUM) | (1u << CH_PRESS);
 f.pinCount  = 2;
 f.pins[0]   = {ROLE_I2C_SDA, "SDA", FLAG_PULLUP};
 f.pins[1]   = {ROLE_I2C_SCL, "SCL", FLAG_PULLUP};
 break;
 case TYPE_BMP280:
 /* The hole at CH_HUM is the whole point of the mask. */
 f.channelMask = (1u << CH_TEMP) | (1u << CH_PRESS);
 f.pinCount  = 2;
 f.pins[0]   = {ROLE_I2C_SDA, "SDA", FLAG_PULLUP};
 f.pins[1]   = {ROLE_I2C_SCL, "SCL", FLAG_PULLUP};
 break;
#endif
 default:
 f.channelMask = (1u << CH_TEMP);
 f.pinCount  = 1;
 f.pins[0]   = {ROLE_DATA, "Data", 0};
 break;
 }
 /* Presentation comes from the channel table, so a driver entry above says
  * only WHICH channels it reports. Every entry used to restate the unit,
  * decimals and icon of each channel it had, which is how {"°C", 1,
  * "thermometer"} ended up written once per driver. A driver that genuinely
  * deviates (same quantity, different unit) can still overwrite values[]
  * after calling this. */
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 if (f.hasChannel(c)) f.values[c] = channelInfo(c).display;
 }
 return f;
 }
};

/** @return number of measurement values this sensor produces. */
inline uint8_t sensorValueCount(SensorType t) {
 return SensorFormat::forType(t).channelCount( );
}

/** RP2040 I2C peripheral selection.
 *  Returns 0 for I2C0 (Wire), 1 for I2C1 (Wire1), or -1 if neither.
 *  Valid I2C0 pins: 0,1,4,5,8,9,12,13,16,17,20,21
 *  Valid I2C1 pins: 2,3,6,7,10,11,14,15,18,19,26,27
 *
 *  @note Wave 2 status: this function IS the selector for the BME280
 *        hardware-I2C path (SensorManager routes matching pairs to
 *        Wire/Wire1 and only falls back to PIO/bit-bang otherwise —
 *        loudly, see the WARN there). Zero PIO cost on the HW path. */
inline int i2cPeripheralForPins(uint8_t sda, uint8_t scl) {
    /* Bitmask of valid I2C pins: bit N set = pin N usable on that peripheral */
    constexpr uint32_t I2C0_MASK = (1u<<0)|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<8)|(1u<<9)
        |(1u<<12)|(1u<<13)|(1u<<16)|(1u<<17)|(1u<<20)|(1u<<21);
    constexpr uint32_t I2C1_MASK = (1u<<2)|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<11)
        |(1u<<14)|(1u<<15)|(1u<<18)|(1u<<19)|(1u<<26)|(1u<<27);
    bool sda0 = (I2C0_MASK & (1u << sda)) != 0;
    bool scl0 = (I2C0_MASK & (1u << scl)) != 0;
    bool sda1 = (I2C1_MASK & (1u << sda)) != 0;
    bool scl1 = (I2C1_MASK & (1u << scl)) != 0;
    if (sda0 && scl0) return 0;
    if (sda1 && scl1) return 1;
    return -1;
}

/** Auto-configure a GPIO pin based on its declared role.
 *  Called by SensorManager during initRuntimeSensors() for every pin
 *  declared by the sensor driver (pins[0..pinCount-1]).
 *
 *  Each role sets direction and pulls. I2C/SPI bus peripherals need
 *  separate Wire.begin()/SPI.begin() calls — see initRuntimeSensors().
 *
 *  ROLE_DATA is set as input with optional pull-up: single-wire sensors
 *  (DHT22, DS18B20) manage direction internally during read/write cycles.
 *  ROLE_POWER defaults to output LOW — sensor VCC off until driver activates.
 */
inline void gpioInitForRole(uint8_t gpio, PinRole role, uint8_t flags) {
 /* Skip gpio_init() for I2C and SPI SCK pins — the peripheral init
  * (Wire.begin() / SPI.begin()) configures the pin mux. Calling gpio_init()
  * would reset the pin to SIO mode, breaking I2C/SPI communication.
  * PIO-managed pins (ROLE_DATA for DS18B20/DHT22) are also skipped —
  * the PIO state machine owns the pin via pio_gpio_init(). */
 if (role != ROLE_I2C_SDA && role != ROLE_I2C_SCL && role != ROLE_SPI_SCK
     && role != ROLE_DATA) {
  gpio_init(gpio);
 }
 if (flags & FLAG_PULLUP)      gpio_pull_up(gpio);
 if (flags & FLAG_PULLDOWN)    gpio_pull_down(gpio);

 switch (role) {
 case ROLE_DATA:
     /* Single-wire: PIO driver owns the pin — no direction override needed. */
     break;
 case ROLE_UART_TX:
 case ROLE_SPI_MOSI:
 case ROLE_SPI_CS:
     gpio_set_dir(gpio, GPIO_OUT);
     break;
 case ROLE_POWER:
     gpio_set_dir(gpio, GPIO_OUT);
     gpio_put(gpio, 0); /* Start with sensor VCC off */
     break;
 case ROLE_UART_RX:
 case ROLE_SPI_MISO:
 case ROLE_ANALOG:
     gpio_set_dir(gpio, GPIO_IN);
     break;
 case ROLE_I2C_SDA:
 case ROLE_I2C_SCL:
     /* Pull-ups already applied above. Peripheral function set by Wire.begin(). */
     break;
 case ROLE_SPI_SCK:
     /* Peripheral function set by SPI.begin(). */
     break;
 default:
     break;
 }
}

/* Implementation — after SensorFormat definition (resolves circular dependency).
 *
 * Reads the mask. It used to be `channel < f.valueCount`, which answered "yes"
 * for CH_HUM on anything reporting two or more channels — including a part with
 * temperature and pressure and no humidity at all. */
inline bool sensorHasChannel(SensorType t, uint8_t channel) {
 return SensorFormat::forType(t).hasChannel(channel);
}

/**
 * @brief Channel id of the n-th channel this type reports, or CH_COUNT.
 *
 * For screens with a fixed number of rows. A caller that asks for row 0 and
 * row 1 gets temperature and humidity from a DHT22, temperature and pressure
 * from a BMP280 — instead of a humidity row the part cannot fill.
 */
inline uint8_t sensorNthChannel(SensorType t, uint8_t n) {
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 if (!sensorHasChannel(t, c)) continue;
 if (n == 0) return c;
 n--;
 }
 return CH_COUNT;
}

/**
 * @brief Number of editable alarm limits — two per channel the type reports.
 *
 * A screen that lists limits one per row asks this instead of assuming 4. A
 * DHT22 has 4, a BMP280 has 4 (but of different quantities), a BME280 has 6.
 */
inline uint8_t sensorLimitCount(SensorType t) {
 return (uint8_t)(sensorValueCount(t) * 2);
}

/** @brief Channel behind limit index i — 2n and 2n+1 share the n-th channel. */
inline uint8_t sensorLimitChannel(SensorType t, uint8_t i) {
 return sensorNthChannel(t, (uint8_t)(i / 2));
}

/** @brief True when limit index i is its channel's MAX, false for its MIN. */
inline bool sensorLimitIsMax(uint8_t i) { return (i & 1u) != 0; }

/** @return true if this sensor reports relative humidity. */
inline bool sensorHasHumidity(SensorType t) {
 return sensorHasChannel(t, CH_HUM);
}
