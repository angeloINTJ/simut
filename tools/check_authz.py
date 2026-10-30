#!/usr/bin/env python3
"""check_authz.py — the authorization matrix, pinned so it cannot silently rot.

Every HTTP route the firmware registers must enforce authorization, or be on a
short allowlist of routes that are unauthenticated *by design* (the login flow
itself, the static assets the login page needs before a session exists). This
gate parses the route registrations across src/WebManager*.cpp, follows each
route to its handler body, and fails if a handler neither checks a permission
nor is allowlisted with a reason.

Route registration used to live only in WebManager_Core.cpp. The feature-model
seam work (docs/analysis/MODELO_DE_RECURSOS.md, P2) moves each feature's routes
into its own WebManager_*.cpp — the panel mirror into WebManager_History.cpp,
POST /api/tls into WebManager_Tls.cpp — so the web core no longer tests
SIMUT_DISPLAY_TFT or SIMUT_WEB_HTTPS to place them. This gate therefore parses
every WebManager*.cpp, not just the core: wherever a route registers, it is
audited, and a feature that grows its own routes cannot slip past by living
outside the core. The scan is text, so the ?op= prose that names
"_server->on( )" in comments does not match the regex, which requires a
"path", HTTP_x, registration.

The UI drivers' routes are audited too, but counted apart (2026-10-14). The
Angulo driver (tools/features.toml, [ui.angulo]) registers GET /api/ui from
src/ui/angulo/WebManager_UiA.cpp, a unit only an image built with ui = "angulo"
compiles, and the pico_base filter does not reach that subdirectory. Its handler
needs the same gate as any other, so the gate parses every C or C++ source or
header under src/ui/ (the extensions of DRIVER_EXTS: c, cc, cpp, cxx, h, hh, hpp,
hxx, inl, ipp, tpp), at any depth and whatever the file is called, by the same
rules, and a finding there fails the check. By convention a driver registers its
routes from its own WebManager*.cpp, but a convention is not a gate: the first
version read only src/ui/*/WebManager*.cpp, and a route moved into a screen's file
(src/ui/angulo/telas/) was not audited at all (found by the review of S2a,
2026-10-14; the same route, moved there ungated, now fails the check). The second
read ".cpp" and ".h" and nothing else, so a route registered from a .hpp, .inl,
.ipp, .cc or .cxx was not audited either: no finding, exit 0, the same as a
clean file (found by a review on 2026-10-15). The list is wider than what a build
compiles on purpose, since a file that is only #included (an .inl, a .tpp)
registers its routes all the same; documents and leftovers (.md, .orig) are not
on it, being no code. The pre-commit hook simut-authz fires on the same list (the
`files:` of .pre-commit-config.yaml), and tools/test_check_authz_drivers.py moves the real
route, ungated, into a file of each extension and holds the hook to DRIVER_EXTS.
What the gate does not do is add the route to the totals: those are the routes of the published images,
tools/check_readme_numbers.py holds them against the three READMEs, and no
published image carried a driver route when this was written (2026-10-14: the Angulo driver was a bench image,
profile pico_wiretft_angulo, publish = false). Since 2026-10-30 (S10c C3) pico2_w_release builds with
ui = "angulo", so a published image serves the route. The totals still leave it out, and the three READMEs
name it next to the total (the images with the Angulo panel also serve GET /api/ui, gated). The route gets a
line of its own.

Why a gate and not just a doc: the dangerous failure here is a NEW route added
without a check — exactly the shape of the /api/restore?op=apply hole that once
let an un-authenticated multipart feed overwrite /config and /history before
the 403 was emitted (the check lived only in the finish handler, which the
framework calls AFTER the whole body streams through the callback). A reviewer
reading a diff that adds one `_server->on(...)` line will not always notice the
handler forgot its gate. This will.

It is deliberately a *source* check, not a running-server test: the arduino-pico
WebServer cannot be stood up on the native host, and the property we care about
— "this handler contains an authorization check" — is visible in the source.
The cost is that it reasons about text, so it is conservative: it asks only that
a gate token be PRESENT in the handler body, not that the token is correct. A
wrong permission bit is a matrix question for docs/AUTHORIZATION.md and human
review; a MISSING gate is what this catches mechanically.

THE PANEL'S HALF (2026-10-28). The touch panel is a fourth surface: Core 1 hands Core 0 an event and Core 0
owns authorisation (docs/AUTHORIZATION.md, "The panel"), so the check has to stand in the code that receives the
event, not only in the menu that offered the row. Every UiEvent enumerator (src/SystemDefs_Records.h) is classified
in PANEL_EVENTS below: GATED (the bit, and where the check lives), UNGATED (a reason taken from the code, which the
gate checks where the code can show it) or a registered DEVIATION. For a GATED event the first arm of the dispatch
chain in src/AppManager_Events.cpp that names it, or the function or case in src/AppManager_Panel.cpp the table
names, must call panelAllowed(<its bit>, ...) before its first write (an assignment to cfg, saveConfiguration,
LOG_CODE, loadTheme, setLanguage, or a handler the table names). An enumerator the table lacks, or a table entry the
enum lacks, is a finding; a function the table names that is not there is exit 2. A registered deviation prints as a
warning, and becomes a finding the day it checks its bit (the entry then moves to GATED).
Comments and string contents are stripped first (check_net_boundary.strip_source). It is a text gate, as above: it
proves that the check is there, with that bit, before the first write; not that its test is the right way round,
that its result is used at all (a `(void)panelAllowed(...)` passes), or that it is joined by `&&` and not `||`; and an
`#if 0` is scanned as code (the review of 2026-10-28 made each of those four pass with no finding).

Usage:  python3 tools/check_authz.py              (exit 0 clean, 1 on a finding, 2 if the tree cannot be read)
        python3 tools/check_authz.py --list       (print the whole matrix, the routes and then the panel's events, exit 0)
        python3 tools/check_authz.py --root DIR   (another tree, DIR/src: tools/test_check_authz.py and tools/test_check_authz_drivers.py use it)

CI runs the no-arg form as its own step, and the test next to it.
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The extensions of the files a UI driver's routes can be registered from. A route
# in a file the gate never opens is not a finding but an absence, and it looks
# exactly like a clean file: with ("cpp", "h") alone, a route moved into a .hpp or
# an .inl passed the gate and the hook (found by a review on 2026-10-15). Named, so
# that the hook's `files:` pattern is held to this list by tools/test_check_authz_drivers.py.
DRIVER_EXTS = ("c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "inl", "ipp", "tpp")

# Tokens whose presence in a handler body proves it performs an authorization
# check. Kept broad on purpose (see module docstring: present, not correct):
#   getAuthPerms      the accessor every gate reads
#   PERM_             any permission-bit comparison
#   _PAGE_SERVE(      the page macros expand to a serveProtectedPage(PERM_...)
#   respondIfLockedOut / isPasswordChangeRequired
#                     the forced-password-change and lockout guards on the
#                     auth-flow POSTs that legitimately predate a session
GATE_TOKENS = (
    "getAuthPerms",
    "PERM_",
    "_PAGE_SERVE(",
    "respondIfLockedOut",
    "isPasswordChangeRequired",
)

# Routes unauthenticated BY DESIGN. Keyed by (path, METHOD). Every entry needs a
# reason — this list is the audited surface a login page can reach with no
# cookie, so growing it is a security decision, not a convenience.
PUBLIC_ALLOWLIST = {
    ("/login", "GET"): "serves the login page itself",
    ("/logout", "GET"): "clears the session; nothing to protect",
    ("/api/login_init", "GET"): "issues the login nonce; the pre-auth step",
    ("/api/login", "POST"): "the credential check; gated by its own lockout, not a prior session",
    ("/api/login_chpass", "POST"): "forced password change during login; gated by lockout + must-change",
    ("/lang.js", "GET"): "static UI script the login page loads before a session exists",
    ("/style.css", "GET"): "static stylesheet the login page loads before a session exists",
    ("/favicon.ico", "GET"): "public site icon",
    ("/apple-touch-icon.png", "GET"): "204 stub so iOS stops 404-spamming; no data",
    ("/api/lang", "GET"): "UI translation dictionary for the login page; strings only, fixed internal path (no user input), same tier as /lang.js",
}

# Routes whose gate lives in a place this text scan cannot see from the handler
# body alone, each verified by hand at the cited location. Without this, the
# check would force a redundant in-body token and misrepresent where the real
# gate is.
VERIFIED_ELSEWHERE = {
    # onNotFound: not a data route — redirects off-host requests (captive
    # portal) and otherwise answers 404. Nothing to authorize.
    "handleNotFound": "redirect/404 only, serves no protected data",
}


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def parse_routes(text):
    """Yield (path, method, [handler_names]) for each _server->on(...) call.

    A registration can span several lines and bind two handlers (the upload
    routes: a completion handler and a data callback). Both must be gated, so
    every &WebManager::name on the logical statement is collected.
    """
    routes = []
    # Split on _server->on( keeping everything up to the matching statement end.
    for m in re.finditer(r'_server->on\(\s*"([^"]+)"\s*,\s*HTTP_(\w+)\s*,', text):
        path = m.group(1)
        method = m.group(2)
        # Grab the statement body from here to the next ';' at paren depth 0.
        tail = text[m.end():]
        depth = 1  # we are already inside the on( paren
        end = 0
        while end < len(tail) and depth > 0:
            c = tail[end]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            end += 1
        stmt = tail[:end]
        handlers = re.findall(r"&WebManager::(\w+)", stmt)
        # Inline lambda route (no bound WebManager method) — record with no
        # handler; the allowlist/body check treats it via its path/method.
        routes.append((path, method, handlers, stmt))
    return routes


def handler_body(all_src, name):
    """Return the body text of void WebManager::name(...) { ... }, or None."""
    for text in all_src.values():
        m = re.search(r"void\s+WebManager::" + re.escape(name) + r"\s*\([^)]*\)\s*\{", text)
        if not m:
            continue
        start = m.end() - 1  # at the opening brace
        depth = 0
        i = start
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    return text[start:i + 1]
            i += 1
        return text[start:]  # unbalanced; return the rest
    return None


def routes_files(root):
    # Every WebManager*.cpp, so a route is audited wherever a feature registers it
    # (see the module docstring). Sorted for a stable scan order.
    return sorted(glob.glob(os.path.join(root, "src", "WebManager*.cpp")))


def driver_files(root):
    # The UI drivers' route files: audited by the same rules, reported apart (see
    # the module docstring). Every file with one of DRIVER_EXTS under src/ui/ at any
    # depth, so a new driver's routes, and a route in any of its files, are found
    # without an edit.
    return sorted(p for ext in DRIVER_EXTS
                  for p in glob.glob(os.path.join(root, "src", "ui", "**", "*." + ext), recursive=True))


def audit(routes, all_src):
    """Judge every route: return (rows, failures), a row being
    (path, method, verdict, detail). all_src is where handler bodies are found."""
    rows = []       # (path, method, verdict, detail)
    failures = []

    for path, method, handlers, stmt in routes:
        key = (path, method)
        # 1. Inline lambda with no WebManager handler.
        if not handlers:
            if key in PUBLIC_ALLOWLIST:
                rows.append((path, method, "PUBLIC", PUBLIC_ALLOWLIST[key]))
            else:
                # A lambda that isn't allowlisted must gate inline.
                if any(tok in stmt for tok in GATE_TOKENS):
                    rows.append((path, method, "GATED", "inline"))
                else:
                    failures.append(f"{method} {path}: inline handler with no gate and not allowlisted")
                    rows.append((path, method, "UNGATED", "inline lambda"))
            continue

        # 2. Bound handler(s). Every handler on the statement must be gated,
        #    unless the route is allowlisted as public.
        public = key in PUBLIC_ALLOWLIST
        gate_detail = []
        route_ok = True
        for h in handlers:
            if h in VERIFIED_ELSEWHERE:
                gate_detail.append(f"{h}: {VERIFIED_ELSEWHERE[h]}")
                continue
            body = handler_body(all_src, h)
            if body is None:
                failures.append(f"{method} {path}: handler {h} definition not found")
                route_ok = False
                continue
            found = [tok for tok in GATE_TOKENS if tok in body]
            if found:
                gate_detail.append(f"{h}✓")
            elif public:
                gate_detail.append(f"{h}: public")
            else:
                failures.append(f"{method} {path}: handler {h} has no authorization gate")
                gate_detail.append(f"{h}✗")
                route_ok = False

        if public and route_ok:
            rows.append((path, method, "PUBLIC", PUBLIC_ALLOWLIST[key]))
        elif route_ok:
            rows.append((path, method, "GATED", ", ".join(gate_detail)))
        else:
            rows.append((path, method, "UNGATED", ", ".join(gate_detail)))
    return rows, failures


def _core_sources(root):
    """The core's own sources, src/*.cpp: where a handler's body is found."""
    src = os.path.join(root, "src")
    all_src = {}
    for fn in os.listdir(src):
        if fn.endswith(".cpp"):
            all_src[fn] = read(os.path.join(src, fn))
    return all_src


def http_half(root):
    """The routes' half: (rows, failures), a row being (path, method, verdict, detail). The published
    images' routes; the UI drivers' are in driver_half( )."""
    routes_text = "\n".join(read(p) for p in routes_files(root))
    return audit(parse_routes(routes_text), _core_sources(root))


def driver_half(root):
    """The UI drivers' routes: (rows, failures), judged by the same rules but reported apart, their rows
    and totals out of the published images' (see the module docstring). A handler there may bind one
    defined in the core, so the core's sources are searched too."""
    src = os.path.join(root, "src")
    driver_src = {os.path.relpath(p, src).replace(os.sep, "/"): read(p) for p in driver_files(root)}
    rows, failures = audit(parse_routes("\n".join(driver_src.values())),
                           dict(_core_sources(root), **driver_src))
    return rows, [f"UI driver (src/ui/**): {f}" for f in failures]


