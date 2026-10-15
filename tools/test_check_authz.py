#!/usr/bin/env python3
"""test_check_authz.py — the proof that tools/check_authz.py reads every file a UI driver can register a route
from, and that the pre-commit hook which runs it fires on the same files.

Run: python3 tools/test_check_authz.py   (exit 0 all checks passed, 1 not)

Why it exists. The gate audited src/ui/ for ".cpp" and ".h" and for nothing else, and the hook `simut-authz` of
.pre-commit-config.yaml had the same two extensions in its `files:` pattern. A route registered from a .hpp,
.inl, .ipp, .cc, .cxx ... under src/ui/ was never opened: no finding, exit 0, and the hook's comment said it
covered "what check_authz.py scans" (found by a review on 2026-10-15). A gate that reports nothing about a file
it never read looks exactly like one that read it and found it clean, so every case below puts the real route
(the Angulo driver's GET /api/ui, from src/ui/angulo/WebManager_UiA.cpp) in a new place and asks the gate what
it saw. Each case is built in a temporary copy of what the gate reads (tools/check_authz.py, src/*.cpp and
src/ui/) and runs the gate there as a subprocess, so what is proved is the file CI runs.

  a  the tree as it is: exit 0, and the driver's one route counted, as gated.
  b  the route moved, its gate line taken out, to src/ui/angulo/Rota.<ext> for each extension of EXTS: exit 1
     and ONE finding, naming GET /api/ui and its handler. .cpp and .h are the controls: the gate caught them
     before the fix too, so a failure on another extension is the gate's and not the moving's. Two more places
     for the same file, because the gate reads src/ui/ at any depth: src/ui/angulo/telas/ and src/ui/ itself.
  c  the route moved, its gate line kept, to Rota.hpp: exit 0 and counted as gated. Exit 0 alone proves nothing
     here (a gate that never opens the file exits 0 as well); the driver line "1 route, 1 gated" is what tells
     "read and gated" from "not read".
  d  the route moved, ungated, to what is not source (notes.md, Rota.txt, Rota.cpp.orig): exit 0 and not
     counted. The cure for a blind spot is a list of extensions, not "read everything": a document that quotes
     a route would be a false alarm.
  e  the hook: the `files:` pattern of `simut-authz`, read out of .pre-commit-config.yaml, fires on
     src/ui/angulo/x.<ext> (and one level deeper) for each extension of EXTS, on src/WebManager_Core.cpp, and on
     exactly the extensions the gate names in DRIVER_EXTS, taken from the gate itself so that the two cannot
     drift: one the gate reads and the hook does not is the blind spot, one the hook fires on and the gate does
     not read is a coverage the comment claims and the gate lacks. md and the near misses (cp, hp, cppx) must
     not fire it.

EXTS is this test's own list, not the gate's: a test that took it from the gate would stop checking an
extension the day someone dropped it there.

Then the instrument is bitten (MUTANTS): each mutant is the gate, or the hook's pattern, with one change, and the
checks that fail must be exactly the ones listed for it: an extension taken out of DRIVER_EXTS (its b and its e,
and c for .hpp, which moves the route there), a document put into it, DRIVER_FILES that ignores the tuple, a
gate that reads one level only, a finding under src/ui/ that no longer fails the check, a hook that loses an
extension, the core, or the depth, or fires on a document. A mutant whose text is no longer in the file is an
error, and the mutants are not run while a real check fails (a mutant of a red gate proves nothing).

Written on 2026-10-15. Against the gate and the hook of 319714d, 19 of the 39 checks pass: the b and e checks of
the extensions c, cc, cxx, hh, hpp, hxx, inl, ipp and tpp fail, and so do case c and e.names. With DRIVER_EXTS all
39 pass, and all 11 mutants are caught, each by exactly the checks listed for it (about 15 s in all).
"""
import concurrent.futures
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True      # importing a copy of the gate must not leave a __pycache__ behind

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
GATE = HERE / "check_authz.py"
HOOKS = ROOT / ".pre-commit-config.yaml"

# The route under test: the Angulo driver's, the only one under src/ui/ today. It is moved whole (the
# registration and the handler) to another file, with the line that gates it kept or taken out.
ROUTE_FILE = "src/ui/angulo/WebManager_UiA.cpp"
GATE_LINE = "  if ( !requirePerm( PERM_SYS_CONFIG ) ) return;\n"

