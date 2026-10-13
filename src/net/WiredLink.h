/**
 * @file WiredLink.h
 * @brief The SIMUT Wire's network: the ENC28J60 module on the cable, through the
 *        PicoENC28J60 library's lwIP interface. Only a build with the `lan`
 *        switch (SIMUT_NET_LAN) has it.
 *
 * WiredLink.cpp is the one unit that includes <PicoENC28J60lwIP.h>, so the images
 * with the radio keep the dependency graph they always had. The pins, the INT line
 * and the PIO block come from src/simut_config.h, section 10b.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include "../simut_config.h"

#if SIMUT_NET_LAN
#include <Arduino.h>
#include <IPAddress.h>

namespace WiredLink {

/** Starts the module once: the hostname, then a static address when `ip` is set
 *  (DHCP otherwise). False when the chip does not answer or the PIO has no room;
 *  lastError( ) says which, in words. A second call after a success does nothing. */
bool begin(const char* hostname, const IPAddress& ip, const IPAddress& gateway,
           const IPAddress& mask, const IPAddress& dns);

/** begin( ) succeeded and end( ) was not called since. */
bool started( );
/** The cable is in and the PHY sees a link partner. */
bool linkUp( );
/** The link is up and the interface has an address. */
bool connected( );

IPAddress localIP( );
IPAddress subnetMask( );
IPAddress gatewayIP( );
IPAddress dnsIP( );
void macAddress(uint8_t mac[6]);

/** Why the last begin( ) failed ("" after a success). */
const char* lastError( );

/** Stops the interface: the PIO state machine and the INT and poll workers go,
 *  so nothing touches the chip while the flash is written for an update. */
void end( );

}  // namespace WiredLink
#endif
