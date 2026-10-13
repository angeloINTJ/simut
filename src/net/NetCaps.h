/**
 * @file NetCaps.h
 * @brief Which network driver the image has (its IDENTITY) and what the rest of
 *        the firmware may know of it (its CLASS). Pure: no #include, no code.
 *
 * Until 2026-10-13 one macro, SIMUT_NET_LAN, said two things at once: "the link
 * is a cable" (hide the SSID, show Cable) and "the chip is the ENC28J60" (its
 * pins GP11-GP15, its PIO program, its library, the 11 sensor slots). A second
 * wired board (W5500, W5100, ...) would have inherited the first one's pins.
 * The two are apart now:
 *
 *   IDENTITY  one macro per driver, and exactly one is 1.
 *     SIMUT_NET_CYW43     the radio of the Pico W and the Pico 2 W. The default:
 *                         an image that names no driver has it.
 *     SIMUT_NET_ENC28J60  an ENC28J60 module on a plain Pico. The manifest passes
 *                         -DSIMUT_NET_ENC28J60=1 (`net = "enc28j60"` in
 *                         tools/features.toml); the CYW43 passes nothing.
 *     Only src/net/, src/simut_config.h (the module's pins), this file and
 *     src/NetworkManager.cpp read an identity macro: the last is the radio's
 *     driver until it moves to src/net/, and reads SIMUT_NET_CYW43 twice, with
 *     the reason in tools/net_boundary.json. tools/check_net_boundary.py holds
 *     it in CI, with the radio's API and the driver libraries.
 *
 *   CLASS     what the link is, derived below from the identity and read by
 *             every other file.
 *     SIMUT_NET_WIFI      joins by SSID and password: scan, setup access point,
 *                         signal level. It is SIMUT_NET_CYW43.
 *     SIMUT_NET_LAN       a cable: the address comes by DHCP or from the
 *                         configuration. It is SIMUT_NET_ENC28J60.
 *
 * The class is never passed with -D. Passed, it could disagree with the identity
 * (a LAN image with the radio's pins); derived here it cannot, and the first
 * #error below stops the build that tries, naming what to pass instead.
 *
 * A file that tests SIMUT_NET_WIFI or SIMUT_NET_LAN includes this header or
 * simut_config.h ITSELF, by a path relative to its own, and not through another
 * header: `#if X` with X undefined is 0, silently, and a feature drops out of
 * the image. It happened on 2026-10-13, when a pure header testing
 * SIMUT_NET_WIFI took the feature out of the radio images. This header has no
 * #include and no code, so every file may take it, the host test builds too.
 *
 * Adding a driver (W5500, W5100, ...): four edits, all in directive lines below.
 *   1. its identity macro, default 0, after SIMUT_NET_ENC28J60's:
 *        #ifndef SIMUT_NET_W5500
 *        #define SIMUT_NET_W5500 0
 *        #endif
 *   2. its term in the default of SIMUT_NET_CYW43: (!SIMUT_NET_ENC28J60 && !SIMUT_NET_W5500)
 *   3. its term in the count of the second #error:
 *        (SIMUT_NET_CYW43 + SIMUT_NET_ENC28J60 + SIMUT_NET_W5500) != 1
 *   4. its class: SIMUT_NET_LAN (SIMUT_NET_ENC28J60 || SIMUT_NET_W5500) for a
 *      cable (a new Wi-Fi driver goes into SIMUT_NET_WIFI the same way).
 * Outside this file: a [net.<id>] in tools/features.toml whose `flags` pass the
 * identity macro, its pins and PIO program under their own `#if` in
 * simut_config.h, and its OTA variant names (simut_config.h refuses a wired
 * driver that has none). A driver that is not a cable also needs its entry in
 * src/net/NetLink.h (linkUp( ), stopForFlash( ); a cable of any chip is covered
 * by the SIMUT_NET_LAN one), and a chip that is reset at boot, its arm in
 * src/net/NetBoot.h. For a cable, also its device header,
 * src/net/<Chip>Device.h: the WiredEth type and the four functions wiredEth( ),
 * wiredLinked( ), wiredErrorText( ) and wiredChipName( ) (Enc28j60Device.h says
 * what each is for), with its `#elif SIMUT_NET_<ID>` before the `#else` of the
 * include chain in src/net/WiredLink.cpp, which stops at an #error naming this
 * step for a wired driver that has none. tools/test_net_caps.py applies the four
 * edits to a toy W5500, and the device header with its #elif, and holds what
 * they must give, so a change here or there that breaks the recipe breaks the
 * test.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#if defined(SIMUT_NET_LAN) || defined(SIMUT_NET_WIFI)
#error "SIMUT_NET_LAN and SIMUT_NET_WIFI are derived from the network driver in src/net/NetCaps.h and never passed: name the driver instead (-DSIMUT_NET_ENC28J60=1 for the cable, nothing for the CYW43, the default)"
#endif

#ifndef SIMUT_NET_ENC28J60
#define SIMUT_NET_ENC28J60 0     // 1 = an ENC28J60 module on the cable, on a plain Pico (no CYW43)
#endif
#ifndef SIMUT_NET_CYW43
#define SIMUT_NET_CYW43 (!SIMUT_NET_ENC28J60)   // the default: the radio, unless another driver is named
#endif

#if (SIMUT_NET_CYW43 + SIMUT_NET_ENC28J60) != 1
#error "exactly one network driver per image: SIMUT_NET_CYW43 (the default) or SIMUT_NET_ENC28J60, not both and not neither"
#endif

#define SIMUT_NET_WIFI (SIMUT_NET_CYW43)
#define SIMUT_NET_LAN (SIMUT_NET_ENC28J60)
