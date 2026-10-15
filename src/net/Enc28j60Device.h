/**
 * @file Enc28j60Device.h
 * @brief The ENC28J60 behind the wired link: what src/net/WiredLink.cpp needs of
 *        a chip and cannot know of it. Included by that unit only, and only by
 *        an image whose identity is SIMUT_NET_ENC28J60 (`net = "enc28j60"`).
 *
 * WiredLink.cpp is what every Ethernet driver of arduino-pico has in common.
 * They are all LwipIntfDev<RawDev> (ENC28J60lwIP, Wiznet5500lwIP,
 * Wiznet5100lwIP, ... and this chip's own library, PicoENC28J60lwIP), with the
 * same hostname( ), config( ), begin(mac), connected( ), the four address
 * getters and end( ). It is written once, over one object, and a chip gives it
 * only what the class cannot say for every chip. A device header, one per
 * chip (src/net/<Chip>Device.h), provides:
 *
 *   WiredEth           the driver's class, a LwipIntfDev<RawDev>. Its header
 *                      must bring in <LwipIntfDev.h>, which all of them do: the
 *                      board's MAC helper (_cyw43_hal_generate_laa_mac) is
 *                      defined there, and WiredLink.cpp takes it from there.
 *   wiredEth( )        the one object, built here because its constructor is
 *                      the chip's own: this one takes a PIO block, the others
 *                      a SPI bus, a chip-select and an interrupt pin, and the
 *                      pins come from simut_config.h under the chip's #if.
 *   wiredLinked( )     the PHY sees a link partner. LwipIntfDev::linkStatus( )
 *                      answers Unknown for a chip that cannot tell, and what
 *                      that means for the cable is the chip's call.
 *   wiredErrorText( )  why begin( ) failed, in words, from the chip's own error
 *                      code; "" for a chip that gives none. The console shows
 *                      it (WiredLink::lastError( )).
 *   wiredChipName( )   the name the log gives the module ("ENC28J60").
 *
 * and nothing else: no state, no behaviour. A W5500Device.h is that list over
 * Wiznet5500lwIP, about 25 lines, plus its #elif in WiredLink.cpp and the
 * other steps of "Adding a driver" in src/net/NetCaps.h.
 *
 * The object is at namespace scope, in an anonymous namespace like everything
 * WiredLink.cpp has of its own, and not a function-local static. The static
 * moved the construction into an out-of-line wiredEth( ) of 420 B with a
 * 4 B guard: pico_wire went from 607,184 to 607,256 B (2026-10-13). Built
 * where it always was, the object left the image byte for byte what it was
 * before the chip moved out of WiredLink.cpp (pico_wire sha256 3edd1440...,
 * measured with the move alone). The price is that this header has ONE
 * includer, WiredLink.cpp: a second would get an interface of its own,
 * silently.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include "../simut_config.h"   /* SIMUT_NET_ENC28J60 and the module's pins are read below: included here, not left to another header */

#if SIMUT_NET_ENC28J60
#include <PicoENC28J60lwIP.h>

using WiredEth = PicoENC28J60lwIP;

/** The module's wiring: simut_config.h section 10b. */
inline ENC28J60Config enc28j60Config( ) {
	ENC28J60Config c;
	c.miso_pin = SIMUT_ENC_MISO;
	c.cs_pin   = SIMUT_ENC_CS;
	c.sclk_pin = SIMUT_ENC_SCK;
	c.mosi_pin = SIMUT_ENC_MOSI;
	/* No reset line: the chip resets over SPI. */
	c.rst_pin = -1;
	c.irq_pin = SIMUT_ENC_INT_PIN;
	c.pio_instance = SIMUT_ENC_PIO;
	/* The library's largest receive buffer, 6.5 KB of the chip's 8 KB: four
	 * full-size frames wait in the chip while a flash write holds the
	 * interrupts off. The price, since the library's 1.3.0: the 1.5 KB left to
	 * send from holds one full-size frame, and PicoENC28J60lwIP uploads a
	 * frame while the one before is on the wire only when the area holds two
	 * (its default, 0x141C). Here a full-size frame waits for the one before,
	 * as every frame did with 1.2.0. */
	c.rx_buffer_size = 0x1A00;
	return c;
}

namespace {

WiredEth eth(enc28j60Config( ));

inline WiredEth& wiredEth( ) { return eth; }

inline bool wiredLinked( ) { return eth.isLinked( ); }

inline const char* wiredErrorText( ) { return PicoENC28J60::errorString(eth.lastError( )); }

inline const char* wiredChipName( ) { return "ENC28J60"; }

}  // namespace
#endif
