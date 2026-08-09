/**
 * @file WebManager_Send.cpp
 * @brief Send infrastructure: safeSend overloads, gzip delivery, safeStreamFile, client disconnect detection.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

/* Position trace (hp= in the HW-WDT autopsy) — see WebManager_History.cpp. */
#define HPOS(v) do { watchdog_hw->scratch[7] = (uint32_t)(v); } while (0)

/* Single point for broken pipe observability.
 * Throttle 5s avoids flooding the log when a handler sends N chunks after the
 * client closes. `origin` identifies the overload (s/sP/sN/gz) — useful for
 * tracking which streaming died. No stack trace; just binary signal. */
void WebManager::maybeLogClientDisconnect(const char* origin) {
 uint32_t now = millis( );
 if (now - _lastDisconnectLogMs < 5000) return;
 _lastDisconnectLogMs = now;
 LogManager::instance( ).log(LOG_WARN, "WEB", WEB_CLIENT_DISCONNECT, String(origin));
}

/* A zero-length chunk is the terminator in HTTP chunked transfer-encoding, so
 * handing sendContent( ) an empty string ENDS the response body. Mid-stream
 * that silently truncates the reply and every later safeSend( ) then sees a
 * finished connection and reports the client as gone.
 *
 * This is not hypothetical: /api/config emits `"ambHwId":"` and then sends
 * jsonEscape(sensors[10].hwId), which is empty on any device without a custom
 * ambient id — the default. The body ended on that opening quote, the browser
 * got JSON it could not parse, and the settings page came up blank. Measured:
 * 483 bytes, ending exactly at `"ambHwId":"`.
 *
 * Nothing to send is success, not a chunk. */
static inline bool sendIsNoOp(const char* c) { return c == nullptr || c[0] == '\0'; }

/* Chunked framing per sendContent call: hex length line + two CRLFs. */
static constexpr size_t SEND_FRAMING_SLACK = 16;

/* Wait until the socket can absorb `need` bytes without parking in lwIP,
 * feeding the watchdog meanwhile. Only this thread writes to the socket, so
 * room can only grow between the check and the write — check-then-send is
 * race-free. isClientGone( ) folds in the handler deadline, the guard latch
 * and the disconnect check.
 *
 * Also the guard for DIRECT _server.send( ) call sites: on a keep-alive
 * connection the unread tail of the PREVIOUS response holds the buffer, so
 * the next response's header write parks exactly like a body write — that
 * was the last face of the slow-reader reboot (C0=[WEB_HIST] with the body
 * path already funneled). */
bool WebManager::waitSendRoom(size_t need, const char* origin) {
 /* Byte room alone is not park-proof: with a slow reader the SEGMENT queue
  * and PBUF pool run out while tcp_sndbuf still reports space, and the
  * write parks inside lwIP anyway (measured: hp=720, death inside
  * sendContent with need bytes free). Demanding half the send buffer keeps
  * the in-flight tail small enough that the queue can't exhaust; a prompt
  * reader drains to full instantly, so only the stalled pay the wait. */
 size_t target = need;
 if (target < (size_t)(TCP_SND_BUF / 2)) target = (size_t)(TCP_SND_BUF / 2);
 uint32_t waited0 = millis( );
 while (_server.client( ).availableForWrite( ) < (int)target) {
 if (isClientGone( )) {
 maybeLogClientDisconnect(origin);
 _drainPending = _server.client( ).connected( ); /* deadline abort: peer alive */
 return false;
 }
 if (millis( ) - waited0 > WEB_SEND_STALL_MS) {
 _cgDisconnHits++; /* counted as a drop we chose — see stall note */
 maybeLogClientDisconnect(origin);
 /* stop(0), never stop( ): the no-arg close runs wait_until_acked,
  * whose clock RESETS on every trickled ACK — the very park this
  * whole path exists to end. */
 _server.client( ).stop(0);
 _drainPending = false; /* pcb gone — nothing left to drain */
 return false;
 }
 HPOS(710);
 feedWatchdog( );
 delay(1);
 }
 return true;
}

/* The one funnel every overload drains into.
 *
 * Slices the payload and, before each slice, waits for the socket's send
 * buffer to have room for it — feeding the watchdog in the wait. When room
 * exists, sendContent( ) is a copy into lwIP's buffer plus a tcp_output:
 * bounded milliseconds. The write itself never parks in lwIP again.
 *
 * That parking was the reboot: a client that trickled its reads kept a
 * single sendContent inside lwIP indefinitely — the write timeout re-arms
 * on every ACK, so slow progress is endless progress — and the SendGuard
 * timer's feeds demonstrably never reached the watchdog during the block
 * (autopsy: HW WATCHDOG "no feed in WDT window", C0=[WEB_SEND], with the
 * guard's ceiling at 120 s untouched). A stalled peer could reboot the
 * device at will; curl-paced runs of the identical workload passed 8/8.
 * The guard stays as the last-resort net around a now-instant call.
 *
 * Slices never emit an empty chunk by construction (`len == 0` returns at
 * the top), preserving the terminator rule above. Extra chunk boundaries
 * are transparent to the client. */
