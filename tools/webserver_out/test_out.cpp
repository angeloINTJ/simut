/* test_out.cpp — the framework's HTTPServer, as patch.sh leaves it (overrides
 * 2m and 2n), on the host, against a client that records every write( ).
 * Built and run by tools/test_webserver_out.py, which says why it exists.
 *
 * Two references, because one is not enough:
 * - The bytes spelled out. The response shapes SIMUT serves are written here
 *   in full, header included, as the device sent them before override 2n:
 *   the header lines of a reply with a Content-Length and of a chunked one
 *   are those `main` (686a1de, override 2m only) answered on the bench on
 *   2026-10-14 (tools/route_sweep.py; /api/perms, /api/status). A mistake in
 *   a line the override edited shows here even when it breaks both of the
 *   runs below the same way.
 * - The same handler run outside _handleRequest( ), where every write goes
 *   straight out: the bytes must be the same as inside, and only the write( )
 *   boundaries may differ. That run shares the code under test, so it holds
 *   the buffer to "the same bytes", not the code to "the right bytes".
 * Then each scenario checks the rule it is named for, at the moment the rule
 * is about: right after send( ) returns, after a body's last byte, and so on.
 *
 * Output: one "RESULT <scenario> PASS|FAIL" line each; exit 1 on any FAIL. */
#include <Arduino.h>
#include <FS.h>
#include "HTTPServer.h"
#include <cstdio>
#include <string>
#include <vector>

unsigned long millis() {
    static unsigned long t = 0;
    return ++t;
}
void delay(unsigned long) {}
void yield() {}
HostRp2040 rp2040;

/* What ArduinoCore-API's String.cpp links against; the core provides them on
 * the device. */
extern "C" {
static char* toBase(unsigned long v, bool neg, char* s, int radix) {
    char tmp[sizeof(unsigned long) * 8 + 1];
    int i = 0;
    do {
        const int d = static_cast<int>(v % radix);
        tmp[i++] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    int j = 0;
    if (neg) {
        s[j++] = '-';
    }
    while (i) {
        s[j++] = tmp[--i];
    }
    s[j] = '\0';
    return s;
}
char* itoa(int v, char* s, int r) {
    return (v < 0 && r == 10) ? toBase(-static_cast<long>(v), true, s, r) : toBase(static_cast<unsigned>(v), false, s, r);
}
char* ltoa(long v, char* s, int r) {
    return (v < 0 && r == 10) ? toBase(-static_cast<unsigned long>(v), true, s, r) : toBase(static_cast<unsigned long>(v), false, s, r);
}
char* utoa(unsigned v, char* s, int r) {
    return toBase(v, false, s, r);
}
char* ultoa(unsigned long v, char* s, int r) {
    return toBase(v, false, s, r);
}
char* dtostrf(double v, signed char w, unsigned char p, char* s) {
    sprintf(s, "%*.*f", w, p, v);
    return s;
}
}

/* A client that takes `room` bytes and then comes back short, as
 * ClientContext does when its write timeout passes. */
struct FakeClient : public WiFiClient {
    std::vector<std::string> writes;  // what each write( ) handed over
    std::string wire;                 // every byte taken, in order
    size_t room = static_cast<size_t>(-1);
    bool up = true;
    int stops = 0;
    int writesAfterStop = 0;          // write( ) calls a stopped client got
    size_t write(const uint8_t* b, size_t n) override {
        if (!up) {
            writesAfterStop++;
            return 0;
        }
        const size_t k = n < room ? n : room;
        room -= k;
        writes.emplace_back(reinterpret_cast<const char*>(b), k);
        wire.append(reinterpret_cast<const char*>(b), k);
        return k;
    }
    uint8_t connected() override {
        return up;
    }
    bool stop(unsigned int) override {
        up = false;
        stops++;
        return true;
    }
};

class TestServer : public HTTPServer {
public:
    /* What handleClient( ) and _parseRequest( ) set before a request. */
    void attach(FakeClient& c, uint8_t version) {
        _currentClient = &c;
        _currentVersion = version;
        _currentMethod = HTTP_GET;
        _chunked = false;
        _contentLength = CONTENT_LENGTH_NOT_SET;
    }
    /* The handler as the server runs it: inside _handleRequest( ). */
    void serve(FakeClient& c, THandlerFunction fn, uint8_t version = 1) {
        static int n = 0;
        const String uri = String("/t") + String(n++);
        on(uri, fn);
        attach(c, version);
        _currentUri = uri;
        _currentHandler = _lastHandler;
        _handleRequest();
    }
    /* The same handler outside _handleRequest( ), then what it would finalize:
     * every write straight out. */
    void direct(FakeClient& c, THandlerFunction fn, uint8_t version = 1) {
        attach(c, version);
        fn();
        _finalizeResponse();
    }
    size_t held() const {
        return _outLen;
    }
};

static std::vector<std::string> failures;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        failures.push_back(what);
    }
}
static std::string str(size_t n) {
    return std::to_string(n);
}
/* Where two byte strings first part, for the failure message. */
static void same(const std::string& got, const std::string& want, const std::string& what) {
    if (got == want) {
        return;
    }
    size_t i = 0;
    while (i < got.size() && i < want.size() && got[i] == want[i]) {
        i++;
    }
    check(false, what + ": " + str(got.size()) + " B against " + str(want.size()) + " B, first difference at byte " + str(i));
}