# ── The panel's half ─────────────────────────────────────────────────────────

class CannotRead(Exception):
    """The tree cannot be looked at: a file, a function or a switch the table names is not there.
    Exit 2: a gate that cannot look must not pass."""


EVENTS_CPP = "src/AppManager_Events.cpp"
PANEL_CPP = "src/AppManager_Panel.cpp"
RECORDS_H = "src/SystemDefs_Records.h"

GATED, UNGATED, DEVIATION = "GATED", "UNGATED", "DEVIATION"

#           status  the bit  where it is received  the function the check lives in  where that is called from
_Ev = collections.namedtuple("_Ev", "status bit at via via_in writes proof why")


def _g(bit, why, at="chain", via=None, via_in=None, writes=()):
    return _Ev(GATED, bit, at, via, via_in, tuple(writes), None, why)


def _u(why, *proof, at="chain"):
    return _Ev(UNGATED, None, at, None, None, (), proof, why)


_INERT = "declared in the enum and never sent or handled by name: src/ mentions it nowhere else"
_NOWRITE = "; the arm writes no config and logs nothing"
_DEV = ("saves the config with no panelAllowed( ) on Core 0, while its Settings row asks for PERM_SYS_CONFIG "
        "(display/SettingsMenu.h, %s). Registered deviation: the maintainer decided on 2026-10-28 that it stays "
        "so for now and is fixed in a later PR")


