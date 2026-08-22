/**
 * @file SensorChannelTable.h
 * @brief THE channel table — one row per measurement axis, read by every layer.
 *
 * @details This is the file to edit when the system learns a new quantity.
 * A row binds a channel to two things: how it is stored (letter, bit width,
 * scale, signedness) and how it is shown (a preset from SensorPresets.h).
 *
 * ADDING A QUANTITY
 *   1. Append a row below and bump CH_COUNT in SensorChannels.h.
 *   2. Pick or add the display preset in SensorPresets.h.
 *   3. Set the bit in the driver's channelMask in SensorHelpers.h.
 * Nothing else should need an edit. Anything that still does is a layer that
 * has not been generalized yet — tools/check_channels.py fails the build when
 * it finds one.
 *
 * The storage columns are not free parameters: they reproduce exactly what
 * histV4DefaultBitWidth( ), histV4DefaultScale( ) and the `channel == 0`
 * signedness test used to hardcode. Changing one changes how history is packed.
 * That is survivable because the .h5 file carries its schema chunks, so existing
 * files keep the widths they were written with — but new and old records of the
 * same channel would then disagree, so treat it as a format change.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include "SensorPresets.h" /* pulls SensorChannels.h for the types */

/**
 * @brief The table. Row index == channel id.
 *
 * A function-local static rather than a header constant: one copy across all
 * translation units without needing C++17 inline variables, which the embedded
 * build does not pin a standard for.
 */
inline const ChannelInfo* channelTable( ) {
	static const ChannelInfo t[CH_COUNT] = {
		/* defMin/defMax preserve the factory alarm band the two configurable
		 * quantities always had (0..40 °C, 20..80 %). Pressure and luminosity
		 * get their whole plausible range instead: nobody has set a limit for
		 * them, and a default that trips is worse than no default. */
		/*         key      name           ltr bits scale signed  saneMin      saneMax     defMin    defMax  i18nKey      display preset */
		/* CH_TEMP  */ { "temp",  "Temperature", 't', 16,  100, true,  -327.68f,     327.66f,    0.0f,     40.0f, "ch_temp",  SensorPresets::TEMPERATURE_CELSIUS },
		/* CH_HUM   */ { "hum",   "Humidity",    'u', 10,   10, false,     0.0f,      102.3f,   20.0f,     80.0f, "ch_hum",   SensorPresets::HUMIDITY_PERCENT    },
		/* CH_PRESS */ { "press", "Pressure",    'p', 14,   10, false,     0.0f,     1638.3f,    0.0f,   1638.3f, "ch_press", SensorPresets::PRESSURE_HPA        },
		/* CH_LUX   */ { "lux",   "Luminosity",  'l', 24,  100, false,     0.0f, 167772.15f,     0.0f, 167772.15f, "ch_lux",   SensorPresets::LIGHT_LUX           },
	};
	return t;
}

/** @return true if `ch` names a real channel. */
inline bool channelValid(uint8_t ch) { return ch < CH_COUNT; }

/**
 * @brief Row for `ch`, or a neutral fallback for an unknown channel.
 *
 * Never returns null: lookups happen in display, API and codec paths where a
 * null check at every call site would be noise. The fallback carries the 'x'
 * that histV4ChannelPrefix( ) has always returned for an unknown channel, so a
 * corrupt or future id still yields a parseable measurement key.
 */
inline const ChannelInfo& channelInfo(uint8_t ch) {
	static const ChannelInfo unknown = {
		"unk", "Channel", 'x', 16, 100, true,
		-327.68f, 327.66f, -327.68f, 327.66f, "ch_unk", { "", 1, "" }
	};
	return channelValid(ch) ? channelTable( )[ch] : unknown;
}

/** @return channel id for a calib.csv / V4 letter, or -1 if no row claims it. */
inline int channelByLetter(char c) {
	for (uint8_t i = 0; i < CH_COUNT; i++) {
		if (channelTable( )[i].letter == c) return (int)i;
	}
	return -1;
}

/** @return channel id for an API key ("press"), or -1. Case-sensitive by design. */
inline int channelByKey(const char* key) {
	if (!key) return -1;
	for (uint8_t i = 0; i < CH_COUNT; i++) {
		const char* k = channelTable( )[i].key;
		const char* p = key;
		while (*k && *k == *p) { k++; p++; }
		if (*k == '\0' && *p == '\0') return (int)i;
	}
	return -1;
}

/* The table is indexed by channel id, so a row per channel is not optional. */
static_assert(CH_COUNT <= MAX_SENSOR_CHANNELS,
              "CH_COUNT exceeds MAX_SENSOR_CHANNELS — raising the cap changes "
              "SensorRecord::channelBitWidth[] and needs a flash schema bump");
