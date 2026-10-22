#!/usr/bin/env python3
"""Positive controls for check_lang_packs.py — the two ceilings and the order, and the Angulo's @ANGULO section.

A gate that never fired is indistinguishable from a gate that cannot fire,
so each failure mode is provoked here on a synthetic pack mutated from the
real es-ES one: a section AFTER @WEBDICT (suffix contract), a @DICT over
LANG_DICT_MAX, and a file over LANG_FILE_MAX. The A-vs-A control runs the
real packs through the same entry point first, and one control checks the
opposite: since the loader reads only @DICT, everything before @WEBDICT may
outgrow the old 16 KB prefix ceiling without tripping anything.

@ANGULO (S10c, B2a): the real tree has no UA_TXT( ) literal and the real packs no such section, so the gate passes with nothing to compare. Here
a COPY of src/ui/angulo gets a source file with literals planted in it, and packs made from the real ones get the section, and each refusal is
provoked with the case that passes beside it: a literal without its entry in one of two packs (the message names the pack, the hash, the English
and the file), an orphan, two literals with one hash (a real FNV-1a collision of two short strings, found by search), the section after @WEBDICT,
a section over UA_TEXTOS_MAX (read from a header the test writes, so that the gate is shown to read it) and at the bound, every shape of malformed
line the device refuses (src/ui/angulo/Textos.cpp), a repeated hash, a repeated section, and the reader of literals on comments, strings,
adjacent pieces, a raw string and escapes.

Run: python3 tools/test_lang_gate.py
"""
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

os.environ["LANG_GATE_NO_RUN"] = "1"
sys.path.insert(0, str(Path(__file__).parent))
import check_lang_packs as gate  # noqa: E402

ROOT = Path(__file__).parent.parent
ES = ROOT / "data" / "lang" / "language_es-ES.lng"
PT = ROOT / "data" / "lang" / "language_pt-BR.lng"
REAL_DIR, REAL_H = gate.ANGULO_DIR, gate.TEXTOS_H

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
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):   # the gate's OK lines go to stdout
        try:
            gate.main()
        except SystemExit as e:
            code = e.code or 0
    return code != 0, err.getvalue()


def with_angulo(raw, body):
    """The pack `raw` with an @ANGULO section just before @WEBDICT, where it belongs."""
    at = raw.index(b"\n@WEBDICT\n") + 1
    return raw[:at] + b"@ANGULO\n" + body + raw[at:]


def entry(en, text="traduzido", eol=b"\n"):
    return b"%08x " % gate.fnv1a32(en) + text.encode("utf-8") + eol


def angulo_tree(td, extra=None, bound=None):
    """Points the gate at a COPY of src/ui/angulo in `td` with the files of `extra` ({name: text}) added, and UA_TEXTOS_MAX set to `bound` in the
    copy's Textos.h when given. The caller restores the real paths (restore_tree)."""
    dst = Path(td) / "angulo"
    shutil.copytree(REAL_DIR, dst)
    for name, text in (extra or {}).items():
        (dst / name).write_text(text, encoding="utf-8")
    if bound is not None:
        h = (dst / "Textos.h").read_text(encoding="utf-8")
        h, n = re.subn(r"(?m)^(constexpr\s+size_t\s+UA_TEXTOS_MAX\s*=\s*)\d+", rf"\g<1>{bound}", h)
        assert n == 1
        (dst / "Textos.h").write_text(h, encoding="utf-8")
    gate.ANGULO_DIR, gate.TEXTOS_H = dst, dst / "Textos.h"
    return dst


def restore_tree():
    gate.ANGULO_DIR, gate.TEXTOS_H = REAL_DIR, REAL_H


def fnv_collision():
    """Two different short strings with one FNV-1a 32 hash, found by search (about 80,000 tries on average: the birthday bound of 32 bits)."""
    seen = {}
    i = 0
    while True:
        t = f"c{i}"
        h = gate.fnv1a32(t)
        if h in seen:
            return seen[h], t
        seen[h] = t
        i += 1


