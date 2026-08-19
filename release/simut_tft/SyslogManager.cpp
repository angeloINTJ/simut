/**
 * @file SyslogManager.cpp
 * @brief Implementation of the RFC 5424 / UDP syslog forwarder.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#include "SyslogManager.h"
#include "Syslog5424.h"
#include "StorageManager.h"
#include <WiFi.h>
#include <string.h>

SyslogManager::SyslogManager( ) {
	mutex_init(&_ringMutex);
}

void SyslogManager::configure(StorageManager& storage) {
	_enabled  = storage.isSyslogEnabled( );
	_serverIp = storage.getSyslogServerIp( );
	_port     = storage.getSyslogPort( );
	_minLevel = storage.getSyslogMinLevel( );

	const SystemConfig& cfg = storage.getConfig( );
	strncpy(_hostname, cfg.deviceName, sizeof(_hostname) - 1);
	_hostname[sizeof(_hostname) - 1] = '\0';
}

void SyslogManager::enqueue(const SyslogEvent& ev) {
	/* Gate before doing any work: the common case is disabled. */
	if (!_enabled || ev.level < _minLevel) return;

	char line[LINE_MAX];
	const size_t len = Syslog5424::format(
		line, sizeof(line),
		ev.level, _hostname, ev.tag, ev.code, ev.context, ev.core,
		ev.uptimeSec, (time_t)ev.epoch, ev.desc, ev.extra);
	(void)len;

	mutex_enter_blocking(&_ringMutex);
	if (_count == SLOTS) {
		/* Full: drop the oldest by advancing over it (head already points at
		 * the oldest slot when full). Keep the newest — it is the one most
		 * likely to explain a reboot. */
		_head = (_head + 1) % SLOTS;
		_count--;
	}
	int slot = (_head + _count) % SLOTS;
	strncpy(_ring[slot], line, LINE_MAX - 1);
	_ring[slot][LINE_MAX - 1] = '\0';
	_count++;
	mutex_exit(&_ringMutex);
}

bool SyslogManager::popLine(char* out) {
	bool got = false;
	mutex_enter_blocking(&_ringMutex);
	if (_count > 0) {
		memcpy(out, _ring[_head], LINE_MAX);
		_head = (_head + 1) % SLOTS;
		_count--;
		got = true;
	}
	mutex_exit(&_ringMutex);
	return got;
}

bool SyslogManager::sendLine(const char* line, size_t len) {
	if (WiFi.status( ) != WL_CONNECTED) return false;
	if (!_udpReady) {
		/* Allocate an ephemeral local socket once. begin() returning 0 means
		 * no socket was available; retry next pump. */
		if (_udp.begin(0) == 0) return false;
		_udpReady = true;
	}
	IPAddress dst(_serverIp);
	if (_udp.beginPacket(dst, _port) == 0) return false;
	_udp.write((const uint8_t*)line, len);
	return _udp.endPacket( ) != 0;
}

void SyslogManager::pump( ) {
	if (!_enabled) return;
	if (WiFi.status( ) != WL_CONNECTED) return; /* AP mode / offline: hold in ring */

	/* Drain the whole ring per pump: at most SLOTS small datagrams, cheap next
	 * to one telemetry POST, and it keeps SIEM latency to one loop tick. */
	char line[LINE_MAX];
	for (int i = 0; i < SLOTS; i++) {
		if (!popLine(line)) break;
		if (!sendLine(line, strlen(line))) {
			/* Send failed (link dropped mid-drain). The line is already popped;
			 * UDP is best-effort by contract, so it is dropped rather than
			 * re-queued — re-queue races the newest records for ring space and
			 * syslog promises no delivery. Stop draining this tick. */
			break;
		}
	}
}

void SyslogManager::flushBlocking( ) {
	if (!_enabled) return;
	if (WiFi.status( ) != WL_CONNECTED) return;

	char line[LINE_MAX];
	int guard = SLOTS * 2; /* bound: enqueue could add from Core 1 during flush */
	while (guard-- > 0 && popLine(line)) {
		if (!sendLine(line, strlen(line))) break;
	}
}
