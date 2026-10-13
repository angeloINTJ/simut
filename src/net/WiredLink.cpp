/**
 * @file WiredLink.cpp
 * @brief The ENC28J60 behind the SIMUT Wire's network (see WiredLink.h).
 *
 * @project SIMUT
 * @license MIT License
 */
#include "WiredLink.h"

#if SIMUT_NET_LAN
#include <PicoENC28J60lwIP.h>

namespace {

ENC28J60Config makeConfig( ) {
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
	 * interrupts off. */
	c.rx_buffer_size = 0x1A00;
	return c;
}

PicoENC28J60lwIP eth(makeConfig( ));
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

/* What lwIP refuses or silently rearranges in a static address
 * (LwipIntf::ipAddressReorder: a mask whose first byte is not 255 is taken
 * for the DNS of the Arduino argument order, and the DNS becomes the mask),
 * checked before the module is told. */
const char* staticAddressError(const IPAddress& ip, const IPAddress& gateway, const IPAddress& mask) {
	if (!ip.isSet( )) return "static address: no IP";
	const uint32_t m = (uint32_t)mask[0] << 24 | (uint32_t)mask[1] << 16 | (uint32_t)mask[2] << 8 | mask[3];
	if (mask[0] != 255 || ((~m + 1) & ~m) != 0) return "static address: the mask is not a netmask";
	for (int i = 0; i < 4; i++) {
		if (gateway.isSet( ) && ((ip[i] ^ gateway[i]) & mask[i])) return "static address: the gateway is outside the subnet";
	}
	return nullptr;
}

}  // namespace

namespace WiredLink {

bool begin(const char* hostname, const IPAddress& ip, const IPAddress& gateway,
           const IPAddress& mask, const IPAddress& dns) {
	if (isStarted) return true;
	if (hostname && *hostname) eth.hostname(hostname);
	if (ip.isSet( )) {
		const char* bad = staticAddressError(ip, gateway, mask);
		/* config( ) answers false for what lwIP will not take (a gateway outside
		 * the subnet), and begin( ) would then run on 0.0.0.0, never connected and
		 * never saying why. The ESP order: ip, gateway, mask, dns. */
		if (bad || !eth.config(ip, gateway, mask, dns)) {
			lastErr = bad ? bad : "static address refused by lwIP";
			return false;
		}
	}
	isStarted = eth.begin(boardMac( ));
	lastErr = isStarted ? "" : PicoENC28J60::errorString(eth.lastError( ));
	return isStarted;
}

bool started( ) { return isStarted; }

bool linkUp( ) { return isStarted && eth.isLinked( ); }

bool connected( ) { return isStarted && eth.connected( ); }

IPAddress localIP( ) { return isStarted ? eth.localIP( ) : IPAddress( ); }
IPAddress subnetMask( ) { return isStarted ? eth.subnetMask( ) : IPAddress( ); }
IPAddress gatewayIP( ) { return isStarted ? eth.gatewayIP( ) : IPAddress( ); }
IPAddress dnsIP( ) { return isStarted ? eth.dnsIP( ) : IPAddress( ); }

void macAddress(uint8_t out[6]) {
	memcpy(out, boardMac( ), 6);
}

const char* lastError( ) { return lastErr; }

void end( ) {
	if (!isStarted) return;
	eth.end( );
	isStarted = false;
}

}  // namespace WiredLink
#endif