def _d(row):
    return _Ev(DEVIATION, "PERM_SYS_CONFIG", "chain", None, None, (), None, _DEV % row)


# Every UiEvent enumerator, once. `at` is where Core 0 receives it: the first arm of the dispatch chain in
# AppManager_Events.cpp that names it ("chain") or its case in handlePanelEvent, AppManager_Panel.cpp ("case").
# `via` is the function the arm or case hands over to, which then holds the check; `via_in` is the function
# that calls it when the arm does not. `writes` names handlers that count as a write for the order rule.
# The reasons are taken from the code at the places they name; the gate checks the ones the code can show.
PANEL_EVENTS = {
    # ── GATED: panelAllowed(<bit>, ...) stands before the first write ──
    "EVT_APPLY_THEME": _g("PERM_SYS_CONFIG",
        "the Themes row asks for the bit (display/SettingsMenu.h); a guard arm in the TFT region of the dispatch "
        "chain refuses the event before the theme handler assigns, saves or logs"),
    "EVT_APPLY_LANG": _g("PERM_SYS_CONFIG",
        "the Idioma row asks for the bit; the same guard arm, ahead of the language handler"),
    "EVT_START_AP": _g("PERM_NET_CONFIG",
        "the setup access point: the arm asks for the bit, then calls startApMode( )", writes=("startApMode",)),
    "EVT_SET_CLOCK": _g("PERM_SYS_CONFIG",
        "the bit /api/set_time asks for; with no session the arm accepts the boot's own question "
        "(clockEntryAtBoot) and nothing else", writes=("setManualTime",)),
    "EVT_SAVE_ALARMS": _g("PERM_ALARM_LIMITS",
        "the limit editor's SAVE: the arm hands the slot to panelSaveAlarmLimits( ), which asks for the bit "
        "before it writes", via="panelSaveAlarmLimits"),
    "EVT_ALARM_DEACTIVATE": _g("PERM_ALARM_BLOCK",
        "the arm only opens the PIN keypad; panelIdentify( ) calls panelDeactivateAlarm( ) once the PIN has said "
        "who is there, and that asks for the bit before it writes",
        via="panelDeactivateAlarm", via_in="panelIdentify"),
    "EVT_SAVE_PASSWORD": _g("PERM_USER_MGR",
        "the legacy keyboard's PIN goes to panelSetUserPin( ) for the session user's own slot; that function asks "
        "for the bit for another account's PIN, and one's own needs the session only", via="panelSetUserPin"),
    "EVT_USER_PIN": _g("PERM_USER_MGR",
        "the case hands the slot and the typed PIN to panelSetUserPin( ): another account's PIN asks for the bit, "
        "one's own does not", at="case", via="panelSetUserPin"),
    "EVT_PIN_POLICY": _g("PERM_USER_MGR",
        "the case asks for the bit before it reads or writes the policy", at="case"),
    "EVT_USER_ADD": _g("PERM_USER_MGR",
        "the case asks for the bit, and for the bits it would grant, before it creates the account", at="case"),
    "EVT_USER_DEL": _g("PERM_USER_MGR",
        "the case asks for the bit before it deletes the account", at="case"),
    "EVT_USER_PERMS": _g("PERM_USER_MGR",
        "the case asks for the bit, and for the bits it would grant, before it changes the account", at="case"),
    "EVT_ALARM_BLOCK": _g("PERM_ALARM_BLOCK",
        "the case asks for the bit, for the slot, before it writes", at="case"),
    "EVT_MAINT_SET": _g("PERM_MAINT",
        "the case asks for the bit, for the slot, before it writes", at="case"),
    # ── DEVIATION: saves with no check; registered, not fixed here (a warning, not a finding) ──
    "EVT_SAVE_SOUNDS": _d("MENU_SOUNDS"),
    "EVT_APPLY_TOUCH_CAL": _d("MENU_TOUCH_CAL"),
    "EVT_SAVE_TOUCH_CAL": _d("MENU_TOUCH_CAL"),
    "EVT_APPLY_DISPLAY_OFFSET": _d("MENU_DISPLAY_OFFSET"),
    # ── UNGATED: a reason, and a proof where the code can give one ──
    "EVT_NONE": _u(_INERT, "inert"),
    "EVT_CHANGE_PAGE": _u(_INERT, "inert"),
    "EVT_OPEN_STATS": _u(_INERT, "inert"),
    "EVT_ALARM_OPEN_MINMAX": _u(_INERT, "inert"),
    "EVT_SLOT_SELECT": _u("moves the selected slot on the dashboard" + _NOWRITE, "nowrite"),
    "EVT_OPEN_GRAPH": _u("opens the graph" + _NOWRITE, "nowrite"),
    "EVT_OPEN_CALENDAR": _u("opens the calendar" + _NOWRITE, "nowrite"),
    "EVT_GRAPH_NAV": _u("moves the graph's window; the loop only accumulates the steps" + _NOWRITE, "nowrite"),
    "EVT_CALENDAR_DAY": _u("opens a day's graph from the calendar" + _NOWRITE, "nowrite"),
    "EVT_CALENDAR_MONTH": _u("changes the month the calendar shows" + _NOWRITE, "nowrite"),
    "EVT_OPEN_SETTINGS": _u("ends any session and asks for a PIN: it grants nothing", "has", "_panelUser = -1",
                            "showAuthUser("),
    "EVT_MENU_SELECT": _u("opens the screen of a menu row and writes nothing; the action each screen then sends "
                          "is checked on its own, except the four registered deviations below (the Users, PIN "
                          "policy, Setup AP and Clock rows also check their bit when they open)", "nowrite"),
    "EVT_AUTH_PIN": _u("the identification itself: the PIN typed is compared in panelIdentify( ), which opens "
                       "the session every GATED event then asks for", "has", "panelIdentify(", at="case"),
    "EVT_ALARM_SILENCE": _u("anyone at the panel can stop the sound; the alarm line records it: the arm calls "
                            "_soundMgr->stopAlarm( ), then pushAlarmAction((int8_t)uiEv.id, ALARM_ERR_ERR_SIL, "
                            "ALARM_ERR_ALARM_SIL) and LOG_CODE(LOG_WARN, \"APP\", APP_UI_ALARM_SILENCED, 120, \"\")",
                            "has", "stopAlarm(", "pushAlarmAction(", "APP_UI_ALARM_SILENCED"),
}

