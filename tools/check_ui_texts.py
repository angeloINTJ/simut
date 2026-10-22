#!/usr/bin/env python3
"""check_ui_texts.py — every text the Ângulo draws goes through UA_TXT( "<English>" ), and this finds the literals that do not (S10c, stage B1, 2026-10-22).

    python3 tools/check_ui_texts.py            # the gate: one `path:line: "text"` per finding, exit 1 on any
    python3 tools/check_ui_texts.py --report   # the count by file and the total, sorted, exit 0: what B3 to B5 work from
    python3 tools/check_ui_texts.py --selftest # the gate's own cases, in temporary trees, and its controls
    python3 tools/check_ui_texts.py --root DIR # another tree (DIR/src/ui/angulo, DIR/tools/ui_texts_allow.json): the selftest uses it

Exit status: 0 no finding, 1 a finding, 2 the tree cannot be read (no src/ui/angulo, no source in it, an allowance file that is not
what this script reads): a gate that cannot look must not pass.

WHY. The maintainer asked on 2026-10-22 for the Ângulo to have translations the way the original panel has them. Today every text the
Ângulo draws is a pt-BR literal in src/ui/angulo/**. Stage B of the S10c plan turns each into UA_TXT( "<English>" ), looked up in an @ANGULO
section of the device's language pack (B2 builds the mechanism, B3 to B5 convert the screens, B6 makes this gate block in CI). TRL( ) is
not that mechanism: nothing on the device reads @TRL (S10c plan, F1: DisplayManager::trlLookup( ) returns nullptr,
src/DisplayManager_LangParser.cpp:255-258). B1, this file, is the gate in REPORT MODE: it is not in CI yet, so that B3 to B5 start from the
real inventory, and B6 only has to switch it on.

THE RULE. A string literal that holds a letter (any Unicode letter, category L*) and is not the whole argument of UA_TXT( ) is a
finding, unless tools/ui_texts_allow.json names it. What the gate sees is what the compiler sees:

  comments        removed by the lexer of tools/check_net_boundary.py (imported, so there is one: `'"'`, `1'000`, `"//"` and R"x(...)x" are
                  read as the compiler reads them; the lines stay, so a finding has the file's line number).
  escapes         decoded before the letter test: "\\xE2\\x80\\xA6" is an ellipsis and has no letter, "\\x41" is an A and has one, "\\\"" is a
                  quote. The allowance and the finding show the decoded text.
  a literal split across lines, in either way C++ allows:
                  (a) a backslash at the end of the line, inside the quotes: the lexer keeps reading through the escaped newline and the
                      decoder removes the backslash-newline, as the compiler's second translation phase does;
                  (b) adjacent literals, with only blanks and comments between them: the compiler joins them, so the gate joins them into
                      ONE text, one finding at the line of the first piece, and `UA_TXT( "a " "b" )` is one argument.
  a raw string    R"delim(...)delim" (with u8, u, U, L in front): recognised by its R in front of the quote, taken verbatim (no escape is
                  decoded, and a quote, `//` or `/*` inside it neither ends it nor starts a comment), and joined with its neighbours like any
                  other piece. None of the 991 quote pairs in src/ui/angulo is one (this script's own test, 2026-10-22), so
                  the selftest is the only reader that has seen one.
  #include names  `#include "x.h"` is a header name and not a literal (the lexer blanks the name and keeps the quotes; the gate skips the
                  quotes that follow `#include`, `#include_next`, `#import` or `__has_include(`).
  UA_TXT( )       a literal (or a run of adjacent pieces) is its argument when UA_TXT, as a whole identifier, and `(` come right before it and
                  `)` right after it. `UA_TXT( ok ? "a" : "b" )`, `UA_TXT( "a", n )` and `UA_TXTX( "a" )` do not qualify: write
                  `ok ? UA_TXT( "a" ) : UA_TXT( "b" )`. B2's check_lang_packs.py will want one pack entry per UA_TXT literal, so the argument is one text.

THE ALLOWANCES. tools/ui_texts_allow.json lists what has letters and is not panel text, one group per reason:

    {"groups": [{"reason": "a unit: ...", "texts": {"src/ui/angulo/telas/X.cpp": ["hPa", "min"]}}]}

An entry is a file, the decoded literal and (through its group) the reason. It covers every occurrence of that literal in that file, and
only there. The groups of the file today: the brand, a unit, a format string with no word (conversions, punctuation and the unit symbols
kB and dBm only), an icon name, an identifier passed to another API (a language tag, a theme id), a name compared with what the user typed,
the bench endpoint's own route, headers and JSON, a linker section, a host-harness tag or diagnostic, a measurement probe, and the message
of a static_assert. The last five go beyond the usual non-text categories (a log tag, a JSON key, a unit, the brand, a format string, an
icon name, an identifier passed to another API): each is plainly not drawn, and each is a group of its own so that it can be reviewed, or
made structural, on its own. What is drawn on the panel is NEVER allowed, even when it is the same in every language (an endonym, a
keypad legend, a protocol name, a weekday initial): B3 to B5 decide those, one by one, with UA_TXT( ) or with an entry and its reason. So
the count of --report is the work left, and nothing in the allowance file makes it look smaller. Two things are findings of their own: an
allowance that matches nothing ("unused allowance": the text was converted, moved, or never had a letter) and the same file and text
allowed in two groups ("allowed twice"). A group with no reason, or an entry that is not a string, is a refusal (exit 2). A missing
allowance file is an empty one.

KNOWN LIMITS. It is a lexical gate, not a parser. It reads literals and not the text a program builds at run time (a snprintf( ) format is a
literal and is seen, its result is not); `#if 0` code is scanned as code; a text that a header outside src/ui/angulo/ defines and the Ângulo
only passes along is not seen; a literal on an `#include` line is a header name, never a text.

MEASURED on 2026-10-22 (S10c, B1, on 78aba05a; `python3 tools/check_ui_texts.py --report`): 80 files read, 736 texts with a letter (564
distinct); the allowance file takes out 213 of them (185 entries, 12 groups) and leaves 523 findings in 33 files (408 distinct). The plan's
grep bound, 774 lines in 65 files, is 778 lines in 65 files by `git grep -nE '"[^"]*[A-Za-z]{3,}[^"]*"'` on c0d835f8 (the four lines are
not explained: the plan's command was not kept) and 819 on this tree. Of those 819: 358 hold only a comment or an #include name, 121 only
allowed literals and 4 a continuation line or a literal with no letter once the escapes are decoded, 336 carry the 487 findings, and 36
findings sit on 24 lines the grep misses (runs shorter than 3 letters). A second reader, g++ -fpreprocessed and a regex, finds the same 831
literal pieces in the same 80 files. The greps and that reader are the step's record, not part of the repository.

Project: SIMUT
License: MIT
"""
from __future__ import annotations

