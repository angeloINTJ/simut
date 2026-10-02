#!/usr/bin/env python3
"""Positive controls for check_lang_packs.py — the two ceilings and the order.

A gate that never fired is indistinguishable from a gate that cannot fire,
so each failure mode is provoked here on a synthetic pack mutated from the
real es-ES one: a section AFTER @WEBDICT (suffix contract), a @DICT over
LANG_DICT_MAX, and a file over LANG_FILE_MAX. The A-vs-A control runs the
real packs through the same entry point first, and one control checks the
opposite: since the loader reads only @DICT, everything before @WEBDICT may
outgrow the old 16 KB prefix ceiling without tripping anything.

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
    file_max, dict_max = gate.lang_limits()
    check("limits read from parser source", file_max > dict_max > 0,
          f"file={file_max} dict={dict_max}")

    # pack_index arithmetic on hand-built files with known offsets
    head = b"# c\n@NAME X\n@DICT\n"
    idx, tail = gate.pack_index(head + b"a\n@WEBDICT\n{}\n")
    check("pack_index: @DICT range", idx.get("DICT") == (len(head), len(head) + 2),
          f"got {idx.get('DICT')}")
    check("pack_index: @WEBDICT runs to EOF, nothing after it",
          idx.get("WEBDICT") == (len(head) + 11, len(head) + 14) and not tail,
          f"got {idx.get('WEBDICT')} tail={tail}")
    idx, tail = gate.pack_index(b"@DICT\na\n")
    check("no @WEBDICT -> @DICT runs to EOF", idx == {"DICT": (6, 8)} and not tail,
          f"got {idx}")
    idx, tail = gate.pack_index(b"@DICT\na\n@DICTX\nb\n")
    check("pack_index: names match exactly", idx == {"DICT": (6, 8)}, f"got {idx}")

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

        # @DICT over LANG_DICT_MAX: one line made longer, so the line count
        # stays right and only the ceiling can fire
        p.write_bytes(real.replace(b"@DICT\n", b"@DICT\n" + b"x" * dict_max, 1))
        bad, err = run_gate([p])
        check("gate fires: @DICT over LANG_DICT_MAX",
              bad and "LANG_DICT_MAX" in err, err[-200:])

        # the bytes before @WEBDICT past the old 16,384 B prefix ceiling: not a
        # failure any more, nothing outside @DICT reaches the heap
        before = len(real.split(b"\n@WEBDICT")[0])
        pad = b"# pad\n" * ((16384 - before) // 6 + 20)
        p.write_bytes(real.replace(b"@HELP\n", b"@HELP\n" + pad, 1))
        bad, err = run_gate([p])
        check("prefix over the old 16 KB ceiling passes",
              not bad and before + len(pad) > 16384, err[-200:])

        # whole file over LANG_FILE_MAX (pad inside the JSON blob: whitespace
        # is legal there, adds no key, and leaves @DICT alone)
        need = file_max - len(real) + 16
        p.write_bytes(real.replace(b"@WEBDICT\n{", b"@WEBDICT\n{" + b" " * need, 1))
        bad, err = run_gate([p])
        check("gate fires: file over LANG_FILE_MAX",
              bad and "LANG_FILE_MAX" in err, err[-200:])

    print(f"\n{passed} passaram, {failed} falharam")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
