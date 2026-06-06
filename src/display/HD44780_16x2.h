/**
 * @file display/HD44780_16x2.h
 * @brief HD44780 16x2 alphanumeric LCD driver (I2C backpack / PCF8574).
 * @details Character display with 16 columns x 2 rows. No pixel graphics,
 * no touch. Communication via I2C (default addr 0x27) to save GPIO pins.
 *
 * Build flag: -DSIMUT_DISPLAY_ALPHA
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

/* I2C backpack commonly uses PCF8574 at address 0x27 (or 0x3F). */
#ifndef HD44780_I2C_ADDR
#define HD44780_I2C_ADDR 0x27
#endif

struct Hd44780_16x2 {
	/* ── Hardware state ──────────────────────────────────────────── */
	bool    initialized = false;
	uint8_t i2cAddr     = HD44780_I2C_ADDR;

	/* Character framebuffer — 2 lines x 16 columns + null terminators.
	 * Updated by write( ) / print( ) / setCursor( ) emulation.
	 * blit( ) sends changed characters to the LCD via I2C. */
	char    line[2][17];
	uint8_t cursorCol = 0, cursorRow = 0;

	/* Screen dimensions in characters (GFX compatibility). */
	static constexpr int16_t width  = 16;
	static constexpr int16_t height = 2;

	/* No pixel framebuffer — always nullptr. */
	void*   canvas      = nullptr;
	void*   canvasSmall = nullptr;

	/* No GFX delegate (character display, not pixel). */
	void*   gfx( ) { return nullptr; }
	void*   tft( ) { return nullptr; }

	/* ── Lifecycle ───────────────────────────────────────────────── */

	void begin( ) {
		if (initialized) return;
		memset(line[0], ' ', 16); line[0][16] = '\0';
		memset(line[1], ' ', 16); line[1][16] = '\0';
		Wire.begin( );
		_initLcd( );
		initialized = true;
	}

	/** Sends changed characters to the LCD via I2C.
	 *  Called by the render loop after updating the framebuffer. */
	void blit( ) {
		if (!initialized) return;
		for (uint8_t row = 0; row < 2; row++) {
			_setDdramAddr(row == 0 ? 0x00 : 0x40);
			for (uint8_t col = 0; col < 16; col++) {
				_writeData(line[row][col]);
			}
		}
	}

	/* ── Character output (GFX print compatibility) ──────────────── */

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
		if (c < 32) return 1;              /* skip control chars */
		if (cursorCol >= 16) { cursorCol = 0; cursorRow = 1; }
		if (cursorRow >= 2)  { cursorRow = 1; cursorCol = 0; }
		line[cursorRow][cursorCol++] = (char)c;
		return 1;
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
	/* ── I2C low-level (PCF8574 backpack) ────────────────────────── */

	void _writeByte(uint8_t data, bool isCmd) {
		uint8_t rs = isCmd ? 0x00 : 0x01;
		uint8_t nibbleH = (data & 0xF0) | 0x08 | rs;  /* EN=1, BL=1 */
		uint8_t nibbleL = ((data << 4) & 0xF0) | 0x08 | rs;

		Wire.beginTransmission(i2cAddr);
		Wire.write(nibbleH);
		Wire.write(nibbleH | 0x04);  /* pulse EN */
		Wire.write(nibbleH & ~0x04);
		Wire.write(nibbleL);
		Wire.write(nibbleL | 0x04);
		Wire.write(nibbleL & ~0x04);
		Wire.endTransmission( );
		delayMicroseconds(50);
	}

	void _writeCmd(uint8_t cmd)  { _writeByte(cmd, true); }
	void _writeData(uint8_t data) { _writeByte(data, false); }
	void _setDdramAddr(uint8_t addr) { _writeCmd(0x80 | addr); }

	void _initLcd( ) {
		delay(50);
		/* 4-bit init sequence (HD44780 datasheet) */
		for (uint8_t i = 0; i < 3; i++) {
			Wire.beginTransmission(i2cAddr);
			Wire.write(0x30 | 0x08);
			Wire.write(0x30 | 0x0C);
			Wire.write(0x30 | 0x08);
			Wire.endTransmission( );
			delayMicroseconds(4500);
		}
		/* Set 4-bit mode */
		Wire.beginTransmission(i2cAddr);
		Wire.write(0x20 | 0x08);
		Wire.write(0x20 | 0x0C);
		Wire.write(0x20 | 0x08);
		Wire.endTransmission( );
		delayMicroseconds(150);

		/* Function: 4-bit, 2 lines, 5x8 dots */
		_writeCmd(0x28);
		/* Display ON, cursor OFF, blink OFF */
		_writeCmd(0x0C);
		/* Entry mode: increment, no shift */
		_writeCmd(0x06);
		/* Clear display */
		_writeCmd(0x01);
		delay(2);
	}
};