static int failed = 0;
static int scenarios = 0;
static void report(const char* name) {
    scenarios++;
    if (failures.empty()) {
        printf("RESULT %s PASS\n", name);
    } else {
        failed++;
        printf("RESULT %s FAIL\n", name);
        for (const auto& f : failures) {
            printf("  - %s\n", f.c_str());
        }
    }
    failures.clear();
}

using Body = std::function<void(TestServer&, FakeClient&)>;

/* Runs `body` both ways and holds the served bytes to the direct run's.
 * Returns the served client, for the scenario's own checks. */
static FakeClient run(TestServer& s, Body body, uint8_t version = 1) {
    FakeClient want;
    s.direct(want, [&]() {
        body(s, want);
    }, version);
    FakeClient got;
    s.serve(got, [&]() {
        body(s, got);
    }, version);
    same(got.wire, want.wire, "the bytes differ from the run with every write straight out");
    check(s.held() == 0, "_out still holds " + str(s.held()) + " B after the request");
    return got;
}

/* The header lines `main` answered with on 2026-10-14 (see the top). */
static std::string withLength(const char* type, size_t n, const char* extra = "") {
    return std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type + "\r\n" + extra +
           "Content-Length: " + str(n) + "\r\nConnection: keep-alive\r\n\r\n";
}
static std::string chunkedHeader(const char* type, const char* extra = "") {
    return std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type + "\r\n" + extra +
           "Accept-Ranges: none\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
}

