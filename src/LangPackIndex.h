/**
 * @file LangPackIndex.h
 * @brief Where each section of a .lng language pack starts and ends, found in
 *        one streamed pass, so the loader reads @DICT and nothing else.
 *
 * @details Until 2026-10-02 loadLangFile( ) read every byte before @WEBDICT
 * into one malloc — 16,351 B for es-ES — copied @DICT (2,910 B) out of it and
 * freed the rest straight away: @LOGCODES and @TRL are never looked up on the
 * device, and @HELP is read again from flash when the CLI asks for it. The
 * 16 KB ceiling on that malloc was therefore spent on bytes thrown away, and
 * es-ES sat 33 B under it. With the ranges below the loader seeks straight to
 * @DICT, so the boot peak is the dictionary itself, and the ceiling bounds
 * what actually stays on the heap.
 *
 * The rules are the ones the in-buffer parser applied, restated for a stream:
 *  - a directive is a line whose first byte is '@'; its name runs to the first
 *    space, tab, CR or LF and must match exactly ("@DICTX" is not @DICT);
 *  - a section's body starts on the line after its directive and ends where
 *    the next directive starts, known or not, or at the end of the file;
 *  - a section that appears twice keeps its last occurrence;
 *  - @NAME and @CODE carry their value on the directive line itself, without
 *    leading spaces and tabs or trailing CR, spaces and tabs;
 *  - @WEBDICT has to be the file's suffix, so a directive after its marker
 *    line marks the pack malformed (tools/check_lang_packs.py refuses to ship
 *    one, and the loader refuses to load one).
 *
 * One result differs, on purpose. The old parser ran the section just before
 * @WEBDICT one byte long — it counted the newline it appended to its own
 * buffer — so a lazy read of that section from the file returned the '@' of
 * "@WEBDICT" as its last character. In both shipped packs that section was
 * @LICENSE, and the '@' sat alone on the last line of the License screen.
 *
 * Pure and header-only, like TelContentType.h: `pio test -e native` covers it
 * (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** The sections the firmware tells apart. The order here means nothing; in a
 *  file the order is free, except that @WEBDICT comes last. */
enum LangSection : uint8_t {
	LANG_SEC_NAME = 0,
	LANG_SEC_CODE,
	LANG_SEC_DICT,
	LANG_SEC_HELP,
	LANG_SEC_LOGCODES,
	LANG_SEC_TRL,
	LANG_SEC_WEBDICT,
	LANG_SEC_COUNT
};

/** What one pass over a pack found. For a section, [start, end) is its body as
 *  file offsets. For @NAME and @CODE it is the value on the directive line. */
struct LangPackIndex {
	uint32_t start[LANG_SEC_COUNT];
	uint32_t end[LANG_SEC_COUNT];
	bool present[LANG_SEC_COUNT];
	/** A directive followed @WEBDICT's marker line. */
	bool tailAfterWebDict;
};

/** Feed it the file in order, in chunks of any size, then call finish( ). */
class LangPackScanner {
public:
	LangPackScanner( ) {
		memset(&_ix, 0, sizeof(_ix));
	}

	void feed(const char* p, size_t n) {
		for (size_t i = 0; i < n; i++, _pos++) {
			const char c = p[i];
			switch (_st) {
			case ST_BODY:
				if (_atCol0 && c == '@') {
					if (_inWebDict) _ix.tailAfterWebDict = true;
					closeBody(_pos);
					_st = ST_NAME;
					_dirLen = 0;
					_dirLong = false;
				}
				break;
			case ST_NAME:
				if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
					_line = identify( );
					_valOpen = false;
					_valStart = _valEnd = _pos;
					if (c == '\n') {
						endLine(_pos + 1);
					} else {
						_st = ST_LINE;
						lineByte(c);
					}
				} else if (_dirLen < sizeof(_dir)) {
					_dir[_dirLen++] = c;
				} else {
					_dirLong = true;
				}
				break;
			case ST_LINE:
				if (c == '\n') endLine(_pos + 1);
				else lineByte(c);
				break;
			}
			_atCol0 = (c == '\n');
		}
	}

	/** Closes whatever the end of the file left open: a directive line with no
	 *  newline after it, and the body that runs to the last byte. */
	void finish(LangPackIndex& out) {
		if (_st == ST_NAME) {
			_line = identify( );
			_valOpen = false;
			_valStart = _valEnd = _pos;
		}
		if (_st != ST_BODY) endLine(_pos);
		closeBody(_pos);
		out = _ix;
	}

private:
	enum State : uint8_t { ST_BODY, ST_NAME, ST_LINE };

	LangPackIndex _ix;
	uint32_t _pos = 0;
	uint32_t _valStart = 0, _valEnd = 0;
	State _st = ST_BODY;
	int8_t _cur = -1;        /* section whose body is open; -1 = none */
	int8_t _line = -1;       /* section named by the directive line being read */
	bool _atCol0 = true;
	bool _inWebDict = false; /* @WEBDICT's marker line has ended */
	bool _valOpen = false;
	bool _dirLong = false;
	uint8_t _dirLen = 0;
	char _dir[8];            /* "LOGCODES", the longest name, fits exactly */

	int8_t identify( ) const {
		if (_dirLong) return -1;
		static const struct { const char* name; uint8_t len; LangSection sec; } kDirs[] = {
			{ "NAME", 4, LANG_SEC_NAME },         { "CODE", 4, LANG_SEC_CODE },
			{ "DICT", 4, LANG_SEC_DICT },         { "HELP", 4, LANG_SEC_HELP },
			{ "LOGCODES", 8, LANG_SEC_LOGCODES }, { "TRL", 3, LANG_SEC_TRL },
			{ "WEBDICT", 7, LANG_SEC_WEBDICT },
		};
		for (const auto& d : kDirs) {
			if (_dirLen == d.len && memcmp(_dir, d.name, d.len) == 0) return (int8_t)d.sec;
		}
		return -1;
	}

	/* A byte of the directive line after the name: only @NAME and @CODE keep
	 * anything from it. */
	void lineByte(char c) {
		if (_line != LANG_SEC_NAME && _line != LANG_SEC_CODE) return;
		if (!_valOpen) {
			if (c == ' ' || c == '\t') return;
			_valOpen = true;
			_valStart = _valEnd = _pos;
		}
		if (c != '\r' && c != ' ' && c != '\t') _valEnd = _pos + 1;
	}

	void endLine(uint32_t bodyAt) {
		_st = ST_BODY;
		if (_line < 0) return;   /* unknown directive: it only ended a body */
		_ix.present[_line] = true;
		if (_line == LANG_SEC_NAME || _line == LANG_SEC_CODE) {
			_ix.start[_line] = _valStart;
			_ix.end[_line] = _valOpen ? _valEnd : _valStart;
			return;
		}
		_ix.start[_line] = _ix.end[_line] = bodyAt;
		_cur = _line;
		if (_line == LANG_SEC_WEBDICT) _inWebDict = true;
	}

	void closeBody(uint32_t at) {
		if (_cur >= 0) _ix.end[_cur] = at;
		_cur = -1;
	}
};
