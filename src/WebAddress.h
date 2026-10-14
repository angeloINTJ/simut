/**
 * @file WebAddress.h
 * @brief The address of this device's web server, written the way a browser needs it.
 *
 * @details The server listens on the configured port (`web port`, 80 by
 * default) and, on a release with a certificate, speaks TLS on 443 unless a
 * port was configured (WebManager::beginServer). Every place that told an
 * operator where the setup AP's page is wrote `192.168.4.1` and nothing
 * else: the console's `[AP] URL` line, the `ap` reply, the panel's "Access on
 * mobile" line and the alpha's first AP page. On the rig, configured for
 * 8080, that address refused the connection with the AP up (2026-10-13, the
 * #268 bench).
 * These leave the default case reading as it always did.
 *
 * Pure, so the host suite checks the forms (test_network). One function for
 * the three forms, defined once in WebAddress.cpp: inline in this header, its
 * copies in three units made the change cost the release 536 B, and this way
 * it costs 328 B (measured 2026-10-14).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** The setup AP's own address (NetworkManager::beginAP). */
#define SIMUT_AP_IP_TEXT "192.168.4.1"

/** Which form of the address to write. */
enum WebAddrForm : uint8_t {
	WEB_ADDR_URL,     /**< `http://192.168.4.1:8080`: the console and the `ap` reply */
	WEB_ADDR_TYPED,   /**< `192.168.4.1:8080`, `https://192.168.4.1`: what a person types */
	WEB_ADDR_SUFFIX,  /**< `:8080`, ` (https)`: after a line that already ends in the bare address */
};

/** The port only when it is not the scheme's own (80, 443), and a port of 0,
 * which nobody set, reads as the default.
 *
 * WEB_ADDR_TYPED drops plain HTTP's `http://`, which a browser fills in, and
 * keeps `https://`, which a browser would not. WEB_ADDR_SUFFIX writes what
 * the panel's "Access on mobile: 192.168.4.1" lacks, `ip` unused: that line
 * is translated, and its text lives in every language pack a device has
 * installed, so the address is completed after it rather than rewritten
 * inside it. Always terminated; returns `out`. */
const char* webAddress(char* out, size_t n, const char* ip, uint16_t port, bool tls,
                       WebAddrForm form = WEB_ADDR_URL);