# What counts as a write, for "the check comes before the first write": an assignment to cfg, a save, a log
# line, the theme or language setters, and the handler functions a table row names.
_WRITE_RES = (
    (re.compile(r"\bcfg\.[A-Za-z_]\w*(?:\s*\[[^\]]*\]|\s*\.\s*[A-Za-z_]\w*)*\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)"),
     "an assignment to cfg"),
    (re.compile(r"\bsaveConfiguration\s*\("), "saveConfiguration( )"),
    (re.compile(r"\bLOG_CODE\s*\("), "LOG_CODE( )"),
    (re.compile(r"\bloadTheme\s*\("), "loadTheme( )"),
    (re.compile(r"\bsetLanguage\s*\("), "setLanguage( )"),
)
ARM_RE = re.compile(r"\b(?:else\s+)?if\s*\(")
LOOP_RE = re.compile(r"while\s*\(\s*_displayMgr->getUiEvent\s*\(\s*uiEv\s*\)\s*\)")
ENUM_RE = re.compile(r"struct\s+UiEvent\s*\{\s*enum\s+EventType\s*\{(.*?)\}", re.S)
Arm = collections.namedtuple("Arm", "start end cond text line")


def _blank_preprocessor(code):
    """Preprocessor lines (and their backslash continuations) blanked, newlines kept: `#if (X)` is not an arm."""
    out, cont = [], False
    for line in code.split("\n"):
        if cont or line.lstrip().startswith("#"):
            cont = line.rstrip().endswith("\\")
            out.append(" " * len(line))
        else:
            out.append(line)
    return "\n".join(out)


