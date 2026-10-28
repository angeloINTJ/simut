#!/usr/bin/env python3
"""Tests tools/check_authz.py's panel half: the gate bites.

Run: python3 tools/test_check_authz.py   (CI runs it as a gate, next to the gate itself)

A gate that finds nothing is the failure this test exists for, so each rule is proved the way a rule is proved:
the real tree passes, and one mutation of a copy of it gives exactly the finding set that mutation should, not
only a non-zero exit code. The copies are of the real src/ (the gate reads nothing else, and no git).

  * the guard arm that refuses EVT_APPLY_THEME and EVT_APPLY_LANG without PERM_SYS_CONFIG: deleted, moved after
    the two handlers, asking for another bit, or present only in a comment -> exactly those two events;
  * a check that comes after a write, a check removed from an inline arm (EVT_START_AP), a check removed from
    the function two events share (panelSetUserPin: EVT_SAVE_PASSWORD and EVT_USER_PIN) -> exactly those events;
  * the table against the enum: an enumerator it does not name, an entry the enum lacks -> that one;
  * the UNGATED reasons that the code can show: a "never mentioned" enumerator that gets mentioned in code (and
    one in a comment, which does not count), a navigation arm that saves, ALARM_SILENCE without its alarm record;
  * a registered deviation that starts checking -> a finding (the entry moves to GATED); the real four are warnings;
  * a function the table names that is not there -> exit 2, a gate that cannot look must not pass;
  * the HTTP half still bites: a route with no gate -> exactly that one finding.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GATE = os.path.join(HERE, "check_authz.py")
sys.path.insert(0, HERE)

FAILS = []
RAN = []
DEVIATIONS = ["EVT_APPLY_DISPLAY_OFFSET", "EVT_APPLY_TOUCH_CAL", "EVT_SAVE_SOUNDS", "EVT_SAVE_TOUCH_CAL"]
THEME_LANG = ["EVT_APPLY_LANG", "EVT_APPLY_THEME"]


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


try:
    import check_authz as ca                    # noqa: E402
except ImportError as e:
    check("tools/check_authz.py exists and imports", False, str(e))
    print(f"\n{len(FAILS)} of {len(RAN)} checks failed")
    sys.exit(1)

EV = "src/AppManager_Events.cpp"
PN = "src/AppManager_Panel.cpp"
RC = "src/SystemDefs_Records.h"
GUARD_RE = re.compile(r" else if \(\(uiEv\.type == UiEvent::EVT_APPLY_THEME \|\| uiEv\.type == UiEvent::EVT_APPLY_LANG\) &&\n"
                      r"\s*!panelAllowed\(PERM_SYS_CONFIG, -1\)\) \{\n[^\n]*\n \}\n")

TREES = tempfile.mkdtemp(prefix="check_authz_test_")
BASE = os.path.join(TREES, "base")
shutil.copytree(os.path.join(ROOT, "src"), os.path.join(BASE, "src"))
N = [0]


def clone():
    """A copy of the real src/ made of hard links: every mutation replaces the file, never writes through it."""
    N[0] += 1
    dst = os.path.join(TREES, "case%d" % N[0])
    try:
        shutil.copytree(BASE, dst, copy_function=os.link)
    except OSError:
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(BASE, dst)
    return dst


def mutate(tree, rel, fn):
    path = os.path.join(tree, rel)
    with open(path, encoding="utf-8", newline="") as fh:
        text = fh.read()
    new = fn(text)
    assert new != text, "the mutation of %s changed nothing" % rel
    os.remove(path)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(new)


def sub_once(old, new):
    def f(text):
        assert text.count(old) == 1, ("occurrences", text.count(old), old[:50])
        return text.replace(old, new)
    return f


def guard_sub(old, new):
    def f(text):
        m = GUARD_RE.search(text)
        assert m, "the guard arm is not in the tree"
        arm = m.group(0)
        assert old in arm, old
        return text[:m.start()] + arm.replace(old, new) + text[m.end():]
    return f


def delete_guard(text):
    m = GUARD_RE.search(text)
    assert m, "the guard arm is not in the tree"
    return text[:m.start()] + text[m.end():]


def move_guard(text):
    m = GUARD_RE.search(text)
    assert m, "the guard arm is not in the tree"
    arm = m.group(0)
    text = text[:m.start()] + text[m.end():]
    anchor = " else if (uiEv.type == UiEvent::EVT_SAVE_ALARMS) {"
    assert text.count(anchor) == 1
    i = text.index(anchor)
    return text[:i] + arm + text[i:]


def run(tree, *extra):
    p = subprocess.run([sys.executable, GATE, "--root", tree] + list(extra), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def panel_of(out):
    return sorted(set(re.findall(r"^  - panel (EVT_[A-Z0-9_]+):", out, re.M)))


def http_of(out):
    return [l[4:] for l in out.splitlines() if l.startswith("  - ") and not l.startswith("  - panel ")]


def warns_of(out):
    return sorted(re.findall(r"^  warning: (EVT_[A-Z0-9_]+)", out, re.M))


def case(name, tree, rc, panel=(), http_n=0, warn=DEVIATIONS, err=None):
    code, out, errout = run(tree)
    ok = code == rc and panel_of(out) == sorted(panel) and len(http_of(out)) == http_n
    if warn is not None and rc != 2:
        ok = ok and warns_of(out) == sorted(warn)
    if err is not None:
        ok = ok and err in errout
    check(name, ok, "rc=%s panel=%s http=%s warnings=%s stderr=%r" %
          (code, panel_of(out), http_of(out), warns_of(out), errout.strip()[:160]))


# ── the real tree ───────────────────────────────────────────────────

case("(h) the real tree: no finding, the four registered deviations as warnings", BASE, 0)
code, out, _ = run(BASE)
check("(h) the HTTP half's line is the old one: 'AUTHZ GATE: clean (N routes ...)' comes first",
      out.startswith("AUTHZ GATE: clean (") and "AUTHZ GATE (panel): clean (" in out, out[:200])
code, lst, _ = run(BASE, "--list")
i_routes = lst.find(" routes: ")
i_panel = lst.find("\nPanel events:")
# The UI drivers' routes (src/ui/**) are listed after the totals, apart and out of them (tools/check_authz.py):
# only the published images' routes are counted against http_half( ).
i_tot = re.search(r"^\d+ routes: ", lst, re.M)
http_rows = re.findall(r"^  (?:GET|POST|PUT|DELETE)\s", lst[:i_tot.start()] if i_tot else lst, re.M)
rows, failures = ca.http_half(BASE)
check("(h) --list prints the routes first (all of them, none ungated), then the panel table",
      code == 0 and 0 < i_routes < i_panel and len(http_rows) == len(rows) and not failures and ", 0 ungated" in lst[:i_panel],
      "rc=%s routes=%s rows=%s failures=%s" % (code, len(http_rows), len(rows), failures))
names = ca.PANEL_EVENTS
listed = re.findall(r"^  (EVT_[A-Z0-9_]+)\s", lst[i_panel:], re.M)
check("(h) --list names every enumerator of the table, once, and none is unclassified",
      sorted(listed) == sorted(names) and len(set(listed)) == len(listed) and "not classified" not in lst,
      "%d listed, %d in the table" % (len(listed), len(names)))
check("(h) the table is what the brief says: 14 gated, 14 ungated, 4 registered deviations",
      (sum(1 for e in names.values() if e.status == ca.GATED), sum(1 for e in names.values() if e.status == ca.UNGATED),
       sum(1 for e in names.values() if e.status == ca.DEVIATION)) == (14, 14, 4))

# ── the guard arm: THEME and LANG ───────────────────────────────────

t = clone(); mutate(t, EV, delete_guard)
case("(a) the guard arm deleted -> THEME and LANG", t, 1, THEME_LANG)
t = clone(); mutate(t, EV, move_guard)
case("(b) the guard arm moved after the THEME and LANG arms -> THEME and LANG", t, 1, THEME_LANG)
t = clone(); mutate(t, EV, guard_sub("panelAllowed(PERM_SYS_CONFIG, -1)", "panelAllowed(PERM_NET_CONFIG, -1)"))
case("(c) the guard arm asks for PERM_NET_CONFIG -> THEME and LANG", t, 1, THEME_LANG)
t = clone(); mutate(t, EV, guard_sub("!panelAllowed(PERM_SYS_CONFIG, -1)", "/* !panelAllowed(PERM_SYS_CONFIG, -1) */ false"))
case("(d) the check only in a comment -> THEME and LANG", t, 1, THEME_LANG)
t = clone(); mutate(t, EV, guard_sub("!panelAllowed(PERM_SYS_CONFIG, -1)", 'false /* "!panelAllowed(PERM_SYS_CONFIG, -1)" */'))
case("(d2) the check only in a comment and a string -> THEME and LANG", t, 1, THEME_LANG)

# ── the table against the enum ──────────────────────────────────────

t = clone(); mutate(t, RC, sub_once("EVT_NONE,", "EVT_NONE,\n EVT_PROBE_NEW,"))
case("(e) a new enumerator the table does not name -> that one", t, 1, ["EVT_PROBE_NEW"])
t = clone(); mutate(t, RC, sub_once("EVT_CALENDAR_MONTH,", ""))
case("(j) a table entry the enum lacks -> that one", t, 1, ["EVT_CALENDAR_MONTH"])

# ── a check that is missing, late, or in the function two events share ──

t = clone(); mutate(t, EV, sub_once("if (panelAllowed(PERM_NET_CONFIG, -1)) startApMode( );", "startApMode( );"))
case("(f) EVT_START_AP's panelAllowed removed -> that one", t, 1, ["EVT_START_AP"])
t = clone(); mutate(t, EV, sub_once("if (panelAllowed(PERM_NET_CONFIG, -1)) startApMode( );",
                                    'LOG_CODE(LOG_INFO, "APP", APP_UI_THEME_CHANGED, 0, "");\n if (panelAllowed(PERM_NET_CONFIG, -1)) startApMode( );'))
case("(m) a log line before EVT_START_AP's check -> that one", t, 1, ["EVT_START_AP"])
t = clone(); mutate(t, PN, sub_once("if (!own && !panelAllowed(PERM_USER_MGR, -1)) {", "if (!own) {"))
case("(l) panelSetUserPin without its check -> EVT_SAVE_PASSWORD and EVT_USER_PIN", t, 1, ["EVT_USER_PIN", "EVT_SAVE_PASSWORD"])   # the scan_secrets.sh rule reads a PASSWORD name quoted before another quoted name as a credential
t = clone(); mutate(t, PN, sub_once("if (!panelAllowed(PERM_USER_MGR, -1)) {\n _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_SETTINGS_MAIN);\n return true;\n }",
                                    "if (false) {\n _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_SETTINGS_MAIN);\n return true;\n }"))
case("(p) EVT_PIN_POLICY's case without its check -> that one", t, 1, ["EVT_PIN_POLICY"])

# ── a function the table names is not there ────────────────────────

t = clone(); mutate(t, PN, lambda s: s.replace("AppManager::panelSaveAlarmLimits(", "AppManager::panelSaveAlarmLimitsX("))
case("(g) panelSaveAlarmLimits missing -> exit 2", t, 2, err="panelSaveAlarmLimits", warn=None)
t = clone(); mutate(t, EV, lambda s: s.replace("getUiEvent(uiEv)", "getUiEventX(uiEv)"))
case("(g2) the dispatch loop missing -> exit 2", t, 2, err="getUiEvent", warn=None)

# ── the UNGATED reasons the code can show ──────────────────────────

t = clone(); mutate(t, EV, lambda s: s + "\nstatic int zzInert = UiEvent::EVT_CHANGE_PAGE;\n")
case("(n) EVT_CHANGE_PAGE, 'never mentioned', named in code -> that one", t, 1, ["EVT_CHANGE_PAGE"])
t = clone(); mutate(t, EV, lambda s: s + "\n// UiEvent::EVT_OPEN_STATS was once sent from here\n")
case("(n2) the same name in a comment -> nothing", t, 0)
t = clone(); mutate(t, EV, sub_once("refreshSelectedSlot( ); }", "refreshSelectedSlot( ); _storageMgr->saveConfiguration( ); }"))
case("(o) EVT_SLOT_SELECT, 'writes nothing', saves -> that one", t, 1, ["EVT_SLOT_SELECT"])
t = clone(); mutate(t, EV, sub_once("pushAlarmAction((int8_t)uiEv.id, ALARM_ERR_ERR_SIL, ALARM_ERR_ALARM_SIL);", ""))
case("(q) EVT_ALARM_SILENCE without the alarm record its reason quotes -> that one", t, 1, ["EVT_ALARM_SILENCE"])

# ── a registered deviation ────────────────────────────────────────

t = clone(); mutate(t, EV, sub_once("else if (uiEv.type == UiEvent::EVT_SAVE_SOUNDS) {",
                                    "else if (uiEv.type == UiEvent::EVT_SAVE_SOUNDS) {\n if (!panelAllowed(PERM_SYS_CONFIG, -1)) continue;"))
case("(i) EVT_SAVE_SOUNDS starts checking -> a finding: it moves to GATED", t, 1, ["EVT_SAVE_SOUNDS"],
     warn=[d for d in DEVIATIONS if d != "EVT_SAVE_SOUNDS"])

# ── the HTTP half still bites ────────────────────────────────────

t = clone()
mutate(t, "src/WebManager_Core.cpp",
       lambda s: s + '\nvoid WebManager::zzProbe( ) { }\nvoid WebManager::zzReg( ) { _server->on("/api/zz_probe", HTTP_GET, &WebManager::zzProbe); }\n')
case("(k) a route with no gate -> exactly that HTTP finding, none in the panel", t, 1, http_n=1)
code, out, _ = run(t)
check("(k) ... and it names the route", "/api/zz_probe" in "".join(http_of(out)), str(http_of(out)))

shutil.rmtree(TREES, ignore_errors=True)
print(f"\n{len(FAILS)} of {len(RAN)} checks failed" if FAILS else f"\nall {len(RAN)} checks passed")
sys.exit(1 if FAILS else 0)