# What a driver's route can be registered from and the gate must read. The first two are what it read before.
EXTS = ("c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "inl", "ipp", "tpp")
# Other places for the same file (relative to src/ui/angulo/): the gate reads src/ui/ at any depth. They keep
# the extension the gate always read, so that depth and extension are not one variable.
DEPTHS = (("depth 2", "telas/Rota.cpp"), ("depth 0", "../Rota.cpp"))
# Not source: the gate must not read them, whatever they quote.
NOT_SOURCE = ("notes.md", "Rota.txt", "Rota.cpp.orig")
# Extensions the hook must stay silent on: a document, a leftover, and names that merely look like source.
DECOYS = ("md", "txt", "json", "py", "orig", "cp", "hp", "cppx")


class Refusal(Exception):
    """The instrument cannot do what a case needs; the case fails with this said, not with a traceback."""


# ── the instrument ───────────────────────────────────────────────────────────────────────────────

def make_base(tmp):
    """What the gate reads, copied once: src/*.cpp and src/ui/. Each case starts from a copy of this."""
    base = tmp / "base"
    (base / "src").mkdir(parents=True)
    for p in sorted((ROOT / "src").glob("*.cpp")):
        shutil.copyfile(p, base / "src" / p.name)
    shutil.copytree(ROOT / "src" / "ui", base / "src" / "ui")
    return base


def make_tree(base, tmp, gate_text):
    tree = Path(tempfile.mkdtemp(dir=tmp))
    shutil.copytree(base / "src", tree / "src")
    (tree / "tools").mkdir()
    (tree / "tools" / "check_authz.py").write_text(gate_text, encoding="utf-8")
    return tree


def move_route(tree, rel, keep_gate):
    """Move the Angulo's route file to src/ui/angulo/<rel>, its gate line kept or taken out."""
    old = tree / ROUTE_FILE
    text = old.read_text(encoding="utf-8")
    if text.count(GATE_LINE) != 1 or text.count("_server->on(") != 1:
        raise Refusal(f"{ROUTE_FILE} no longer has the shape this test moves (one route, its gate line once)")
    if not keep_gate:
        text = text.replace(GATE_LINE, "")
    new = tree / "src" / "ui" / "angulo" / rel
    new.parent.mkdir(parents=True, exist_ok=True)
    new.write_text(text, encoding="utf-8")
    old.unlink()


def run_gate(tree):
    p = subprocess.run([sys.executable, "-B", str(tree / "tools" / "check_authz.py")],
                       cwd=tree, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def gate_after_move(base, tmp, gate_text, rel, keep_gate):
    """(exit status, output) of the gate on a copy of the tree whose route was moved to src/ui/angulo/<rel>
    (rel None: the tree as it is)."""
    tree = make_tree(base, tmp, gate_text)
    try:
        if rel is not None:
            move_route(tree, rel, keep_gate)
        return run_gate(tree)
    finally:
        shutil.rmtree(tree, ignore_errors=True)


def findings(out):
    return [line[4:] for line in out.splitlines() if line.startswith("  - ")]


def driver_counts(out):
    """(routes, gated, public) of the line the gate prints about src/ui/**, or None when it prints none."""
    m = re.search(r"UI drivers, src/ui/\*\*: (\d+) routes?, (\d+) gated, (\d+) public-by-design", out)
    return tuple(int(g) for g in m.groups()) if m else None


def hook_pattern(config_text):
    """The `files:` pattern of the simut-authz hook, as pre-commit applies it (re.search on the path)."""
    block = re.search(r"^[ \t]*- id:[ \t]*simut-authz[ \t]*$(.*?)(?=^[ \t]*- id:|\Z)", config_text, re.S | re.M)
    if not block:
        raise Refusal("no hook with the id simut-authz in .pre-commit-config.yaml")
    m = re.search(r"^[ \t]*files:[ \t]*(\S.*?)[ \t]*$", block.group(1), re.M)
    if not m:
        raise Refusal("the simut-authz hook has no `files:` line")
    value = m.group(1)
    if value[0] == "'" and value[-1] == "'" and len(value) > 1:
        return value[1:-1].replace("''", "'")
    if value[0] in "'\"":
        raise Refusal(f"the `files:` of simut-authz is quoted in a way this reader does not take: {value}")
    return re.sub(r"[ \t]+#.*$", "", value)      # a plain YAML scalar ends where ` #` begins


def gate_exts(gate_text, tmp):
    """DRIVER_EXTS of the gate under test, or None when it has none. Imported from a copy of the source, which is
    how a mutant of the gate is read too."""
    d = Path(tempfile.mkdtemp(dir=tmp))
    path = d / "check_authz.py"
    path.write_text(gate_text, encoding="utf-8")
    spec = importlib.util.spec_from_file_location("check_authz_under_test_" + d.name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return getattr(module, "DRIVER_EXTS", None)


# Each mutant is the gate (target "gate") or the hook's configuration ("hook") with one change:
# (id, what it does, target, text, replacement, the checks that must fail).
ALL_B = {f"b[{e}]" for e in EXTS} | {f"b[{label}]" for label, _ in DEPTHS}
MUTANTS = [
    ("m1", "the gate stops reading .hpp (c moves the route there too)", "gate", '"hpp", ', "", {"b[hpp]", "c", "e[hpp]"}),
    ("m2", "the gate stops reading .inl", "gate", '"inl", ', "", {"b[inl]", "e[inl]"}),
    ("m3", "the gate stops reading .c", "gate", '"c", ', "", {"b[c]", "e[c]"}),
    ("m4", "the gate reads documents (.md) too", "gate", '"tpp")', '"tpp", "md")', {"d[notes.md]", "e[md]"}),
    ("m5", "DRIVER_EXTS is named but DRIVER_FILES keeps the two it always had", "gate", "for ext in DRIVER_EXTS",
     'for ext in ("cpp", "h")', {f"b[{e}]" for e in EXTS if e not in ("cpp", "h")} | {"c"}),
    ("m6", "the gate reads src/ui/ one level down only", "gate", "recursive=True", "recursive=False",
     {"b[depth 2]", "b[depth 0]"}),
    ("m7", "a finding under src/ui/ no longer fails the check", "gate",
     'failures += [f"UI driver (src/ui/**): {f}" for f in driver_failures]', "pass", ALL_B),
    ("m8", "the hook stops firing on .inl", "hook", "inl|", "", {"e[inl]"}),
    ("m9", "the hook fires on .md too", "hook", "|tpp)", "|tpp|md)", {"e[md]"}),
    ("m10", "the hook stops firing on the core's WebManager*.cpp", "hook", "WebManager.*\\.cpp|", "", {"e.core"}),
    ("m11", "the hook fires one level down only (src/ui/*/x, not src/ui/*/*/x)", "hook", "ui/.*\\.(", "ui/[^/]+/[^/]+\\.(",
     {f"e[{e}]" for e in EXTS}),
]


# ── the cases ────────────────────────────────────────────────────────────────────────────────────

def gate_cases(gate_text, base, tmp):
    """The cases that run the gate (a to d) and the one that reads its list of extensions (e.names).
    Returns ([(id, title, fn)], listed): e.names puts the gate's DRIVER_EXTS (None if it has none) in listed[0]."""
    cases = []
    listed = [None]

    def gate_on(rel, keep_gate):
        return gate_after_move(base, tmp, gate_text, rel, keep_gate)

    def case_a():
        rc, out = gate_on(None, True)
        return rc == 0 and driver_counts(out) == (1, 1, 0), f"exit {rc}, driver line {driver_counts(out)}"
    cases.append(("a", "the tree as it is: exit 0, the driver's one route counted as gated", case_a))

    def case_b(rel):
        def run():
            rc, out = gate_on(rel, False)
            f = findings(out)
            ok = (rc == 1 and len(f) == 1 and "GET /api/ui" in f[0] and "handleApiUiA" in f[0]
                  and "has no authorization gate" in f[0])
            return ok, f"exit {rc}, {len(f)} finding(s) {f[:2]}"
        return run
    for ext in EXTS:
        cases.append((f"b[{ext}]", f"ungated route in src/ui/angulo/Rota.{ext}: exit 1, one finding that names it",
                      case_b(f"Rota.{ext}")))
    for label, rel in DEPTHS:
        where = os.path.normpath("src/ui/angulo/" + rel)
        cases.append((f"b[{label}]", f"ungated route in {where}: exit 1, one finding that names it", case_b(rel)))

    def case_c():
        rc, out = gate_on("Rota.hpp", True)
        return rc == 0 and driver_counts(out) == (1, 1, 0), f"exit {rc}, driver line {driver_counts(out)}"
    cases.append(("c", "gated route in src/ui/angulo/Rota.hpp: exit 0, and counted as gated (read, not skipped)", case_c))

    def case_d(name):
        def run():
            rc, out = gate_on(name, False)
            return rc == 0 and driver_counts(out) is None, \
                f"exit {rc}, driver line {driver_counts(out)}, {len(findings(out))} finding(s)"
        return run
    for name in NOT_SOURCE:
        cases.append((f"d[{name}]", f"ungated route in src/ui/angulo/{name}: exit 0, not read", case_d(name)))

    def case_names():
        listed[0] = gate_exts(gate_text, tmp)
        exts = listed[0]
        ok = (isinstance(exts, (tuple, list)) and len(exts) > 0 and len(set(exts)) == len(exts)
              and all(isinstance(e, str) and re.fullmatch(r"[A-Za-z0-9]+", e) for e in exts))
        return ok, ("check_authz.py has no DRIVER_EXTS, so the hook has no list to be held to" if exts is None
                    else f"DRIVER_EXTS is not a tuple of plain extensions: {exts!r}")
    cases.append(("e.names", "the gate names the extensions it reads (DRIVER_EXTS), plain and without repeats", case_names))
    return cases, listed


def hook_cases(pattern, listed):
    """The e cases about the hook, once the gate's list is known (listed: a sequence, or None when the gate has
    none, which e.names has already failed: then the hook is held to EXTS alone)."""
    def fires(path):                                   # how pre-commit applies `files:`: re.search on the path
        return re.search(pattern, path) is not None

    def should_fire(ext):
        return (ext in listed) if listed is not None else (ext in EXTS)

    def per_ext(ext):
        def run():
            here, deeper = fires(f"src/ui/angulo/x.{ext}"), fires(f"src/ui/angulo/telas/x.{ext}")
            if should_fire(ext):
                return here and deeper, f"the hook does not fire on src/ui/angulo/x.{ext}" \
                    + (", which the gate reads" if listed is not None else "")
            return not (here or deeper), f"the hook fires on src/ui/angulo/x.{ext}, which the gate does not read"
        return run

    cases = [("e.core", "the hook fires on src/WebManager_Core.cpp",
              lambda: (fires("src/WebManager_Core.cpp"), "the pattern does not match src/WebManager_Core.cpp"))]
    for ext in sorted(set(EXTS) | set(listed or ()) | set(DECOYS)):
        cases.append((f"e[{ext}]", f"the hook {'fires' if should_fire(ext) else 'is silent'} on src/ui/angulo/x.{ext}",
                      per_ext(ext)))
    return cases


def run_cases(cases, pool):
    def one(case):
        cid, title, fn = case
        try:
            ok, detail = fn()
        except Exception as e:                         # the instrument failing is a failed case, said, not a traceback
            ok, detail = False, f"{type(e).__name__}: {e}"
        return cid, title, bool(ok), "" if ok else detail
    return list(pool.map(one, cases))


def all_checks(gate_text, config_text, base, tmp, pool):
    """Every check, against one gate (its source text) and one .pre-commit-config.yaml (its text):
    [(id, title, ok, detail)] in the order they are printed."""
    cases, listed = gate_cases(gate_text, base, tmp)
    results = run_cases(cases, pool)                   # e.names has filled listed[0] by now
    try:
        pattern = hook_pattern(config_text)
        re.compile(pattern)
    except (Refusal, re.error) as e:
        return results + [("e.hook", "the simut-authz hook's `files:` pattern is read", False, str(e))]
    return results + run_cases(hook_cases(pattern, listed[0]), pool)


def main():
    gate_text = GATE.read_text(encoding="utf-8")
    config_text = HOOKS.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="test_check_authz_") as t:
        tmp = Path(t)
        base = make_base(tmp)
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
            results = all_checks(gate_text, config_text, base, tmp, pool)
            failed = [r for r in results if not r[2]]
            for cid, title, ok, detail in results:
                print(f"[{'ok  ' if ok else 'FAIL'}] {cid}  {title}" + (f" - {detail}" if not ok and detail else ""))
            print(f"\n{len(results) - len(failed)} of {len(results)} checks passed")
            if failed:
                print("FAILED:\n  " + "\n  ".join(r[0] for r in failed))
                print(f"\n{len(MUTANTS)} mutants not run: a mutant of a gate that already fails proves nothing")
                return 1
            print(f"\n{len(MUTANTS)} mutants (the checks that fail must be exactly the listed ones):")
            wrong = []
            for mid, title, target, old, new, expected in MUTANTS:
                source = gate_text if target == "gate" else config_text
                if source.count(old) != 1:
                    print(f"[FAIL] {mid}  {title} - its text is {source.count(old)} times in the {target}, not once: {old!r}")
                    wrong.append(mid)
                    continue
                mutated = source.replace(old, new)
                res = all_checks(mutated if target == "gate" else gate_text, mutated if target == "hook" else config_text,
                                 base, tmp, pool)
                fell = {r[0] for r in res if not r[2]}
                if fell == expected:
                    print(f"[ok  ] {mid}  {title}: fails {sorted(fell)}")
                else:
                    print(f"[FAIL] {mid}  {title}: fails {sorted(fell)}, expected {sorted(expected)}")
                    wrong.append(mid)
    if wrong:
        print(f"\nFAILED: {len(wrong)} of {len(MUTANTS)} mutants: {', '.join(wrong)}")
        return 1
    print(f"\nOK: {len(results)} checks and {len(MUTANTS)} mutants")
    return 0


if __name__ == "__main__":
    sys.exit(main())