def _match_close(code, i, op, cl, rel):
    """Index of the bracket that closes the one at code[i]."""
    depth = 0
    for k in range(i, len(code)):
        c = code[k]
        if c == op:
            depth += 1
        elif c == cl:
            depth -= 1
            if depth == 0:
                return k
    raise CannotRead("%s:%d: unbalanced '%s'" % (rel, code.count("\n", 0, i) + 1, op))


def _stmt_end(code, j):
    depth = 0
    for k in range(j, len(code)):
        c = code[k]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ";" and depth <= 0:
            return k + 1
    return len(code)


def _first_write(text, handlers=()):
    """(offset, label) of the earliest write in `text`, or None."""
    pats = list(_WRITE_RES) + [(re.compile(r"\b" + re.escape(h) + r"\s*\("), h + "( )") for h in handlers]
    best = None
    for rx, label in pats:
        m = rx.search(text)
        if m and (best is None or m.start() < best[0]):
            best = (m.start(), label)
    return best


def _verdict(text, bit, handlers=()):
    """None when panelAllowed(bit, ...) stands in `text` before its first write; else (kind, why)."""
    m = re.search(r"\bpanelAllowed\s*\(\s*" + re.escape(bit) + r"\s*,", text)
    w = _first_write(text, handlers)
    if m is None:
        return ("nocheck", "no panelAllowed(%s, ...) in it%s" % (bit, "" if w is None else " (it writes: %s)" % w[1]))
    if w is not None and w[0] < m.start():
        return ("late", "%s comes before panelAllowed(%s, ...)" % (w[1], bit))
    return None


