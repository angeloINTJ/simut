#!/usr/bin/env python3
"""Tests tools/check_patch_sentinels.py, the gate that keeps every step of patch.sh able to tell its patch is applied.

Run: python3 tools/test_patch_sentinels.py   (CI runs it as a gate, next to the gate itself)

A gate that finds nothing is the failure this test exists for, so each rule is proved the way a rule is
proved: the violation put into a clean case gives exactly ONE finding, of that rule, on its line, and the
same text where the rule is not broken gives none.

  * The reader of unified diffs: only `+` lines count (not context, not removed lines, not the `+++` header,
    and an added line that LOOKS like a header, `++ x`, is still an added line); the path is the `+++` path
    minus the -p1 prefix; two files in one patch stay apart; "\\ No newline" is neither side.
  * Synthetic trees (a temp directory with a patch.sh and a patches/): the four rules, the variables patch.sh
    builds its paths from (assigned above the guard, and assigned AGAIN), and the command's exit codes and
    output.
  * The real tree: it passes, and the gate reads every `patch -p1` of the script. Then each of its guards is
    broken in turn, four ways (its sentinel changed; the marker in its patch changed; the guard taken away;
    the patch file gone), and each gives exactly one finding, at that step. One more is 2b-ter as it was on
    2026-10-15, the sentinel that started this: "drop a non-TLS client" against a patch that adds "tell a
    non-TLS client from a TLS one by its FIRST BYTE". It passed the first run; on the second, `patch` met a
    patch already applied and `set -e` stopped patch.sh there (reproduced on an isolated core, 2026-10-16).
  * The gate only reads: the sha256 of patch.sh and of every patch is the same after every run.
"""
import contextlib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "check_patch_sentinels.py")
OVR = os.path.join(HERE, "arduino_pico_overrides")
sys.path.insert(0, HERE)
import check_patch_sentinels as cps                 # noqa: E402

FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def brief(found):
    return "; ".join(f"{f.line}:{f.rule}:{f.text[:70]}" for f in found) or "no finding"


def exactly(name, found, rule, line, mention=None):
    """`found` is ONE finding, of this rule, on this line (and its text says `mention`, if given)."""
    check(name, len(found) == 1 and found[0].rule == rule and found[0].line == line
          and (mention is None or mention in found[0].text), brief(found))


# ── synthetic trees ────────────────────────────────────────────────────────────────────────────

HEAD = '#!/usr/bin/env bash\nset -e\nFW=""\nOVR="$ROOT/tools/arduino_pico_overrides"\n'     # 4 lines
MARK = "SIMUT override — alpha"
ADD = "// " + MARK + ", the marker"


def step(guard=MARK, file='"$A"', patch='"$OVR/patches/a.patch"'):
    return (f'if grep -q "{guard}" {file}; then\n    echo "[patch] a já aplicada — nada a fazer"\nelse\n'
            f'    echo "[patch] aplicando a"\n    patch -p1 -d "$FW" < {patch}\nfi\n')


def script(*body, assigns='A="$FW/lib/A.cpp"\n'):
    return HEAD + assigns + "".join(body)


def diff(path, *added, before="int a;", after="int b;", removed=()):
    lines = [" " + before] + ["-" + r for r in removed] + ["+" + a for a in added] + [" " + after]
    return (f"--- a/{path}\n+++ b/{path}\n@@ -1,{2 + len(removed)} +1,{2 + len(added)} @@\n" + "\n".join(lines) + "\n")


def line_of(text, needle, nth=1):
    return [i for i, line in enumerate(text.splitlines(), 1) if needle in line][nth - 1]


@contextlib.contextmanager
def tree(sh, patches):
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "patches"))
        files = {"patch.sh": sh, **{f"patches/{k}": v for k, v in patches.items()}}
        for rel, body in files.items():
            with open(os.path.join(d, rel), "w", encoding="utf-8") as f:
                f.write(body)
        yield d


