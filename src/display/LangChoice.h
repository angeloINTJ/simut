/**
 * @file display/LangChoice.h
 * @brief The panel's Idioma screen as plain decisions: how many rows it lists,
 *        which one it opens on, which choice Core 0 accepts, and which choice
 *        has to wait for a boot.
 *
 * @details Until 2026-10-24 the screen listed a pack only when it was LOADED,
 * and a pack loads only at a boot in another language: in English it would
 * keep its @DICT on the heap (at most LANG_DICT_MAX + 2 = 6,146 B; 3,294 B with
 * the shipped pt-BR, check_lang_packs.py) and, on the Angulo images, its @ANGULO
 * table too (8,416 B in RAM with the shipped pt-BR, host_telas.py; both measured
 * 2026-10-28). So a unit that
 * booted in English listed English alone, and nothing on a product image could
 * write the language back: the emergency CLI has no `language`, and the web has
 * no key for it. The screen now lists a pack it has only FOUND in /lang, its
 * name and code read from the file at boot with nothing kept on the heap.
 * Choosing it saves the language and says "Reboot required": the pack, the
 * console, the log and the web page follow at the next boot, as they always did.
 *
 * Plain ints, like SettingsMenu.h, so the host test needs no display. The
 * ceiling of two rows is LANG_COUNT (SystemDefs_Records.h); the panel's
 * DisplayManager_i18n.cpp asserts that they agree, so a third language has to
 * come here too.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

/** English, plus one pack: LANG_COUNT. */
constexpr int LANG_CHOICE_ROWS = 2;

/** How many rows the screen lists. A pack that is in RAM is listed whatever
 *  the scan of /lang said; one only found at boot is listed too, and it is not
 *  in RAM. */
constexpr int langRowsListed(bool packLoaded, bool packFoundAtBoot) {
	return (packLoaded || packFoundAtBoot) ? LANG_CHOICE_ROWS : 1;
}

/** The row the screen opens on: the saved language if that row is listed,
 *  English otherwise. A unit saved in a pack it could not load used to open on
 *  a row that was not there, with nothing highlighted. */
constexpr int langRowAtOpen(int saved, int rows) {
	return (saved >= 0 && saved < rows) ? saved : 0;
}

/** Whether Core 0 takes the id the screen sent. The screen cannot send a row
 *  it does not list, but this side does not take the screen's word. */
constexpr bool langChoiceValid(int chosen, int rows) {
	return chosen >= 0 && chosen < rows;
}

/** Whether a valid choice has to wait for the next boot. A pack that is not in
 *  RAM translates nothing yet, and loading it now would be a malloc of its
 *  @DICT in the middle of the uptime. English never waits, and a pack already
 *  loaded applies at once. */
constexpr bool langAppliesAtBoot(int chosen, bool packLoaded) {
	return chosen != 0 && !packLoaded;
}
