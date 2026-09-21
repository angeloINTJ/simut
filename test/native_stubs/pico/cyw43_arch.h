/**
 * @file pico/cyw43_arch.h (native stub)
 * @brief Just enough of the SDK header for NetworkManager.cpp to compile here.
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
 * @project SIMUT
 * @license MIT License
 */

#pragma once

extern unsigned g_cyw43StaModeEnables;

inline void cyw43_arch_enable_sta_mode( ) { g_cyw43StaModeEnables++; }