def run(sh, patches):
    with tree(sh, patches) as d:
        return cps.check(os.path.join(d, "patch.sh"), d)


def cli(sh, patches, *extra):
    with tree(sh, patches) as d:
        r = subprocess.run([sys.executable, GATE, "--ovr", d, *extra], capture_output=True, text=True)
        return r.returncode, r.stdout, r.stderr, d


def diff_cases():
    d = ("--- a/lib/A.cpp\n+++ b/lib/A.cpp\n@@ -1,3 +1,4 @@\n int a;\n-int old;\n+int new;\n+// SIMUT override — added\n int b;\n"
         "--- a/lib/B.h\t2026-10-16\n+++ b/lib/B.h\t2026-10-16\n@@ -1,2 +1,3 @@\n int c;\n+++ looks like a header\n int d;\n"
         "@@ -10 +11,2 @@\n--- looks like a header too\n+// kept\n+// ok\n")
    got = cps.added_lines(d)
    check("added_lines: only the + lines, per file, -p1 prefix and header timestamp dropped",
          got == {"lib/A.cpp": ["int new;", "// SIMUT override — added"],
                  "lib/B.h": ["++ looks like a header", "// kept", "// ok"]}, str(got))
    got = cps.added_lines(diff("lib/A.cpp", "int x;", before="// " + MARK, removed=["// " + MARK + " v0"]))
    check("added_lines: a marker in a context line or a removed line is not an added line",
          got == {"lib/A.cpp": ["int x;"]}, str(got))
    nn = "--- a/f\n+++ b/f\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n\\ No newline at end of file\n"
    check("added_lines: '\\ No newline at end of file' inside a hunk counts for neither side",
          cps.added_lines(nn) == {"f": ["b"]}, str(cps.added_lines(nn)))
    blank = "--- a/f\n+++ b/f\n@@ -1,3 +1,4 @@\n a\n\n+x\n c\n"      # the blank context line lost its space
    check("added_lines: a context line that lost its space is still context",
          cps.added_lines(blank) == {"f": ["x"]}, str(cps.added_lines(blank)))