int main() {
    TestServer s;
    s.enableKeepAlive(true);  /* as SIMUT runs it */

    /* 1. send( ) with a C string: the whole response is out when it returns,
     * in one write. /api/action's reboot sends {"ok":true} this way, then
     * stops the client and resets; the device sent these 106 B. */
    {
        size_t atReturn = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.send(200, "application/json", "{\"ok\":true}");
            atReturn = c.wire.size();
        };
        const std::string want = withLength("application/json", 11) + "{\"ok\":true}";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atReturn == want.size(), "send( ) returned with " + str(atReturn) + " of " + str(want.size()) + " B out");
        check(got.writes.size() == 1, "header and body took " + str(got.writes.size()) + " writes, not 1");
        report("send_whole_cstr");
    }

    /* 2. send( ) with a String: the same rule, the other overload. */
    {
        size_t atReturn = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.send(200, "text/plain", String("hello"));
            atReturn = c.wire.size();
        };
        const std::string want = withLength("text/plain", 5) + "hello";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atReturn == want.size(), "send( ) returned with " + str(atReturn) + " of " + str(want.size()) + " B out");
        check(got.writes.size() == 1, "header and body took " + str(got.writes.size()) + " writes, not 1");
        report("send_whole_string");
    }

    /* 3. A chunked reply of a few small pieces, the shape of /api/status, with
     * its header: nothing leaves while the handler runs, and then all of it in
     * one write. */
    {
        size_t whileRunning = 99;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.sendHeader("Cache-Control", "no-cache");
            srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
            srv.send(200, "application/json", "");
            srv.sendContent("{\"sys\":");
            srv.sendContent("1");
            srv.sendContent(",");
            srv.sendContent("\"metr\":2}");
            whileRunning = c.wire.size();
        };
        const std::string want = chunkedHeader("application/json", "Cache-Control: no-cache\r\n") +
                                 "7\r\n{\"sys\":\r\n1\r\n1\r\n1\r\n,\r\n9\r\n\"metr\":2}\r\n0\r\n\r\n";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(whileRunning == 0, "the reply started leaving while the handler ran (" + str(whileRunning) + " B)");
        check(got.writes.size() == 1, "the reply took " + str(got.writes.size()) + " writes, not 1");
        report("chunked_one_write");
    }

    /* 4. A chunked reply larger than a segment, in 512 B pieces as safeSendN
     * slices them: every write but the last is a whole segment. */
    {
        const std::string piece(512, 'x');
        auto body = [&](TestServer& srv, FakeClient&) {
            srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
            srv.send(200, "application/json", "");
            for (int i = 0; i < 10; i++) {
                srv.sendContent(piece.c_str(), piece.size());
            }
        };
        FakeClient got = run(s, body);
        bool whole = got.writes.size() > 1;
        for (size_t i = 0; i + 1 < got.writes.size(); i++) {
            whole = whole && got.writes[i].size() == 1460;
        }
        check(whole, "the writes were not whole segments of 1,460 B (" + str(got.writes.size()) + " writes)");
        report("chunked_full_segments");
    }

    /* 5. A body with a Content-Length, streamed as safeStreamFile( ) does:
     * when its last byte is handed over, all of it is out, because
     * safeStreamFile( ) drains the socket right after, inside the handler. */
    {
        size_t atLastByte = 0;
        const std::string a(1024, 'a'), b(1024, 'b'), c3(952, 'c');
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.setContentLength(3000);
            srv.send(200, "application/octet-stream", "");
            srv.sendContent(a.c_str(), a.size());
            srv.sendContent(b.c_str(), b.size());
            srv.sendContent(c3.c_str(), c3.size());
            atLastByte = c.wire.size();
        };
        const std::string want = withLength("application/octet-stream", 3000) + a + b + c3;
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atLastByte == want.size(), "after the body's last byte, " + str(atLastByte) + " of " + str(want.size()) + " B were out");
        report("content_length_last_byte");
    }

    /* 6. A handler that ends its chunked reply itself (chunkedResponseFinalize,
     * the terminating chunk): all of it is out right then. */
    {
        size_t atEnd = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
            srv.send(200, "text/plain", "");
            srv.sendContent("abc");
            srv.chunkedResponseFinalize();
            atEnd = c.wire.size();
        };
        const std::string want = chunkedHeader("text/plain") + "3\r\nabc\r\n0\r\n\r\n";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atEnd == want.size(), "after the terminating chunk, " + str(atEnd) + " of " + str(want.size()) + " B were out");
        report("terminator_flushes");
    }

    /* 7. HTTP/1.0 with no length: no chunks, no Content-Length, the body ends
     * with the connection. Its tail leaves at _finalizeResponse( ). */
    {
        auto body = [&](TestServer& srv, FakeClient&) {
            srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
            srv.send(200, "text/plain", "");
            srv.sendContent("abc");
        };
        FakeClient got = run(s, body, 0);
        same(got.wire, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nabc", "not the bytes spelled out");
        report("http10_tail_at_finalize");
    }

    /* 8. The client takes 100 B and then comes back short: override 2m ends
     * the response there, counts it once and closes the client, and nothing
     * more is written. */
    {
        const std::string piece(512, 'y');
        const uint32_t before = s.shortWrites();
        FakeClient got;
        got.room = 100;
        s.serve(got, [&]() {
            s.setContentLength(CONTENT_LENGTH_UNKNOWN);
            s.send(200, "application/json", "");
            for (int i = 0; i < 6; i++) {
                s.sendContent(piece.c_str(), piece.size());
            }
        });
        check(s.shortWrites() == before + 1, "shortWrites( ) moved " + str(s.shortWrites() - before) + ", not 1");
        check(got.stops == 1, "the client was stopped " + str(got.stops) + " times, not once");
        check(got.writes.size() == 1 && got.wire.size() == 100, "after the short write: " + str(got.writes.size()) + " writes, " + str(got.wire.size()) + " B");
        check(got.writesAfterStop == 0, "the response went on: " + str(got.writesAfterStop) + " write( ) calls after the client was stopped");
        check(s.held() == 0, "_out still holds " + str(s.held()) + " B");
        report("short_write_ends_response");
    }

    /* 9. A handler that stops its own client mid-reply (SIMUT's
     * dropAbortedStream( ) over TLS): nothing more reaches the wire after
     * that, what _out held included, and a client that is gone is not counted
     * as a short write. */
    {
        const uint32_t before = s.shortWrites();
        size_t atStop = 0;
        FakeClient got;
        s.serve(got, [&]() {
            s.setContentLength(CONTENT_LENGTH_UNKNOWN);
            s.send(200, "application/json", "");
            s.sendContent("abc");
            atStop = got.wire.size();
            s.stopClient(1);
            s.sendContent("def");
        });
        check(got.wire.size() == atStop, str(got.wire.size() - atStop) + " B reached the wire after stopClient( )");
        check(s.shortWrites() == before, "a stopped client was counted as a short write");
        report("stop_client_sends_nothing_more");
    }

    /* 10. Outside _handleRequest( ), as from an upload handler or a hook,
     * after a request has run: writes go straight out, header and body apart,
     * as upstream wrote them. */
    {
        FakeClient first;
        s.serve(first, [&]() {
            s.send(200, "text/plain", "x");
        });
        FakeClient got;
        s.attach(got, 1);
        s.send(200, "text/plain", "x");
        check(got.writes.size() == 2, "outside _handleRequest( ): " + str(got.writes.size()) + " writes, not 2");
        report("outside_handler_writes_straight");
    }

    /* 11. streamFile( ): the header, then the file, in order. */
    {
        const std::string data(3000, 'f');
        auto body = [&](TestServer& srv, FakeClient&) {
            File f(data, "/x.bin");
            srv.streamFile(f, "application/octet-stream");
        };
        FakeClient got = run(s, body);
        same(got.wire, withLength("application/octet-stream", 3000) + data, "not the bytes spelled out");
        report("stream_file_order");
    }

    /* 12. A send( ) larger than a segment: the header and the first bytes of
     * the body fill one segment, and the rest, a segment or more with nothing
     * held, goes straight out in one write, uncopied. */
    {
        const std::string text(6000, 'h');
        const String big(text.c_str());
        auto body = [&](TestServer& srv, FakeClient&) {
            srv.send(200, "text/html", big);
        };
        FakeClient got = run(s, body);
        same(got.wire, withLength("text/html", 6000) + text, "not the bytes spelled out");
        check(got.writes.size() == 2 && got.writes[0].size() == 1460,
              "the reply took " + str(got.writes.size()) + " writes, the first of " + str(got.writes.empty() ? 0 : got.writes[0].size()) + " B; expected a segment, then the rest at once");
        report("big_send");
    }

    /* 13. send_P( ) with a PROGMEM string: the whole response out when it
     * returns. SIMUT does not call it; the framework's own handlers might. */
    {
        size_t atReturn = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.send_P(200, "text/plain", "hello");
            atReturn = c.wire.size();
        };
        const std::string want = withLength("text/plain", 5) + "hello";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atReturn == want.size(), "send_P( ) returned with " + str(atReturn) + " of " + str(want.size()) + " B out");
        check(got.writes.size() == 1, "header and body took " + str(got.writes.size()) + " writes, not 1");
        report("send_p_whole");
    }

    /* 14. send_P( ) with a length: the same rule, the other overload. */
    {
        size_t atReturn = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.send_P(200, "text/plain", "hello", 5);
            atReturn = c.wire.size();
        };
        const std::string want = withLength("text/plain", 5) + "hello";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atReturn == want.size(), "send_P( ) returned with " + str(atReturn) + " of " + str(want.size()) + " B out");
        check(got.writes.size() == 1, "header and body took " + str(got.writes.size()) + " writes, not 1");
        report("send_p_len_whole");
    }

    /* 15. A chunked reply in PROGMEM pieces ended by sendContent_P(""): all of
     * it out at the terminating chunk. */
    {
        size_t atEnd = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
            srv.send(200, "text/plain", "");
            srv.sendContent_P("abc");
            srv.sendContent_P("");
            atEnd = c.wire.size();
        };
        const std::string want = chunkedHeader("text/plain") + "3\r\nabc\r\n0\r\n\r\n";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atEnd == want.size(), "after the terminating chunk, " + str(atEnd) + " of " + str(want.size()) + " B were out");
        report("content_p_terminator");
    }

    /* 16. A body with a Content-Length sent with sendContent_P( ): out with
     * its last byte. */
    {
        size_t atLastByte = 0;
        auto body = [&](TestServer& srv, FakeClient& c) {
            srv.setContentLength(6);
            srv.send(200, "text/plain", "");
            srv.sendContent_P("abcdef", 6);
            atLastByte = c.wire.size();
        };
        const std::string want = withLength("text/plain", 6) + "abcdef";
        FakeClient got = run(s, body);
        same(got.wire, want, "not the bytes spelled out");
        check(atLastByte == want.size(), "after the body's last byte, " + str(atLastByte) + " of " + str(want.size()) + " B were out");
        report("content_p_length_last_byte");
    }

    printf("SCENARIOS %d\n", scenarios);
    printf("%d of %d scenarios failed\n", failed, scenarios);
    return failed ? 1 : 0;
}
