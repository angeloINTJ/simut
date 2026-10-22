#!/usr/bin/env python3
"""Fails the build when a .lng @DICT does not have exactly TR_KEYS_COUNT lines.

DisplayManager_LangParser.cpp partitions @DICT positionally — line N is
LangKey N — and rejects the whole file if the count differs. A rejected pack
is not a visible error: setLanguage() falls back to English and the device
just stops speaking Portuguese, which is exactly the kind of silence that
costs a debugging session. Adding a LangKey without adding its line in both
packs now breaks the build with the count named.

Also refuses a key inserted anywhere but the end while packs are unchanged,
since that shifts every string after it without changing any line count.

And a line whose marker does not match the firmware's own: the panel puts a
number where a line says "{n}" (display/CountInText.h), so a pack line that
drops the marker shows no number, or a stale one written into the text, which
is how "PIN too short (min 4)" said 4 under any policy until 2026-10-04.

And a @HELPWIRE ahead of @HELP: the alpha finds its console help by the prefix
"@HELP" (DisplayManager_Alpha.cpp), so a radio alpha would print the wired
build's help, `ip` and no `ap`, from such a pack (2026-10-13).

And the Angulo's own section, @ANGULO (S10c, B2a): the touch panel of the Angulo driver draws UA_TXT( "<English>" ) and finds the text of the
language in this section, one line each, "<8 hex digits: FNV-1a 32 of the English> <the text>" (src/ui/angulo/Textos.h). The gate holds, for
every pack: each line well formed and no hash twice (the device refuses the whole section otherwise and every text shows its English); a line
for every UA_TXT( ) literal under src/ui/angulo, and none for a hash that matches no literal (an orphan: the English changed); no hash shared
by two literals; the section no larger than UA_TEXTOS_MAX, read from Textos.h; and, as for every section, before @WEBDICT. The shipped packs
have no @ANGULO yet and the sources no UA_TXT( ) literal, so the check passes with nothing to compare (2026-10-22): the conversion (B3 to
B5) fills both sides, and the pack's size then meets LANG_FILE_MAX (the step that crosses it raises it).

The English of a UA_TXT( ) literal is the compiler's bytes (S10c, B2a-b): the escapes of every piece are decoded to bytes, the pieces' bytes joined
and the join read as UTF-8, because that is what the device hashes ("a\\xE2\\x80" "\\xA6" is a and an ellipsis). An English that is not valid UTF-8 is
refused where it is written, once for the whole tree: a pack is a UTF-8 file and has no line for those bytes.
"""
import json
import os
import re
import sys
from pathlib import Path