def synthetic_cases():
    ok = {"a.patch": diff("lib/A.cpp", ADD)}
    f, st = run(script(step()), ok)
    check("a guard on the text its patch adds is no finding (and is read as a step)", not f and len(st) == 1, brief(f))

    t = script(step(guard="SIMUT override — drop a non-TLS client"))
    new_line = "// SIMUT override — tell a non-TLS client from a TLS one by its FIRST BYTE,"
    f, _ = run(t, {"a.patch": diff("lib/A.cpp", new_line)})
    exactly("2b-ter as it was: a sentinel the patch does not add is one finding, at the guard, naming what it adds",
            f, "not-added", line_of(t, "if grep"), mention="tell a non-TLS client")

    t = script(step())
    f, _ = run(t, {"a.patch": diff("lib/A.cpp", "int x;", before="// " + MARK)})
    exactly("a sentinel only in a context line is not added", f, "not-added", line_of(t, "if grep"))
    f, _ = run(t, {"a.patch": diff("lib/A.cpp", "int x;", removed=["// " + MARK])})
    exactly("a sentinel only in a removed line is not added", f, "not-added", line_of(t, "if grep"))
    f, _ = run(t, {"a.patch": diff("lib/Z.cpp", ADD)})
    exactly("a patch that does not touch the grepped file is not added", f, "not-added", line_of(t, "if grep"),
            mention="does not touch lib/A.cpp")

    t = script(step(), step(file='"$B"'), assigns='A="$FW/lib/A.cpp"\nB="$FW/lib/B.cpp"\n')
    f, _ = run(t, {"a.patch": diff("lib/A.cpp", "int x;") + diff("lib/B.cpp", ADD)})
    exactly("the marker added to the OTHER file of the patch is not added; the guard on that file is clean",
            f, "not-added", line_of(t, "if grep", 1))
    f, _ = run(t, {"a.patch": diff("lib/A.cpp", ADD) + diff("lib/B.cpp", ADD)})
    check("the same patch with the marker added to both files is clean", not f, brief(f))

    t = script(step())
    f, _ = run(t, {})
    exactly("a patch file that is not there is no-patch, at the patch command", f, "no-patch",
            line_of(t, "patch -p1 -d"), mention="patches/a.patch")

    unguarded = {
        "a bare patch -p1": 'patch -p1 -d "$FW" < "$OVR/patches/a.patch"\n',
        "a patch fed by a pipe": 'cat "$OVR/patches/a.patch" | patch -p1 -d "$FW"\n',
        "a patch with other flags": 'if grep -q "' + MARK + '" "$A"; then\n  :\nelse\n  patch -p0 -d "$FW" < "$OVR/patches/a.patch"\nfi\n',
        "a patch behind a test that is not a grep": 'if [ -f "$A" ]; then\n  :\nelse\n  patch -p1 -d "$FW" < "$OVR/patches/a.patch"\nfi\n',
        "a patch in the THEN branch of a guard": 'if grep -q "' + MARK + '" "$A"; then\n  patch -p1 -d "$FW" < "$OVR/patches/a.patch"\nfi\n',
    }
    for name, extra in unguarded.items():
        t = script(step(), extra)
        f, _ = run(t, ok)         # the guarded step before it is clean: one finding, not two
        exactly(f"unguarded: {name} is one finding, at its patch command", f, "unguarded", line_of(t, "patch -p", 2))

    for ch in ("$HOME", "`x`", "\\\\", "[a]", "a*", "^a"):
        t = script(step(guard=MARK + " " + ch))
        f, _ = run(t, {"a.patch": diff("lib/A.cpp", "// " + MARK + " " + ch)})
        exactly(f"unreadable: a sentinel with {ch!r} is one finding although the patch adds that very text",
                f, "unreadable", line_of(t, "if grep"))
    for what, t in (("a variable that was never assigned", script(step(file='"$NOPE"'))),
                    ("a file outside $FW", script(step(file='"/etc/hosts"'))),
                    ("a command substitution", script(step(file='"$(pwd)/A.cpp"'))),
                    ("a command substitution after $FW", script(step(file='"$FW/lib/$(echo A).cpp"')))):
        f, _ = run(t, ok)
        exactly(f"unreadable: a guard file from {what} is one finding", f, "unreadable", line_of(t, "if grep"))
    t = script(step(patch='"$NOPE/a.patch"'))
    f, _ = run(t, ok)
    exactly("unreadable: a patch path from a variable never assigned is one finding, at the patch command",
            f, "unreadable", line_of(t, "patch -p1 -d"))

    f, st = run(script(step(), assigns='D="$FW/lib"\nA="$D/A.cpp"\n'), ok)
    check("variables: a path built from another variable is resolved", not f and len(st) == 1, brief(f))
    f, st = run(script(step(), assigns='D="$FW/lib"\nA="${D}/A.cpp"\n'), ok)
    check("variables: ${NAME} is resolved too", not f and len(st) == 1, brief(f))
    twice = script(step(), 'A="$FW/lib/B.cpp"\n', step(patch='"$OVR/patches/b.patch"'))
    f, st = run(twice, {"a.patch": diff("lib/A.cpp", ADD), "b.patch": diff("lib/B.cpp", ADD)})
    check("variables: a name assigned AGAIN is read as of the guard that uses it", not f and len(st) == 2, brief(f))
    f, _ = run(twice, {"a.patch": diff("lib/A.cpp", ADD), "b.patch": diff("lib/A.cpp", ADD)})
    exactly("variables: the second guard greps B, so a patch that adds to A is one finding, at that guard",
            f, "not-added", line_of(twice, "if grep", 2))

    t = script(step(), step(guard="SIMUT override — beta", file='"$B"', patch='"$OVR/patches/b.patch"'),
               assigns='A="$FW/lib/A.cpp"\nB="$FW/lib/B.cpp"\n')
    f, st = run(t, {"a.patch": diff("lib/A.cpp", ADD), "b.patch": diff("lib/B.cpp", "// SIMUT override — beta")})
    check("two clean steps are two steps and no finding", not f and len(st) == 2, brief(f))