import argparse
import bisect
import collections
import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_net_boundary as cnb               # noqa: E402  (its source stripper: one lexer for the gates, imported and not copied)


class CannotRead(Exception):
    """The tree cannot be read (no src/ui/angulo, no source in it, an allowance file that is not what this reads): exit status 2."""


DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIR = "src/ui/angulo"
# The same list as tools/check_ui_boundary.py: an .inl or .ipp is code too, and a text hidden in one would pass a gate that only read .cpp and .h.
EXTS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp")
ALLOW_FILE = "tools/ui_texts_allow.json"
MARK = "UA_TXT"

# The one place the lexer is called from. The selftest's controls replace it (a no-op, to see the cases bite).
STRIPPER = cnb.strip_source

Lit = collections.namedtuple("Lit", "path line text converted")          # one text (adjacent pieces joined); converted: it is UA_TXT( )'s argument
Allow = collections.namedtuple("Allow", "path text reason")
Scan = collections.namedtuple("Scan", "findings converted allowed unused twice files")


# ── the gate ───────────────────────────────────────────────────────────────────────────────────

BLANK = " \t\r\n\f\v"


def pieces(code):
    """[(a, c)]: the offsets of the two quotes of every string literal in `code`, the lexer's output. The lexer blanks the contents of strings,
    comments and raw strings and keeps the quotes and the newlines, so a literal is two quotes with nothing but blanks between them, and a quote
    followed by code (a literal that was never closed: the compiler refuses the file) is skipped without taking the next literal with it."""
    out, i, n = [], 0, len(code)
    while True:
        a = code.find('"', i)
        if a < 0:
            return out
        c = a + 1
        while c < n and code[c] in BLANK:
            c += 1
        if c < n and code[c] == '"':
            out.append((a, c))
            i = c + 1
        else:
            i = a + 1


_INCLUDE_LINE = re.compile(r"[ \t]*#[ \t]*(?:include_next|include|import)[ \t]*\Z")
_HAS_INCLUDE = re.compile(r"__has_include(?:_next)?\s*\(\s*\Z")


def is_directive_name(code, a):
    """The quote at a opens a header name (#include, #include_next, #import, __has_include( )): the lexer keeps its quotes, and it is not a text."""
    before = code[code.rfind("\n", 0, a) + 1:a]
    return _INCLUDE_LINE.match(before) is not None or _HAS_INCLUDE.search(before) is not None


_GAP = re.compile(r"[ \t\r\n\f\v]*(?:u8|u|U|L)?R?\Z")


def adjacent(code, i, j):
    """code[i:j] lies between the closing quote of one piece and the opening quote of the next: nothing but blanks (a comment is blank in `code`)
    and the next piece's own encoding prefix, so the compiler joins the two into one literal."""
    return _GAP.match(code[i:j]) is not None


_RAW_PREFIX = re.compile(r"(?:^|[^A-Za-z0-9_])(?:u8|u|U|L)?R\Z")
_RAW_BODY = re.compile(r"([^()\\\s\"]{0,16})\((.*)\)\1\Z", re.S)


def raw_body(code, text, a, c):
    """The body of the raw string whose quotes are at a and c, or None for a normal literal: R"delim(body)delim", with u8, u, U or L before the
    R and no identifier character before that (the lexer's rule: `fooR"x"` is a normal literal). The body is verbatim."""
    if _RAW_PREFIX.search(code[max(0, a - 4):a]) is None:
        return None
    m = _RAW_BODY.match(text[a + 1:c])
    return m.group(2) if m else None


