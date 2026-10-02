/**
 * @file display/SettingsMenu.h
 * @brief The panel's Settings menu: its items, the order they are listed in,
 *        who sees each one, and what a row reads.
 *
 * @details The menu lists only what the bits of the account at the panel can
 * open (v24), so a row is not an item: the list is built per session, and the
 * screen draws `_menuItems[row]`. Three things key on the ITEM id, never on
 * the row — the event Core 1 sends when a row is entered (EVT_MENU_SELECT,
 * dispatched in AppManager_Events.cpp), the icon the row draws, and the label.
 * The ids keep the order the items were written in; MENU_ORDER is the order
 * they are shown in.
 *
 * Row numbers belong to the list and not to the labels: a row reads "N. " and
 * its label when the menu lists every item, and its label alone when the list
 * is filtered, because a filtered list numbered by the full menu would skip.
 * Until 2026-10-02 each translation carried its row's number ("1. Visual
 * Themes"), so the screen stripped it from filtered lists and from screen
 * titles; packs already installed still carry those numbers, and
 * menuLabelNoNumber( ) still strips them.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Normally these come from SystemDefs_Limits.h, which pulls the sensor headers
 * with it — the same standalone-header trick as FsSecretPath.h, so the host
 * test can include this file alone. DisplayManager.h includes SystemDefs.h
 * first, so in the firmware the #ifndef keeps the one definition there. */
#ifndef PERM_SYS_CONFIG
#define PERM_SYS_CONFIG 0x0008
#endif
#ifndef PERM_NET_CONFIG
#define PERM_NET_CONFIG 0x0010
#endif
#ifndef PERM_USER_MGR
#define PERM_USER_MGR 0x0100
#endif
#ifndef PERM_ALARM_LIMITS
#define PERM_ALARM_LIMITS 0x0400
#endif
#ifndef PERM_ALARM_BLOCK
#define PERM_ALARM_BLOCK  0x0800
#endif
#ifndef PERM_MAINT
#define PERM_MAINT        0x1000
#endif
#ifndef PERM_PANEL_ALARM_ANY
#define PERM_PANEL_ALARM_ANY (PERM_ALARM_LIMITS | PERM_ALARM_BLOCK | PERM_MAINT)
#endif

/** The items, by id. An id is the value EVT_MENU_SELECT carries and the icon
 *  the row draws, so a new item is appended here and placed in MENU_ORDER. */
enum SettingsMenuItem : uint8_t {
	MENU_THEMES,          /**< Visual Themes */
	MENU_ALARMS,          /**< Alarm Limits */
	MENU_SOUNDS,          /**< Alarm Sounds */
	MENU_LANG,            /**< System Language */
	MENU_OWN_PIN,         /**< Change Password: the session's own PIN */
	MENU_TOUCH_CAL,       /**< Touch Calibration */
	MENU_LICENSE,         /**< License */
	MENU_STATUS,          /**< System Status */
	MENU_DISPLAY_OFFSET,  /**< Display Alignment */
	MENU_USERS,           /**< Users (v24) */
	MENU_PIN_POLICY,      /**< PIN security (v25) */
	MENU_SETUP_AP,        /**< Configuration Mode: the setup access point (2.7.1) */
	MENU_CLOCK,           /**< Date and time (2026-10-01) */
	MENU_ITEM_COUNT
};

/* The bit each item needs, by id; 0 is everyone's: one's own PIN, the license
 * and the status screen. Core 0 checks the bit again when the event arrives,
 * because authorisation lives there — leaving a row out is only courtesy.
 * The tables are `inline constexpr` for the reason PinKeypad.h gives: one copy
 * in the image, not one per object file. */
inline constexpr uint16_t MENU_NEED[] = {
	PERM_SYS_CONFIG,      /* MENU_THEMES */
	PERM_PANEL_ALARM_ANY, /* MENU_ALARMS */
	PERM_SYS_CONFIG,      /* MENU_SOUNDS */
	PERM_SYS_CONFIG,      /* MENU_LANG */
	0,                    /* MENU_OWN_PIN */
	PERM_SYS_CONFIG,      /* MENU_TOUCH_CAL */
	0,                    /* MENU_LICENSE */
	0,                    /* MENU_STATUS */
	PERM_SYS_CONFIG,      /* MENU_DISPLAY_OFFSET */
	PERM_USER_MGR,        /* MENU_USERS */
	PERM_USER_MGR,        /* MENU_PIN_POLICY */
	PERM_NET_CONFIG,      /* MENU_SETUP_AP */
	PERM_SYS_CONFIG,      /* MENU_CLOCK: /api/set_time's bit */
};
static_assert(sizeof(MENU_NEED) / sizeof(MENU_NEED[0]) == MENU_ITEM_COUNT,
              "every Settings item needs its bit in MENU_NEED");

/* The order the rows are listed in: the order the items were written in. */
inline constexpr uint8_t MENU_ORDER[] = {
	MENU_THEMES, MENU_ALARMS, MENU_SOUNDS, MENU_LANG,
	MENU_OWN_PIN, MENU_TOUCH_CAL, MENU_LICENSE, MENU_STATUS,
	MENU_DISPLAY_OFFSET, MENU_USERS, MENU_PIN_POLICY, MENU_SETUP_AP,
	MENU_CLOCK,
};
static_assert(sizeof(MENU_ORDER) / sizeof(MENU_ORDER[0]) == MENU_ITEM_COUNT,
              "every Settings item needs its place in MENU_ORDER");

/** Fills `out` with the ids a session holding `perms` sees, in menu order, and
 *  returns how many. `out` has room for every item: an account holding every
 *  bit sees them all. */
inline uint8_t settingsMenuBuild(uint16_t perms, uint8_t out[MENU_ITEM_COUNT]) {
	uint8_t n = 0;
	for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
		const uint8_t item = MENU_ORDER[i];
		if (MENU_NEED[item] == 0 || (perms & MENU_NEED[item])) out[n++] = item;
	}
	return n;
}

/** "11. PIN security" -> "PIN security"; anything that does not start with
 *  digits, a dot and a space comes back as it is. A screen titled after its
 *  menu item uses it, and so does every row. noinline: a translation unit
 *  with a single caller inlined its own copy, 28 B in drawPinPolicy( )
 *  beside the shared one (measured 2026-10-02, pico_w_release). */
inline __attribute__((noinline)) const char* menuLabelNoNumber(const char* s) {
	const char* p = s;
	while (*p >= '0' && *p <= '9') p++;
	if (p != s && p[0] == '.' && p[1] == ' ') return p + 2;
	return s;
}

/** What row `row` (from 0) of a list of `count` items reads: the label, with
 *  the row's number in front when the list holds every item. Returns either
 *  a tail of `label` or `buf`. */
inline const char* settingsMenuRowText(const char* label, uint8_t row, uint8_t count,
                                       char* buf, size_t size) {
	label = menuLabelNoNumber(label);
	if (count < MENU_ITEM_COUNT) return label;
	snprintf(buf, size, "%u. %s", (unsigned)row + 1u, label);
	return buf;
}