def cli_cases():
    ok = {"a.patch": diff("lib/A.cpp", ADD)}
    rc, out, err, _ = cli(script(step()), ok)
    check("command: a clean tree exits 0 and says how many steps it read",
          rc == 0 and out.strip().endswith("0 findings in 1 guarded steps") and not err, f"{rc} {out!r} {err!r}")
    t = script(step(guard="SIMUT override — drop a non-TLS client"))
    rc, out, err, d = cli(t, ok)
    line = line_of(t, "if grep")
    check("command: a finding exits 1 as `path:LINE: rule: text`, with the rule's hint on stderr",
          rc == 1 and out.startswith(f"{d}/patch.sh:{line}: not-added: ") and "1 findings in 1 guarded steps" in out
          and "check_patch_sentinels: not-added: grep the" in err, f"{rc} {out!r} {err!r}")
    rc, out, err, _ = cli(script(), ok)
    check("command: a script with no guarded step exits 2, not 0", rc == 2 and "does not understand" in err, f"{rc} {out!r} {err!r}")
    t = script('patch -p1 -d "$FW" < "$OVR/patches/a.patch"\n')
    rc, out, err, _ = cli(t, ok)
    check("command: a script whose only patch is unguarded exits 1 with that finding (not 2: it did read something)",
          rc == 1 and f":{line_of(t, 'patch -p1')}: unguarded: " in out, f"{rc} {out!r} {err!r}")
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run([sys.executable, GATE, "--ovr", d], capture_output=True, text=True)
        check("command: no patch.sh exits 2 and says which file", r.returncode == 2 and "patch.sh" in r.stderr, f"{r.returncode} {r.stderr!r}")
    with tree(script(step()), ok) as d:
        other = os.path.join(d, "other.sh")
        with open(other, "w", encoding="utf-8") as f:
            f.write(script(step(guard="SIMUT override — drop a non-TLS client")))
        r = subprocess.run([sys.executable, GATE, "--ovr", d, "--patch-sh", other], capture_output=True, text=True)
        check("command: --patch-sh reads another script against the same patches", r.returncode == 1 and "other.sh:6: not-added" in r.stdout,
              f"{r.returncode} {r.stdout!r}")


