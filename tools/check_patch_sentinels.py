#!/usr/bin/env python3
"""check_patch_sentinels.py — every step of tools/arduino_pico_overrides/patch.sh can tell its patch is applied.

patch.sh is run again and again: after every override that merges, after a framework reinstall, and by CI
on a warm ~/.platformio cache (build.yml: "the script is idempotent, so a warm cache that already carries
them is fine"). Each step is therefore

    if grep -q "<sentinel>" "<file>"; then
        echo "... nada a fazer"
    else
        patch -p1 -d "$FW" < "<patch>"
    fi

and the sentinel must be text the PATCH ITSELF adds to <file>: in practice the `// SIMUT override — ...`
line at the top of its first hunk. Text that is not there costs nothing on the first run, when the file is
still the factory one and `patch` applies. On the second run grep finds nothing, `patch` meets a patch that
is already applied and asks "Reversed (or previously applied) patch detected! Assume -R? [n]" (a "y" there
reverses the override), then rejects the hunk to a .rej file and exits 1, and under `set -e` the script
dies on that line: the steps after it do not run, nor does the cache invalidation of step 3.

That happened to step 2b-ter (#274, merged 2026-10-15): it grepped "drop a non-TLS client" and its patch
adds "tell a non-TLS client from a TLS one by its FIRST BYTE". Nothing failed until the first re-run, on
2026-10-16. The gate reads patch.sh and the patches and holds four rules:

  unguarded   a `patch` command that is not the else-branch of an `if grep -q "<sentinel>" "<file>"; then`
              and written `patch -p1 -d "$FW" < "<patch>"`. Every `patch` command is read, so a step in
              another shape is a finding until this gate learns the shape; it is never skipped.
  unreadable  a sentinel that bash or grep would not read as the characters written ($ ` \\ inside double
              quotes; [ ] * ^ make `grep -q` a pattern, not a substring), or a path built from a variable
              this gate cannot resolve (it knows $FW, $OVR and the NAME="..." lines above the guard).
  no-patch    the patch file the step applies does not exist.
  not-added   the sentinel is not in a line the patch ADDS (a `+` line: not context, not a removed line)
              to the file the guard greps (the `+++` path of its hunk, minus the -p1 prefix). The file
              matters: 2f greps HTTPServer.h, 2m and 2n grep HTTPServer.cpp, in patches that touch both.

What is not `patch` is not read: the backup (guarded by `[ ! -f ... ]`) and the copy of lwipopts.h (a
plain `cp`, which does the same thing every time) are idempotent by construction.

    python3 tools/check_patch_sentinels.py                    # the gate: `path:LINE: rule: text`, exit 1 on any finding
    python3 tools/check_patch_sentinels.py --patch-sh FILE    # another script (the test; the old 2b-ter, as a mutation)
    python3 tools/check_patch_sentinels.py --ovr DIR          # another overrides directory: where $OVR points

Exit 0 clean, 1 findings, 2 a file it could not read or a script in which it found neither a guarded step nor
any `patch` command (a gate that reads nothing must not pass).
"""
import argparse
import os
import re
import sys
from typing import NamedTuple, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_OVR = os.path.join(HERE, "arduino_pico_overrides")
FW = "<FW>"      # what $FW stands for here: the root of the framework package, on whatever machine this runs

ASSIGN = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)="([^"]*)"\s*(?:#.*)?$')
GUARD = re.compile(r'^\s*if grep -q "([^"]*)" "([^"]*)"; then\s*$')
PATCH_CMD = re.compile(r"\bpatch\s+-")
PATCH_OK = re.compile(r'^\s*patch -p1 -d "\$FW" < "([^"]+)"\s*$')
VAR = re.compile(r"\$(?:([A-Za-z_][A-Za-z0-9_]*)|\{([A-Za-z_][A-Za-z0-9_]*)\})")
HUNK = re.compile(r"^@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@")
SYNTAX = re.compile(r"[$`\\\[\]*^]")

RULES = ("unguarded", "unreadable", "no-patch", "not-added")
HINTS = {
    "unguarded": 'write the step as `if grep -q "<sentinel>" "<file>"; then ... else ... patch -p1 -d "$FW" '
                 '< "<patch>"; fi`: a patch with no guard is applied again on the second run',
    "unreadable": "the sentinel is a plain substring (none of $ ` \\ [ ] * ^) and the paths use only $FW, $OVR and "
                  "NAME=\"...\" lines above the guard",
    "no-patch": "the step applies a file that is not in patches/",
    "not-added": "grep the `// SIMUT override — ...` line the patch ADDS to that file: a sentinel the patch does "
                 "not add finds nothing on the second run, and `patch` stops the script at the applied hunk",
}


class Finding(NamedTuple):
    line: int
    rule: str
    text: str


class Step(NamedTuple):
    line: int                 # the `if grep` line: where a wrong sentinel is fixed
    sentinel: str
    file: Optional[str]       # the file the guard greps, $FW/... resolved; None if it could not be
    patch: Optional[str]      # the patch file, resolved; None if it could not be
    patch_line: int


def read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def expand(s: str, env: dict) -> Optional[str]:
    """s with $VAR and ${VAR} from env; None when a variable is unknown or a `$` is left that it cannot read."""
    try:
        out = VAR.sub(lambda m: env[m.group(1) or m.group(2)], s)
    except KeyError:
        return None
    return None if "$" in out else out


