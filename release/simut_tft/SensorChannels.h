/**
 * @file SensorChannels.h
 * @brief Channel and unit TYPES. No table, no catalogue — just the vocabulary.
 *
 * @details Split into its own dependency-free header so the three layers can
 * stack without a cycle:
 *
 *   SensorChannels.h    types      (this file, <stdint.h> only)
 *        ^
 *   SensorPresets.h     the unit catalogue — ~80 quantities, one entry each
 *        ^
 *   SensorChannelTable.h  binds each channel to its storage identity + a preset
 *        ^
 *   SensorHelpers.h     per-driver metadata (which channels, which pins)
 *
 * A CHANNEL is a measurement axis with an identity in storage, calibration and
 * the APIs. A PRESET is how a quantity is displayed in one particular unit.
 * They are not the same thing: PRESSURE_HPA and PRESSURE_PSI are one channel
 * shown two ways, which is why the unit lives in a preset and the packing
 * lives in the channel row.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

/* Same guard the other headers use, so this file can appear anywhere in the
 * include chain without fighting over who defines it. */
#ifndef MAX_SENSOR_CHANNELS
#define MAX_SENSOR_CHANNELS 4
#endif

/** Universal measurement axes. Channel 0 is temperature for every sensor. */
enum SensorChannel : uint8_t {
	CH_TEMP  = 0, /**< Temperature */
	CH_HUM   = 1, /**< Relative humidity */
	CH_PRESS = 2, /**< Atmospheric pressure */
	CH_LUX   = 3, /**< Luminosity */
	CH_COUNT = 4  /**< Sentinel — must equal the number of rows in the table */
};

/** How to display one value: unit, decimal places, icon. */
struct SensorValueFormat {
	const char* unit;     /**< "°C", "%", "hPa", "pH", "ppm", "" */
	uint8_t     decimals; /**< Decimals to SHOW; the channel's scale decides what is STORED */
	const char* icon;     /**< Icon identifier for procedural drawing */
};

/**
 * @brief Everything true of a QUANTITY rather than of a driver.
 *
 * Adding a measurement axis means appending one row to the table in
 * SensorChannelTable.h and bumping CH_COUNT. A driver then only sets the bit
 * in its channelMask — it does not restate what the channel means, which is
 * what made `{"°C", 1, "thermometer"}` appear once per driver and what let the
 * calibration code hardcode its own copy of the letters histV4ChannelPrefix( )
 * already knew.
 */
struct ChannelInfo {
	const char*       key;      /**< Stable identifier in the JSON APIs: "temp", "press" */
	const char*       name;     /**< English display name: "Temperature". Translate via i18nKey. */
	char              letter;   /**< calib.csv id prefix AND V4 measurement-key prefix */
	uint8_t           bitWidth; /**< V4 packing width */
	uint32_t          scale;    /**< V4 raw = round(value * scale) */
	bool              isSigned; /**< Negative readings are meaningful (temperature) */
	float             saneMin;  /**< Plausible range: chart axis defaults + sanity checks */
	float             saneMax;
	const char*       i18nKey;  /**< Label key shared by stat cards and calibration inputs */
	/** Canonical presentation. The unit here is also the unit the channel is
	 *  STORED in: a driver that reads PSI converts to hPa before reporting,
	 *  rather than writing a second unit into the same history column. A
	 *  driver may still override presentation per channel via
	 *  SensorFormat::values[]. */
	SensorValueFormat display;
};
