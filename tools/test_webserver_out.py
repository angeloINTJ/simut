#!/usr/bin/env python3
"""test_webserver_out.py — override 2n's write buffer, run on the host in the framework's own HTTPServer.

Usage:
    python3 tools/test_webserver_out.py
    SIMUT_WEBSERVER_OUT=require python3 tools/test_webserver_out.py   # CI: no framework = failure

Override 2n (arduino_pico_overrides/patches/webserver_fewer_writes.patch) collects
a response's writes in a buffer of one TCP segment and decides when they leave:
when it is full, at the end of send( ), with the last byte of a body sent after
setContentLength( ), at the terminating chunk and at _finalizeResponse( ). The
host suites in test/ do not compile the framework, and the bench sees the bytes
that arrive, not the moment the server let them go, which is what two of those
rules are about. So this compiles the patched HTTPServer.cpp itself, from where
PlatformIO keeps the framework (PLATFORMIO_CORE_DIR, else ~/.platformio) after
patch.sh, with host stand-ins for the board (webserver_out/stubs/), and runs
webserver_out/test_out.cpp: sixteen scenarios, each held to the bytes spelled
out, as `main` answered on the bench before 2n, and to the same handler run with
every write straight out. That second run shares the code under test, so it
holds the buffer to "the same bytes" only; the spelled-out bytes catch a line
2n got wrong in both.

Then the mutants. Each takes one rule out of a copy of HTTPServer.cpp and must
make exactly the scenarios listed for it fail; "no_buffer" turns the buffer off
altogether and shows which scenarios would see 2n missing. A mutant whose text
is no longer in the source is an error, not a pass: the code moved and the
mutant has to follow it.

Without the framework on disk (a checkout that never ran patch.sh, the gates
job) it says SKIP and exits 0, unless SIMUT_WEBSERVER_OUT=require: the
pico_w_release job of build.yml runs it after patch.sh, where a missing
framework is a broken build, not an absence.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
STUBS = os.path.join(HERE, "webserver_out", "stubs")
TEST = os.path.join(HERE, "webserver_out", "test_out.cpp")
MARKER_2M = "SIMUT override — a short write ends the response"
MARKER_2N = "SIMUT override — a response's small writes leave together"

SEND_CSTR_FLUSH = """    if (contentLength) {
        sendContent(content, contentLength);
    }
    if (!_chunked) {
        _outFlush();  /* SIMUT override — see send( ) above */
    }
}"""
SEND_STRING_FLUSH = """    if (content.length()) {
        sendContent(content);
    }
    if (!_chunked) {
        _outFlush();  /* SIMUT override — the whole response, out before returning (_outAppend) */
    }
}"""
# name: (text in HTTPServer.cpp, its replacement, the scenarios that must fail)
MUTANTS = {
    "send_flush_cstr": (SEND_CSTR_FLUSH, SEND_CSTR_FLUSH.split("    if (!_chunked)")[0] + "}",
                        {"send_whole_cstr"}),
    "send_flush_string": (SEND_STRING_FLUSH, SEND_STRING_FLUSH.split("    if (!_chunked)")[0] + "}",
                          {"send_whole_string"}),
    "body_last_byte": ("""        if (!_outBodyLeft) {
            _outFlush();
        }""", """        if (!_outBodyLeft) {
        }""", {"content_length_last_byte", "content_p_length_last_byte"}),  # the helper both share
    "terminator_flush": ("""        if (contentLength == 0) {
            _chunked = false;
            _outFlush();  /* SIMUT override — the response ended (_outAppend) */
        }""", """        if (contentLength == 0) {
            _chunked = false;
        }""", {"terminator_flushes"}),
    "finalize_flush": ("""        sendContent("");
    }
    _outFlush();  /* SIMUT override — a response streamed with a Content-Length (_outAppend) */""",
                       """        sendContent("");
    }""", {"http10_tail_at_finalize"}),
    "hold_stays_on": ("""    _outHold = false;
    _currentUri = "";""", """    _currentUri = "";""", {"outside_handler_writes_straight"}),
    # Override 2m's rule, which 2n's writes go through: after a short write
    # nothing more of the response is written.
    "short_write_goes_on": ("""    _writeBroken = true;
    if (_currentClient->connected()) {""", """    if (_currentClient->connected()) {""",
                            {"short_write_ends_response"}),
    "send_p_flush": ("""    _outAppend(header.c_str(), header.length());
    sendContent_P(content);
    if (!_chunked) {
        _outFlush();  /* SIMUT override — see send( ) above */
    }
}""", """    _outAppend(header.c_str(), header.length());
    sendContent_P(content);
}""", {"send_p_whole"}),
    "send_p_len_flush": ("""    sendContent_P(content, contentLength);
    if (!_chunked) {
        _outFlush();  /* SIMUT override — see send( ) above */
    }
}""", """    sendContent_P(content, contentLength);
}""", {"send_p_len_whole"}),
    "terminator_flush_p": ("""        if (size == 0) {
            _chunked = false;
            _outFlush();  /* SIMUT override — the response ended (_outAppend) */
        }""", """        if (size == 0) {
            _chunked = false;
        }""", {"content_p_terminator"}),
    "body_last_byte_p": ("""        _outBodySent(size);  /* SIMUT override */""", "", {"content_p_length_last_byte"}),
    "big_write_copied": ("""        if (_outLen == 0 && l >= OUT_BUF_SIZE) {
            /* A segment's worth or more and nothing held: no copy to make. */
            return _writeAll(b, l);
        }""", "", {"big_send"}),
    # Two that corrupt the bytes in a line 2n edited, which the run with every
    # write straight out shares: only the bytes spelled out catch them.
    "chunk_crlf_short": ("""        if (!_outAppend(footer, 2)) {
            return;
        }
        if (contentLength == 0) {""", """        if (!_outAppend(footer, 1)) {
            return;
        }
        if (contentLength == 0) {""", {"chunked_one_write", "terminator_flushes"}),
    "header_short": ("""    _prepareHeader(header, code, content_type, contentLength);
    _outAppend(header.c_str(), header.length());
    if (contentLength) {""", """    _prepareHeader(header, code, content_type, contentLength);
    _outAppend(header.c_str(), header.length() - 1);
    if (contentLength) {""", {"send_whole_cstr", "chunked_one_write", "content_length_last_byte",
                              "terminator_flushes", "http10_tail_at_finalize", "content_p_terminator",
                              "content_p_length_last_byte"}),
    "no_buffer": ("""    _outHold = (_out != nullptr);""", """    _outHold = false;""",
                  {"send_whole_cstr", "send_whole_string", "chunked_one_write",
                   "chunked_full_segments", "big_send", "send_p_whole", "send_p_len_whole"}),
}


def framework():
    core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.expanduser("~/.platformio")
    for name in ("framework-arduinopico", "framework-arduino-pico"):
        fw = os.path.join(core, "packages", name)
        if os.path.isdir(fw):
            return fw
    return None


def compile_objs(fw, build, ws):
    """String.cpp, mimetable.cpp and the test do not change between mutants."""
    flags = ["-std=gnu++17", "-O0", "-g", "-w", "-I" + STUBS, "-I" + ws,
             "-I" + os.path.join(fw, "ArduinoCore-API"),
             "-I" + os.path.join(fw, "libraries", "http-parser", "src")]
    objs = []
    for src in (os.path.join(fw, "ArduinoCore-API", "api", "String.cpp"),
                os.path.join(ws, "detail", "mimetable.cpp"), TEST):
        obj = os.path.join(build, os.path.basename(src) + ".o")
        subprocess.run(["g++", *flags, "-c", src, "-o", obj], check=True)
        objs.append(obj)
    return flags, objs


def run(flags, objs, ws, cpp_text, build, tag):
    """Builds HTTPServer.cpp from `cpp_text` (next to the real headers) and runs the scenarios."""
    d = os.path.join(build, tag)
    os.makedirs(d, exist_ok=True)
    src = os.path.join(d, "HTTPServer.cpp")
    with open(src, "w", encoding="utf-8") as f:
        f.write(cpp_text)
    exe = os.path.join(d, "test_out")
    r = subprocess.run(["g++", *flags, "-I" + ws, src, *objs, "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr[-2000:]
    r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    results = dict(re.findall(r"^RESULT (\S+) (PASS|FAIL)$", r.stdout, re.M))
    m = re.search(r"^SCENARIOS (\d+)$", r.stdout, re.M)
    if not m or int(m.group(1)) != len(results):
        return None, "the scenarios did not all report:\n" + r.stdout[-2000:]
    return results, r.stdout


def main():
    fw = framework()
    if fw is None:
        msg = "no arduino-pico framework under PLATFORMIO_CORE_DIR or ~/.platformio"
        if os.environ.get("SIMUT_WEBSERVER_OUT") == "require":
            sys.exit(f"FAIL: {msg}")
        print(f"SKIP: {msg}; nothing was tested")
        return 0
    ws = os.path.join(fw, "libraries", "WebServer", "src")
    cpp_path = os.path.join(ws, "HTTPServer.cpp")
    cpp = open(cpp_path, encoding="utf-8").read()
    for marker, which in ((MARKER_2M, "2m"), (MARKER_2N, "2n")):
        if marker not in cpp:
            sys.exit(f"FAIL: {cpp_path} does not carry override {which}; run tools/arduino_pico_overrides/patch.sh")
    if not shutil.which("g++"):
        sys.exit("FAIL: g++ not found")
    print(f"[webserver-out] {cpp_path}")

    bad = 0
    with tempfile.TemporaryDirectory() as build:
        flags, objs = compile_objs(fw, build, ws)
        results, out = run(flags, objs, ws, cpp, build, "patched")
        if results is None:
            sys.exit(f"FAIL: the patched HTTPServer.cpp did not build:\n{out}")
        print(out.rstrip())
        scenarios = len(results)
        if any(v != "PASS" for v in results.values()):
            print("FAIL: the patched server does not pass its own scenarios")
            bad += 1

        for name, (old, new, want) in MUTANTS.items():
            n = cpp.count(old)
            if n != 1:
                print(f"[webserver-out] ERROR mutant {name}: its text is in the source {n} times, not once")
                bad += 1
                continue
            results, out = run(flags, objs, ws, cpp.replace(old, new), build, name)
            if results is None:
                print(f"[webserver-out] ERROR mutant {name}: did not build\n{out}")
                bad += 1
                continue
            failed = {k for k, v in results.items() if v == "FAIL"}
            if failed == want:
                print(f"[webserver-out] mutant {name}: caught by {', '.join(sorted(failed))}")
            else:
                print(f"[webserver-out] MISSED mutant {name}: failed {sorted(failed) or 'nothing'}, "
                      f"expected {sorted(want)}")
                bad += 1
    print(f"[webserver-out] {'OK' if not bad else 'FAIL'}: {scenarios} scenarios, {len(MUTANTS)} mutants, {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
