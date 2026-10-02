/**
 * @file WebDictValue.h
 * @brief One string out of a language pack's @WEBDICT, read as a stream.
 *
 * @details The License screen opens with the sentences the /license web page
 * shows, in the language of the installed pack. Those sentences are already
 * in the pack, in @WEBDICT — one JSON object on one line, 23 KB in es-ES —
 * which the loader never reads into RAM: the browser gets it straight from
 * flash. This reads one key's value out of that stream through a small chunk,
 * so the screen can have the page's own words without either a copy of the
 * blob in RAM or a second translation of them in the pack.
 *
 * What it matches is the token "<key>", optional whitespace, a colon, optional
 * whitespace, and an opening quote. In valid JSON that sequence only occurs at
 * a key: inside a string every quote is escaped, so "<key>" with bare quotes
 * on both sides cannot be the content of a value, and a value that equals the
 * key's name is followed by ',' or '}', never by ':'. The value is decoded —
 * \n, \t, \", \\, \/, \uXXXX with surrogate pairs — into UTF-8. When a key
 * appears twice the last one wins, as JSON.parse does in the browser, so the
 * panel shows what the page shows.
 *
 * A value longer than the buffer is cut at a character boundary, never inside
 * one, and clipped( ) says so.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

class WebDictValue {
public:
	static constexpr size_t KEY_MAX = 31;

	WebDictValue(const char* key, char* out, size_t cap) : _out(out), _cap(cap) {
		if (_out && _cap) _out[0] = '\0';
		_pat[_patLen++] = '"';
		while (key && *key && _patLen < KEY_MAX + 1) _pat[_patLen++] = *key++;
		_pat[_patLen++] = '"';
		_fail[0] = 0;
		for (size_t i = 1, k = 0; i < _patLen; i++) {
			while (k > 0 && _pat[i] != _pat[k]) k = _fail[k - 1];
			if (_pat[i] == _pat[k]) k++;
			_fail[i] = (uint8_t)k;
		}
	}

	void feed(const char* p, size_t n) {
		for (size_t i = 0; i < n; i++) step((unsigned char)p[i]);
	}

	/** The closing quote of a value was seen. */
	bool done( ) const { return _done; }
	/** The value did not fit; what is in the buffer is its head. */
	bool clipped( ) const { return _clipped; }
	size_t length( ) const { return _len; }

private:
	enum State : uint8_t { S_KEY, S_COLON, S_OPEN, S_VALUE, S_ESC, S_HEX };

	char* _out;
	size_t _cap;
	size_t _len = 0;
	char _pat[KEY_MAX + 3];
	uint8_t _fail[KEY_MAX + 3];
	size_t _patLen = 0;
	size_t _m = 0;            /* how much of the pattern matched so far */
	State _st = S_KEY;
	bool _done = false;
	bool _clipped = false;
	uint32_t _hex = 0;
	uint8_t _hexN = 0;
	uint32_t _hi = 0;         /* a high surrogate waiting for its pair */
	unsigned char _raw[4];    /* a raw UTF-8 sequence being collected */
	uint8_t _rawN = 0, _rawWant = 0;

	static bool ws(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	void match(unsigned char c) {
		while (_m > 0 && c != (unsigned char)_pat[_m]) _m = _fail[_m - 1];
		if (c == (unsigned char)_pat[_m]) _m++;
		if (_m == _patLen) { _st = S_COLON; _m = 0; }
	}

	void step(unsigned char c) {
		switch (_st) {
		case S_KEY:
			match(c);
			break;
		case S_COLON:
			if (ws(c)) break;
			if (c == ':') { _st = S_OPEN; break; }
			_st = S_KEY; match(c);
			break;
		case S_OPEN:
			if (ws(c)) break;
			if (c == '"') {
				/* a later occurrence replaces an earlier one */
				_len = 0; _done = false; _clipped = false; _hi = 0; _rawN = 0;
				if (_out && _cap) _out[0] = '\0';
				_st = S_VALUE;
				break;
			}
			_st = S_KEY; match(c);
			break;
		case S_VALUE:
			if (c == '"') { endRaw( ); flushHi( ); _done = true; _st = S_KEY; break; }
			if (c == '\\') { endRaw( ); _st = S_ESC; break; }
			flushHi( );
			rawByte(c);
			break;
		case S_ESC:
			_st = S_VALUE;
			switch (c) {
			case 'n': flushHi( ); put('\n'); break;
			case 't': flushHi( ); put('\t'); break;
			case 'r': flushHi( ); put('\r'); break;
			case 'b': flushHi( ); put('\b'); break;
			case 'f': flushHi( ); put('\f'); break;
			case 'u': _st = S_HEX; _hex = 0; _hexN = 0; break;
			default:  flushHi( ); put(c); break;       /* \" \\ \/ */
			}
			break;
		case S_HEX: {
			const int v = (c >= '0' && c <= '9') ? c - '0'
			            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
			            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
			if (v < 0) {                     /* not JSON; keep the character */
				flushHi( ); emit(0xFFFD);
				_st = S_VALUE; step(c);
				break;
			}
			_hex = (_hex << 4) | (uint32_t)v;
			if (++_hexN == 4) { _st = S_VALUE; unit(_hex); }
			break;
		}
		}
	}

	void unit(uint32_t u) {
		if (u >= 0xD800 && u <= 0xDBFF) { flushHi( ); _hi = u; return; }
		if (u >= 0xDC00 && u <= 0xDFFF) {
			if (_hi) { const uint32_t cp = 0x10000 + ((_hi - 0xD800) << 10) + (u - 0xDC00); _hi = 0; emit(cp); }
			else emit(0xFFFD);
			return;
		}
		flushHi( );
		emit(u);
	}

	void flushHi( ) { if (_hi) { _hi = 0; emit(0xFFFD); } }

	void emit(uint32_t cp) {
		unsigned char b[4];
		size_t n;
		if (cp < 0x80)         { b[0] = (unsigned char)cp; n = 1; }
		else if (cp < 0x800)   { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); n = 2; }
		else if (cp < 0x10000) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F);
		                         b[2] = 0x80 | (cp & 0x3F); n = 3; }
		else                   { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F);
		                         b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); n = 4; }
		putSeq(b, n);
	}

	/* Raw UTF-8 in the JSON is collected a whole character at a time, so a
	 * value cut at the buffer's end never ends in half a character. */
	void rawByte(unsigned char c) {
		if (_rawN > 0 && (c & 0xC0) == 0x80) {
			_raw[_rawN++] = c;
			if (_rawN == _rawWant) endRaw( );
			return;
		}
		endRaw( );
		if (c >= 0xC0 && c <= 0xF7) {
			_raw[0] = c; _rawN = 1;
			_rawWant = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
			return;
		}
		put(c);
	}

	void endRaw( ) {
		if (_rawN) { putSeq(_raw, _rawN); _rawN = 0; }
	}

	void put(unsigned char c) { putSeq(&c, 1); }

	void putSeq(const unsigned char* b, size_t n) {
		if (_clipped) return;
		if (!_out || _len + n + 1 > _cap) { _clipped = true; return; }
		for (size_t i = 0; i < n; i++) _out[_len++] = (char)b[i];
		_out[_len] = '\0';
	}
};
