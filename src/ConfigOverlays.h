/**
 * @file ConfigOverlays.h
 * @brief The writes into the SystemConfig::reserved[] overlays, made on the
 *        configuration handed in.
 *
 * @details StorageManager's setters for these overlays wrote into the live
 * configuration, and nothing else. The commit's parsers called them under
 * `!dry`, which is where findings 5, 17 and 69 of
 * docs/analysis/PLANO_REVISAO_EXTERNA.md came from (2026-10-02):
 *
 *   - a dry run skipped them, so its copy never changed and it answered "no
 *     restart" for a field whose real save restarts (`h_int`, `ntp_enabled`,
 *     `slog_*`, `m_had`, `dns_auto`, `dns2`, `web_ka` — all reserved[], all
 *     CFG_RESERVED) — the page offered "apply now", and the device restarted;
 *     the admin's first PIN cleared its must-change bit the same way;
 *   - a try (`_nosave`) wrote them into the RAM, outside its copy, and then
 *     either answered 409 with the RAM changed and not saved, or overwrote
 *     them with the copy and lost the value without a word.
 *
 * Written on the configuration they are given, the parser hands them the copy
 * on a dry run or a try and the live configuration on a save, the same as
 * every `cfg.x = ...`; classifyConfigChanges( ) then sees what they did. The
 * StorageManager setters are these, on the live configuration, so the bytes
 * are written by one implementation. test/test_alarm_queue pins them.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include "SystemDefs_Records.h"
#include "SystemDefs_Time.h"   /* safeCopy */

/* One copy of each, shared by the commit and StorageManager's setters: inlined,
 * the bodies landed in handleApiCommitAll( ) as well, which grew 344 B — the
 * release's .bin 64 B and the test image's 200 B (2026-10-02). */
#define NOINLINE_OVERLAY inline __attribute__((noinline))

/** The network-time overlay, brought up with its defaults — DNS by DHCP, NTP
 *  on — the first time anything writes into it. */
inline NetworkTimeData* cfgNetworkTimeOverlay(SystemConfig& cfg) {
	NetworkTimeData* nt = reinterpret_cast<NetworkTimeData*>(cfg.reserved + NETTIME_OFFSET);
	if (nt->magic != NETTIME_MAGIC) {
		nt->magic = NETTIME_MAGIC;
		nt->flags = FLAG_DNS_AUTO | FLAG_NTP_ENABLED;
		nt->dns2[0] = '\0';
		nt->pad[0] = nt->pad[1] = 0;
	}
	return nt;
}

NOINLINE_OVERLAY void cfgSetDnsAuto(SystemConfig& cfg, bool on) {
	NetworkTimeData* nt = cfgNetworkTimeOverlay(cfg);
	if (on) nt->flags |= FLAG_DNS_AUTO;
	else nt->flags &= ~FLAG_DNS_AUTO;
}

NOINLINE_OVERLAY void cfgSetNtpEnabled(SystemConfig& cfg, bool on) {
	NetworkTimeData* nt = cfgNetworkTimeOverlay(cfg);
	if (on) nt->flags |= FLAG_NTP_ENABLED;
	else nt->flags &= ~FLAG_NTP_ENABLED;
}

NOINLINE_OVERLAY void cfgSetSecondaryDns(SystemConfig& cfg, const char* ip) {
	NetworkTimeData* nt = cfgNetworkTimeOverlay(cfg);
	safeCopy(nt->dns2, ip ? ip : "", sizeof(nt->dns2));
}

NOINLINE_OVERLAY void cfgSetHistoryIntervalMin(SystemConfig& cfg, uint16_t minutes) {
	if (minutes < HISTORY_INTERVAL_MIN_MIN) minutes = HISTORY_INTERVAL_MIN_MIN;
	if (minutes > HISTORY_INTERVAL_MAX_MIN) minutes = HISTORY_INTERVAL_MAX_MIN;
	HistoryConfigData* hc = reinterpret_cast<HistoryConfigData*>(cfg.reserved + HISTORY_CONFIG_OFFSET);
	hc->magic = HISTORY_CONFIG_MAGIC;
	hc->pad = 0;
	hc->intervalMin = minutes;
}

/** The stored bit is the OPT-OUT, so a legacy overlay comes up with keep-alive
 *  on (StorageManager::isWebKeepAliveEnabled( )). */
NOINLINE_OVERLAY void cfgSetWebKeepAlive(SystemConfig& cfg, bool on) {
	SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(cfg.reserved + SETUP_FLAGS_OFFSET);
	if (sf->magic != SETUP_FLAGS_MAGIC) {
		sf->magic = SETUP_FLAGS_MAGIC;
		sf->flags = 0;
	}
	if (on) sf->flags &= ~FLAG_WEB_KEEPALIVE_OFF;
	else sf->flags |= FLAG_WEB_KEEPALIVE_OFF;
}

/** The admin's first real PIN clears the must-change bit. A legacy overlay is
 *  brought up empty — the bit was never set in one. */
NOINLINE_OVERLAY void cfgClearMustChangePin(SystemConfig& cfg) {
	SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(cfg.reserved + SETUP_FLAGS_OFFSET);
	if (sf->magic != SETUP_FLAGS_MAGIC) {
		sf->magic = SETUP_FLAGS_MAGIC;
		sf->flags = 0;
	} else {
		sf->flags &= ~FLAG_MUST_CHANGE_PIN;
	}
}

NOINLINE_OVERLAY void cfgSetHaDiscovery(SystemConfig& cfg, bool on) {
	HaDiscoveryData* ha = reinterpret_cast<HaDiscoveryData*>(cfg.reserved + HA_DISCOVERY_OFFSET);
	if (ha->magic != HA_DISCOVERY_MAGIC) { ha->magic = HA_DISCOVERY_MAGIC; ha->flags = 0; }
	if (on) ha->flags |= FLAG_HA_DISCOVERY;
	else ha->flags &= ~FLAG_HA_DISCOVERY;
}

NOINLINE_OVERLAY void cfgSetSyslog(SystemConfig& cfg, bool enabled, uint32_t serverIp,
                         uint16_t port, uint8_t minLevel) {
	SyslogConfigData* sl = reinterpret_cast<SyslogConfigData*>(cfg.reserved + SYSLOG_CONFIG_OFFSET);
	sl->magic = SYSLOG_CONFIG_MAGIC;
	sl->flags = syslogPackFlags(enabled, minLevel);
	sl->port = port;
	sl->serverIp = serverIp;
}