bool WebManager::safeSendN(const char* data, size_t len, const char* origin) {
 if (len == 0 || data == nullptr) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect(origin); return false; }

 _server.client( ).setTimeout(500);
 feedWatchdog( );

 size_t off = 0;
 while (off < len) {
 const size_t piece = ((len - off) > WEB_STREAM_CHUNK_SOFT)
                          ? WEB_STREAM_CHUNK_SOFT : (len - off);

 if (!waitSendRoom(piece + SEND_FRAMING_SLACK, origin)) return false;

 {
 SendGuard sg;
 LogManager::TraceScope _tSend(0, MOD_WEB_SEND);
 HPOS(720);
 _server.sendContent(data + off, piece);
 HPOS(721);
 }
 off += piece;
 }

 bool gone = isClientGone( );
 if (gone) maybeLogClientDisconnect(origin);
 /* connected( ), not !gone: a deadline/guard abort leaves the client
  * CONNECTED with an un-ACKed tail — precisely the case whose polite
  * close parks. Success or abort, a live peer still needs the drain. */
 _drainPending = _server.client( ).connected( );
 return !gone;
}

bool WebManager::safeSend(const char* content) {
 if (sendIsNoOp(content)) return !isClientGone( );
 return safeSendN(content, strlen(content), "s");
}

bool WebManager::safeSend(const char* data, size_t len) {
 return safeSendN(data, len, "sN");
}

bool WebManager::safeSend(const String& content) {
 return safeSendN(content.c_str( ), content.length( ), "sStr");
}

bool WebManager::safeSend_P(const char* content) {
 /* PROGMEM is plain XIP-addressable flash on the RP2040 — the pointer is
  * readable as-is, so the funnel slices it like any other buffer. */
 if (sendIsNoOp(content)) return !isClientGone( );
 return safeSendN(content, strlen(content), "sP");
}


/* The last door on the slow-reader reboot: the framework ends a keep-alive
 * client with `delete _currentClient` → stop( ) → wait_until_acked( ), whose
 * timeout is per-PROGRESS — every trickled ACK moves it forward — and whose
 * exit needs the tail fully ACKed. A reader draining at a crawl parks Core 0
 * there indefinitely with the watchdog unfed, wearing the last-marked module
 * in the autopsy (measured as C0=[WEB_HIST] at up≈15 s into each aborted
 * stream). Called from update( ) after handleClient( ): give the peer one
 * FED, bounded window to ACK what lwIP still holds; past it, stop(0) ends
 * the connection without the polite wait. With the tail either ACKed or the
 * pcb gone, the framework's later close exits instantly. */
void WebManager::drainOrDrop( ) {
 WiFiClient c = _server.client( );
 if (!c || !c.connected( )) return;
 if (c.availableForWrite( ) >= (int)TCP_SND_BUF) return; /* nothing pending */
 uint32_t t0 = millis( );
 while (c.availableForWrite( ) < (int)TCP_SND_BUF) {
 HPOS(600);
 if (!c.connected( )) return;
 if (millis( ) - t0 > WEB_SEND_STALL_MS) {
 _cgDisconnHits++;
 maybeLogClientDisconnect("drain");
 HPOS(601);
 c.stop(0);
 HPOS(602);
 return;
 }
 feedWatchdog( );
 delay(1);
 }
}

void WebManager::detectGzipSupport( ) {
 _clientAcceptsGzip = false;
 if (_server.hasHeader("Accept-Encoding")) {
 String ae = _server.header("Accept-Encoding");
 _clientAcceptsGzip = (ae.indexOf("gzip") >= 0);
 }
}

bool WebManager::safeSend_GZ(const uint8_t* gz_data, size_t gz_len) {
 /* The 512-byte copy loop this used to carry is the funnel's job now, and
  * PROGMEM needs no staging copy on the RP2040. */
 return safeSendN((const char*)gz_data, gz_len, "gz");
}

void WebManager::safeStreamFile(File& f, const String& contentType) {
 const size_t CHUNK = 1024;
 uint8_t buf[CHUNK];
 _server.setContentLength(f.size( ));
 _server.send(200, contentType, "");

 _server.client( ).setTimeout(500);

 bool hasMore = true;
 while (hasMore) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_FILE, 0, "");
 return;
 }

 size_t n = 0;
 {
 ReadGuard rg(_storageRef);
 if (f.available( )) n = f.read(buf, CHUNK);
 hasMore = f.available( );
 }
 if (n > 0) safeSend((const char*)buf, n);
 feedWatchdog( );
 }
 safeSend("");
}
