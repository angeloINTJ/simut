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
  the English     is the compiler's BYTES. Each piece's escapes are decoded to bytes (\\xE2\\x80\\xA6 is three bytes, one ellipsis; \\u and \\U are
                  code points written in UTF-8), the pieces' bytes are joined, and the join is read as UTF-8: UA_TXT( "a\\xE2\\x80" "\\xA6" ) is a and an
                  ellipsis, the bytes the device hashes (decoding each piece by itself would read two replacement characters, and the gate's hash
                  would differ from the device's). An English that is not valid UTF-8 is a finding of its own: a pack is a UTF-8 file and has no
                  line for those bytes.

A LOOKED-UP TEXT IS NEVER A FORMAT (S10c, B2a-b). A pack is a file anyone with PERM_FILE_UPLOAD can put on the device, the loader keeps only hashes
and so cannot compare a translation's conversions with the English's, and the build has no -Wformat-nonliteral: snprintf( b, 16, UA_TXT( "%d min" ), v )
compiles under -Wall -Wextra -Werror, and a translation with another conversion, or a %n, would read the stack or write where nobody pointed (37 of the
408 texts hold a %, 2026-10-22). So the words go in as arguments of a format that is a literal: snprintf( b, 16, "%d %s", v, UA_TXT( "min" ) ). If a
language's word order ever forces a conversion into a translation, a call-site helper must compare the translation's conversion sequence with the
English literal's (same count and types, no %n, no *) and fall back to the English on any difference; none exists yet. This gate refuses UA_TXT( ) as
the format argument of printf, vprintf, sprintf, vsprintf, fprintf, vfprintf, dprintf, vdprintf, asprintf, vasprintf, snprintf and vsnprintf (and
newlib's iprintf family), called as a function or as a member (`Serial.printf( )`), and of any function the kit declares with a `const char *` right
before `...` or a `va_list`, found by its declaration whatever its name (none exists today: src/ui/angulo has no such function, 2026-10-22). The format
is the argument at the function's position, and UA_TXT as an identifier anywhere in it (a ternary, a cast) is the finding: one per call, at the line
of the UA_TXT. `-Wformat-nonliteral` was measured and left off: on 2026-10-22, with the project's own flags added to, it names 0 sites in the 34 units
of src/ui/angulo that each Ângulo image compiles, plus Textos.cpp, and 1 outside them (src/SystemDefs_Records.h:962, a lambda of the products).

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
only passes along is not seen; a literal on an `#include` line is a header name, never a text. A looked-up text that reaches a printf
through a variable (`const char *f = UA_TXT( ... ); snprintf( b, n, f, v )`), through a macro that forwards its arguments, through a function
pointer or through a function the kit declares without `...` or a va_list is not seen: that is -Wformat-nonliteral's, which is off. A template-id's
comma before the format argument (`f<a, b>( ... )`) shifts the argument's position: angle brackets do not nest.

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

Lit = collections.namedtuple("Lit", "path line text converted raw")      # one text (adjacent pieces joined); converted: UA_TXT( )'s argument; raw: its bytes
Allow = collections.namedtuple("Allow", "path text reason")
Fmt = collections.namedtuple("Fmt", "path line function")                  # a looked-up text used as the format of a printf-family call
Scan = collections.namedtuple("Scan", "findings converted allowed unused twice files formats badutf8")


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


def decode_bytes(body):
    """The BYTES a normal literal holds: the escapes decoded (a backslash at the end of a line removed; \\x and octal as one byte each, so that
    "\\xE2\\x80\\xA6" is three bytes; \\u and \\U as code points written in UTF-8) and the rest of the source text in UTF-8. Whether they are valid UTF-8 is
    for is_utf8( ): a piece may end in the middle of a character that the next piece completes."""
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
            out += (chr(cp) if cp <= 0x10FFFF and not 0xD800 <= cp <= 0xDFFF else "�").encode("utf-8")
        else:
            out += _SIMPLE.get(k, k).encode("utf-8")
        pos = m.end()
    out += body[pos:].encode("utf-8", "replace")
    return bytes(out)


def decode(body):
    """The text a normal literal holds: decode_bytes( ) read as UTF-8, a byte that is not UTF-8 shown as U+FFFD."""
    return decode_bytes(body).decode("utf-8", "replace")


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


def is_utf8(raw):
    """The bytes of an English are valid UTF-8 (the strict decoder: no overlong form, no surrogate, nothing cut short)."""
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


def _piece_bytes(code, text, a, c):
    """The bytes of one piece of a literal: a raw string verbatim, a normal one with its escapes decoded (decode_bytes)."""
    raw = raw_body(code, text, a, c)
    return raw.encode("utf-8") if raw is not None else decode_bytes(text[a + 1:c])


def texts_of(rel, text, code=None):
    """[Lit]: every text of one file that holds a letter, in order. A text is a literal or a run of adjacent ones, joined as the compiler joins
    them, at the line of its first quote; `converted` says it is UA_TXT( )'s argument. The join is of the BYTES of the pieces (an escape may be split
    across two of them: "a\\xE2\\x80" "\\xA6" is a and an ellipsis) and `raw` is that join, what the compiler holds and the device hashes; `text` is
    `raw` read as UTF-8, a byte that is not UTF-8 shown as U+FFFD (is_utf8( raw ) tells). `code` is the lexer's output for `text`, when the caller has it."""
    if code is None:
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
        raw = b"".join(_piece_bytes(code, text, a, c) for a, c in g)
        body = raw.decode("utf-8", "replace")
        if has_letter(body):
            out.append(Lit(rel, bisect.bisect_left(newlines, g[0][0]) + 1, body, inside_ua_txt(code, g[0][0], g[-1][1]), raw))
    return out


# The printf family: name -> the index of the format argument. A call is `name (` with `name` a whole identifier, wherever it is called from
# (`Serial.printf(`, `std::snprintf(`); UA_TXT as an identifier anywhere in that argument is a looked-up text used as a format.
FORMATTERS = {"printf": 0, "vprintf": 0, "iprintf": 0, "sprintf": 1, "vsprintf": 1, "siprintf": 1, "fprintf": 1, "vfprintf": 1, "fiprintf": 1,
              "dprintf": 1, "vdprintf": 1, "asprintf": 1, "vasprintf": 1, "snprintf": 2, "vsnprintf": 2, "sniprintf": 2}
_CALL = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z_]\w*)\s*\(")
_MARK = re.compile(r"(?<![A-Za-z0-9_])" + MARK + r"(?![A-Za-z0-9_])")
_VARIADIC_FMT = re.compile(r"const\s+char\s*\*\s*(?:[A-Za-z_]\w*)?\s*,\s*(?:\.\.\.|va_list\b)")


def call_args(code, p):
    """The top-level arguments of the call whose `(` is at p, as (start, end) offsets in `code`, where strings and comments are blank, so that every
    comma and parenthesis is code. Brackets and braces nest like parentheses; angle brackets do not. A call that never closes gives the arguments
    it has."""
    depth, start, args = 0, p + 1, []
    for i in range(p + 1, len(code)):
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                if args or code[start:i].strip(BLANK):
                    args.append((start, i))
                return args
            depth -= 1
        elif c == "," and depth == 0:
            args.append((start, i))
            start = i + 1
    return args


def kit_formatters(code):
    """{name: index of the format argument} for the functions that `code` (a file, comments and strings blank) declares or defines with a
    `const char *` right before `...` or a va_list: the kit's own printf family, found by its declaration whatever the name."""
    out = {}
    for m in _VARIADIC_FMT.finditer(code):
        depth, i, commas = 0, m.start() - 1, 0
        while i >= 0:                                  # back to the `(` of the parameter list, counting the commas of its own level
            c = code[i]
            if c in ")]}":
                depth += 1
            elif c in "([{":
                if depth == 0:
                    break
                depth -= 1
            elif c == "," and depth == 0:
                commas += 1
            i -= 1
        if i < 0 or code[i] != "(":
            continue
        j = i - 1
        while j >= 0 and code[j] in BLANK:
            j -= 1
        end = j + 1
        while j >= 0 and (code[j].isalnum() or code[j] == "_"):
            j -= 1
        name = code[j + 1:end]
        if name and not name[0].isdigit():
            out[name] = commas
    return out


def format_uses(code, table):
    """[(offset of the UA_TXT, function)]: one per call of a printf-family function (`table`: name -> index of its format argument) whose format
    argument holds UA_TXT as an identifier. A call with fewer arguments (a declaration, a macro's body) has no such argument and is skipped."""
    out = []
    for m in _CALL.finditer(code):
        k = table.get(m.group(1))
        if k is None:
            continue
        args = call_args(code, m.end() - 1)
        if len(args) <= k:
            continue
        t = _MARK.search(code, args[k][0], args[k][1])
        if t:
            out.append((t.start(), m.group(1)))
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
    allowances that matched nothing, the ones named twice, the number of files read, the looked-up texts used as the format of a printf-family call
    (Fmt) and the UA_TXT( ) English that is not UTF-8 (Lit)."""
    base = os.path.join(root, SCAN_DIR)
    if not os.path.isdir(base):
        raise CannotRead(f"no {SCAN_DIR}/ under {root}")
    sources = []                                       # (path relative to root, text, the lexer's code)
    for dirpath, dirs, names in os.walk(base):
        dirs.sort()
        for f in sorted(names):
            if not f.endswith(EXTS):
                continue
            path = os.path.join(dirpath, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            sources.append((os.path.relpath(path, root).replace(os.sep, "/"), text, STRIPPER(text)[0]))
    if not sources:
        raise CannotRead(f"no source under {SCAN_DIR}/: a gate with nothing to read must not pass")
    lits = []
    for rel, text, code in sources:
        lits.extend(texts_of(rel, text, code))
    formatters = dict(FORMATTERS)
    for _rel, _text, code in sources:
        formatters.update(kit_formatters(code))
    formats = []
    for rel, text, code in sources:
        uses = format_uses(code, formatters)
        if uses:
            newlines = [m.start() for m in re.finditer("\n", text)]
            formats.extend(Fmt(rel, bisect.bisect_left(newlines, off) + 1, fn) for off, fn in uses)
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
    badutf8 = [lit for lit in lits if lit.converted and not is_utf8(lit.raw)]
    return Scan(findings, converted, allowed, unused, twice, len(sources), formats, badutf8)


def bad_message(lit):
    return (f"{lit.path}:{lit.line}: UA_TXT( ) English is not valid UTF-8 (bytes {lit.raw.hex()}): the device hashes the compiler's bytes and a pack, "
            "a UTF-8 file, has no line for them; write the character as its UTF-8 bytes")


def format_message(f):
    return (f"{f.path}:{f.line}: UA_TXT( ) is the format of {f.function}( ): a looked-up text is never a format (a pack is a file any uploader can "
            "write); pass the words as %s arguments of a literal format")


def print_findings(res):
    """The default mode: every finding as `path:line: "text"`, then the English that is not UTF-8 and the looked-up texts used as a format, then the
    allowance problems, then the count. -> the exit status."""
    for f in res.findings:
        print(f"{f.path}:{f.line}: {json.dumps(f.text, ensure_ascii=False)}")
    for lit in res.badutf8:
        print(bad_message(lit))
    for f in res.formats:
        print(format_message(f))
    for p, t, r in res.unused:
        print(f"{ALLOW_FILE}: unused allowance: {json.dumps(t, ensure_ascii=False)} in {p} matches no literal outside UA_TXT( ) ({r[:60]})")
    for p, t, k in res.twice:
        print(f"{ALLOW_FILE}: allowed twice: {json.dumps(t, ensure_ascii=False)} in {p} is in {k} groups")
    n = len(res.findings) + len(res.unused) + len(res.twice) + len(res.badutf8) + len(res.formats)
    if n:
        print("check_ui_texts: a text the panel draws goes through UA_TXT( \"<English>\" ); a literal that is not panel text (a unit, an icon name, a format "
              f"string with no word, a bench tag) is named in {ALLOW_FILE} with its reason (see the docstring of tools/check_ui_texts.py)", file=sys.stderr)
    print(f"check_ui_texts: {n} findings ({len(res.findings)} texts outside UA_TXT( ), {len(res.unused)} unused allowances, {len(res.twice)} allowed twice, "
          f"{len(res.badutf8)} English not UTF-8, {len(res.formats)} looked-up texts used as a format)")
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
    for lit in res.badutf8:
        print(bad_message(lit))
    for f in res.formats:
        print(format_message(f))
    print(f"check_ui_texts: {len(res.findings)} texts in {len(by_file)} of {res.files} files ({len({f.text for f in res.findings})} distinct); "
          f"{res.converted} already in UA_TXT( ); {sum(res.allowed.values())} literals allowed in {len(res.allowed)} groups; "
          f"{len(res.unused)} unused allowances; {len(res.badutf8)} English not UTF-8; {len(res.formats)} looked-up texts used as a format")


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

        # 11. a looked-up text is never a format; the English is the compiler's bytes (S10c, B2a-b)
        def formats(text, extra=None):
            """[(line, function)] of the looked-up texts that are the format of a printf-family call, in one file planted in a temporary tree."""
            files = {A + "a.cpp": text}
            files.update(extra or {})
            return [(f.line, f.function) for f in scan(T.make(files)).formats]

        for call, fn in (('printf( UA_TXT( "%d min" ), v );', "printf"),
                         ('sprintf( b, UA_TXT( "%d min" ), v );', "sprintf"),
                         ('snprintf( b, 16, UA_TXT( "%d min" ), v );', "snprintf"),
                         ('vsprintf( b, UA_TXT( "%d min" ), ap );', "vsprintf"),
                         ('vsnprintf( b, 16, UA_TXT( "%d min" ), ap );', "vsnprintf"),
                         ('fprintf( stderr, UA_TXT( "%d min" ), v );', "fprintf"),
                         ('Serial.printf( UA_TXT( "%d min" ), v );', "printf"),
                         ('std::snprintf( b, 16, UA_TXT( "%d min" ), v );', "snprintf"),
                         ('snprintf( b, g( 1, 2 ), UA_TXT( "%d min" ), v );', "snprintf"),
                         ('snprintf( b, 16, ok ? UA_TXT( "%d min" ) : UA_TXT( "%d h" ), v );', "snprintf"),
                         ('snprintf( b, 16, (const char *)UA_TXT( "%d min" ), v );', "snprintf"),
                         ('f( snprintf( b, 16, UA_TXT( "%d min" ), v ), 2 );', "snprintf")):
            case("UA_TXT( ) as the format is refused, once, naming the function: " + call,
                 lambda call=call, fn=fn: eq(formats(_src(call)), [(1, fn)]))
        case("a call over several lines: the finding is at the line of the UA_TXT",
             lambda: eq(formats(_src("snprintf( b,", "          sizeof b,", '          UA_TXT( "%d min" ),', "          v );")), [(3, "snprintf")]))
        case("the words go in as %s arguments of a literal format: none of these uses a looked-up text as a format",
             lambda: eq(formats(_src('snprintf( b, 16, "%s min", UA_TXT( "hour" ) );', 'printf( "%s\\n", UA_TXT( "x" ) );',
                                     'fprintf( stderr, "%s", ok ? UA_TXT( "a" ) : UA_TXT( "b" ) );', 'snprintf( b, g( 1, 2 ), "%s", UA_TXT( "x" ) );',
                                     'sprintf( b, "%d %s", n, UA_TXT( "x" ) );')), []))
        case("not a use: a comment, a string that says UA_TXT, another identifier (MY_UA_TXT)",
             lambda: eq(formats(_src('// snprintf( b, 16, UA_TXT( "x %d" ), v );', 'snprintf( b, 16, "UA_TXT( x )", v );',
                                     'snprintf( b, 16, MY_UA_TXT( "x %d" ), v );')), []))
        case("a call with fewer arguments than the format's position, an unclosed call and a bare name are no finding and no crash",
             lambda: eq(formats(_src("snprintf( b );", "printf( );", "int (*p)( ) = snprintf;", 'snprintf( b, 16, UA_TXT( "x"')), []))
        kit = {A + "log.h": _src("void falar( int nivel, const char *fmt, ... );", "int vfalar( const char *formato, va_list ap );")}
        case("a function the kit declares with a const char * before ... or va_list is a printf-family function too, found by its declaration",
             lambda: eq(formats(_src('falar( 1, UA_TXT( "%d min" ), v );', 'vfalar( UA_TXT( "%d" ), ap );', 'falar( 1, "%s", UA_TXT( "x" ) );'), extra=kit),
                        [(1, "falar"), (2, "vfalar")]))
        case("and with no declaration of it the same calls give nothing: the discovery, and not the table, found them",
             lambda: eq(formats(_src('falar( 1, UA_TXT( "%d min" ), v );', 'vfalar( UA_TXT( "%d" ), ap );')), []))

        def format_in_default_mode():
            rc, out, err = _run("--root", T.make({A + "a.cpp": _src('snprintf( b, 16, UA_TXT( "%d min" ), v );')}))
            return (rc == 1 and 'a.cpp:1: UA_TXT( ) is the format of snprintf( )' in out
                    and out.strip().splitlines()[-1].startswith("check_ui_texts: 1 findings")), "%d %r" % (rc, out)
        case("the default mode: a looked-up text used as a format is one finding, with path:line and the function, exit 1", format_in_default_mode)
        case("the twin: the same words as a %s argument of a literal format (a format with no word, allowed) exit 0",
             lambda: eq(_run("--root", T.make({A + "a.cpp": _src('snprintf( b, 16, "%d %s", n, UA_TXT( "hour" ) );')}, _allow((A + "a.cpp", "%d %s"))))[0], 0))

        def lits(text):
            return [(l.text, l.raw, l.converted) for l in texts_of(A + "a.cpp", text)]
        case("an escape split across adjacent literals is one character: UA_TXT( \"a\\xE2\\x80\" \"\\xA6\" ) is a and an ellipsis, the bytes the compiler holds",
             lambda: eq(lits(_src(r'f( UA_TXT( "a\xE2\x80" "\xA6" ) );')), [("a…", b"a\xe2\x80\xa6", True)]))
        case("\"caf\\xC3\\xA9\", \"x\\t\" and a raw string are the compiler's bytes, as they were",
             lambda: eq(lits(_src(r'f( "caf\xC3\xA9", "x\t", R"(R\n"q")" );')),
                        [("café", b"caf\xc3\xa9", False), ("x\t", b"x\t", False), ('R\\n"q"', b'R\\n"q"', False)]))
        case("decode_bytes( ): \\t is the one byte 09, \\x41 is A", lambda: eq((decode_bytes(r"\t"), decode_bytes(r"\x41")), (b"\t", b"A")))
        case("an English that is not UTF-8 is refused as a finding of its own: its line, the word UTF-8, exit 1",
             lambda: (lambda rc, out, err: (rc == 1 and "a.cpp:1:" in out and "UTF-8" in out
                                            and out.strip().splitlines()[-1].startswith("check_ui_texts: 1 findings"), "%d %r" % (rc, out)))(
                 *_run("--root", T.make({A + "a.cpp": _src(r'f( UA_TXT( "a\xFF" ) );')}))))
        case("the twin: the same English written as UTF-8 (\\xC3\\xA9) is no finding",
             lambda: eq(_run("--root", T.make({A + "a.cpp": _src(r'f( UA_TXT( "a\xC3\xA9" ) );')}))[0], 0))
        case("a text that is not UTF-8 outside UA_TXT( ) is a finding as before, shown with the replacement character",
             lambda: eq(finds(_src(r'f( "a\xFF" );')), [(1, "a\ufffd")]))

        # 10. the real tree
        def real_tree():
            r = scan(DEFAULT_ROOT)
            return not r.unused and not r.twice and r.files > 0, "unused %r, twice %r, files %d" % (r.unused[:2], r.twice[:2], r.files)
        case("the real tree: every allowance of tools/ui_texts_allow.json matches a literal of src/ui/angulo, none twice", real_tree)

        def bench_reason():
            with open(os.path.join(DEFAULT_ROOT, ALLOW_FILE), encoding="utf-8") as fh:
                groups = json.load(fh)["groups"]
            reasons = [g["reason"] for g in groups if A + "WebManager_UiA.cpp" in g["texts"]]
            with open(os.path.join(DEFAULT_ROOT, "tools/generated/profiles.ini"), encoding="utf-8") as fh:
                parts = re.split(r"(?m)^\[env:(\w+)\]\s*$", fh.read())          # [head, name, body, name, body, ...]
            builds = sorted(n for n, body in zip(parts[1::2], parts[2::2]) if "+<ui/angulo/WebManager_UiA.cpp>" in body)
            named = sorted(n for n in parts[1::2] if reasons and re.search(r"\b%s\b" % re.escape(n), reasons[0]))
            return len(reasons) == 1 and bool(builds) and named == builds, "the images that compile it: %r; the reason names: %r" % (builds, named)
        case("the bench endpoint's reason names the images that compile WebManager_UiA.cpp, and only those (tools/generated/profiles.ini)", bench_reason)

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
    ("escapes are not decoded", lambda: {"decode_bytes": lambda body: body.encode("utf-8")}),
    ("adjacent literals are not joined", lambda: {"adjacent": lambda code, i, j: False}),
    ("UA_TXT( ) is not recognised", lambda: {"inside_ua_txt": lambda code, first, last: False}),
    ("raw strings are read as normal literals", lambda: {"raw_body": lambda code, text, a, c: None}),
    ("the allowances are ignored", lambda: {"is_allowed": lambda table, path, text: False}),
    ("the format rule sees nothing", lambda: {"format_uses": lambda code, table: []}),
    ("the kit's own formatters are not discovered", lambda: {"kit_formatters": lambda code: {}}),
    ("the English is decoded piece by piece, with replacement",
     lambda: {"_piece_bytes": (lambda real: lambda code, text, a, c: real(code, text, a, c).decode("utf-8", "replace").encode("utf-8"))(_piece_bytes)}),
    ("an English that is not UTF-8 is accepted", lambda: {"is_utf8": lambda raw: True}),
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
