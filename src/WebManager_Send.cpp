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
 * Also the guard for DIRECT _server->send( ) call sites: on a keep-alive
 * connection the unread tail of the PREVIOUS response holds the buffer, so
 * the next response's header write parks exactly like a body write — that
 * was the last face of the slow-reader reboot (C0=[WEB_HIST] with the body
 * path already funneled). */
bool WebManager::waitSendRoom(size_t need, const char* origin) {
 /* Over TLS this whole room-wait does not apply and actively breaks the send.
  * availableForWrite( ) on a WiFiClientSecure reports free space in BearSSL's
  * ~1 KB output buffer, not the lwIP send buffer, so it is always below
  * TCP_SND_BUF/2 and the loop below spins until WEB_SEND_STALL_MS and aborts —
  * which is exactly why a streamed page came back empty over HTTPS on the
  * bench while a small non-chunked reply went through. BearSSL owns the
  * batching into records, and the SendGuard around each sendContent( ) feeds
  * the watchdog through the blocking encrypt-and-write, so the plain-TCP pbuf
  * accounting the loop exists for has no role here. Feed once, honour a real
  * abort latch, and let the write proceed. */
 if (_serverIsHttps) {
  feedWatchdog( );
  return !isClientGone( );
 }
 /* Byte room alone is not park-proof: with a slow reader the SEGMENT queue
  * and PBUF pool run out while tcp_sndbuf still reports space, and the
  * write parks inside lwIP anyway (measured: hp=720, death inside
  * sendContent with need bytes free). Demanding half the send buffer keeps
  * the in-flight tail small enough that the queue can't exhaust; a prompt
  * reader drains to full instantly, so only the stalled pay the wait. */
 size_t target = need;
 if (target < (size_t)(TCP_SND_BUF / 2)) target = (size_t)(TCP_SND_BUF / 2);
 uint32_t waited0 = millis( );
 while (_server->client( ).availableForWrite( ) < (int)target) {
 if (isClientGone( )) {
 maybeLogClientDisconnect(origin);
 if (_chunkedResponse) { dropAbortedStream(origin); return false; }
 _drainPending = _server->client( ).connected( ); /* deadline abort: peer alive */
 return false;
 }
 if (millis( ) - waited0 > WEB_SEND_STALL_MS) {
 _cgDisconnHits++; /* counted as a drop we chose — see stall note */
 maybeLogClientDisconnect(origin);
 /* stop(1), and 0 would not do: stop's argument caps flush's
  * wait_until_acked, whose clock RESETS on every trickled ACK — but
  * 0 does not mean "no wait", it means "use the 300 ms default"
  * (WIFICLIENT_MAX_FLUSH_WAIT_MS). A peer ACKing every 250 ms
  * extends a stop(0) forever, unfed — measured: sip-250 clients
  * reboot the device, sip-450 clients cannot, and 300 sits exactly
  * between. 1 ms is the smallest honest cap: one scheduler pass,
  * then tcp_close — and close( ) falls back to tcp_abort on error. */
 _server->client( ).stop(1);
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
 /* One response served = the browser is being used. This is the funnel every
  * text response passes through (safeSend, safeSend_P, the String overload all
  * delegate here), so it is the cheapest honest place to say "someone is
  * there". SIMUT Air resets its inactivity timer from this; without it the web
  * operator was hibernated mid-login while the serial CLI, which has always
  * called airMarkActivity( ), kept the device awake indefinitely. */
 if (_activityCb) _activityCb( );
 if (len == 0 || data == nullptr) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect(origin); return false; }

 _server->client( ).setTimeout(500);
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
 _server->sendContent(data + off, piece);
 HPOS(721);
 }
 off += piece;
 }

 bool gone = isClientGone( );
 if (gone) {
 maybeLogClientDisconnect(origin);
 /* Chunked + aborted = the framework will write the terminator into
  * whatever this abort leaves behind, inside this same handleClient
  * call — end the pcb NOW so that write is a no-op (see below). */
 if (_chunkedResponse) { dropAbortedStream(origin); return false; }
 }
 /* connected( ), not !gone: a deadline/guard abort leaves the client
  * CONNECTED with an un-ACKed tail — precisely the case whose polite
  * close parks. Success or abort, a live peer still needs the drain. */
 _drainPending = _server->client( ).connected( );
 /* 722 says this funnel RETURNED. Without it, every park anywhere between
  * the last sendContent and the next stamp wore 721 alike, which is why the
  * storm residual could not be placed: 721 covers "inside the tail of
  * safeSendN" and "anywhere the framework goes after the handler" at once.
  * With 722, a residual still wearing 721 is ours; one wearing 722 is the
  * framework's, and 740 (WebManager_Core.cpp) narrows it further. */
 HPOS(722);
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
 /* Over TLS this tail-drain does not apply: it waits on availableForWrite( )
  * reaching TCP_SND_BUF, which a WiFiClientSecure (reporting BearSSL's ~1 KB
  * out buffer) never reaches, so it would spin to WEB_SEND_STALL_MS and stop(1)
  * the connection after every response — the same availableForWrite mismatch
  * that broke waitSendRoom over TLS. BearSSL's own close/flush drains the
  * record and the TCP tail; leave it to it. */
 if (_serverIsHttps) { watchdog_update( ); return; }
 /* Drain the just-sent response's un-ACKed tail — but ONLY if the client that
  * sent it still exists.
  *
  * handleClient( ) OWNS _currentClient: on the way out it does
  *   if (!keepCurrentClient) { delete _currentClient; _currentClient = nullptr; }
  * and keepCurrentClient is false whenever the peer is no longer connected. A
  * sensor change mid-load is exactly that — the browser RSTs the in-flight graph
  * and opens a fresh connection for the new selection (the "several downloads at
  * once, loading bar confused" the user saw) — so handleClient nulls
  * _currentClient. But _drainPending was latched true by that response's
  * completed send and is still set here. The old code then did
  *   WiFiClient c = _server->client( );          // = *(ClientType*)nullptr
  * whose copy reads members through a null `this`, takes a garbage ClientContext*
  * out of ROM, and ref()'s it — a load to a wild address that parks the bus until
  * the watchdog fires. Autopsy: C0=[WEB_POLL] hp=6031 (the copy), C1=[DISPLAY],
  * only under overlapping requests. Reproduced by the user by switching sensors
  * while a graph loaded; not by any single-stream synthetic client.
  *
  * Take the pointer, not a copy: &_server->client( ) is &*(ClientType*)_currentClient,
  * which folds to _currentClient with NO dereference (so a null is returned as
  * null, never read through), and bail if the client is gone. This also drops the
  * per-drain WiFiClient copy/SList churn entirely. _currentClient is only ever a
  * live client or nullptr — handleClient never leaves it dangling — so the null
  * check is a complete guard. */
 HPOS(603);
 watchdog_update( );
 WiFiClient* c = &_server->client( );
 if (!c || !c->connected( )) return;
 if (c->availableForWrite( ) >= (int)TCP_SND_BUF) return; /* nothing pending */
 uint32_t t0 = millis( );
 while (c->availableForWrite( ) < (int)TCP_SND_BUF) {
 HPOS(600);
 if (!c->connected( )) return;
 if (millis( ) - t0 > WEB_SEND_STALL_MS) {
 _cgDisconnHits++;
 maybeLogClientDisconnect("drain");
 HPOS(601);
 c->stop(1); /* 0 = 300 ms flush default, resettable by ACKs — see waitSendRoom */
 HPOS(602);
 return;
 }
 watchdog_update( );
 delay(1);
 }
}

