/**
 * @file display/HD44780_16x2.h
 * @brief HD44780 16x2 alphanumeric LCD driver.
 * @details Supports two interface modes selected at compile time:
 *
 *   HD44780_MODE_I2C (default) — PCF8574 I2C backpack (addr 0x27)
 *     Pins: SDA/SCL only. Set -DHD44780_I2C_ADDR=0x3F if needed.
 *
 *   HD44780_MODE_PARALLEL — 4-bit parallel GPIO
 *     Pins: RS, EN, D4, D5, D6, D7. Define via -DHD44780_RS=pin etc.
 *
 * Build flag: -DSIMUT_DISPLAY_ALPHA
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <Arduino.h>
/* Alpha build: GFX library excluded. Forward-declare only for TFT build. */
#if SIMUT_DISPLAY_TFT
class Adafruit_GFX;
#endif

/* ── Interface selection ───────────────────────────────────────────── */

#ifndef HD44780_MODE_I2C
#ifndef HD44780_MODE_PARALLEL
#define HD44780_MODE_I2C 1   /* default: I2C backpack */
#endif
#endif

/* ── I2C mode defaults ─────────────────────────────────────────────── */
/* GPIO 0-15 are reserved for sensors. I2C1 on GP26/GP27 (free in alpha). */

#if HD44780_MODE_I2C
#include <Wire.h>
#ifndef HD44780_I2C_ADDR
#define HD44780_I2C_ADDR 0x27
#endif
#ifndef HD44780_I2C_SDA
#define HD44780_I2C_SDA 26
#endif
#ifndef HD44780_I2C_SCL
#define HD44780_I2C_SCL 27
#endif
#define HD44780_WIRE Wire1  /* I2C1 bus */
#endif

/* ── Parallel mode pin defaults ────────────────────────────────────── */
/* GPIO 0-15 are reserved for sensors. GP16-21 = SPI pins, free in alpha. */

#if HD44780_MODE_PARALLEL
#ifndef HD44780_RS
#define HD44780_RS 16
#endif
#ifndef HD44780_EN
#define HD44780_EN 17
#endif
#ifndef HD44780_D4
#define HD44780_D4 18
#endif
#ifndef HD44780_D5
#define HD44780_D5 19
#endif
#ifndef HD44780_D6
#define HD44780_D6 20
#endif
#ifndef HD44780_D7
#define HD44780_D7 21
#endif
#endif

/* ── Driver struct ──────────────────────────────────────────────────── */

struct Hd44780_16x2 {
	bool initialized = false;

	/* Character framebuffer — 2 lines x 16 columns */
	char    line[2][17];
	uint8_t cursorCol = 0, cursorRow = 0;

	/* Screen dimensions (characters, for GFX compatibility) */
	static constexpr int16_t width  = 16;
	static constexpr int16_t height = 2;

	/* No pixel framebuffer — void* in alpha (GFX excluded). */
#if SIMUT_DISPLAY_TFT
	Adafruit_GFX* tft        = nullptr;
	Adafruit_GFX* canvas      = nullptr;
	Adafruit_GFX* canvasSmall = nullptr;
	Adafruit_GFX* gfx( ) { return tft; }
#else
	void* tft        = nullptr;
	void* canvas      = nullptr;
	void* canvasSmall = nullptr;
	void* gfx( ) { return tft; }
#endif

	/* ── Lifecycle ───────────────────────────────────────────────── */

	void begin( ) {
		if (initialized) return;
		memset(line[0], ' ', 16); line[0][16] = '\0';
		memset(line[1], ' ', 16); line[1][16] = '\0';
#if HD44780_MODE_I2C
		HD44780_WIRE.setSDA(HD44780_I2C_SDA);
		HD44780_WIRE.setSCL(HD44780_I2C_SCL);
		HD44780_WIRE.begin( );
#endif
#if HD44780_MODE_PARALLEL
		pinMode(HD44780_RS, OUTPUT);
		pinMode(HD44780_EN, OUTPUT);
		pinMode(HD44780_D4, OUTPUT);
		pinMode(HD44780_D5, OUTPUT);
		pinMode(HD44780_D6, OUTPUT);
		pinMode(HD44780_D7, OUTPUT);
		digitalWrite(HD44780_EN, LOW);
#endif
		_initLcd( );
		initialized = true;
	}

	/** Push framebuffer to LCD. */
	void blit( ) {
		if (!initialized) return;
		for (uint8_t row = 0; row < 2; row++) {
			_setDdramAddr(row == 0 ? 0x00 : 0x40);
			for (uint8_t col = 0; col < 16; col++) {
				_writeData(line[row][col]);
			}
		}
	}

