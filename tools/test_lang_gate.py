#!/usr/bin/env python3
"""Positive controls for check_lang_packs.py — the split-ceiling gates.

A gate that never fired is indistinguishable from a gate that cannot fire,
so each new failure mode is provoked here on a synthetic pack mutated from
the real es-ES one: a section AFTER @WEBDICT (suffix contract), a resident
prefix over LANG_RESIDENT_MAX, and a file over LANG_FILE_MAX. The A-vs-A
control runs the real packs through the same entry point first.

Run: python3 tools/test_lang_gate.py
"""
import contextlib
import io
import os
import shutil
import sys
import tempfile
from pathlib import Path

os.environ["LANG_GATE_NO_RUN"] = "1"
sys.path.insert(0, str(Path(__file__).parent))
import check_lang_packs as gate  # noqa: E402

ROOT = Path(__file__).parent.parent
ES = ROOT / "data" / "lang" / "language_es-ES.lng"

passed = failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print(f"PASS {name}")
    else:
        failed += 1
        print(f"FAIL {name}" + (f" — {detail}" if detail else ""))


def run_gate(packs):
    """(exited_nonzero, stderr_text) for main() over the given pack list."""
    gate.PACKS = [Path(p) for p in packs]
    err = io.StringIO()
    code = 0
    with contextlib.redirect_stderr(err):
        try:
            gate.main()
        except SystemExit as e:
            code = e.code or 0
    return code != 0, err.getvalue()


def main():
    file_max, res_max = gate.lang_limits()
    check("limits read from parser source", file_max > res_max > 0,
          f"file={file_max} res={res_max}")

    # resident_split arithmetic on a hand-built file with known offsets
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "t.lng"
        p.write_bytes(b"# c\n@NAME X\n@DICT\na\n@WEBDICT\n{}\n")
        res, has, tail = gate.resident_split(p)
        check("resident_split offset", res == len(b"# c\n@NAME X\n@DICT\na\n"),
              f"got {res}")
        check("resident_split flags", has and not tail)
        p.write_bytes(b"@DICT\na\n")
        res, has, tail = gate.resident_split(p)
        check("no @WEBDICT -> resident is whole file",
              res == p.stat().st_size and not has and not tail)

    # A-vs-A: the real packs pass through the same entry point
    bad, err = run_gate(sorted((ROOT / "data" / "lang").glob("*.lng")))
    check("A-vs-A: real packs pass", not bad, err[-200:])

    real = ES.read_bytes()
    with tempfile.TemporaryDirectory() as td:
        # a section after @WEBDICT breaks the suffix contract
        p = Path(td) / "language_es-ES.lng"
        p.write_bytes(real + b"\n@HELP\nstray\n")
        bad, err = run_gate([p])
        check("gate fires: section after @WEBDICT",
              bad and "AFTER" in err and "@WEBDICT" in err, err[-200:])

        # resident prefix over LANG_RESIDENT_MAX (pad @HELP, before @WEBDICT)
        pad = b"# pad\n" * ((res_max // 6) + 2)
        p.write_bytes(real.replace(b"@HELP\n", b"@HELP\n" + pad, 1))
        bad, err = run_gate([p])
        check("gate fires: resident over LANG_RESIDENT_MAX",
              bad and "LANG_RESIDENT_MAX" in err, err[-200:])

        # whole file over LANG_FILE_MAX (pad inside the JSON blob: whitespace
        # is legal there, adds no key, and leaves the resident prefix alone)
        need = file_max - len(real) + 16
        p.write_bytes(real.replace(b"@WEBDICT\n{", b"@WEBDICT\n{" + b" " * need, 1))
        bad, err = run_gate([p])
        check("gate fires: file over LANG_FILE_MAX",
              bad and "LANG_FILE_MAX" in err, err[-200:])

    print(f"\n{passed} passaram, {failed} falharam")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