def read_steps(text: str, ovr: str):
    """The guarded `patch` steps of a patch.sh, and a finding for every `patch` command outside one."""
    env = {"FW": FW, "OVR": ovr}
    steps, findings = [], []
    guard, in_else = None, False        # (line, sentinel, file) from `if grep -q ...; then` to `fi`
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = ASSIGN.match(raw)
        if m:                           # NAME="..." at column 0; the value at the point of use is the one read
            if m.group(1) not in ("FW", "OVR"):         # those two are what the gate says they are, whatever the script does
                value = expand(m.group(2), env)
                if value is not None:
                    env[m.group(1)] = value
            continue
        m = GUARD.match(raw)
        if m:
            guard, in_else = (n, m.group(1), expand(m.group(2), env)), False
            continue
        if guard and line == "else":
            in_else = True
            continue
        if guard and line == "fi":
            guard, in_else = None, False
            continue
        if PATCH_CMD.search(line):
            ok = PATCH_OK.match(raw)
            if guard and in_else and ok:
                steps.append(Step(guard[0], guard[1], guard[2], expand(ok.group(1), env), n))
            else:
                findings.append(Finding(n, "unguarded", line))
    return steps, findings


def added_lines(text: str) -> dict:
    """{path: [the lines the diff adds]} for a unified diff; path as `patch -p1` reads it (first component dropped).

    Hunks are read by their line counts, not by prefix alone, so an added line that looks like a file header
    (`++ x`) is still an added line, and a removed one (`-- x`) is not a header."""
    out, cur = {}, None
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
            cur = lines[i + 1][4:].split("\t")[0].split("/", 1)[-1]
            out.setdefault(cur, [])
            i += 2
            continue
        m = HUNK.match(lines[i])
        if m and cur is not None:
            old = int(m.group(1)) if m.group(1) is not None else 1
            new = int(m.group(2)) if m.group(2) is not None else 1
            i += 1
            while (old > 0 or new > 0) and i < len(lines):
                h = lines[i]
                if h.startswith("+"):
                    out[cur].append(h[1:])
                    new -= 1
                elif h.startswith("-"):
                    old -= 1
                elif not h.startswith("\\"):        # context (a blank one may have lost its space); "\ No newline" is neither
                    old -= 1
                    new -= 1
                i += 1
            continue
        i += 1
    return out


def check(sh_path: str, ovr: str):
    """(findings, steps) for one patch.sh whose $OVR is the directory ovr."""
    steps, findings = read_steps(read(sh_path), ovr)
    adds = {}
    for st in steps:
        s = st.sentinel
        if SYNTAX.search(s):
            findings.append(Finding(st.line, "unreadable", f"the sentinel {s!r} has a character that bash or grep "
                                    f"reads as syntax, not as itself"))
            continue
        if st.file is None or not st.file.startswith(FW + "/"):
            findings.append(Finding(st.line, "unreadable", f"cannot tell which file of the framework the guard greps"))
            continue
        if st.patch is None:
            findings.append(Finding(st.patch_line, "unreadable", "cannot tell which patch file the step applies"))
            continue
        name = os.path.relpath(st.patch, ovr)
        if not os.path.isfile(st.patch):
            findings.append(Finding(st.patch_line, "no-patch", f"{name} does not exist"))
            continue
        if st.patch not in adds:
            adds[st.patch] = added_lines(read(st.patch))
        rel = st.file[len(FW) + 1:]
        if rel not in adds[st.patch]:
            touched = ", ".join(sorted(adds[st.patch])) or "no file"
            findings.append(Finding(st.line, "not-added", f"{name} does not touch {rel} (it touches {touched})"))
        elif not any(s in line for line in adds[st.patch][rel]):
            marks = [line.strip()[:90] for line in adds[st.patch][rel] if "SIMUT override" in line][:2]
            findings.append(Finding(st.line, "not-added", f"{s!r} is in no line {name} adds to {rel}; "
                                    + (f"it adds {marks}" if marks else "it adds no `SIMUT override` line there")))
    return sorted(findings, key=lambda f: (f.line, RULES.index(f.rule))), steps


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Every guard of patch.sh against its patch: see the module docstring.")
    ap.add_argument("--ovr", default=DEFAULT_OVR, help="the overrides directory, where $OVR points (default: this repository's)")
    ap.add_argument("--patch-sh", default=None, help="the script to read (default: <ovr>/patch.sh)")
    args = ap.parse_args(argv)
    ovr = os.path.abspath(args.ovr)
    sh = os.path.abspath(args.patch_sh or os.path.join(ovr, "patch.sh"))
    shown = os.path.relpath(sh, ROOT) if sh.startswith(ROOT + os.sep) else sh
    try:
        findings, steps = check(sh, ovr)
    except (OSError, UnicodeDecodeError) as e:
        print(f"check_patch_sentinels: {e}", file=sys.stderr)
        return 2
    if not steps and not findings:
        print(f"check_patch_sentinels: no `if grep -q ...; then ... patch` step found in {shown}: "
              f"the gate does not understand the file", file=sys.stderr)
        return 2
    for f in findings:
        print(f"{shown}:{f.line}: {f.rule}: {f.text}")
    for rule in RULES:
        if any(f.rule == rule for f in findings):
            print(f"check_patch_sentinels: {rule}: {HINTS[rule]}", file=sys.stderr)
    print(f"check_patch_sentinels: {len(findings)} findings in {len(steps)} guarded steps")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