/* Why an aborted CHUNKED stream must die hard, not politely.
 *
 * An aborted chunked response is unfinishable: the peer will never get the
 * terminator, so there is nothing left for a graceful close to deliver. And
 * the graceful path is not merely useless here — it is the last standing
 * storm signature. When the handler returns from an abort, the framework's
 * _finalizeResponse( ) runs inside the SAME handleClient call — before
 * update( )'s drainOrDrop can intervene — and, seeing _chunked, writes the
 * terminator into the socket the abort just left stuffed. On a slow reader
 * that write meets an exhausted segment queue (the hp=720 lesson: byte room
 * is not segment room) and waits inside ClientContext, where every trickled
 * ACK re-arms the timeout — unbounded, unfed, wearing the abort's hp=900
 * under C0=[WEB_POLL]. With the pcb aborted first, that write and the
 * framework's later delete both exit instantly. Non-chunked responses keep
 * the polite drain: their finalize is a no-op, and small replies deserve
 * the chance to flush. */
void WebManager::dropAbortedStream(const char* origin) {
 (void)origin;
 /* Over TLS, the availableForWrite terminator-room test below is meaningless
  * (BearSSL out buffer, not lwIP), so just stop the secure client — its close
  * tears down the TLS session and the underlying pcb. See drainOrDrop. */
 if (_serverIsHttps) { _server->client( ).stop(1); _drainPending = false; return; }
 /* Pointer, not a copy — same reason as drainOrDrop: &_server->client( ) folds to
  * _currentClient with no dereference, so a client the framework already retired
  * (null) is caught by the guard instead of read through. This path runs inside
  * handleClient, where _currentClient is normally live, but the guard costs
  * nothing and closes the same class of null-deref for good. */
 WiFiClient* c = &_server->client( );
 if (!c || !c->connected( )) { _drainPending = false; return; }
 /* Surgical, not blanket: a socket with room for the 5-byte terminator
  * (plus framing slack) lets _finalizeResponse complete instantly, and the
  * polite path then hands the legitimate deadline-truncated reader a
  * VALIDLY-FRAMED chunked ending — json cut short, framing whole, the
  * documented behavior tools rely on. Hard-closing those trades a clean
  * truncation for a mid-chunk RST (the kernel discards buffered rx on
  * reset — measured as InvalidChunkLength in a healthy client). Only a
  * socket without even terminator room — a reader that is not draining —
  * meets the write that parks, and only it gets the RST.
  *
  * Half the send buffer, not "room for 5 bytes": byte room is not segment
  * room (the hp=720 lesson, and it bit this very check on the bench — 32
  * bytes "free" with the segment queue exhausted parked the terminator
  * write and rebooted the device). Half in flight keeps the queue
  * healthy by construction, the same predicate waitSendRoom trusts. */
 if (c->availableForWrite( ) >= (int)(TCP_SND_BUF / 2)) { _drainPending = true; return; }
 c->stop(1); /* 1, not 0: see waitSendRoom */
 _abortDrops++;
 _drainPending = false;
}

