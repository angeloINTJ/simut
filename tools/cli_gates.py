#!/usr/bin/env python3
"""cli_gates.py — which lines of a C++ source a given image compiles, for the gates
that read the console's parser as text.

tools/check_cli_help.py and tools/check_air_consistency.py decide what an image's
console accepts by reading src/CommandParser.cpp. They used to know two guards by
the start of a line (`#if SIMUT_CLI_FULL`, `#if SIMUT_AIR`); a wired build
(SIMUT_NET_LAN, 2026-10-13) adds `#if SIMUT_NET_WIFI` and `#if SIMUT_CLI_FULL ||
SIMUT_NET_LAN`, so they evaluate the expression instead. Only four macros are known.
Anything else (another macro, `defined`, a comparison) is left as it is: a gate that
keeps a line it cannot judge errs on the side of checking it.

    eval_gate("SIMUT_CLI_FULL || SIMUT_NET_LAN", {"SIMUT_NET_LAN": 1})  -> True
    strip_gated(source, {"SIMUT_CLI_FULL": 0, "SIMUT_NET_LAN": 1})      -> source, the
        branches the image does not compile blanked, line count unchanged

Tests: tools/test_cli_gates.py.
"""
import re

KNOWN = ("SIMUT_CLI_FULL", "SIMUT_AIR", "SIMUT_NET_LAN", "SIMUT_NET_WIFI")

_TOKEN = re.compile(r"\s*(?:(\d+)|([A-Za-z_]\w*)|(&&|\|\||!|\(|\)))")
_DIRECTIVE = re.compile(r"^\s*#\s*(if|elif|else|endif|ifdef|ifndef)\b(.*)$")


def _tokens(expr):
    """The expression's tokens, or None when it holds anything else."""
    expr = re.sub(r"/\*.*?\*/", " ", expr).split("//", 1)[0].strip()
    out, pos = [], 0
    while pos < len(expr):
        m = _TOKEN.match(expr, pos)
        if not m or m.end() == pos:
            return None
        num, name, op = m.groups()
        out.append(("num", int(num)) if num is not None else
                   ("name", name) if name is not None else ("op", op))
        pos = m.end()
    return out or None


def eval_gate(expr, flags):
    """True or False for an `#if` expression over the known macros (C precedence:
    `!` over `&&` over `||`), None for anything it cannot judge. SIMUT_NET_WIFI is
    !SIMUT_NET_LAN unless `flags` names it; a known macro `flags` lacks is 0."""
    try:
        toks = _tokens(expr or "")
        if toks is None:
            return None
        values = {m: int(flags.get(m, 0)) for m in KNOWN if m != "SIMUT_NET_WIFI"}
        values["SIMUT_NET_WIFI"] = (int(flags["SIMUT_NET_WIFI"]) if "SIMUT_NET_WIFI" in flags
                                    else int(not values["SIMUT_NET_LAN"]))
    except (TypeError, ValueError):
        return None
    pos = 0

    def peek():
        return toks[pos] if pos < len(toks) else None

    def take():
        nonlocal pos
        pos += 1
        return toks[pos - 1]

    def primary():
        t = peek()
        if t == ("op", "!"):
            take()
            return not primary()
        if t == ("op", "("):
            take()
            v = either()
            if peek() != ("op", ")"):
                raise ValueError("unbalanced")
            take()
            return v
        if t is not None and t[0] == "num":
            return take()[1] != 0
        if t is not None and t[0] == "name" and t[1] in values:
            return values[take()[1]] != 0
        raise ValueError("not a known operand")

    def both():
        v = primary()
        while peek() == ("op", "&&"):
            take()
            v = primary() and v
        return v

    def either():
        v = both()
        while peek() == ("op", "||"):
            take()
            v = both() or v
        return v

    try:
        v = either()
    except ValueError:
        return None
    return bool(v) if pos == len(toks) else None


def strip_gated(src, flags):
    """`src` with the branches the image does not compile, and the directives of every
    block it could judge, turned into empty lines. A block whose `#if` or any `#elif`
    eval_gate( ) cannot judge, and every #ifdef/#ifndef block, stays as written; the
    blocks inside it are still judged. A block inside a removed branch goes with it.
    An unterminated block closes at the end; an `#endif` with no block stays."""
    lines = src.split("\n")
    blocks, stack = [], []
    for i, line in enumerate(lines):
        m = _DIRECTIVE.match(line)
        if not m:
            continue
        word, rest = m.group(1), m.group(2)
        if word in ("if", "ifdef", "ifndef"):
            stack.append({"branches": [(i, word, rest)], "end": None})
            blocks.append(stack[-1])
        elif word in ("elif", "else") and stack:
            stack[-1]["branches"].append((i, word, rest))
        elif word == "endif" and stack:
            stack.pop()["end"] = i
    for b in stack:
        b["end"] = len(lines)

    blank = set()
    for b in blocks:
        br = b["branches"]
        if br[0][1] != "if":
            continue
        verdicts = [eval_gate(rest, flags) if word in ("if", "elif") else True
                    for _, word, rest in br]
        if any(v is None for v in verdicts):
            continue
        chosen = next((k for k, v in enumerate(verdicts) if v), None)
        for k, (i, _, _) in enumerate(br):
            blank.add(i)
            stop = br[k + 1][0] if k + 1 < len(br) else b["end"]
            if k != chosen:
                blank.update(range(i + 1, stop))
        if b["end"] < len(lines):
            blank.add(b["end"])
    return "\n".join("" if i in blank else line for i, line in enumerate(lines))