def _has(text, token):
    return re.search(r"\s*".join(re.escape(p) for p in token.split()), text) is not None


def _case_segment(body, name):
    """(text, offset in body) of `case UiEvent::name:` up to the next case or default label, or None."""
    m = re.search(r"\bcase\s+UiEvent::" + re.escape(name) + r"\s*:", body)
    if m is None:
        return None
    n = re.search(r"\bcase\s+UiEvent::\w+\s*:|\bdefault\s*:", body[m.end():])
    end = m.end() + n.start() if n else len(body)
    return body[m.start():end], m.start()


class _Tree:
    """The sources of one tree, comments and string contents stripped once (check_net_boundary.strip_source)."""

    def __init__(self, root):
        self.root = root
        self._code = {}
        self._arms = None
        self._fn = {}

    def code(self, rel):
        if rel not in self._code:
            import check_net_boundary as cnb
            try:
                text = read(os.path.join(self.root, rel))
            except OSError as e:
                raise CannotRead("%s: %s" % (rel, e.strerror or e))
            stripped, _includes = cnb.strip_source(text)
            self._code[rel] = _blank_preprocessor(stripped)
        return self._code[rel]

    def line(self, rel, pos):
        return self.code(rel).count("\n", 0, pos) + 1

    def enum(self):
        m = ENUM_RE.search(self.code(RECORDS_H))
        names = re.findall(r"\b(EVT_[A-Z0-9_]+)\b", m.group(1)) if m else []
        if not names:
            raise CannotRead("%s: struct UiEvent / enum EventType not found" % RECORDS_H)
        return names

    def arms(self):
        """Every if / else if after the dispatch loop `while (_displayMgr->getUiEvent(uiEv))`, in text order."""
        if self._arms is None:
            code = self.code(EVENTS_CPP)
            loop = LOOP_RE.search(code)
            if loop is None:
                raise CannotRead("%s: the loop `while (_displayMgr->getUiEvent(uiEv))` is not there" % EVENTS_CPP)
            out = []
            for a in ARM_RE.finditer(code, loop.end()):
                po = a.end() - 1
                pc = _match_close(code, po, "(", ")", EVENTS_CPP)
                j = pc + 1
                while j < len(code) and code[j].isspace():
                    j += 1
                if j < len(code) and code[j] == "{":
                    end = _match_close(code, j, "{", "}", EVENTS_CPP) + 1
                else:
                    end = _stmt_end(code, j)
                out.append(Arm(a.start(), end, code[po + 1:pc], code[a.start():end], self.line(EVENTS_CPP, a.start())))
            self._arms = out
        return self._arms

    def first_arm(self, name):
        rx = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])")
        for arm in self.arms():
            if rx.search(arm.cond):
                return arm
        return None

    def function(self, rel, name):
        """(body, offset) of AppManager::name. Of several definitions (the one-line stubs of the images without a
        panel, #if !SIMUT_DISPLAY_TFT) the longest."""
        key = (rel, name)
        if key not in self._fn:
            code = self.code(rel)
            best = None
            for m in re.finditer(r"\bAppManager::" + re.escape(name) + r"\s*\([^)]*\)\s*(?:const\s*)?\{", code):
                o = m.end() - 1
                c = _match_close(code, o, "{", "}", rel)
                if best is None or c - o > best[1] - best[0]:
                    best = (o, c)
            if best is None:
                raise CannotRead("%s: no definition of AppManager::%s( )" % (rel, name))
            self._fn[key] = (code[best[0]:best[1] + 1], best[0])
        return self._fn[key]

    def mentions(self, names):
        """{name: [(file, line)]}: where src/ names an enumerator in code, outside the enum itself."""
        import check_net_boundary as cnb
        found = {n: [] for n in names}
        pat = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(re.escape(n) for n in names) + r")(?![A-Za-z0-9_])")
        em = ENUM_RE.search(self.code(RECORDS_H))
        enum_span = (em.start(1), em.end(1)) if em else (0, 0)
        for dirpath, dirnames, filenames in os.walk(os.path.join(self.root, "src")):
            dirnames.sort()
            for fn in sorted(filenames):
                if not fn.endswith((".h", ".hpp", ".c", ".cpp", ".ino")):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, self.root).replace(os.sep, "/")
                raw = read(path)
                if not pat.search(raw):
                    continue
                code, _includes = cnb.strip_source(raw)
                for m in pat.finditer(code):
                    if rel == RECORDS_H and enum_span[0] <= m.start() < enum_span[1]:
                        continue
                    found[m.group(1)].append((rel, code.count("\n", 0, m.start()) + 1))
        return found


