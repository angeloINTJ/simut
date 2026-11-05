#!/usr/bin/env python3
"""test_api_lcd.py -- the answers of GET /api/lcd, run on the host (2026-11-04).

WebManager::handleApiLcd( ) (src/WebManager_History.cpp) cannot run in a native suite: it lives in a class that needs the arduino-pico WebServer, a session and the
display manager. So this cuts the function out of the source, verbatim, and compiles it with g++ against stand-ins that record what it does: a server that keeps every
header and every send( ), a `requirePerm` that answers as the real one does (it sends its own 401 or 403 and returns false), and a display whose lcdGlass( ) returns what
the case says. The real LcdGlass.h and LcdGlassJson.h are included, and PERM_SYS_CONFIG is read from src/SystemDefs_Limits.h. The stand-in display has ONE method, lcdGlass( ):
a handler that paused rendering, parked Core 1 or touched the SPI would not compile.

  python3 tools/lcd_alpha/test_api_lcd.py       exit 0 when every check holds, 1 when one fails, 2 when it cannot run (no g++, the function or the constant not found)

What it holds, one case each:
  ok          permission, a glass, millis( ) 250 after the blit: 200, ONE send( ) with the length, application/json, the header `Cache-Control: no-store`, and the body is the
              text written out by hand below (the formatter's own text is not the oracle); requirePerm was asked once, with PERM_SYS_CONFIG; the display was read once
  wrap        the clock wrapped between the blit and the read: the age is 512 ms and not 49 days
  busy        lcdGlass( ) = -1 (Core 1 kept the shadow under update): 503, `Retry-After: 1`, the Angulo's key and wording, no Cache-Control, one send( )
  none        lcdGlass( ) = 0 (no frame blitted yet): 503, `Retry-After: 1`, "nenhum quadro no LCD ainda"; the glass the display did not fill is not formatted
  nodisplay   permission and no display reference: 500, one send( ), the display never read
  denied      no permission: the 403 is requirePerm's own and the handler adds nothing (one send( ), no header), and the display is never read
  denied_nodisplay  no permission AND no display reference: still requirePerm's 403, not the 500: the permission is the first line
The text: the handler's first statement is `if (!requirePerm(PERM_SYS_CONFIG)) return;`, and it names no malloc, new, String, pauseRendering, Core 1 park, SPI or chunk.
The check bites: eight edits of the cut function (the permission taken out, moved below the display test, for another bit; Retry-After taken out; `r <= 0` as `r < 0`;
Cache-Control taken out; the clock read as 0; a send in two parts) must each fail exactly the cases they are about.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
HISTORY = ROOT / 'src' / 'WebManager_History.cpp'
LIMITS = ROOT / 'src' / 'SystemDefs_Limits.h'

# The glass of the case, as the native cases of test/test_validators/alpha_glass_json.cpp build it, and its text at 250 ms and at the wrap, written by hand.
BODY_OK = ('{"linhas":["000102030405060708090a0b0c0d0e0f","f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"],'
           '"cgram":["0001020304050607","08090a0b0c0d0e0f","1011121314151617","18191a1b1c1d1e1f",'
           '"2021222324252627","28292a2b2c2d2e2f","3031323334353637","38393a3b3c3d3e3f"],"seq":4242,"age_ms":250}')
BODY_WRAP = BODY_OK.replace('"age_ms":250', '"age_ms":512')
BUSY = '{"erro":"o LCD estava sendo redesenhado; tente de novo"}'
NONE = '{"erro":"nenhum quadro no LCD ainda"}'
NODISPLAY = '{"erro":"o aparelho não tem LCD"}'
DENIED = '{"error":"Forbidden"}'      # what the stand-in requirePerm sends: the real one's 401 or 403 body is not the handler's

CPP = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <utility>
#include "display/LcdGlassJson.h"

static uint32_t g_now = 0;
static uint32_t millis( ) { return g_now; }

struct FakeServer {
	std::vector<std::pair<std::string, std::string>> headers;
	int sends = 0, code = 0, length = -1;
	std::string type, body;
	void sendHeader(const char* k, const char* v) { headers.emplace_back(k, v); }
	void send(int c, const char* t, const char* b) { sends++; code = c; type = t; body = b; length = -1; }
	void send(int c, const char* t, const char* b, size_t n) { sends++; code = c; type = t; body.assign(b, n); length = (int)n; }
};

struct DisplayManager {                       /* ONE method: a handler that paused rendering, parked Core 1 or read the SPI would not compile */
	int8_t result = 0;
	LcdGlass glass;
	mutable int calls = 0;
	int8_t lcdGlass(LcdGlass &out) const { calls++; if (result > 0) out = glass; return result; }
};

struct WebManager {
	FakeServer* _server = nullptr;
	DisplayManager* _displayRef = nullptr;
	bool permOk = true;
	int permCalls = 0;
	unsigned permBit = 0;
	bool requirePerm(unsigned bit) {          /* as the real one: it answers by itself when it refuses */
		permCalls++; permBit = bit;
		if (!permOk) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return false; }
		return true;
	}
	void handleApiLcd( );
};

@@HANDLER@@

static LcdGlass known( ) {
	LcdGlass g; memset(&g, 0, sizeof(g));
	for (int c = 0; c < 16; c++) { g.ddram[0][c] = (uint8_t)c; g.ddram[1][c] = (uint8_t)(0xF0 + c); }
	for (int s = 0; s < 8; s++) for (int i = 0; i < 8; i++) g.cgram[s][i] = (uint8_t)(s * 8 + i);
	g.seq = 4242; g.blitMs = 1000;
	return g;
}

static void run(const char* name, bool permOk, bool hasDisplay, int8_t result, uint32_t blitMs, uint32_t now) {
	FakeServer s; DisplayManager d; WebManager w;
	d.result = result; d.glass = known( ); d.glass.blitMs = blitMs;
	g_now = now;
	w._server = &s; w._displayRef = hasDisplay ? &d : nullptr; w.permOk = permOk;
	w.handleApiLcd( );
	printf("%s\t%d\t%d\t%d\t%u\t%d\t%d\t%s\t", name, s.code, s.sends, w.permCalls, w.permBit, hasDisplay ? d.calls : -1, s.length, s.type.c_str());
	for (auto& h : s.headers) printf("%s: %s;", h.first.c_str(), h.second.c_str());
	printf("\t%s\n", s.body.c_str());
}

int main( ) {
	run("ok",               true,  true,  1,  1000u,       1250u);
	run("wrap",             true,  true,  1,  0xFFFFFF00u, 0x100u);
	run("busy",             true,  true,  -1, 1000u,       1250u);
	run("none",             true,  true,  0,  1000u,       1250u);
	run("nodisplay",        true,  false, 1,  1000u,       1250u);
	run("denied",           false, true,  1,  1000u,       1250u);
	run("denied_nodisplay", false, false, 1,  1000u,       1250u);
	return 0;
}
'''


def skip_literal(t, i):
    """The index just past the string, character literal or comment that starts at t[i], or i when none starts there."""
    if t.startswith('//', i):
        j = t.find('\n', i)
        return len(t) if j < 0 else j
    if t.startswith('/*', i):
        j = t.find('*/', i + 2)
        return len(t) if j < 0 else j + 2
    if t[i] in '"\'':
        j = i + 1
        while j < len(t) and t[j] != t[i]:
            j += 2 if t[j] == '\\' else 1
        return j + 1
    return i


def cut_handler(text):
    """`void WebManager::handleApiLcd( ) { ... }` verbatim, braces in strings and comments passed over; None when it is not there."""
    m = re.search(r'^void WebManager::handleApiLcd\( \) \{', text, re.M)
    if not m:
        return None
    i, depth = m.end() - 1, 0
    while i < len(text):
        j = skip_literal(text, i)
        if j != i:
            i = j
            continue
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[m.start():i]
    return None


def perm(name):
    m = re.search(r'#define\s+' + name + r'\s+(0x[0-9A-Fa-f]+|\d+)', LIMITS.read_text(encoding='utf-8'))
    if not m:
        raise SystemExit(f'test_api_lcd: {name} not found in {LIMITS}')
    return int(m.group(1), 0)


def run_cpp(handler, tmp, tag):
    """Compiles the harness with `handler` and runs it: {case: fields} or an error string."""
    gxx = shutil.which('g++')
    if not gxx:
        print('test_api_lcd: g++ not found', file=sys.stderr)
        sys.exit(2)
    src = pathlib.Path(tmp) / f'{tag}.cpp'
    src.write_text(CPP.replace('@@HANDLER@@', handler), encoding='utf-8')
    exe = pathlib.Path(tmp) / tag
    cmd = [gxx, '-std=gnu++17', '-O1', '-Wall', '-Wextra', f'-I{ROOT / "src"}', f'-DPERM_SYS_CONFIG={perm("PERM_SYS_CONFIG")}',
           f'-DPERM_FILE_READ={perm("PERM_FILE_READ")}', str(src), '-o', str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return 'does not compile: ' + r.stderr.strip()[:600]
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    if r.returncode:
        return f'exited {r.returncode}'
    out = {}
    for line in r.stdout.splitlines():
        f = line.split('\t')
        out[f[0]] = dict(code=int(f[1]), sends=int(f[2]), perm_calls=int(f[3]), perm_bit=int(f[4]), display_calls=int(f[5]), length=int(f[6]),
                         type=f[7], headers=f[8], body=f[9])
    return out


def verdicts(res, bit):
    """{case: [what is wrong]} for the answers `res` (a dict from run_cpp) against what each case must give."""
    bad = {}

    def need(case, cond, what):
        if not cond:
            bad.setdefault(case, []).append(what)

    ok, wrap = res.get('ok'), res.get('wrap')
    for case, body in (('ok', BODY_OK), ('wrap', BODY_WRAP)):
        r = res[case]
        need(case, r['code'] == 200 and r['sends'] == 1, f'code {r["code"]}, {r["sends"]} sends: 200 and one send( ) expected')
        need(case, r['type'] == 'application/json', f'type {r["type"]}')
        need(case, r['length'] == len(body.encode()) and r['body'] == body, f'body {r["body"][:70]}... length {r["length"]}')
        need(case, r['headers'] == 'Cache-Control: no-store;', f'headers {r["headers"]!r}')
        need(case, r['perm_calls'] == 1 and r['perm_bit'] == bit, f'requirePerm asked {r["perm_calls"]} times with {r["perm_bit"]:#x}, not once with {bit:#x}')
        need(case, r['display_calls'] == 1, f'the display read {r["display_calls"]} times')
    for case, body in (('busy', BUSY), ('none', NONE)):
        r = res[case]
        need(case, r['code'] == 503 and r['sends'] == 1 and r['type'] == 'application/json', f'code {r["code"]}, {r["sends"]} sends, {r["type"]}')
        need(case, r['headers'] == 'Retry-After: 1;', f'headers {r["headers"]!r}')
        need(case, r['body'] == body and r['length'] == -1, f'body {r["body"]!r}')
        need(case, r['perm_calls'] == 1 and r['perm_bit'] == bit and r['display_calls'] == 1, f'perm {r["perm_calls"]}, display {r["display_calls"]}')
    r = res['nodisplay']
    need('nodisplay', r['code'] == 500 and r['sends'] == 1 and r['type'] == 'application/json' and r['body'] == NODISPLAY and r['headers'] == '',
         f'code {r["code"]}, {r["sends"]} sends, body {r["body"]!r}, headers {r["headers"]!r}')
    need('nodisplay', r['perm_calls'] == 1 and r['perm_bit'] == bit, 'the permission was not asked first with the bit')
    for case in ('denied', 'denied_nodisplay'):
        r = res[case]
        need(case, r['code'] == 403 and r['sends'] == 1 and r['body'] == DENIED and r['headers'] == '', f'code {r["code"]}, {r["sends"]} sends, body {r["body"]!r}, headers {r["headers"]!r}')
        need(case, r['perm_calls'] == 1 and r['perm_bit'] == bit, f'requirePerm asked {r["perm_calls"]} times with {r["perm_bit"]:#x}')
        need(case, r['display_calls'] <= 0, 'the display was read for a caller who may not see it')
    return bad


# (what is done to the cut function, the pattern, the replacement, the cases that must fail and no others)
MUTANTS = (
    ('the permission taken out', r'\s*if \(!requirePerm\(PERM_SYS_CONFIG\)\) return;', '', {'ok', 'wrap', 'busy', 'none', 'nodisplay', 'denied', 'denied_nodisplay'}),
    ('the permission asked after the display test',
     r'(\s*)if \(!requirePerm\(PERM_SYS_CONFIG\)\) return;(\s*)(if \(!_displayRef\) \{.*?\n \})', r'\2\3\1if (!requirePerm(PERM_SYS_CONFIG)) return;', {'nodisplay', 'denied_nodisplay'}),
    ('the permission for another bit', r'requirePerm\(PERM_SYS_CONFIG\)', 'requirePerm(PERM_FILE_READ)', {'ok', 'wrap', 'busy', 'none', 'nodisplay', 'denied', 'denied_nodisplay'}),
    ('Retry-After taken out', r'\s*_server->sendHeader\("Retry-After", "1"\);', '', {'busy', 'none'}),
    ('no frame treated as a frame (r < 0)', r'if \(r <= 0\)', 'if (r < 0)', {'none'}),
    ('Cache-Control taken out', r'\s*_server->sendHeader\("Cache-Control", "no-store"\);', '', {'ok', 'wrap'}),
    ('the clock read as 0', r'millis\( \)', '0', {'ok', 'wrap'}),
    ('the body sent in two parts', r'(\s*)_server->send\(200, "application/json", json, n\);', r'\1_server->send(200, "application/json", json, n / 2);\1_server->send(200, "application/json", json, n);',
     {'ok', 'wrap'}),
)

FORBIDDEN = ('malloc', 'new ', 'String', 'pauseRendering', 'requestCore1Park', 'awaitCore1Park', 'SPI', 'readRect', 'chunk', 'static char')


def main():
    fails, count = [], [0]

    def case(label, ok, detail=''):
        count[0] += 1
        if ok:
            print(f'ok   {label}')
        else:
            fails.append(label)
            print(f'FAIL {label}' + (f': {detail}' if detail else ''))

    handler = cut_handler(HISTORY.read_text(encoding='utf-8'))
    if handler is None:
        print(f'test_api_lcd: WebManager::handleApiLcd( ) not found in {HISTORY}', file=sys.stderr)
        return 2
    bit = perm('PERM_SYS_CONFIG')
    with tempfile.TemporaryDirectory(prefix='api_lcd_') as tmp:
        res = run_cpp(handler, tmp, 'real')
        if isinstance(res, str):
            print(f'test_api_lcd: the handler {res}', file=sys.stderr)
            return 2
        bad = verdicts(res, bit)
        for name in ('ok', 'wrap', 'busy', 'none', 'nodisplay', 'denied', 'denied_nodisplay'):
            case(f'GET /api/lcd, case {name}', name not in bad, '; '.join(bad.get(name, [])))
        body = re.sub(r'/\*.*?\*/', '', handler, flags=re.S)
        body = re.sub(r'//[^\n]*', '', body)
        first = re.search(r'\{\s*(.*?;)', body, re.S)
        case('the first statement is `if (!requirePerm(PERM_SYS_CONFIG)) return;`',
             first is not None and re.sub(r'\s+', ' ', first.group(1)) == 'if (!requirePerm(PERM_SYS_CONFIG)) return;', repr(first.group(1) if first else None))
        named = [w for w in FORBIDDEN if w in body]
        case('it names no malloc, new, String, pauseRendering, Core 1 park, SPI, readRect, chunk or static buffer', not named, f'names {named}')
        for i, (label, pattern, repl, want) in enumerate(MUTANTS):
            new, n = re.subn(pattern, repl, handler, flags=re.S)
            if n != 1:
                case(f'mutant "{label}"', False, f'the pattern matches {n} times in the handler, not once: the mutant would test nothing')
                continue
            got = run_cpp(new, tmp, f'm{i}')
            if isinstance(got, str):
                case(f'mutant "{label}"', False, got)
                continue
            failed = set(verdicts(got, bit))
            case(f'mutant "{label}" fails exactly {sorted(want)}', failed == want, f'failed {sorted(failed)}')
    print(f'test_api_lcd: {count[0]} checks, {len(fails)} failed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
