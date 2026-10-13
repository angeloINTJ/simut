/**
 * @file NetLink.h
 * @brief The two things the rest of the firmware asks of whatever network the
 *        image has, answered by the driver the image names:
 *
 *   net::linkUp( )        the link carries traffic: a datagram sent now can leave.
 *   net::stopForFlash( )  the network stops, so nothing reaches the chip while the
 *                         flash is written (the OTA's teardown, ota/orchestrator.cpp).
 *
 * The syslog asked the first and the OTA the second, each with an `#if` of its
 * own and the radio's API on one arm (SyslogManager.cpp and ota/orchestrator.cpp,
 * until 2026-10-13). A third driver would have been a third arm in each of them,
 * and in the next file that asks. They ask here, once, and neither includes
 * <WiFi.h> or WiredLink.h for it.
 *
 * The entry is chosen by the image's driver, as the display's is in
 * src/display/DisplayDriver.h: the CYW43 by its identity, a cable by its class,
 * because every cable answers WiredLink.h and a W5500 needs no entry of its own.
 * A driver that is neither stops the build at the #error below, which says what
 * is missing, and not at a `net::linkUp` the compiler cannot find.
 *
 * Each arm is the very call its caller made before it moved here, and the seven
 * radio images (Pico W and Pico 2 W) kept their sha256 (2026-10-13).
 * That took two things the plain code does not say:
 *   - always_inline, not just `inline`. Left to -Os, the wired arm stayed a
 *     function of its own (16 B, and the three call sites 8 B shorter each), and
 *     on the RP2350 the radio arm was folded in after the branch predictor had
 *     run, which laid SyslogManager::flushBlocking( ) out another way (+8 B).
 *   - the wired arm is an early return and not `linkUp( ) && connected( )`: the
 *     `&&` of an inlined function reverses the branches of flushBlocking( ) (the
 *     same instructions in another block order, +8 B in pico_wiretft).
 *
 * What the boot asks of the chip is src/net/NetBoot.h's; what the pages, the
 * console and the panels ask is the class's, through NetworkManager.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include "NetCaps.h"   /* the identity and the class are read below: included here, not left to another header */

#if SIMUT_NET_CYW43

#include <WiFi.h>

namespace net {

/** The CYW43's own state: joined to an access point. */
inline __attribute__((always_inline)) bool linkUp( ) { return WiFi.status( ) == WL_CONNECTED; }

/** WiFi.end( ), the radio's own stop. */
inline __attribute__((always_inline)) void stopForFlash( ) { WiFi.end( ); }

}  // namespace net

#elif SIMUT_NET_LAN

#include "WiredLink.h"

namespace net {

/** The cable is in and the interface has an address. A wired build's WiFi.status( )
 *  is the radio's that is not there, and never says WL_CONNECTED, so it asks the
 *  module (2026-10-13, review of the wired driver). */
inline __attribute__((always_inline)) bool linkUp( ) {
	if (!WiredLink::linkUp( )) return false;
	return WiredLink::connected( );
}

/** A wired build has no radio to stop: the module stops instead (WiredLink::end( );
 *  for the ENC28J60 its PIO state machine and its INT handler are released), so
 *  nothing reaches it while the flash is written. */
inline __attribute__((always_inline)) void stopForFlash( ) { WiredLink::end( ); }

}  // namespace net

#else
#error "this network driver has no entry in src/net/NetLink.h: add its linkUp( ) and stopForFlash( ) (a cable of any chip is covered by the SIMUT_NET_LAN entry; see \"Adding a driver\" in src/net/NetCaps.h)"
#endif
