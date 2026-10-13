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

}  // namespace

namespace WiredLink {

bool begin(const char* hostname, const IPAddress& ip, const IPAddress& gateway,
           const IPAddress& mask, const IPAddress& dns) {
	if (isStarted) return true;
	if (hostname && *hostname) eth.hostname(hostname);
	if (ip.isSet( )) {
		eth.config(ip, gateway, mask, dns);   /* the ESP order: ip, gateway, mask, dns */
	}
	isStarted = eth.begin( );
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

void macAddress(uint8_t mac[6]) {
	if (isStarted) {
		eth.macAddress(mac);
	} else {
		memset(mac, 0, 6);
	}
}

const char* lastError( ) { return lastErr; }

void end( ) {
	if (!isStarted) return;
	eth.end( );
	isStarted = false;
}

}  // namespace WiredLink
#endif