	/* ── Character output ────────────────────────────────────────── */

	void setCursor(uint8_t col, uint8_t row) {
		cursorCol = (col < 16) ? col : 15;
		cursorRow = (row < 2)  ? row : 1;
	}

	void clear( ) {
		memset(line[0], ' ', 16);
		memset(line[1], ' ', 16);
		cursorCol = 0; cursorRow = 0;
	}

	size_t write(uint8_t c) {
		if (c == '\n') { cursorRow = 1; cursorCol = 0; return 1; }
		if (c < 32 && c > 7) return 1;  /* allow CGRAM chars 0-7 */
		if (cursorCol >= 16) { cursorCol = 0; cursorRow = 1; }
		if (cursorRow >= 2)  { cursorRow = 1; cursorCol = 0; }
		line[cursorRow][cursorCol++] = (char)c;
		return 1;
		}

	/** Load a 5x8 custom character bitmap into CGRAM.
	 *  @param num    Slot 0-7
	 *  @param bitmap 8-byte pixel pattern (LSB = top row) */
	void createChar(uint8_t num, const uint8_t* bitmap) {
		if (!initialized) return;
		_writeCmd(0x40 + (num & 0x07) * 8);
		for (uint8_t i = 0; i < 8; i++) {
			_writeData(pgm_read_byte(&bitmap[i]));
		}
		_writeCmd(0x80);  /* back to DDRAM address 0 */
	}

	size_t print(const char* s) {
		if (!s) return 0;
		size_t n = 0;
		while (*s) { write((uint8_t)*s++); n++; }
		return n;
	}

	/* ── No touch ────────────────────────────────────────────────── */
	bool getTouch(int16_t&, int16_t&, int16_t&) { return false; }
	bool touched( )      { return false; }
	bool isScreenTouched( ) { return false; }

private:
	/* ── Low-level write (interface-specific) ────────────────────── */

#if HD44780_MODE_I2C
	void _writeNibble(uint8_t nibble, bool rs) {
		uint8_t bits = (nibble & 0xF0) | 0x08 | (rs ? 0x01 : 0x00);
		HD44780_WIRE.beginTransmission(HD44780_I2C_ADDR);
		HD44780_WIRE.write(bits | 0x04);  /* EN=1 */
		HD44780_WIRE.write(bits & ~0x04); /* EN=0 */
		HD44780_WIRE.endTransmission( );
		delayMicroseconds(50);
	}
#endif

#if HD44780_MODE_PARALLEL
	void _writeNibble(uint8_t data, bool rs) {
		digitalWrite(HD44780_RS, rs ? HIGH : LOW);
		digitalWrite(HD44780_D4, (data >> 4) & 1);
		digitalWrite(HD44780_D5, (data >> 5) & 1);
		digitalWrite(HD44780_D6, (data >> 6) & 1);
		digitalWrite(HD44780_D7, (data >> 7) & 1);
		delayMicroseconds(1);  /* data setup: HD44780 needs >=40ns; RP2040 gpio_put ~20ns */
		digitalWrite(HD44780_EN, HIGH);
		delayMicroseconds(1);
		digitalWrite(HD44780_EN, LOW);
		delayMicroseconds(50);
	}
#endif

	void _writeByte(uint8_t data, bool isCmd) {
		_writeNibble(data & 0xF0, !isCmd);
		_writeNibble(data << 4,   !isCmd);
	}

	void _writeCmd(uint8_t cmd)   { _writeByte(cmd, true); }
	void _writeData(uint8_t data) { _writeByte(data, false); }
	void _setDdramAddr(uint8_t a) { _writeCmd(0x80 | a); }

	void _initLcd( ) {
		/* Force known state: keep EN low, let power stabilize */
		digitalWrite(HD44780_EN, LOW);
		delay(100);

		/* 4-bit init sequence (HD44780 datasheet). Use ms delays for
		   compatibility with clone controllers that need longer settling. */
		for (uint8_t i = 0; i < 3; i++) {
			_writeNibble(0x30, false);
			delay(5);
		}
		_writeNibble(0x20, false);  /* switch to 4-bit mode */
		delay(2);

		_writeCmd(0x28); /* 4-bit, 2 lines, 5x8 */
		_writeCmd(0x08); /* display OFF */
		_writeCmd(0x01); /* clear display */
		delay(5);
		_writeCmd(0x06); /* increment, no shift */
		_writeCmd(0x0C); /* display ON, cursor OFF, no blink */
	}
};

