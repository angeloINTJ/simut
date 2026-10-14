/**
 * @file WiredLink.cpp
 * @brief The wired link of a build with a cable (see WiredLink.h): what every
 *        LwipIntfDev Ethernet driver of arduino-pico has in common, over the
 *        one chip the image names.
 *
 * The chip's own part, its library, its object and its pins, is in the device
 * header chosen by the image's identity below (Enc28j60Device.h says what a
 * device header provides). Nothing here names a chip.
 *
 * @project SIMUT
 * @license MIT License
 */
#include "WiredLink.h"
#include "../simut_config.h"   /* SIMUT_NET_LAN is tested below: included here, not left to another header */

#if SIMUT_NET_LAN
#include "StaticAddress.h"

/* The device header is the one place a chip's library is included. A wired
 * driver without one stops here, naming what is missing, and not at a
 * `wiredEth` the compiler cannot find. */
#if SIMUT_NET_ENC28J60
#include "Enc28j60Device.h"
#else
#error "a wired driver needs its device header here: src/net/<Chip>Device.h and its #elif (\"Adding a driver\" in src/net/NetCaps.h)"
#endif

namespace {

bool isStarted = false;
const char* lastErr = "";

/* The MAC the core's LwipIntfDev::begin( ) makes when given none: the board's
 * locally administered address with its fourth byte moved by the interface
 * number, 2 on a build whose only netif is this one; locally administered
 * and unicast. Made here and handed to begin( ), so the getters answer it
 * before the module starts too (a module that does not start left /api/status
 * and the telemetry with 00:00:00:00:00:00), and it is the address the bench
 * Wire has had since 2026-10-12, 2A:15:E3:4E:18:24. */
uint8_t mac[6];
bool macMade = false;

const uint8_t* boardMac( ) {
	if (!macMade) {
		_cyw43_hal_generate_laa_mac(0, mac);
		mac[3] += 2;
		mac[0] = (uint8_t)((mac[0] & 0xfe) | 0x02);
		macMade = true;
	}
	return mac;
}

}  // namespace

namespace WiredLink {

bool begin(const char* hostname, const IPAddress& ip, const IPAddress& gateway,
           const IPAddress& mask, const IPAddress& dns) {
	if (isStarted) return true;
	if (hostname && *hostname) wiredEth( ).hostname(hostname);
	if (ip.isSet( )) {
		const char* bad = staticAddressError(ip, gateway, mask);
		/* config( ) answers false for what lwIP will not take (a gateway outside
		 * the subnet), and begin( ) would then run on 0.0.0.0, never connected and
		 * never saying why. The ESP order: ip, gateway, mask, dns. */
		if (bad || !wiredEth( ).config(ip, gateway, mask, dns)) {
			lastErr = bad ? bad : "static address refused by lwIP";
			return false;
		}
	}
	isStarted = wiredEth( ).begin(boardMac( ));
	lastErr = isStarted ? "" : wiredErrorText( );
	return isStarted;
}

bool started( ) { return isStarted; }

bool linkUp( ) { return isStarted && wiredLinked( ); }

bool connected( ) { return isStarted && wiredEth( ).connected( ); }

IPAddress localIP( ) { return isStarted ? wiredEth( ).localIP( ) : IPAddress( ); }
IPAddress subnetMask( ) { return isStarted ? wiredEth( ).subnetMask( ) : IPAddress( ); }
IPAddress gatewayIP( ) { return isStarted ? wiredEth( ).gatewayIP( ) : IPAddress( ); }
IPAddress dnsIP( ) { return isStarted ? wiredEth( ).dnsIP( ) : IPAddress( ); }

void macAddress(uint8_t out[6]) {
	memcpy(out, boardMac( ), 6);
}

const char* chipName( ) { return wiredChipName( ); }

const char* lastError( ) { return lastErr; }

void end( ) {
	if (!isStarted) return;
	wiredEth( ).end( );
	isStarted = false;
}

}  // namespace WiredLink
#endif
