/**
 * @file SyslogManager.h
 * @brief RFC 5424 / UDP syslog forwarder — the audit trail that leaves the box.
 * @details Ships an append-only copy of qualifying log records to a SIEM over
 * UDP/514. Deliberately NOT a third TelemetryTransport: UDP is
 * fire-and-forget, so there is no handshake, no ~16 KB TLS client, no flash
 * cursor and no reconnection state machine — none of the baggage the telemetry
 * send loop carries (and needed four patches for).
 *
 * Threading is the whole design. logCode()/log() run on EITHER core; all
 * network access on this chip is Core 0 (app.loop). So:
 *   - enqueue() runs on the logging core, under LogManager's _logMutex. It only
 *     formats (pure Syslog5424::format) and copies the line into a ring under
 *     this object's own mutex. No socket touched. Fast, core-safe.
 *   - pump() runs on Core 0 from AppManager::loop, drains the ring and sends.
 *     The ring lock is released before each send, so the network never blocks
 *     a logging core.
 *   - flushBlocking() drains synchronously from the pre-reboot hook (Core 0),
 *     so a deliberate reboot pushes the last WARN/FATAL out before the reset.
 *     A hard dual-core hang loses the ring — acknowledged; nothing saves that.
 *
 * The nested lock order is always _logMutex → ring mutex (enqueue) or ring
 * mutex alone (pump/flush); it never inverts, so it cannot deadlock. The sink
 * must not log from inside enqueue (it holds _logMutex) — it does not.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stdint.h>
#include <WiFiUdp.h>
#include "pico/sync.h"
#include "LogManager.h" /* SyslogEvent */

class StorageManager;

class SyslogManager {
public:
	SyslogManager( );

	/** Read the overlay (reserved[56..63]) from storage and (re)configure:
	 *  enabled, server IP, port, min level, and the hostname (device name).
	 *  Core 0 only. Config changes reboot the device, so in practice this runs
	 *  at boot and the settings are stable during operation. */
	void configure(StorageManager& storage);

	/** The LogManager sink: format one record and enqueue it. Called on any
	 *  core under _logMutex. Gates on enabled + min level before formatting. */
	void enqueue(const SyslogEvent& ev);

	/** Drain a bounded batch and send over UDP. Core 0, from the main loop. */
	void pump( );

	/** Drain everything now, blocking. Core 0, from the pre-reboot hook. */
	void flushBlocking( );

	bool isEnabled( ) const { return _enabled; }

private:
	static constexpr int SLOTS = 8;      /**< ring depth (bursts between pumps) */
	static constexpr int LINE_MAX = 256; /**< one RFC 5424 datagram */

	bool sendLine(const char* line, size_t len);
	bool popLine(char* out); /**< pull one line under the ring lock; false if empty */

	WiFiUDP _udp;
	bool _udpReady = false;

	/* Config. _enabled/_minLevel are read on the logging core and written on
	 * Core 0 — volatile so the enqueue gate is not hoisted. The rest is touched
	 * only on Core 0 (pump/configure) or is stable after boot (_hostname). */
	volatile bool _enabled = false;
	volatile uint8_t _minLevel = LOG_INFO;
	uint32_t _serverIp = 0;
	uint16_t _port = 514;
	char _hostname[32] = {0};

	/* Ring of pre-formatted lines. Drop-oldest on overflow keeps the newest
	 * (most forensically relevant) records. */
	mutex_t _ringMutex;
	char _ring[SLOTS][LINE_MAX];
	int _head = 0;  /**< next write slot */
	int _count = 0; /**< occupied slots */
};