def _site(t, name, ev):
    """(text, where) of the place that receives the event, or None."""
    if ev.at == "chain":
        arm = t.first_arm(name)
        return None if arm is None else (arm.text, "%s:%d" % (EVENTS_CPP, arm.line))
    body, base = t.function(PANEL_CPP, "handlePanelEvent")
    seg = _case_segment(body, name)
    return None if seg is None else (seg[0], "%s:%d" % (PANEL_CPP, t.line(PANEL_CPP, base + seg[1])))


def _gate_problem(t, name, ev):
    """None when the event's check stands; else (kind, message)."""
    site = _site(t, name, ev)
    if site is None:
        place = ("an arm of the dispatch chain in " + EVENTS_CPP if ev.at == "chain"
                 else "a case in handlePanelEvent (" + PANEL_CPP + ")")
        return ("nosite", "nothing receives it: %s names it" % place)
    text, where = site
    if ev.via:
        called, called_where = text, where
        if ev.via_in:
            called, base = t.function(PANEL_CPP, ev.via_in)
            called_where = "%s:%d" % (PANEL_CPP, t.line(PANEL_CPP, base))
        if not re.search(r"\b" + re.escape(ev.via) + r"\s*\(", called):
            return ("uncalled", "%s does not call %s( ), where the check lives" % (called_where, ev.via))
        ftext, fbase = t.function(PANEL_CPP, ev.via)
        v = _verdict(ftext, ev.bit, ev.writes)
        return None if v is None else (v[0], "%s( ) (%s:%d): %s" % (ev.via, PANEL_CPP, t.line(PANEL_CPP, fbase), v[1]))
    v = _verdict(text, ev.bit, ev.writes)
    return None if v is None else (v[0], "%s: %s" % (where, v[1]))


def _proof_problem(t, name, ev, mentions):
    """None when the UNGATED reason holds where the code can show it; else a message."""
    kind = ev.proof[0]
    if kind == "inert":
        hits = mentions.get(name, [])
        return None if not hits else "the table says nothing sends or handles it, but %s:%d names it" % hits[0]
    site = _site(t, name, ev)
    if site is None:
        return "nothing receives it, but the table gives a reason that needs the place"
    text, where = site
    if kind == "nowrite":
        w = _first_write(text)
        return None if w is None else "%s: the table says it writes nothing, but %s is in it" % (where, w[1])
    missing = [tok for tok in ev.proof[1:] if not _has(text, tok)]
    return None if not missing else "%s: the reason quotes %s, which is not there" % (where, ", ".join(missing))


def panel_half(root):
    """(findings, warnings, names): (event, message) pairs, and the enumerators in enum order. May raise CannotRead."""
    t = _Tree(root)
    names = t.enum()
    findings, warnings = [], []
    for n in names:
        if n not in PANEL_EVENTS:
            findings.append((n, "not classified: add it to PANEL_EVENTS in tools/check_authz.py as GATED (the bit, "
                                "and where the check lives) or UNGATED (the reason)"))
    for n in PANEL_EVENTS:
        if n not in names:
            findings.append((n, "classified in PANEL_EVENTS, but UiEvent has no such enumerator: remove the entry"))
    inert = [n for n in names if n in PANEL_EVENTS and PANEL_EVENTS[n].proof and PANEL_EVENTS[n].proof[0] == "inert"]
    mentions = t.mentions(inert) if inert else {}
    for n in names:
        ev = PANEL_EVENTS.get(n)
        if ev is None:
            continue
        if ev.status == GATED:
            p = _gate_problem(t, n, ev)
            if p:
                findings.append((n, p[1]))
        elif ev.status == UNGATED:
            p = _proof_problem(t, n, ev, mentions)
            if p:
                findings.append((n, p))
        else:
            p = _gate_problem(t, n, ev)
            if p is None:
                findings.append((n, "registered as a deviation, but it now checks %s before its first write: "
                                    "move it to GATED in PANEL_EVENTS" % ev.bit))
            elif p[0] in ("nocheck", "late"):
                warnings.append((n, "no %s check: %s" % (ev.bit, ev.why)))
            else:
                findings.append((n, p[1]))
    return findings, warnings, names


def _where(ev):
    if ev.proof and ev.proof[0] == "inert":
        return "-"
    w = "arm" if ev.at == "chain" else "case"
    if ev.via:
        w += " -> " + (ev.via_in + " -> " if ev.via_in else "") + ev.via
    return w


def _counts(names):
    c = collections.Counter(PANEL_EVENTS[n].status for n in names if n in PANEL_EVENTS)
    return c[GATED], c[UNGATED], c[DEVIATION]