void WebManager::detectGzipSupport( ) {
 _clientAcceptsGzip = false;
 if (_server->hasHeader("Accept-Encoding")) {
 String ae = _server->header("Accept-Encoding");
 _clientAcceptsGzip = (ae.indexOf("gzip") >= 0);
 }
}

bool WebManager::safeSend_GZ(const uint8_t* gz_data, size_t gz_len) {
 /* The other response funnel — the pre-compressed pages. See safeSendN. */
 if (_activityCb) _activityCb( );
 /* The 512-byte copy loop this used to carry is the funnel's job now, and
  * PROGMEM needs no staging copy on the RP2040. */
 return safeSendN((const char*)gz_data, gz_len, "gz");
}

/* Why this one drains before returning, unlike every other response.
 *
 * setContentLength( ) means NOT chunked, and non-chunked is the one shape the
 * abort discipline never covered: safeSendN's hard close is gated on
 * _chunkedResponse, so a file stream leaves its un-ACKed tail to the framework.
 * The framework then retires the client inside handleClient( ) —
 * WebServerTemplate.h, case CLIENT_MUST_STOP — with a bare
 * `_currentClient->stop( )`. Bare means maxWaitMs 0, and flush( ) reads 0 as
 * "use the 300 ms default", not as "do not wait"; wait_until_acked( ) then
 * renews its clock on every ACK that moves sndbuf, so a peer trickling ACKs
 * holds Core 0 there with nothing feeding the watchdog.
 *
 * update( )'s drainOrDrop( ) cannot help: it runs after handleClient( )
 * returns, and the retirement happened inside it. Measured on the bench,
 * 2026-08-10: one /download per boot, response 200 in 0,05 s and the watchdog
 * reboot ~8 s later, autopsy `C0=[WEB_POLL] hp=721 (219)` — hp=721 being the
 * stamp right after sendContent, with drainOrDrop's own HPOS(600) never set.
 *
 * Draining here makes the framework's stop( ) find a flushed buffer and return
 * on its first pass. Same move f0f8e23 made for chunked aborts, on the side
 * that was left out. */
void WebManager::safeStreamFile(File& f, const String& contentType) {
 const size_t CHUNK = 1024;
 uint8_t buf[CHUNK];
 _server->setContentLength(f.size( ));
 _server->send(200, contentType, "");

 _server->client( ).setTimeout(500);

 bool hasMore = true;
 while (hasMore) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_FILE, 0, "");
 drainOrDrop( );
 _drainPending = false;
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
 drainOrDrop( );
 _drainPending = false;
}
