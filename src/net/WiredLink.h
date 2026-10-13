/**
 * @file WiredLink.h
 * @brief The network of a build with a cable (the SIMUT Wire): a wired module
 *        on the framework's lwIP Ethernet interface (LwipIntfDev). Only an image
 *        whose network driver is a cable (SIMUT_NET_LAN, `net = "enc28j60"` in
 *        the manifest) has it.
 *
 * The API is the class's: any wired driver answers it. WiredLink.cpp holds what
 * the drivers share and includes, by the image's identity, the device header of
 * its chip (src/net/Enc28j60Device.h), which is the one place that includes the
 * chip's library, so the images with the radio keep the dependency graph they
 * always had. The pins, the INT line and the PIO block come from
 * src/simut_config.h, section 10b.
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
 *  (DHCP otherwise). False when the chip does not answer, the PIO has no room, or
 *  the static address is not one lwIP takes (no IP, a mask that is not a
 *  netmask, a gateway outside the subnet); lastError( ) says which, in words. A
 *  second call after a success does nothing. */
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
/** The interface's MAC, from the board's id; the same before begin( ). */
void macAddress(uint8_t mac[6]);

/** Why the last begin( ) failed ("" after a success). */
const char* lastError( );

/** The driver's name, for the log ("ENC28J60"): the chip of the image. */
const char* chipName( );

/** Stops the interface: the PIO state machine and the INT and poll workers go,
 *  so nothing touches the chip while the flash is written for an update (the
 *  OTA's teardown calls it through net::stopForFlash( ), src/net/NetLink.h). */
void end( );

}  // namespace WiredLink
#endif
