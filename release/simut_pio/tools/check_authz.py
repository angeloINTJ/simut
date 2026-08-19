#!/usr/bin/env python3
"""check_authz.py — the authorization matrix, pinned so it cannot silently rot.

Every HTTP route the firmware registers must enforce authorization, or be on a
short allowlist of routes that are unauthenticated *by design* (the login flow
itself, the static assets the login page needs before a session exists). This
gate parses the route table in src/WebManager_Core.cpp, follows each route to
its handler body, and fails if a handler neither checks a permission nor is
allowlisted with a reason.

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

Usage:  python3 tools/check_authz.py         (exit 0 clean, 1 on a finding)
        python3 tools/check_authz.py --list  (print the whole matrix, exit 0)

CI runs the no-arg form as its own step.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
ROUTES_FILE = os.path.join(SRC, "WebManager_Core.cpp")

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


def main():
    list_mode = "--list" in sys.argv

    routes_text = read(ROUTES_FILE)
    all_src = {}
    for fn in os.listdir(SRC):
        if fn.endswith(".cpp"):
            all_src[fn] = read(os.path.join(SRC, fn))

    routes = parse_routes(routes_text)
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

    if list_mode:
        rows.sort(key=lambda r: (r[0], r[1]))
        w = max(len(r[0]) for r in rows)
        for path, method, verdict, detail in rows:
            print(f"  {method:5} {path:<{w}}  {verdict:8} {detail}")
        print(f"\n{len(rows)} routes: "
              f"{sum(1 for r in rows if r[2]=='GATED')} gated, "
              f"{sum(1 for r in rows if r[2]=='PUBLIC')} public-by-design, "
              f"{sum(1 for r in rows if r[2]=='UNGATED')} ungated")
        return 0

    if failures:
        print("AUTHZ GATE: FAIL")
        for f in failures:
            print(f"  - {f}")
        print("\nEvery route must gate on a PERM_/getAuthPerms check, or be added to")
        print("PUBLIC_ALLOWLIST in tools/check_authz.py with a reason. If a gate lives")
        print("outside the handler body, cite it in VERIFIED_ELSEWHERE.")
        return 1

    print(f"AUTHZ GATE: clean ({len(rows)} routes — "
          f"{sum(1 for r in rows if r[2]=='GATED')} gated, "
          f"{sum(1 for r in rows if r[2]=='PUBLIC')} public-by-design)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