def print_panel_list(names, findings, warnings):
    bad = {n for n, _ in findings}
    warn = {n for n, _ in warnings}
    shown = list(names) + [n for n in PANEL_EVENTS if n not in names]
    w = max(len(n) for n in shown)
    print("\nPanel events: every UiEvent (src/SystemDefs_Records.h) and who checks the session's bit on Core 0")
    for n in shown:
        ev = PANEL_EVENTS.get(n)
        if ev is None:
            print(f"  {n:<{w}}  {'-':9} {'-':18} {'-':8} {'-':34} not classified")
            continue
        verdict = "FINDING" if n in bad else ("warning" if n in warn else "ok")
        print(f"  {n:<{w}}  {ev.status:9} {ev.bit or '-':18} {verdict:8} {_where(ev):34} {ev.why}")
    g, u, d = _counts(names)
    print(f"\n{len(names)} events: {g} gated, {u} ungated, {d} registered deviations")


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    list_mode = "--list" in argv
    root = ROOT
    if "--root" in argv:
        i = argv.index("--root")
        if i + 1 >= len(argv):
            print("check_authz: --root needs a directory", file=sys.stderr)
            return 2
        root = os.path.abspath(argv[i + 1])
    if not os.path.isdir(os.path.join(root, "src")):
        print(f"check_authz: no src/ under {root}", file=sys.stderr)
        return 2

    rows, failures = http_half(root)
    driver_rows, driver_failures = driver_half(root)
    failures = failures + driver_failures

    if list_mode:
        rows.sort(key=lambda r: (r[0], r[1]))
        w = max(len(r[0]) for r in rows)
        for path, method, verdict, detail in rows:
            print(f"  {method:5} {path:<{w}}  {verdict:8} {detail}")
        print(f"\n{len(rows)} routes: "
              f"{sum(1 for r in rows if r[2]=='GATED')} gated, "
              f"{sum(1 for r in rows if r[2]=='PUBLIC')} public-by-design, "
              f"{sum(1 for r in rows if r[2]=='UNGATED')} ungated")
        if driver_rows:
            driver_rows.sort(key=lambda r: (r[0], r[1]))
            w = max(len(r[0]) for r in driver_rows)
            print("\nUI drivers, src/ui/** (not in the totals above):")
            for path, method, verdict, detail in driver_rows:
                print(f"  {method:5} {path:<{w}}  {verdict:8} {detail}")
        try:
            findings, warnings, names = panel_half(root)
        except CannotRead as e:
            print(f"check_authz: the panel's half cannot look: {e}", file=sys.stderr)
            return 2
        print_panel_list(names, findings, warnings)
        return 0

    rc = 0
    if failures:
        print("AUTHZ GATE: FAIL")
        for f in failures:
            print(f"  - {f}")
        print("\nEvery route must gate on a PERM_/getAuthPerms check, or be added to")
        print("PUBLIC_ALLOWLIST in tools/check_authz.py with a reason. If a gate lives")
        print("outside the handler body, cite it in VERIFIED_ELSEWHERE.")
        rc = 1
    else:
        print(f"AUTHZ GATE: clean ({len(rows)} routes — "
              f"{sum(1 for r in rows if r[2]=='GATED')} gated, "
              f"{sum(1 for r in rows if r[2]=='PUBLIC')} public-by-design)")
        if driver_rows:
            # No parenthesised "(N routes — G gated, P public-by-design)" here: that
            # shape is what check_readme_numbers.py reads the published totals by.
            print(f"AUTHZ GATE: UI drivers, src/ui/**: {len(driver_rows)} route{'s' if len(driver_rows) != 1 else ''}, "
                  f"{sum(1 for r in driver_rows if r[2]=='GATED')} gated, "
                  f"{sum(1 for r in driver_rows if r[2]=='PUBLIC')} public-by-design; "
                  f"not in the totals above: they are the UI driver's, and only an image built with ui = \"angulo\" serves them "
                  f"(the Pico 2 W release since 2026-10-30, S10c C3, and the bench image pico_wiretft_angulo)")

    try:
        findings, warnings, names = panel_half(root)
    except CannotRead as e:
        print(f"AUTHZ GATE (panel): cannot look: {e}", file=sys.stderr)
        return 2
    if findings:
        print("AUTHZ GATE (panel): FAIL")
        for n, msg in findings:
            print(f"  - panel {n}: {msg}")
        print("\nEvery UiEvent is classified in PANEL_EVENTS (tools/check_authz.py). A GATED event must call")
        print("panelAllowed(<its bit>, ...) before its first write: in the first arm of the dispatch chain")
        print("(src/AppManager_Events.cpp) that names it, or in the function or case the table names")
        print("(src/AppManager_Panel.cpp). Core 0 does not take the screen's word: the event arrives from Core 1.")
        rc = 1
    else:
        g, u, d = _counts(names)
        print(f"AUTHZ GATE (panel): clean ({len(names)} events — {g} gated, {u} ungated, {d} registered deviations)")
    for n, msg in warnings:
        print(f"  warning: {n} — {msg}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