# Same dual mode as check_channels.py: PlatformIO exec's a pre: script without
# __file__, so the project root comes from the injected SCons env there and
# from the script's own location when run by hand.
try:
    Import("env")  # noqa: F821 — provided by SCons
    ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    ROOT = Path(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

HEADER = ROOT / "src" / "DisplayManager.h"
I18N = ROOT / "src" / "DisplayManager_i18n.cpp"
PARSER = ROOT / "src" / "DisplayManager_LangParser.cpp"
PACKS = sorted((ROOT / "data" / "lang").glob("*.lng"))
ANGULO_DIR = ROOT / "src" / "ui" / "angulo"
TEXTOS_H = ANGULO_DIR / "Textos.h"

# A pack over a ceiling is the same invisible failure as a short one:
# loadLangFile() returns false, setLanguage() reverts to English, and nothing
# says so. Two ceilings, both read from the parser source: LANG_FILE_MAX bounds
# the file on flash, LANG_DICT_MAX the @DICT — the one section that is read
# into RAM, and the one allocation a pack keeps for the whole uptime. A warning
# band gives notice before a routine addition tips either over the edge in a
# commit that "just adds a string".
CEIL_WARN_FRAC = 0.95


def lang_limits():
    """(LANG_FILE_MAX, LANG_DICT_MAX) read from the parser source, so this
    gate can never drift from what loadLangFile() actually enforces."""
    src = PARSER.read_text(encoding="utf-8")
    out = []
    for name in ("LANG_FILE_MAX", "LANG_DICT_MAX"):
        m = re.search(name + r"\s*=\s*(\d+)", src)
        if not m:
            raise SystemExit(f"check_lang_packs: {name} not found in "
                             "DisplayManager_LangParser.cpp")
        out.append(int(m.group(1)))
    return tuple(out)


BODY_SECTIONS = {b"ANGULO", b"DICT", b"HELP", b"HELPWIRE", b"LICENSE", b"LOGCODES", b"TRL",
                 b"WEBDICT"}


def help_order_problem(data):
    """The name of the directive a radio alpha would take for @HELP, or None.

    The alpha reads its help from the first line that starts with "@HELP" —
    a prefix, not a name (DisplayManager_Alpha.cpp) — so that line must be
    @HELP itself. A wired build's @HELPWIRE (2026-10-13) goes after it; the
    other readers (src/LangPackIndex.h) match the whole name and do not care
    about the order."""
    for ln in data.split(b"\n"):
        if ln.startswith(b"@HELP"):
            name = re.split(rb"[ \t\r]", ln[1:], maxsplit=1)[0]
            return None if name == b"HELP" else name.decode("utf-8", "replace")
    return None


def pack_index(data):
    """({section: (start, end)}, tail_directives) for the bytes of a pack.

    The rules of src/LangPackIndex.h, which is what loadLangFile() seeks by:
    a directive is a line whose first byte is '@', its name runs to the first
    space, tab or CR, and a body runs from the line after its directive to the
    next directive of any kind, or to the end of the file. The last occurrence
    of a section wins. tail_directives lists every directive found after the
    @WEBDICT marker line: the device rejects those packs, because the blob
    must be the file's suffix."""
    idx, tail = {}, []
    cur, in_wd, off = None, False, 0
    lines = data.split(b"\n")
    for i, ln in enumerate(lines):
        nl = 1 if i < len(lines) - 1 else 0
        if ln.startswith(b"@"):
            if in_wd:
                tail.append(ln.rstrip(b"\r").decode("utf-8", "replace"))
            if cur is not None:
                idx[cur] = (idx[cur][0], off)
                cur = None
            name = re.split(rb"[ \t\r]", ln[1:], maxsplit=1)[0]
            if name in BODY_SECTIONS:
                body = off + len(ln) + nl
                cur = name.decode()
                idx[cur] = (body, body)
                in_wd = in_wd or name == b"WEBDICT"
        off += len(ln) + nl
    if cur is not None:
        idx[cur] = (idx[cur][0], len(data))
    return idx, tail


# The License screen opens with five strings of the pack's @WEBDICT, the
# sentences the /license page shows, each followed by the page's paragraph
# break; the firmware composes them into one 768 B buffer
# (src/DisplayManager.cpp, _licOpening / kLicOpeningSep). A pack whose opening
# does not fit shows the English one instead, silently — so it fails here.
LIC_OPENING_KEYS = ["lic_sub", "lic_summary_title", "lic_summary", "lic_summary_note",
                    "lic_legal_note"]
LIC_OPENING_SEPS = ["\n\n", "\n", "\n\n", "\n\n", "\n\n"]
LIC_OPENING_CAP = 768


def opening_bytes(path):
    """UTF-8 bytes of the License screen's opening for this pack, or None when
    the pack has no @WEBDICT or lacks one of the five keys."""
    data = path.read_bytes()
    idx, _ = pack_index(data)
    if "WEBDICT" not in idx:
        return None
    a, b = idx["WEBDICT"]
    try:
        wd = json.loads(data[a:b])
    except ValueError:
        return None
    if any(k not in wd for k in LIC_OPENING_KEYS):
        return None
    return sum(len(wd[k].encode("utf-8")) + len(sep)
               for k, sep in zip(LIC_OPENING_KEYS, LIC_OPENING_SEPS))


def enum_keys():
    src = HEADER.read_text(encoding="utf-8")
    m = re.search(r"enum\s+LangKey\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("check_lang_packs: enum LangKey not found in DisplayManager.h")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    keys = [k.strip() for k in body.split(",") if k.strip()]
    if keys[-1] != "TR_KEYS_COUNT":
        raise SystemExit("check_lang_packs: TR_KEYS_COUNT must be the last enumerator")
    return keys[:-1]


def english_dict():
    """DICTIONARY_EN, the firmware's own lines, in LangKey order. An entry is
    every literal up to the next comma, so adjacent literals join as the
    compiler joins them. The markers are all this is read for, so escapes are
    left as they are written."""
    src = I18N.read_text(encoding="utf-8")
    m = re.search(r"DICTIONARY_EN\[TR_KEYS_COUNT\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("check_lang_packs: DICTIONARY_EN not found in DisplayManager_i18n.cpp")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    out, cur = [], None
    for tok in re.finditer(r'"((?:[^"\\]|\\.)*)"|,', body):
        if tok.group(0) == ",":
            if cur is not None:
                out.append(cur)
            cur = None
        else:
            cur = (cur or "") + tok.group(1)
    if cur is not None:
        out.append(cur)
    return out


MARKER = re.compile(r"\{[a-z]+\}")


def check_markers(pack, lines, english, keys):
    ok = True
    for i, key in enumerate(keys):
        want, have = sorted(MARKER.findall(english[i])), sorted(MARKER.findall(lines[i]))
        if want != have:
            print(f"[lang-packs] FAIL {pack.name}: {key} carries {have or 'no marker'}, "
                  f"the firmware's line {want or 'none'} — the panel puts the number "
                  f"where the marker is, so this line would show none, or a stale one",
                  file=sys.stderr)
            ok = False
    return ok


def dict_lines(path):
    """Lines of the @DICT block.

    Anchored on lines that START with the directive: the es-ES header comment
    contains the text "@DICT/@HELP/@LICENSE", and a plain substring search
    lands there and reports a one-line dictionary.
    """
    lines = path.read_text(encoding="utf-8").split("\n")
    try:
        start = next(i for i, ln in enumerate(lines) if ln.strip() == "@DICT") + 1
    except StopIteration:
        raise SystemExit(f"check_lang_packs: {path.name} has no @DICT section")
    out = []
    for off, ln in enumerate(lines[start:]):
        if ln.startswith("@"):
            break
        out.append(ln)
    # The parser counts line BOUNDARIES, so an empty line is a string — an
    # empty one. Both packs used to end the block with a blank line, which is
    # how a stale 109-key pack still satisfied a 111-key firmware: it filled
    # the two new slots with "" and drew blank labels instead of falling back
    # to English. Blanks are rejected outright rather than tolerated at the end.
    blanks = [i for i, ln in enumerate(out) if ln.strip() == ""]
    while out and out[-1].strip() == "":
        out.pop()
    return out, blanks


def section(path, name):
    """Body lines of @<name>, or [] when the section is absent."""
    lines = path.read_text(encoding="utf-8").split("\n")
    try:
        start = next(i for i, ln in enumerate(lines)
                     if ln.strip() == "@" + name) + 1
    except StopIteration:
        return []
    out = []
    for ln in lines[start:]:
        if ln.startswith("@"):
            break
        out.append(ln)
    return out


def fnv1a32(s):
    """FNV-1a 32 of the bytes of `s` (a str is taken as UTF-8, which is what a pack carries; bytes are hashed as they are)."""
    h = 0x811C9DC5
    for b in (s if isinstance(s, bytes) else s.encode("utf-8")):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


# A C++ string literal, plus the adjacent-literal concatenation the sources use.
_STR = r'"(?:[^"\\]|\\.)*"'
_TRL = re.compile(r'TRL\s*\(\s*((?:' + _STR + r')(?:\s*' + _STR + r')*)\s*\)', re.S)


def _unescape(lit):
    out = []
    for p in re.findall(_STR, lit, re.S):
        b = p[1:-1]
        for a, z in (('\\"', '"'), ("\\n", "\n"), ("\\t", "\t"),
                     ("\\r", "\r"), ("\\\\", "\\")):
            b = b.replace(a, z)
        out.append(b)
    return "".join(out)


def trl_literals():
    """{english: first "file:line"} for every TRL("...") under src/."""
    found = {}
    for f in sorted((ROOT / "src").rglob("*")):
        if f.suffix not in (".cpp", ".h", ".hpp", ".c"):
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in _TRL.finditer(text):
            en = _unescape(m.group(1))
            if en:
                found.setdefault(en, f"{f.relative_to(ROOT)}:"
                                     f"{text.count(chr(10), 0, m.start()) + 1}")
    return found


def web_keys():
    """({key: where}, {runtime_prefix}) for every translation the browser asks for.

    WebUI.h is the source of every page, so it is normally the whole answer.
    data/web/*.html.gz is scanned as well because a test env may hold a page
    diet there (custom_fs_pages, see tools/build_webui_gz.py): those files are
    generated from the same WebUI.h and read the same @WEBDICT, and scanning
    them costs nothing when the directory is empty, which is what a shipping
    layout leaves behind.
    """
    import gzip
    srcs = [("WebUI.h", (ROOT / "WebUI.h").read_text(encoding="utf-8",
                                                     errors="replace"))]
    for gz in sorted((ROOT / "data" / "web").glob("*.html.gz")):
        srcs.append((gz.name, gzip.decompress(gz.read_bytes())
                                  .decode("utf-8", "replace")))
    keys, prefixes = {}, set()
    for name, text in srcs:
        for pat in (r'data-i18n[a-z-]*=\\?["\']([A-Za-z0-9_]+)\\?["\']',
                    r'\bt\(\s*[\'"]([A-Za-z0-9_]+)[\'"]\s*[,)]'):
            for m in re.finditer(pat, text):
                keys.setdefault(m.group(1), name)
        # t('ch_' + id): the key is assembled at runtime, so the prefix is
        # all the check can know about it.
        for m in re.finditer(r'\bt\(\s*[\'"]([A-Za-z0-9_]+_)[\'"]\s*\+', text):
            prefixes.add(m.group(1))
    return keys, prefixes


def check_trl(pack, live):
    """@TRL is keyed by a hash of the ENGLISH string, so editing the English
    silently orphans the translation — the lookup misses and the line comes
    out in English with nothing logged."""
    have = {}
    for ln in section(pack, "TRL"):
        if not ln.strip():
            continue
        h, _, txt = ln.partition(" ")
        try:
            have[int(h, 16)] = txt
        except ValueError:
            print(f"[lang-packs] FAIL {pack.name}: malformed @TRL line {ln!r}",
                  file=sys.stderr)
            return False
    if not have:
        return True  # a pack may legitimately ship without @TRL
    live_hashes = {fnv1a32(t) for t in live}
    missing = [(t, w) for t, w in live.items() if fnv1a32(t) not in have]
    orphan = [h for h in have if h not in live_hashes]
    for t, where in sorted(missing):
        print(f"[lang-packs] FAIL {pack.name}: @TRL has no {fnv1a32(t):08x} for "
              f"{t[:60]!r} ({where}) — that line logs in English", file=sys.stderr)
    for h in sorted(orphan):
        print(f"[lang-packs] WARN {pack.name}: @TRL {h:08x} matches no live "
              f"TRL() — the English changed and {have[h][:48]!r} is dead weight",
              file=sys.stderr)
    return not missing


def textos_max():
    """UA_TEXTOS_MAX, the most bytes an @ANGULO section may hold, read from the Angulo kit's header so that this gate can never drift from what
    textosCarregar( ) enforces. A header that cannot be read stops the gate: a gate that cannot look must not pass."""
    try:
        src = TEXTOS_H.read_text(encoding="utf-8")
    except OSError as e:
        raise SystemExit(f"check_lang_packs: cannot read {TEXTOS_H}: {e}")
    m = re.search(r"^constexpr\s+size_t\s+UA_TEXTOS_MAX\s*=\s*(\d+)\s*;", src, re.M)
    if not m:
        raise SystemExit("check_lang_packs: UA_TEXTOS_MAX not found in src/ui/angulo/Textos.h")
    return int(m.group(1))


def _angulo_scan():
    """({english: first "file:line"}, [("file:line", shown)]) for every UA_TXT( "..." ) under src/ui/angulo, read as the compiler reads them and as
    check_ui_texts.py does (one lexer: comments removed, escapes decoded to bytes, adjacent literals one text, a raw string verbatim). The first holds
    the English that is valid UTF-8, keyed by its text (the hash of the text's UTF-8 is the hash of the compiler's bytes); the second the English that
    is not, which no pack can carry a line for, with the text as far as it reads. A literal with no letter is not a text (check_ui_texts.py's rule) and
    is in neither. Only a file that names UA_TXT is lexed, which keeps this cheap: it runs in every build."""
    tools = str(ROOT / "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)   # PlatformIO exec's this file with no __file__, and its own directory is not on the path
    import check_ui_texts as cut
    found, refused = {}, []
    for f in sorted(ANGULO_DIR.rglob("*")):
        if f.suffix not in cut.EXTS or not f.is_file():
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        if cut.MARK not in text:
            continue
        rel = "src/ui/angulo/" + f.relative_to(ANGULO_DIR).as_posix()
        for lit in cut.texts_of(rel, text):
            if not lit.converted:
                continue
            where = f"{rel}:{lit.line}"
            if cut.is_utf8(lit.raw):
                found.setdefault(lit.text, where)
            else:
                refused.append((where, lit.text))
    return found, refused


def angulo_literals():
    """{english: first "file:line"} for the UA_TXT( "..." ) English that is valid UTF-8 (see _angulo_scan)."""
    return _angulo_scan()[0]


def angulo_bad_literals():
    """[("file:line", shown)] for the UA_TXT( "..." ) English that is not valid UTF-8 (see _angulo_scan)."""
    return _angulo_scan()[1]


def angulo_collisions(live):
    """{hash: [(english, where), ...]} for the hashes two literals share: the lookup could not tell them apart."""
    by_hash = {}
    for en, where in live.items():
        by_hash.setdefault(fnv1a32(en), []).append((en, where))
    return {h: v for h, v in by_hash.items() if len(v) > 1}


_ANGULO_HEAD = re.compile(rb"[0-9A-Fa-f]{8} ")


def angulo_entries(body):
    """({hash: text}, [problem]) for the bytes of an @ANGULO body, by the rules textosCarregar( ) applies (src/ui/angulo/Textos.cpp): lines end at
    '\\n' with a '\\r' before it allowed, a blank line is skipped, any other must be 8 hex digits, one space and a text that shows a character
    (a space or a tab does not) and holds no NUL; a hash may come once. This also wants the text to be UTF-8, which the device does not test:
    the Angulo's fonts decode it, and a byte that is not UTF-8 would draw as a wrong glyph."""
    have, problems = {}, []
    for no, ln in enumerate(body.split(b"\n"), 1):
        ln = ln[:-1] if ln.endswith(b"\r") else ln
        if not ln:
            continue
        txt = ln[9:]
        if not _ANGULO_HEAD.match(ln) or not txt.strip(b" \t") or b"\0" in txt:
            problems.append(f"line {no} is not \"<8 hex digits> <text>\" with a visible text and no NUL: {ln[:40].decode('utf-8', 'replace')!r}")
            continue
        try:
            text = txt.decode("utf-8")
        except UnicodeDecodeError:
            problems.append(f"line {no}: the text is not UTF-8: {ln[:40].decode('utf-8', 'replace')!r}")
            continue
        h = int(ln[:8], 16)
        if h in have:
            problems.append(f"line {no}: hash {h:08x} is on an earlier line too")
            continue
        have[h] = text
    return have, problems


def check_angulo(pack, data, idx, live, limit, clashes):
    """The @ANGULO section of one pack against the UA_TXT( ) literals of the Angulo (see the module docstring). Every refusal is a FAIL: the device
    refuses the whole section for a malformed line, a repeated hash or a size over the bound, and shows the English for any text it lacks, and
    none of these is visible on the glass until someone reads the other language."""
    ok = True
    twice = sum(1 for ln in data.split(b"\n") if re.match(rb"@ANGULO(?:[ \t\r]|$)", ln)) > 1
    a, b = idx.get("ANGULO", (0, 0))
    present = "ANGULO" in idx
    if twice:
        print(f"[lang-packs] FAIL {pack.name}: @ANGULO appears more than once — the device keeps the last and the others are dead weight",
              file=sys.stderr)
        ok = False
    if present and b - a > limit:
        print(f"[lang-packs] FAIL {pack.name}: @ANGULO is {b - a} B, over UA_TEXTOS_MAX ({limit}) — the device refuses the whole section and "
              f"every Angulo text shows its English", file=sys.stderr)
        ok = False
    elif present and b - a >= int(limit * CEIL_WARN_FRAC):
        print(f"[lang-packs] WARN {pack.name}: @ANGULO is {b - a} B, {(b - a) * 100 // limit}% of UA_TEXTOS_MAX ({limit - (b - a)} B left)",
              file=sys.stderr)
    have, problems = angulo_entries(data[a:b]) if present else ({}, [])
    for pr in problems:
        print(f"[lang-packs] FAIL {pack.name}: @ANGULO {pr} — the device refuses the whole section and every Angulo text shows its English",
              file=sys.stderr)
        ok = False
    if problems:
        return False
    live_hashes = {fnv1a32(t): t for t in live}
    missing = sorted((where, t) for t, where in live.items() if fnv1a32(t) not in have)
    for where, t in missing[:20]:
        print(f"[lang-packs] FAIL {pack.name}: @ANGULO has no {fnv1a32(t):08x} for {t[:60]!r} ({where}) — that text shows in English",
              file=sys.stderr)
    if len(missing) > 20:
        print(f"[lang-packs] FAIL {pack.name}: ... and {len(missing) - 20} more UA_TXT( ) literals without a line in @ANGULO", file=sys.stderr)
    orphans = sorted(h for h in have if h not in live_hashes)
    for h in orphans:
        print(f"[lang-packs] FAIL {pack.name}: @ANGULO {h:08x} matches no UA_TXT( ) literal — the English changed or the text is gone, and "
              f"{have[h][:48]!r} is dead weight against UA_TEXTOS_MAX", file=sys.stderr)
    if missing or orphans:
        ok = False
    for h, who in sorted(clashes.items()):
        print(f"[lang-packs] FAIL {pack.name}: UA_TXT( ) literals share the hash {h:08x}: "
              + " and ".join(f"{t[:40]!r} ({w})" for t, w in who) + " — the lookup could not tell them apart", file=sys.stderr)
        ok = False
    if ok:
        print(f"[lang-packs] OK {pack.name}: @ANGULO " + (f"{len(have)} texts, {b - a} B ({(b - a) * 100 // limit}% of UA_TEXTOS_MAX)"
              if present else "absent") + f"; {len(live)} UA_TXT( ) literals under src/ui/angulo"
              + ("" if live or present else " (nothing to compare yet)"))
    return ok


def check_webdict(pack, keys, prefixes):
    body = "\n".join(section(pack, "WEBDICT")).strip()
    if not body:
        return True
    try:
        wd = json.loads(body)
    except json.JSONDecodeError as e:
        print(f"[lang-packs] FAIL {pack.name}: @WEBDICT is not valid JSON ({e}) "
              f"— GET /api/lang serves it verbatim and the page keeps English",
              file=sys.stderr)
        return False
    missing = sorted(k for k in keys if k not in wd)
    dead = sorted(k for k in wd if k not in keys
                  and not any(k.startswith(p) for p in prefixes))
    for k in missing:
        print(f"[lang-packs] FAIL {pack.name}: @WEBDICT has no {k!r} "
              f"(asked for by {keys[k]}) — that label stays English",
              file=sys.stderr)
    if dead:
        print(f"[lang-packs] WARN {pack.name}: {len(dead)} @WEBDICT key(s) no "
              f"consumer asks for, spending bytes against the ceiling: "
              f"{', '.join(dead[:12])}{' ...' if len(dead) > 12 else ''}",
              file=sys.stderr)
    return not missing


def main():
    keys = enum_keys()
    want = len(keys)
    ceil, dict_ceil = lang_limits()
    warn_at = int(ceil * CEIL_WARN_FRAC)
    dict_warn_at = int(dict_ceil * CEIL_WARN_FRAC)
    trl_live = trl_literals()
    wkeys, wprefixes = web_keys()
    ua_max = textos_max()
    ua_live, ua_bad = _angulo_scan()
    ua_clashes = angulo_collisions(ua_live)
    english = english_dict()
    if len(english) != want:
        raise SystemExit(f"check_lang_packs: read {len(english)} lines out of DICTIONARY_EN, "
                         f"TR_KEYS_COUNT is {want}: the reader is wrong, not the table")
    failed = False
    for where, shown in ua_bad:
        print(f"[lang-packs] FAIL {where}: the English {shown[:60]!r} of a UA_TXT( ) is not valid UTF-8 — the device hashes the compiler's bytes and a pack, "
              f"a UTF-8 file, cannot carry a line for them; write the character as its UTF-8 bytes (\\xE2\\x80\\xA6 is one ellipsis)", file=sys.stderr)
        failed = True
    for pack in PACKS:
        size = pack.stat().st_size
        raw = pack.read_bytes()
        idx, tail = pack_index(raw)
        dict_bytes = idx["DICT"][1] - idx["DICT"][0] if "DICT" in idx else 0
        if size > ceil:
            print(f"[lang-packs] FAIL {pack.name}: {size} B exceeds LANG_FILE_MAX "
                  f"({ceil}) — loadLangFile() rejects it and the UI silently "
                  f"reverts to English", file=sys.stderr)
            failed = True
        elif size >= warn_at:
            print(f"[lang-packs] WARN {pack.name}: {size} B is {size * 100 // ceil}% "
                  f"of the {ceil} B file ceiling ({ceil - size} B left) — the next "
                  f"addition may tip it over, and the failure is invisible at runtime",
                  file=sys.stderr)
        if tail:
            print(f"[lang-packs] FAIL {pack.name}: section(s) {tail} appear AFTER "
                  f"@WEBDICT — the blob must be the file's suffix; the device "
                  f"rejects such a pack and the UI silently reverts to English",
                  file=sys.stderr)
            failed = True
        first_help = help_order_problem(pack.read_bytes())
        if first_help:
            print(f"[lang-packs] FAIL {pack.name}: @{first_help} comes before @HELP — "
                  f"the alpha finds its help by the prefix \"@HELP\", so a radio "
                  f"alpha would print this section instead; move it after @HELP",
                  file=sys.stderr)
            failed = True
        if dict_bytes > dict_ceil:
            print(f"[lang-packs] FAIL {pack.name}: @DICT is {dict_bytes} B, over "
                  f"LANG_DICT_MAX ({dict_ceil}) — the dictionary is the part of a "
                  f"pack that lives on the heap for the whole uptime; "
                  f"loadLangFile() rejects it", file=sys.stderr)
            failed = True
        elif dict_bytes >= dict_warn_at:
            print(f"[lang-packs] WARN {pack.name}: @DICT is {dict_bytes} B, "
                  f"{dict_bytes * 100 // dict_ceil}% of the {dict_ceil} B RAM "
                  f"ceiling ({dict_ceil - dict_bytes} B left)", file=sys.stderr)
        else:
            print(f"[lang-packs] OK {pack.name}: @DICT {dict_bytes} B "
                  f"({dict_bytes * 100 // dict_ceil}% of RAM ceiling), file {size} B "
                  f"({size * 100 // ceil}% of file ceiling)"
                  + ("" if "WEBDICT" in idx else " — no @WEBDICT"))
        opening = opening_bytes(pack)
        if opening is not None and opening + 1 > LIC_OPENING_CAP:
            print(f"[lang-packs] FAIL {pack.name}: the License screen's opening is "
                  f"{opening} B, over the {LIC_OPENING_CAP - 1} B the firmware holds — the "
                  f"panel would show the English one instead", file=sys.stderr)
            failed = True
        lines, blanks = dict_lines(pack)
        if blanks:
            print(f"[lang-packs] FAIL {pack.name}: blank line(s) inside @DICT at "
                  f"offset(s) {blanks} — the parser reads those as empty strings "
                  f"and every key after one shifts by a slot", file=sys.stderr)
            failed = True
            continue
        if len(lines) != want:
            print(f"[lang-packs] FAIL {pack.name}: @DICT has {len(lines)} lines, "
                  f"TR_KEYS_COUNT is {want} "
                  f"({'missing ' + str(want - len(lines)) if len(lines) < want else 'extra ' + str(len(lines) - want)})",
                  file=sys.stderr)
            failed = True
        else:
            print(f"[lang-packs] OK {pack.name}: {len(lines)} strings")
            if not check_markers(pack, lines, english, keys):
                failed = True
        # @DICT is the only section the count check can see. @TRL and
        # @WEBDICT rot differently — a key goes missing and just that one
        # string falls back to English, which no count catches.
        if not check_trl(pack, trl_live):
            failed = True
        if not check_webdict(pack, wkeys, wprefixes):
            failed = True
        if not check_angulo(pack, raw, idx, ua_live, ua_max, ua_clashes):
            failed = True
    if failed:
        print("[lang-packs] None of these failures is visible at runtime: a short "
              "pack is rejected whole and the UI reverts to English, a blank line "
              "makes the keys after it draw the wrong string or nothing at all, "
              "and a missing @TRL/@WEBDICT/@ANGULO entry silently leaves that one "
              "line in English.", file=sys.stderr)
        raise SystemExit(1)


if not os.environ.get("LANG_GATE_NO_RUN"):
    main()

# PlatformIO imports this as a pre: script; there is no Import("env") use here
# because the check needs nothing from the build environment. LANG_GATE_NO_RUN
# exists for tools/test_lang_gate.py, which imports the functions above and
# runs positive controls against synthetic packs without tripping the gate.
