/**
 * @file StaticAddress.h
 * @brief What lwIP refuses, or silently rearranges, in a static address, checked
 *        before a wired module is told. Pure: only <IPAddress.h>, so the host
 *        suite reaches it (test/test_wired/test_main.cpp).
 *
 * It lived in WiredLink.cpp until 2026-10-13, in an anonymous namespace no test
 * could call. The rules are lwIP's and not a chip's, so every wired driver
 * shares them.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include <IPAddress.h>

/**
 * Why `ip`, `gateway` and `mask` are not a static address lwIP takes, in words,
 * or nullptr when they are. A gateway of 0.0.0.0 means none and is taken.
 *
 * The first-byte rule is lwIP's (LwipIntf::ipAddressReorder, called by every
 * LwipIntfDev::config( )): when the third address of (ip, gateway, mask, dns)
 * does not start with 255, the four are read in the Arduino order (ip, dns,
 * gateway, mask) instead: the mask is taken for the gateway, the DNS for the
 * mask, the gateway for the DNS. So a mask such as 254.0.0.0, a netmask all the
 * same, is refused here, before lwIP rearranges it.
 */
inline const char* staticAddressError(const IPAddress& ip, const IPAddress& gateway, const IPAddress& mask) {
	if (!ip.isSet( )) return "static address: no IP";
	const uint32_t m = (uint32_t)mask[0] << 24 | (uint32_t)mask[1] << 16 | (uint32_t)mask[2] << 8 | mask[3];
	if (mask[0] != 255 || ((~m + 1) & ~m) != 0) return "static address: the mask is not a netmask";
	for (int i = 0; i < 4; i++) {
		if (gateway.isSet( ) && ((ip[i] ^ gateway[i]) & mask[i])) return "static address: the gateway is outside the subnet";
	}
	return nullptr;
}