def angulo_cases():
    es, pt = ES.read_bytes(), PT.read_bytes()
    plant = {"plant.cpp": 'const char *a = UA_TXT( "Planted text" );\n'}
    line = entry("Planted text", "Texto plantado")
    h = f"{gate.fnv1a32('Planted text'):08x}"

    def packs(td, es_body, pt_body):
        e, p = Path(td) / "language_es-ES.lng", Path(td) / "language_pt-BR.lng"
        e.write_bytes(es if es_body is None else with_angulo(es, es_body))
        p.write_bytes(pt if pt_body is None else with_angulo(pt, pt_body))
        return [p, e]

    try:
        # the real tree: the section is absent from both packs and no literal exists, so the check has nothing to compare and passes
        bad, err = run_gate(sorted((ROOT / "data" / "lang").glob("*.lng")))
        check("@ANGULO: the shipped packs pass with no section and no literal", not bad, err[-200:])
        check("@ANGULO: UA_TEXTOS_MAX is read from the header of the tree the gate points at", gate.textos_max() == int(
            re.search(r"(?m)^constexpr\s+size_t\s+UA_TEXTOS_MAX\s*=\s*(\d+)", REAL_H.read_text(encoding="utf-8")).group(1)))

        # a literal without its entry in ONE of the two packs: the other pack's entry does not stand for it
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, plant)
            bad, err = run_gate(packs(td, line, None))
            check("gate fires: a UA_TXT literal without its pt-BR entry names the pack, the hash, the English and the file",
                  bad and "language_pt-BR.lng" in err and h in err and "Planted text" in err and "plant.cpp:1" in err
                  and "language_es-ES.lng: @ANGULO has no" not in err, err[-300:])
            bad, err = run_gate(packs(td, line, line))
            check("  and the twin with both entries passes", not bad, err[-300:])
            bad, err = run_gate(packs(td, None, line))
            check("gate fires: the same literal without its es-ES entry", bad and "language_es-ES.lng" in err and h in err, err[-300:])
            # an orphan: an entry for an English that is not in the sources (it changed, or the text is gone)
            orphan = entry("Gone text", "Texto que saiu")
            bad, err = run_gate(packs(td, line + orphan, line))
            check("gate fires: an entry whose hash matches no literal is an orphan, named by its hash",
                  bad and "language_es-ES.lng" in err and f"{gate.fnv1a32('Gone text'):08x}" in err and "matches no UA_TXT" in err, err[-300:])
            # the section after @WEBDICT: the device refuses the whole pack
            for nome, raw in (("es-ES", es + b"\n@ANGULO\n" + line), ("pt-BR", pt + b"\n@ANGULO\n" + line)):
                p = Path(td) / f"language_{nome}.lng"
                p.write_bytes(raw)
                bad, err = run_gate([p])
                check(f"gate fires: @ANGULO after @WEBDICT in the {nome} pack", bad and "AFTER" in err and "@ANGULO" in err, err[-300:])
            # a repeated section
            ps = packs(td, line, line)
            ps[1].write_bytes(with_angulo(with_angulo(es, line), line))
            bad, err = run_gate(ps)
            check("gate fires: @ANGULO twice in a pack", bad and "more than once" in err, err[-300:])
            # an entry that the device refuses: each shape alone, with the good line beside it
            maus = [
                ("seven hex digits", b"1234567 texto\n"),
                ("a non-hex digit", b"1234567g texto\n"),
                ("no space after the hash", b"12345678texto\n"),
                ("a tab after the hash", b"12345678\ttexto\n"),
                ("no text", b"12345678 \n"),
                ("a text of blanks", b"12345678 \t \n"),
                ("a NUL in the text", b"12345678 te\0xt\n"),
                ("a comment line", b"# aviso\n"),
                ("the hash of the literal twice", line),
            ]
            for nome, mau in maus:
                bad, err = run_gate(packs(td, line + mau, line))
                check(f"gate fires: {nome}", bad and "language_es-ES.lng: @ANGULO" in err and "@ANGULO line 2" in err, err[-300:])
            # a text that is not UTF-8 never reaches the entry check through main( ): the pack is read as UTF-8 earlier and the gate stops there, as it
            # always did for any pack that is not UTF-8. The entry check holds the rule all the same, for the day the order changes.
            have, problems = gate.angulo_entries(b"12345678 caf\xe9\n" + entry("Planted text", "Texto plantado"))
            check("angulo_entries: a text that is not UTF-8 is a problem at its line, and the good line after it is still read",
                  len(problems) == 1 and "UTF-8" in problems[0] and "line 1" in problems[0] and list(have) == [gate.fnv1a32("Planted text")], str(problems))
            # the line ends the device accepts: CRLF and blank lines between (a last line with no newline is the device's to accept, at the end of a file;
            # in a pack @WEBDICT follows, and its directive has to start a line)
            variantes = line.rstrip(b"\n") + b"\r\n\n\n" + entry("Other", "Outro", eol=b"\r\n")
            with tempfile.TemporaryDirectory() as td2:
                angulo_tree(td2, {"plant.cpp": 'const char *a = UA_TXT( "Planted text" ), *b = UA_TXT( "Other" );\n'})
                bad, err = run_gate(packs(td2, variantes, variantes))
                check("  CRLF line ends and blank lines pass", not bad, err[-300:])
            restore_tree()

        # two literals with one hash
        a, b = fnv_collision()
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, {"plant.cpp": f'const char *a = UA_TXT( "{a}" ), *b = UA_TXT( "{b}" );\n'})
            body = entry(a, "um")
            bad, err = run_gate(packs(td, body, body))
            check(f"gate fires: two literals with one FNV-1a hash ({a!r}, {b!r}) are named together",
                  bad and "share the hash" in err and repr(a) in err and repr(b) in err, err[-300:])
            restore_tree()
            angulo_tree(td + "/o", {"plant.cpp": f'const char *a = UA_TXT( "{a}" ), *b = UA_TXT( "{b}x" );\n'})
            body = entry(a, "um") + entry(b + "x", "dois")
            bad, err = run_gate(packs(td, body, body))
            check("  and two literals with two hashes pass", not bad, err[-300:])
            restore_tree()

        # the size bound, read from the header the gate points at
        for bound, quer_falha in ((len(line), False), (len(line) - 1, True)):
            with tempfile.TemporaryDirectory() as td:
                angulo_tree(td, plant, bound=bound)
                bad, err = run_gate(packs(td, line, line))
                check(f"@ANGULO of {len(line)} B against UA_TEXTOS_MAX {bound}: " + ("gate fires" if quer_falha else "the bound is inclusive and it passes"),
                      bad == quer_falha and (not quer_falha or ("over UA_TEXTOS_MAX" in err and "language_es-ES.lng" in err)), err[-300:])
                restore_tree()
        # a header the gate cannot read stops it: a gate that cannot look must not pass
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td)
            (Path(td) / "angulo" / "Textos.h").write_text("// no bound here\n", encoding="utf-8")
            try:
                gate.textos_max()
                stopped = False
            except SystemExit:
                stopped = True
            check("gate stops when Textos.h has no UA_TEXTOS_MAX", stopped)
            (Path(td) / "angulo" / "Textos.h").unlink()
            try:
                gate.textos_max()
                stopped = False
            except SystemExit:
                stopped = True
            check("gate stops when Textos.h is not there", stopped)
            restore_tree()

        # the English is the compiler's bytes (S10c, B2a-b): an escape split across adjacent literals is one character, and an English that is not
        # UTF-8 is refused where it is written (a pack file is UTF-8 text: it cannot carry a line for the bytes the device would hash)
        def fnv_bytes(b):
            h = 0x811C9DC5
            for c in b:
                h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
            return h

        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, {"bytes.cpp": 'const char *a = UA_TXT( "a\\xE2\\x80" "\\xA6" ), *b = UA_TXT( "caf\\xC3\\xA9" ), *c = UA_TXT( "tab\\t" );\n'})
            got = sorted(gate.fnv1a32(t) for t in gate.angulo_literals())
            want = sorted(fnv_bytes(w) for w in (b"a\xe2\x80\xa6", b"caf\xc3\xa9", b"tab\t"))
            check("literals: the hash of an English is the hash of the compiler's bytes, an escape split across adjacent literals included",
                  got == want, f"got {[f'{h:08x}' for h in got]}, the bytes' {[f'{h:08x}' for h in want]}")
            restore_tree()
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, {"plant.cpp": 'const char *a = UA_TXT( "Bad \\xFF" );\n'})
            bad, err = run_gate(packs(td, None, None))
            check("gate fires: a UA_TXT English that is not UTF-8 is refused where it is written (file:line) and is not asked of the packs",
                  bad and "UTF-8" in err and "plant.cpp:1" in err and "has no" not in err, err[-300:])
            restore_tree()
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, {"plant.cpp": 'const char *a = UA_TXT( "Good \\xC3\\xA9" );\n'})
            good = entry("Good é", "Bom é")
            bad, err = run_gate(packs(td, good, good))
            check("  and the twin written as UTF-8 passes with its entries", not bad, err[-300:])
            restore_tree()
        have, problems = gate.angulo_entries(b"12345678 abc\r\r\n12345679 x\r\n")
        check("angulo_entries: ONE CR before the newline is the line end; a second is the text's, as the loader reads it (src/ui/angulo/Textos.cpp)",
              not problems and have == {0x12345678: "abc\r", 0x12345679: "x"}, str((have, problems)))

        # the reader of literals: the compiler's reading, not a grep
        fonte = (
            '// UA_TXT( "in a line comment" )\n/* UA_TXT( "in a block comment" ) */\nconst char *a = "Outside";\n'
            'const char *b = UA_TXT( "Two " "pieces" );\nconst char *c = UA_TXT( R"(Raw "quoted")" );\n'
            'const char *d = UA_TXT( "Caf\\xC3\\xA9" );\nconst char *e = UA_TXT( "Line\\n" );\n'
            'const char *f = UA_TXT(\n    "multi"\n    "line" );\nconst char *g = UA_TXT( "123" );\nconst char *h = UA_TXTX( "Near miss" );\n')
        with tempfile.TemporaryDirectory() as td:
            angulo_tree(td, {"leitor.cpp": fonte})
            got = gate.angulo_literals()
            want = {"Two pieces", 'Raw "quoted"', "Café", "Line\n", "multiline"}
            check("literals: comments, plain strings, near misses and texts with no letter are not literals; pieces join, a raw string is verbatim, "
                  "escapes decode", set(got) == want, f"got {sorted(got)}")
            check("literals: each is at the line of its first quote", got.get("multiline") == "src/ui/angulo/leitor.cpp:9" and got.get("Two pieces") == "src/ui/angulo/leitor.cpp:4",
                  str(got))
            restore_tree()
    finally:
        restore_tree()


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

        # the License screen's opening over its buffer: one of its five strings
        # made longer inside @WEBDICT, so only that check can fire
        grown = real.replace(b'"lic_summary":"', b'"lic_summary":"' + b"x" * 200, 1)
        p.write_bytes(grown)
        bad, err = run_gate([p])
        check("gate fires: License opening over its buffer",
              bad and "opening" in err, err[-200:])

        # @HELPWIRE ahead of @HELP: the alpha's prefix match would take it
        wire = real.index(b"\n@HELPWIRE\n") + 1
        web = real.index(b"\n@WEBDICT\n") + 1
        helpat = real.index(b"\n@HELP\n") + 1
        swapped = real[:helpat] + real[wire:web] + real[helpat:wire] + real[web:]
        check("swap keeps the bytes", sorted(swapped) == sorted(real))
        p.write_bytes(swapped)
        bad, err = run_gate([p])
        check("gate fires: @HELPWIRE before @HELP",
              bad and "@HELPWIRE comes before @HELP" in err, err[-200:])
        check("help_order_problem: the shipped order is fine",
              gate.help_order_problem(real) is None)
        check("help_order_problem: no help at all is fine",
              gate.help_order_problem(b"@DICT\na\n@WEBDICT\n{}") is None)
        check("help_order_problem: @HELPWIRE alone is not",
              gate.help_order_problem(b"@DICT\na\n@HELPWIRE\nw\n") == "HELPWIRE")

        # whole file over LANG_FILE_MAX (pad inside the JSON blob: whitespace
        # is legal there, adds no key, and leaves @DICT alone)
        need = file_max - len(real) + 16
        p.write_bytes(real.replace(b"@WEBDICT\n{", b"@WEBDICT\n{" + b" " * need, 1))
        bad, err = run_gate([p])
        check("gate fires: file over LANG_FILE_MAX",
              bad and "LANG_FILE_MAX" in err, err[-200:])

    angulo_cases()

    print(f"\n{passed} passaram, {failed} falharam")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