def real_cases():
    sh = os.path.join(OVR, "patch.sh")
    with open(sh, encoding="utf-8") as f:
        text = f.read()
    found, steps = cps.check(sh, OVR)
    check("the real patch.sh: no finding", not found, brief(found))
    n = len(re.findall(r"^\s*patch -p1\b", text, re.M))
    check(f"the gate reads every `patch -p1` of the real script ({len(steps)} steps of {n})", n > 0 and len(steps) == n)

    def broken(edit):
        """The real script with `edit(lines)` applied: (findings, steps, new text)."""
        lines = text.split("\n")
        edit(lines)
        new = "\n".join(lines)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "patch.sh")
            with open(p, "w", encoding="utf-8") as f:
                f.write(new)
            got, st = cps.check(p, OVR)
        return got, st, new

    def one(got, rule, line):
        return len(got) == 1 and got[0].rule == rule and got[0].line == line

    bad = []
    for s in steps:
        got, _, _ = broken(lambda ls, s=s: ls.__setitem__(s.line - 1, ls[s.line - 1].replace(s.sentinel, "SIMUT override — nothing like this")))
        if not one(got, "not-added", s.line):
            bad.append((s.line, brief(got)))
    check(f"each of the {len(steps)} sentinels, changed in patch.sh, is ONE not-added finding, at its own guard", not bad, str(bad))

    bad = []
    for s in steps:
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(os.path.join(OVR, "patches"), os.path.join(d, "patches"))
            p = os.path.join(d, os.path.relpath(s.patch, OVR))
            with open(p, encoding="utf-8") as f:
                body = f.read().split("\n")
            hit = [i for i, line in enumerate(body) if line.startswith("+") and s.sentinel in line]
            for i in hit:
                body[i] = body[i].replace(s.sentinel, "SIMUT overrid3")
            with open(p, "w", encoding="utf-8") as f:
                f.write("\n".join(body))
            got, _ = cps.check(sh, d)
        if not hit or not one(got, "not-added", s.line):
            bad.append((s.line, len(hit), brief(got)))
    check(f"each of the {len(steps)} markers, changed in its patch, is ONE not-added finding, at that step's guard", not bad, str(bad))

    bad = []
    for s in steps:
        got, _, _ = broken(lambda ls, s=s: ls.__setitem__(s.line - 1, "if true; then"))
        if not one(got, "unguarded", s.patch_line):
            bad.append((s.line, brief(got)))
    check(f"each of the {len(steps)} steps, with its guard taken away, is ONE unguarded finding, at its patch command", not bad, str(bad))

    bad = []
    for s in steps:
        base = os.path.basename(s.patch)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "patch.sh")
            with open(p, "w", encoding="utf-8") as f:
                f.write(text.replace(base, "no_such_file.patch"))
            got, _ = cps.check(p, OVR)
        if not one(got, "no-patch", s.patch_line):
            bad.append((s.patch_line, brief(got)))
    check(f"each of the {len(steps)} patch files, gone, is ONE no-patch finding, at its patch command", not bad, str(bad))

    ter = [s for s in steps if s.patch.endswith("wifiserversecure_drop_plaintext.patch")]
    check("2b-ter is among the steps read", len(ter) == 1)
    if ter:
        s = ter[0]
        got, _, _ = broken(lambda ls: ls.__setitem__(s.line - 1, ls[s.line - 1].replace(s.sentinel, "SIMUT override — drop a non-TLS client")))
        exactly('2b-ter with the sentinel of 2026-10-15 ("drop a non-TLS client") is one finding, at its guard, naming its patch',
                got, "not-added", s.line, mention="wifiserversecure_drop_plaintext.patch")

    got, _, new = broken(lambda ls: ls.insert(len(ls) - 1, 'patch -p1 -d "$FW" < "$OVR/patches/new.patch"'))
    exactly("a step added to the real script with no guard is one unguarded finding, at its line",
            got, "unguarded", line_of(new, "new.patch"))


def read_only_cases():
    def digest():
        h = hashlib.sha256()
        for path in [os.path.join(OVR, "patch.sh")] + sorted(
                os.path.join(OVR, "patches", n) for n in os.listdir(os.path.join(OVR, "patches"))):
            h.update(path.encode())
            with open(path, "rb") as f:
                h.update(f.read())
        return h.hexdigest()
    before = digest()
    r = subprocess.run([sys.executable, GATE], capture_output=True, text=True)
    cps.main([])
    check("the real tree through the command: exit 0, and the sha256 of patch.sh and every patch is the same after",
          r.returncode == 0 and re.search(r"0 findings in \d+ guarded steps", r.stdout) and digest() == before,
          f"{r.returncode} {r.stdout!r} {r.stderr!r}")


# ── run ────────────────────────────────────────────────────────────────────────────────────────

diff_cases()
synthetic_cases()
cli_cases()
real_cases()
read_only_cases()

print(f"\n{len(RAN) - len(FAILS)} of {len(RAN)} checks passed")
if FAILS:
    print("FAILED:\n  " + "\n  ".join(FAILS))
    sys.exit(1)