_ESCAPE = re.compile(r"\\(?:\r\n|\n|\r)|\\x[0-9A-Fa-f]+|\\[0-7]{1,3}|\\u[0-9A-Fa-f]{4}|\\U[0-9A-Fa-f]{8}|\\.", re.S)
_SIMPLE = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}      # \\ \' \" \? and the rest: the character itself


def decode(body):
    """The text a normal literal holds: the escapes decoded (a backslash at the end of a line removed; \\x and octal as bytes, so that
    "\\xE2\\x80\\xA6" is one ellipsis; \\u and \\U as code points), read as UTF-8."""
    out, pos = bytearray(), 0
    for m in _ESCAPE.finditer(body):
        out += body[pos:m.start()].encode("utf-8", "replace")
        e = m.group()
        k = e[1]
        if k in "\r\n":
            pass
        elif k == "x":
            out.append(int(e[2:], 16) & 0xFF)
        elif k in "01234567":
            out.append(int(e[1:], 8) & 0xFF)
        elif k in "uU":
            cp = int(e[2:], 16)
            out += (chr(cp) if cp <= 0x10FFFF and not 0xD800 <= cp <= 0xDFFF else "\ufffd").encode("utf-8")
        else:
            out += _SIMPLE.get(k, k).encode("utf-8")
        pos = m.end()
    out += body[pos:].encode("utf-8", "replace")
    return out.decode("utf-8", "replace")


def has_letter(s):
    return any(unicodedata.category(ch)[0] == "L" for ch in s)


_PREFIX = re.compile(r"(?:^|[^A-Za-z0-9_])((?:u8|u|U|L)?R?)\Z")


def inside_ua_txt(code, first, last):
    """The pieces from the quote at `first` to the quote at `last` are, whole, the argument of UA_TXT( ): UA_TXT as an identifier of its own and
    `(` come right before them, and `)` right after (blanks and comments between are fine)."""
    pre = _PREFIX.search(code[max(0, first - 4):first])            # u8, u, U, L and R in front of the quote belong to the literal
    i = first - 1 - (len(pre.group(1)) if pre else 0)
    while i >= 0 and code[i] in BLANK:
        i -= 1
    if i < 0 or code[i] != "(":
        return False
    i -= 1
    while i >= 0 and code[i] in BLANK:
        i -= 1
    end = i + 1
    while i >= 0 and (code[i].isalnum() or code[i] == "_"):
        i -= 1
    if code[i + 1:end] != MARK:
        return False
    k, n = last + 1, len(code)
    while k < n and code[k] in BLANK:
        k += 1
    return k < n and code[k] == ")"


def _piece_text(code, text, a, c):
    raw = raw_body(code, text, a, c)
    return raw if raw is not None else decode(text[a + 1:c])


def texts_of(rel, text):
    """[Lit]: every text of one file that holds a letter, in order. A text is a literal or a run of adjacent ones, joined as the compiler joins
    them, at the line of its first quote; `converted` says it is UA_TXT( )'s argument."""
    code, _includes = STRIPPER(text)
    newlines = [m.start() for m in re.finditer("\n", text)]
    groups, group = [], []
    for a, c in pieces(code):
        if is_directive_name(code, a):
            continue
        if group and adjacent(code, group[-1][1] + 1, a):
            group.append((a, c))
        else:
            if group:
                groups.append(group)
            group = [(a, c)]
    if group:
        groups.append(group)
    out = []
    for g in groups:
        body = "".join(_piece_text(code, text, a, c) for a, c in g)
        if has_letter(body):
            out.append(Lit(rel, bisect.bisect_left(newlines, g[0][0]) + 1, body, inside_ua_txt(code, g[0][0], g[-1][1])))
    return out


def is_allowed(table, path, text):
    return (path, text) in table


def load_allowances(root):
    """[Allow] from tools/ui_texts_allow.json (none when the file is not there). CannotRead when it is not what this reads: a gate that cannot
    tell what is allowed must not pass."""
    path = os.path.join(root, ALLOW_FILE)
    if not os.path.exists(path):
        return []
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError) as e:
        raise CannotRead(f"cannot read {ALLOW_FILE}: {e}")
    groups = data.get("groups") if isinstance(data, dict) else None
    if not isinstance(groups, list):
        raise CannotRead(f'{ALLOW_FILE} is not {{"groups": [{{"reason": "...", "texts": {{"path": ["literal", ...]}}}}, ...]}}')
    out = []
    for k, g in enumerate(groups):
        reason = g.get("reason") if isinstance(g, dict) else None
        texts = g.get("texts") if isinstance(g, dict) else None
        if not isinstance(reason, str) or not reason.strip():
            raise CannotRead(f"{ALLOW_FILE}: group {k} has no reason: say why these literals are not panel text")
        if not isinstance(texts, dict) or not texts:
            raise CannotRead(f"{ALLOW_FILE}: group {k} ({reason[:40]!r}) has no texts: an object of path -> [literal, ...]")
        for p, lst in texts.items():
            if not isinstance(lst, list) or not lst or not all(isinstance(t, str) for t in lst):
                raise CannotRead(f"{ALLOW_FILE}: group {k} ({reason[:40]!r}), {p}: a non-empty list of strings is needed")
            out.extend(Allow(p, t, reason) for t in lst)
    return out


