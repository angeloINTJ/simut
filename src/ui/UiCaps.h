/**
 * @file UiCaps.h
 * @brief Which UI driver the touch panel has (its IDENTITY) and what the rest of
 *        the firmware may know of it (its CAPABILITIES). Pure: no #include, no code.
 *
 * What the touch panel looks like is a choice made when the image is built, the way
 * the network driver is (src/net/NetCaps.h): `ui = "<id>"` in tools/features.toml
 * names a [ui.<id>], and an image has that driver and no other.
 *
 *   IDENTITY  one macro per driver, and exactly one is 1.
 *     SIMUT_UI_ANGULO     the Angulo panel: the driver of pico2_w_release since
 *                         2026-10-30 (S10c C3) and of pico_wiretft_angulo. The manifest
 *                         passes -DSIMUT_UI_ANGULO=1 (`ui = "angulo"`); the original passes nothing.
 *     SIMUT_UI_ORIGINAL   the panel of the Pico W release, and of pico2_w_original.
 *                         The default: an image that names no UI driver has it. Derived below as "not Angulo".
 *
 *   CAPABILITY  what the driver can do, derived from the identity. Every file that
 *               needs to know asks the capability, not the driver, so that a third
 *               driver changes this header and not its callers.
 *     SIMUT_UI_CUSTOM_THEMES  the theme catalogue and the .thm files a user
 *                         uploads: the original's. Angulo has colours of its own.
 *                         Read by WebManager_Files.cpp: where it is 0, the rescan after a .thm is uploaded or deleted,
 *                         and its log record, are not compiled. The file itself is stored and deleted like any other; the Angulo
 *                         neither offers it as a theme nor applies it, and it takes effect when the board runs the original
 *                         (S10c, A'4). The original keeps the value it has today.
 *     SIMUT_UI_PACK_TEXTS  the driver reads a section of its own from the language pack: the Angulo's @ANGULO, the texts it draws
 *                         (src/ui/angulo/Textos.h). Read by the pack's scanner (LangPackIndex.h), the loader (DisplayManager_LangParser.cpp) and
 *                         the ActiveLang record (DisplayManager.h): where it is 0 the scanner takes @ANGULO for an unknown directive, as every
 *                         image did before S10c B2b, and nothing is kept. Not the identity, which check_ui_boundary.py keeps out of those files.
 *
 * The derived macros are never passed with -D. Passed, they could disagree with
 * the identity (an Angulo image that still says it is the original); derived here
 * they cannot, and the first #error below stops the build that tries, naming what
 * to pass instead.
 *
 * Test them with `#if`, never with `#ifdef`, `#ifndef` or `defined( )`: after
 * this header SIMUT_UI_ANGULO is always defined, 0 on the original, so
 * `#ifdef SIMUT_UI_ANGULO` is true on every image (shown with the host
 * preprocessor in the review of 2026-10-14).
 *
 * A file that tests one of these macros includes this header or simut_config.h
 * ITSELF, by a path relative to its own, and not through another header: `#if X`
 * with X undefined is 0, silently, and a feature drops out of the image (it
 * happened to the network class on 2026-10-13, see src/net/NetCaps.h). This header
 * has no #include and no code, so every file may take it, the host test builds too.
 *
 * Angulo draws on the touch panel, so it needs SIMUT_DISPLAY_TFT. That is checked
 * in simut_config.h, after the display macros are settled and not here, because
 * this header is pure and cannot see them.
 *
 * tools/test_ui_caps.py preprocesses this header and simut_config.h on the host
 * and holds each answer, and takes each #error out in turn to see exactly its
 * cases fail.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

// A derived macro passed from outside is a second source of truth: an image built
// with -DSIMUT_UI_ORIGINAL=1 -DSIMUT_UI_ANGULO=1 would test true for both. The test
// is `defined`, so the value (even 0) does not matter, as for the network's class.
#if defined(SIMUT_UI_ORIGINAL) || defined(SIMUT_UI_CUSTOM_THEMES) || defined(SIMUT_UI_PACK_TEXTS)
#error "SIMUT_UI_ORIGINAL, SIMUT_UI_CUSTOM_THEMES and SIMUT_UI_PACK_TEXTS are derived from the UI driver in src/ui/UiCaps.h and never passed: name the driver instead (-DSIMUT_UI_ANGULO=1 for Angulo, nothing for the original, the default)"
#endif

#ifndef SIMUT_UI_ANGULO
#define SIMUT_UI_ANGULO 0        // 1 = the Angulo panel, the driver of pico2_w_release since 2026-10-30 (S10c C3) and of pico_wiretft_angulo (the manifest's `ui = "angulo"`)
#endif
#define SIMUT_UI_ORIGINAL (!SIMUT_UI_ANGULO)   // the default: the panel of the Pico W release, and of pico2_w_original, unless another driver is named

// ORIGINAL is the negation of ANGULO, so the two add up to 1 for any 0 or 1 and to
// something else for -DSIMUT_UI_ANGULO=2, a value that would test true yet is not a
// driver. Counting keeps "exactly one" true when a third driver joins the sum.
#if (SIMUT_UI_ORIGINAL + SIMUT_UI_ANGULO) != 1
#error "exactly one UI driver per image: SIMUT_UI_ANGULO is 0 (the original panel, the default) or 1 (Angulo), nothing else"
#endif

#define SIMUT_UI_CUSTOM_THEMES (SIMUT_UI_ORIGINAL)
#define SIMUT_UI_PACK_TEXTS (SIMUT_UI_ANGULO)   // S10c, B2b: the Angulo has a section of the language pack of its own, @ANGULO
