/**
 * @file pico/cyw43_arch.h (native stub)
 * @brief Just enough of the SDK header for the radio's unit of NetworkManager
 *        (src/net/NetworkManager_Cyw43.cpp) to compile here.
 *
 * NetworkManager calls cyw43_arch_enable_sta_mode( ) on exactly one path:
 * starting a scan while the setup AP is up, where the STA interface is down
 * and asking cyw43_wifi_scan( ) anyway is the documented way to leave
 * wifi_scan_state stuck at 1 for the rest of the boot (see WiFi.h).
 *
 * The count is observable so a test can assert the STA interface was brought
 * up BEFORE the sweep, which is the whole point of that path — a stub that
 * only swallowed the call would let the ordering regress silently.
 *
 * Since 2026-10-18 the unit also reads two return codes of the driver: one read
 * the chip has to answer, made when a scan that never finished started too
 * quickly to say whether the chip answered it, and the link's level. g_cyw43Rc
 * is what the chip answers both: 0, or the driver's error for a chip that went
 * silent — -CYW43_EIO (-6: "F2 not ready", the first ~16 sends, in milliseconds)
 * or -CYW43_ETIMEDOUT (-2: each send after the SDPCM credits ran out, 1 s each). A
 * failed RSSI read leaves the caller's variable as it was, as the driver does.
 * g_cyw43Ioctls counts both calls, so a test can tell that the chip was asked.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

extern unsigned g_cyw43StaModeEnables;

inline void cyw43_arch_enable_sta_mode( ) { g_cyw43StaModeEnables++; }

enum { CYW43_ITF_STA = 0, CYW43_ITF_AP = 1 };
#define CYW43_IOCTL_GET_ANTDIV (0x7e)

struct cyw43_t { volatile uint32_t wifi_scan_state; };
extern cyw43_t cyw43_state;
extern int g_cyw43Rc;
extern unsigned g_cyw43Ioctls;

inline int cyw43_ioctl(cyw43_t*, uint32_t, size_t, uint8_t*, uint32_t) {
    g_cyw43Ioctls++;
    return g_cyw43Rc;
}

/** Defined by the suite: WiFi.rssi when g_cyw43Rc is 0, the variable untouched otherwise. */
int cyw43_wifi_get_rssi(cyw43_t* self, int32_t* rssi);