def scan(root):
    """-> Scan: the findings (texts outside UA_TXT( ) that no allowance names), how many sit in UA_TXT( ), the allowed ones by reason, the
    allowances that matched nothing, the ones named twice, and the number of files read."""
    base = os.path.join(root, SCAN_DIR)
    if not os.path.isdir(base):
        raise CannotRead(f"no {SCAN_DIR}/ under {root}")
    lits, files = [], 0
    for dirpath, dirs, names in os.walk(base):
        dirs.sort()
        for f in sorted(names):
            if not f.endswith(EXTS):
                continue
            path = os.path.join(dirpath, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                lits.extend(texts_of(os.path.relpath(path, root).replace(os.sep, "/"), fh.read()))
            files += 1
    if not files:
        raise CannotRead(f"no source under {SCAN_DIR}/: a gate with nothing to read must not pass")
    table = collections.defaultdict(list)                  # (path, text) -> the reasons that name it
    for al in load_allowances(root):
        table[(al.path, al.text)].append(al.reason)
    findings, used, allowed, converted = [], set(), collections.Counter(), 0
    for lit in lits:
        if lit.converted:
            converted += 1
        elif is_allowed(table, lit.path, lit.text):
            used.add((lit.path, lit.text))
            allowed[table[(lit.path, lit.text)][0]] += 1
        else:
            findings.append(lit)
    findings.sort(key=lambda f: (f.path, f.line))
    unused = sorted((p, t, r) for (p, t), rs in table.items() if (p, t) not in used for r in rs)
    twice = sorted((p, t, len(rs)) for (p, t), rs in table.items() if len(rs) > 1)
    return Scan(findings, converted, allowed, unused, twice, files)


def print_findings(res):
    """The default mode: every finding as `path:line: "text"`, then the allowance problems, then the count. -> the exit status."""
    for f in res.findings:
        print(f"{f.path}:{f.line}: {json.dumps(f.text, ensure_ascii=False)}")
    for p, t, r in res.unused:
        print(f"{ALLOW_FILE}: unused allowance: {json.dumps(t, ensure_ascii=False)} in {p} matches no literal outside UA_TXT( ) ({r[:60]})")
    for p, t, k in res.twice:
        print(f"{ALLOW_FILE}: allowed twice: {json.dumps(t, ensure_ascii=False)} in {p} is in {k} groups")
    n = len(res.findings) + len(res.unused) + len(res.twice)
    if n:
        print("check_ui_texts: a text the panel draws goes through UA_TXT( \"<English>\" ); a literal that is not panel text (a unit, an icon name, a format "
              f"string with no word, a bench tag) is named in {ALLOW_FILE} with its reason (see the docstring of tools/check_ui_texts.py)", file=sys.stderr)
    print(f"check_ui_texts: {n} findings ({len(res.findings)} texts outside UA_TXT( ), {len(res.unused)} unused allowances, {len(res.twice)} allowed twice)")
    return 1 if n else 0


def print_report(res):
    """--report: the count by file, biggest first and then by path, what the allowance file takes out by reason, and the total."""
    by_file = collections.Counter(f.path for f in res.findings)
    print("check_ui_texts --report: the texts outside UA_TXT( ), by file")
    for path, k in sorted(by_file.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{k:6d}  {path}")
    if res.allowed:
        print(f"taken out by {ALLOW_FILE} (not panel text), by reason:")
        for reason, k in sorted(res.allowed.items(), key=lambda kv: (-kv[1], kv[0])):
            print(f"{k:6d}  {reason[:90]}")
    print(f"check_ui_texts: {len(res.findings)} texts in {len(by_file)} of {res.files} files ({len({f.text for f in res.findings})} distinct); "
          f"{res.converted} already in UA_TXT( ); {sum(res.allowed.values())} literals allowed in {len(res.allowed)} groups; "
          f"{len(res.unused)} unused allowances")


# ── the selftest ───────────────────────────────────────────────────────────────────────────────

A = SCAN_DIR + "/"


def _src(*lines):
    return "\n".join(lines) + "\n"


def _allow(*pairs, reason="a test reason"):
    texts = {}
    for p, t in pairs:
        texts.setdefault(p, []).append(t)
    return {"groups": [{"reason": reason, "texts": texts}]}


class _Trees:
    """Temporary trees, one per case: files maps a path under the root to its text; allow is a dict (written as JSON) or a raw string."""

    def __init__(self, base):
        self.base, self.n = base, 0

    def make(self, files, allow=None):
        self.n += 1
        root = os.path.join(self.base, "t%03d" % self.n)
        os.makedirs(os.path.join(root, SCAN_DIR), exist_ok=True)
        for rel, text in files.items():
            p = os.path.join(root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8", newline="") as fh:
                fh.write(text)
        if allow is not None:
            p = os.path.join(root, ALLOW_FILE)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(allow if isinstance(allow, str) else json.dumps(allow, ensure_ascii=False))
        return root


def _run(*argv):
    """main( ) in this process, with its output: (exit status, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = main(list(argv))
    return rc, out.getvalue(), err.getvalue()


def _cli(*args):
    """The script as a process, the way CI will run it (B6): (exit status, stdout + stderr)."""
    r = subprocess.run([sys.executable, os.path.abspath(__file__), *args], capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout + r.stderr


@contextlib.contextmanager
def patched(**names):
    """Module globals replaced for a while: how the controls break one part of the gate at a time."""
    g = globals()
    saved = {k: g[k] for k in names}
    g.update(names)
    try:
        yield
    finally:
        g.update(saved)


def run_cases(process=True):
    """-> [(label, ok, detail)]. process=False leaves out the cases that start the script as a process (the controls do not need them)."""
    results = []

    def case(label, fn):
        try:
            ok, detail = fn()
        except Exception as e:                      # a case that raises has failed, and says why
            ok, detail = False, "raised %s: %s" % (type(e).__name__, e)
        results.append((label, bool(ok), detail))

    def eq(got, want):
        return got == want, "got %r, want %r" % (got, want)

    with tempfile.TemporaryDirectory() as base:
        T = _Trees(base)

        def finds(text, allow=None, name="a.cpp", extra=None):
            """[(line, text)] of the findings of one file planted in a temporary tree."""
            files = {A + name: text}
            files.update(extra or {})
            return [(f.line, f.text) for f in scan(T.make(files, allow)).findings]

        def exactly_one(label, text, want="Cancelar", line=1):
            case(label, lambda: eq(finds(text), [(line, want)]))

        # 1. the planted literal, and where the gate looks
        exactly_one("a planted literal gives exactly one finding, that one, at its line", _src('const char *kA = "Cancelar";'))
        exactly_one("the finding carries the line of the literal (the third)", _src("int a;", "int b;", 'const char *kA = "Cancelar";'), line=3)
        case("a header in a subdirectory is read too, and the finding names its path",
             lambda: eq([(f.path, f.line, f.text) for f in scan(T.make({A + "telas/x.h": _src('constexpr const char *K = "Salvar";')})).findings],
                        [("src/ui/angulo/telas/x.h", 1, "Salvar")]))
        case("the same literal outside src/ui/angulo, and in a file that is not source, is nobody's finding",
             lambda: eq(finds("int x;\n", extra={"src/other.cpp": _src('f("Cancelar");'), "src/ui/original/x.cpp": _src('f("Cancelar");'),
                                                 "tools/x.cpp": _src('f("Cancelar");'), A + "notes.txt": 'f("Cancelar");\n'}), []))
        case("several literals in one line are each a finding, in order",
             lambda: eq(finds(_src('f( "Salvar", "Cancelar" );')), [(1, "Salvar"), (1, "Cancelar")]))
        case("the same literal twice is two findings (the count is occurrences)",
             lambda: eq(finds(_src('f("Cancelar");', 'g("Cancelar");')), [(1, "Cancelar"), (2, "Cancelar")]))
        exactly_one("a #define is code: its literal is a finding", _src('#define TXT "Cancelar"'))
        case("a file with CRLF line ends gives the same findings and line numbers",
             lambda: eq(finds('int a;\r\nconst char *k = "Salvar";\r\n'), [(2, "Salvar")]))

        # 2. comments
        case("the literal in a // comment, whole or after code, is no finding",
             lambda: eq(finds(_src('// const char *a = "Cancelar";', 'int x; // say "Cancelar"')), []))
        case("the literals in a /* */ comment over two lines are no finding",
             lambda: eq(finds(_src('/* "Cancelar"', ' * and "Salvar" */', "int y;")), []))
        case("a comment before a real literal on the same line does not hide it",
             lambda: eq(finds(_src('/* "A" */ const char *b = "Salvar";')), [(1, "Salvar")]))

        # 3. UA_TXT( )
        case("inside UA_TXT( ), in the forms people write it, none (spaces, lines, a comment, several, adjacent pieces, a raw string)",
             lambda: eq(finds(_src('const char *a = UA_TXT("Cancelar");', 'const char *b = UA_TXT( "Cancelar" );',
                                   "const char *c = UA_TXT(", '    "Cancelar"', ");", 'const char *d = UA_TXT( /* English */ "Cancelar" );',
                                   'f( UA_TXT("a"), UA_TXT("b") );', 'const char *e = UA_TXT( "Nenhum som toca, "',
                                   '                        "nem o do alarme." );', 'const char *r = UA_TXT( R"(Salvar "tudo")" );')), []))
        for near in ('UA_TXTX("Cancelar")', 'MY_UA_TXT("Cancelar")', 'UA_TXT_FMT("Cancelar")', 'ua_txt("Cancelar")', 'UA_TXT("Cancelar", 1)',
                     'UA_TXT "Cancelar"', 'ua::txt("Cancelar")', 'UA_TXT( "Cancelar" ) "Salvar"'):
            exactly_one("a near miss of UA_TXT( ) is a finding: " + near, _src("f( " + near + " );"), want="Salvar" if "Salvar" in near else "Cancelar")
        case("UA_TXT( ok ? \"Sim\" : \"Nao\" ) is not UA_TXT's argument: both are findings (the ternary goes outside)",
             lambda: eq(finds(_src('f( UA_TXT( ok ? "Sim" : "Nao" ) );')), [(1, "Sim"), (1, "Nao")]))
        case("a literal next to UA_TXT( ) but outside it is a finding: f( UA_TXT(\"a\"), \"b\" )",
             lambda: eq(finds(_src('f( UA_TXT("a"), "b" );')), [(1, "b")]))

        # 4. a literal split across lines
        case("adjacent literals on two lines are ONE text, joined, at the line of the first piece",
             lambda: eq(finds(_src('const char *m = "Nenhum som toca, "', '                "nem o do alarme.";')),
                        [(1, "Nenhum som toca, nem o do alarme.")]))
        case("a comment between adjacent pieces does not part them",
             lambda: eq(finds(_src('const char *m = "abc " /* c */', '   "def";')), [(1, "abc def")]))
        case("pieces separated by a comma are two texts",
             lambda: eq(finds(_src('f( "abc",', '   "def" );')), [(1, "abc"), (2, "def")]))
        case("a backslash at the end of the line inside the quotes joins the lines: one text, no backslash",
             lambda: eq(finds('const char *m = "abc\\\ndef";\n'), [(1, "abcdef")]))

        # 5. raw strings
        exactly_one("a raw string is a literal: R\"x(Cancelar)x\" gives its body", _src('const char *r = R"x(Cancelar)x";'))
        case("a raw string is verbatim: quotes, //, /* */ and a backslash-n inside it stay as written, and it is one finding",
             lambda: eq(finds(_src(r'const char *r = R"(Say "hi" // not /* a comment */ \n)";')), [(1, 'Say "hi" // not /* a comment */ \\n')]))
        case("a raw string over two lines is one text, and the code after it is still read (no derailment)",
             lambda: eq(finds(_src('auto r = R"(Linha um', 'linha "dois")"; const char *s = "Salvar";')), [(1, 'Linha um\nlinha "dois"'), (2, "Salvar")]))
        case("a raw string next to a normal literal is joined with it, as the compiler does: R\"(a)\" \"b\" is \"ab\"",
             lambda: eq(finds(_src('const char *m = R"(ab)" "cd";')), [(1, "abcd")]))
        case("a raw string with no letter (digits) is no finding, and u8R\"( )\" is a raw string too",
             lambda: eq(finds(_src('f( R"(123)" );', 'g( u8R"(Salvar)" );')), [(2, "Salvar")]))
        case("an identifier that ends in R in front of a quote is not a raw string: fooR\"x\" would read x as a plain literal",
             lambda: eq(finds(_src('#define fooR(s) s', 'const char *a = fooR"(Salvar)";')), [(2, "(Salvar)")]))

        # 6. escapes
        case("escapes with no letter after decoding (an ellipsis, a minus sign, a middle dot, \\n, \\t) are no finding",
             lambda: eq(finds(_src(r'const char *a[] = { "\xE2\x80\xA6", "\xE2\x88\x92", "\xC2\xB7", "\n", "\t", "°" };')), []))
        case("escapes that decode to a letter are findings with the decoded text: \\x41, \\101, \\u00e9",
             lambda: eq(finds(_src(r'const char *a[] = { "\x41", "\101", "é" };')), [(1, "A"), (1, "A"), (1, "é")]))
        case("an escaped quote and an escaped backslash are read before the letter test: \"Say \\\"hi\\\"\" and \"\\\\x41\"",
             lambda: eq(finds(_src(r'const char *a[] = { "Say \"hi\"", "\\x41" };')), [(1, 'Say "hi"'), (1, "\\x41")]))

        # 7. what has no letter, what has
        case("literals with no letter are no finding: %, digits, empty, a space, a middle dot, an ellipsis, a degree sign, +, -, 0.5",
             lambda: eq(finds(_src('const char *a[] = { "%", "123", "", " ", "·", "…", "°", "+", "-", "0.5" };')), []))
        for t in ("%d", "σ", "ç", "°C", "Δ", "h"):
            exactly_one("a literal with a letter is a finding, whatever the letter: " + t, _src('f("%s");' % t), want=t)
        case("a character literal is no string: '\"' and 'a' give nothing, and the literal after '\"' is still read",
             lambda: eq(finds(_src("char q = '\"'; char c = 'a'; const char *s = \"Texto\";")), [(1, "Texto")]))
        case("a quote that is never closed does not derail the next literal",
             lambda: eq(finds(_src('const char *a = "abc;', 'const char *b = "Salvar";')), [(2, "Salvar")]))
        case("#include names are header names and not literals; the literal after them is still a finding",
             lambda: eq(finds(_src('#include "Angulo.h"', "#include <string.h>", '  #  include "telas/Base.h"', 'const char *k = "Salvar";')),
                        [(4, "Salvar")]))

        # 8. the allowances
        ok_allow = _allow((A + "a.cpp", "Cancelar"))
        case("an allowance covers the literal it names", lambda: eq(finds(_src('f("Cancelar");'), ok_allow), []))
        case("one allowance covers every occurrence in its file", lambda: eq(finds(_src('f("Cancelar");', 'g("Cancelar");'), ok_allow), []))
        case("an allowance is for its file: the same literal in another file stays a finding, and the allowance is unused",
             lambda: (lambda r: (len(r.findings) == 1 and len(r.unused) == 1, "%r %r" % (r.findings, r.unused)))(
                 scan(T.make({A + "a.cpp": "int x;\n", A + "b.cpp": _src('f("Cancelar");')}, ok_allow))))
        case("an allowance that matches nothing is itself exactly one finding, named 'unused allowance'",
             lambda: (lambda rc, out, err: (rc == 1 and out.count(": unused allowance: ") == 1 and out.strip().splitlines()[-1].startswith("check_ui_texts: 1 findings"),
                                           "%d %r" % (rc, out)))(*_run("--root", T.make({A + "a.cpp": "int x;\n"}, ok_allow))))
        case("an allowance whose text now sits in UA_TXT( ) is unused too: the mark comes down in the change that converts",
             lambda: (lambda r: (not r.findings and len(r.unused) == 1 and r.converted == 1, "%r %r %r" % (r.findings, r.unused, r.converted)))(
                 scan(T.make({A + "a.cpp": _src('f(UA_TXT("Cancelar"));')}, ok_allow))))
        case("an allowance for a text with no letter is unused (it can never match)",
             lambda: eq(len(scan(T.make({A + "a.cpp": _src('f("123");')}, _allow((A + "a.cpp", "123")))).unused), 1))
        case("the same file and text in two groups is 'allowed twice', one finding",
             lambda: (lambda rc, out, err: (rc == 1 and out.count(": allowed twice: ") == 1, "%d %r" % (rc, out)))(
                 *_run("--root", T.make({A + "a.cpp": _src('f("Cancelar");')},
                                        {"groups": [{"reason": "one", "texts": {A + "a.cpp": ["Cancelar"]}},
                                                    {"reason": "two", "texts": {A + "a.cpp": ["Cancelar"]}}]}))))
        for label, allow in (("a group with no reason", {"groups": [{"reason": " ", "texts": {A + "a.cpp": ["Cancelar"]}}]}),
                             ("a group whose texts are not strings", {"groups": [{"reason": "r", "texts": {A + "a.cpp": [1]}}]}),
                             ("an allowance file that is not JSON", "{not json"),
                             ("an allowance file that is a list", "[]")):
            case("exit 2, a refusal and not a pass: " + label,
                 lambda allow=allow: (lambda rc, out, err: (rc == 2 and "check_ui_texts" in err, "%d %r %r" % (rc, out, err)))(
                     *_run("--root", T.make({A + "a.cpp": _src('f("Cancelar");')}, allow))))
        case("no allowance file is an empty one: the gate still runs and finds",
             lambda: eq(_run("--root", T.make({A + "a.cpp": _src('f("Cancelar");')}))[0], 1))

        # 9. the modes and the exit status
        rows_root = T.make({A + "a.cpp": _src('f("Salvar");', 'f("Cancelar");', 'f("Salvar");'), A + "b.cpp": _src('f("Voltar");'),
                            A + "c.cpp": _src('f("Um");', 'f("Dois");', 'f("Tres");'), A + "d.h": _src("int x;")})

        def report():
            rc, out, err = _run("--root", rows_root, "--report")
            rows = re.findall(r"^\s*(\d+)  (\S+)$", out, re.M)
            return (rc == 0 and rows == [("3", A + "a.cpp"), ("3", A + "c.cpp"), ("1", A + "b.cpp")]
                    and re.search(r"7 texts in 3 of 4 files \(6 distinct\)", out) is not None), "%d %r" % (rc, out)
        case("--report: the count by file, biggest first and then by path, the total, 'distinct', and exit 0 with findings", report)

        def default_mode():
            rc, out, err = _run("--root", rows_root)
            lines = out.strip().splitlines()
            shown = [l for l in lines if re.match(r'^src/ui/angulo/[a-d]\.(cpp|h):\d+: "', l)]
            return rc == 1 and len(shown) == 7 and lines[-1].startswith("check_ui_texts: 7 findings"), "%d %r" % (rc, out[-200:])
        case("the default mode: every finding as path:line: \"text\", the count on the last line, exit 1", default_mode)

        clean = T.make({A + "a.cpp": _src("int x = 1;", 'f(UA_TXT("Salvar"));')})
        case("a clean tree: the default mode exits 0 and says 0 findings; --report exits 0 and lists no file",
             lambda: (lambda a, b: (a[0] == 0 and b[0] == 0 and a[1].strip().splitlines()[-1].startswith("check_ui_texts: 0 findings")
                                    and not re.findall(r"^\s*\d+  \S+$", b[1], re.M), "%r %r" % (a, b)))(_run("--root", clean), _run("--root", clean, "--report")))
        case("exit 2: no src/ui/angulo at all", lambda: eq(_run("--root", os.path.join(base, "nowhere"))[0], 2))
        empty = os.path.join(base, "empty")
        os.makedirs(os.path.join(empty, SCAN_DIR))
        case("exit 2: src/ui/angulo with no source in it (a gate with nothing to read must not pass)", lambda: eq(_run("--root", empty)[0], 2))

        if process:
            bad = T.make({A + "a.cpp": _src('f("Cancelar");')})
            case("the script as a process: a finding exits 1 and prints it; --report exits 0; a clean tree exits 0; no tree exits 2",
                 lambda: (lambda a, b, c, d: (a[0] == 1 and 'a.cpp:1: "Cancelar"' in a[1] and b[0] == 0 and c[0] == 0 and d[0] == 2, "%r %r %r %r" % (a, b, c, d)))(
                     _cli("--root", bad), _cli("--root", bad, "--report"), _cli("--root", clean), _cli("--root", os.path.join(base, "nowhere"))))

        # 10. the real tree
        def real_tree():
            r = scan(DEFAULT_ROOT)
            return not r.unused and not r.twice and r.files > 0, "unused %r, twice %r, files %d" % (r.unused[:2], r.twice[:2], r.files)
        case("the real tree: every allowance of tools/ui_texts_allow.json matches a literal of src/ui/angulo, none twice", real_tree)

        def planted_in_real():
            copy = os.path.join(base, "real")
            shutil.copytree(os.path.join(DEFAULT_ROOT, SCAN_DIR), os.path.join(copy, SCAN_DIR))
            os.makedirs(os.path.join(copy, "tools"))
            if os.path.exists(os.path.join(DEFAULT_ROOT, ALLOW_FILE)):
                shutil.copy(os.path.join(DEFAULT_ROOT, ALLOW_FILE), os.path.join(copy, ALLOW_FILE))
            n0 = len(scan(copy).findings)
            plant = os.path.join(copy, SCAN_DIR, "zz_plant.cpp")

            def count(text):
                with open(plant, "w", encoding="utf-8") as fh:
                    fh.write(text)
                return len(scan(copy).findings)
            got = (count('const char *p = "Plantado";\n'), count('// const char *p = "Plantado";\n'), count('const char *p = UA_TXT("Plantado");\n'))
            return got == (n0 + 1, n0, n0), "%r against %d" % (got, n0)
        case("a copy of the real tree: a planted literal adds exactly one finding; in a comment and in UA_TXT( ), none", planted_in_real)

    return results


# The controls: each breaks one part of the gate, and the cases must notice. A control that no case notices is a part of the gate nothing tests.
CONTROLS = (
    ("the lexer of check_net_boundary.py is replaced by a no-op", lambda: {"STRIPPER": lambda text: (text, [])}),
    ("escapes are not decoded", lambda: {"decode": lambda body: body}),
    ("adjacent literals are not joined", lambda: {"adjacent": lambda code, i, j: False}),
    ("UA_TXT( ) is not recognised", lambda: {"inside_ua_txt": lambda code, first, last: False}),
    ("raw strings are read as normal literals", lambda: {"raw_body": lambda code, text, a, c: None}),
    ("the allowances are ignored", lambda: {"is_allowed": lambda table, path, text: False}),
)


def selftest(no_strip=False):
    """The cases, then (unless no_strip) the controls. no_strip is the first control alone, from the command line: the exit status must not be 0."""
    n = bad = 0

    def show(results, indent=""):
        nonlocal n, bad
        for label, ok, detail in results:
            n += 1
            bad += not ok
            print("%s%s %s%s" % (indent, "ok  " if ok else "FAIL", label, ("" if ok or not detail else " | " + detail[:300])))
    if no_strip:
        with patched(**CONTROLS[0][1]()):
            show(run_cases())
        print("RESULT: %d cases, %d failed (the control: the lexer is a no-op; at least one must fail)" % (n, bad))
        return 1 if bad else 0
    show(run_cases())
    for name, make in CONTROLS:
        with patched(**make()):
            failed = [label for label, ok, detail in run_cases(process=False) if not ok]
        n += 1
        if not failed:
            bad += 1
        print("%s control: %s -> %d case(s) fail%s" % ("ok  " if failed else "FAIL", name, len(failed), "" if failed else ": NO CASE NOTICED IT"))
        for label in failed[:3]:
            print("      e.g. " + label[:110])
    print("RESULT: %d cases and controls, %d failed" % (n, bad))
    return 1 if bad else 0


# ── main ───────────────────────────────────────────────────────────────────────────────────────

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="The Ângulo's literals outside UA_TXT( ): see the module docstring.")
    ap.add_argument("--root", default=DEFAULT_ROOT, help="the tree to look at (default: this repository)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--report", action="store_true", help="the count by file and the total, sorted; exit 0")
    mode.add_argument("--selftest", action="store_true", help="the gate's own cases and controls, in temporary trees")
    ap.add_argument("--no-strip", action="store_true", help="with --selftest: run the cases with the lexer replaced by a no-op (the control; it must exit 1)")
    args = ap.parse_args(argv)
    if args.no_strip and not args.selftest:
        ap.error("--no-strip is the selftest's control and needs --selftest")
    if args.selftest:
        return selftest(no_strip=args.no_strip)
    root = os.path.abspath(args.root)
    try:
        res = scan(root)
    except CannotRead as e:
        print(f"check_ui_texts: {e}", file=sys.stderr)
        return 2
    if args.report:
        print_report(res)
        return 0
    return print_findings(res)


if __name__ == "__main__":
    sys.exit(main())
