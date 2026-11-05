#!/usr/bin/env python3
"""
Source gate for the alpha glass's seqlock (src/display/LcdGlass.h): where its four fences stand.

WHY THIS EXISTS
---------------
Core 1 publishes what the alpha's 16x2 shows and Core 0 reads it (docs/CONCURRENCY.md, "The alpha's glass"). The shadow is a
seqlock, and a seqlock is only as good as its barriers: the writer fences after the odd count and before the even one, the reader
after its first look at the count and before its second. On the host the fence is a no-op, so a single-threaded test cannot see one:
the review of step 1 (2026-11-04) took each of the four out in turn and all 408 native cases still passed. The cases hold what a
host can see (the order of the stores, a publish that dies, a reader overtaken at every byte); this holds the one thing it cannot,
on the text of the header, and bites itself with mutants (--selftest).

  G1  fence( ) is one definition: `__atomic_thread_fence( __ATOMIC_SEQ_CST )`, the full barrier the header says is `dmb ish`.
  G2  the shadow's members are the ones this check knows: the count, `volatile uint32_t seq`, and the data it guards (SHARED).
      A new member has to be added here, and the rules below read again with it in mind.
  G3  open( ), the writer's first half: ONE store to the count, then ONE fence( ), then the data it touches (blitted), every one
      of them unconditional.
  G4  close( ), the writer's second half: ONE fence( ), then ONE store to the count, every one unconditional; no data.
  G5  putBlank( ), putFrame( ), putGlyph( ): open( probe ) is the first statement and close( probe ) the last, once each, and
      nothing in between touches the count or a fence( ): the data stores sit between the odd store and the even one.
  G6  read( ), the reader: the count is looked at before the first read of the data.
  G7  read( ): an unconditional fence( ) between that first look and the first read of the data.
  G8  read( ): an unconditional fence( ) between the last read of the data and the second look at the count.
  G9  read( ): the count is looked at again after the data.

A fence( ) counts only as the header spells it, a statement of its own at the level of the code it orders. A comment or a string
that says fence( ) is not one, and one under an `if` (a block, or a statement with no brace) is a condition and not a barrier. The
check reads the order of the text, and the compiler keeps it only because the fence is a compiler barrier too; that it is `dmb ish`
on the M0+ and the M33 was read from the disassembly (docs/CONCURRENCY.md).

Exit code: 0 clean, 1 findings, 2 cannot read the header.
Run: python3 tools/check_glass_seqlock.py [--header FILE] [--verbose]
     python3 tools/check_glass_seqlock.py --selftest     the gate's own cases: the clean header, the mutants, the command line

Project: SIMUT
License: MIT
"""
import argparse
import bisect
import collections
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, 'src', 'display', 'LcdGlass.h')
TAG = '[glass-seqlock]'


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


# ── reading the header ─────────────────────────────────────────────────────────────────────────────────────────────

# The data the count guards: the members of LcdGlassShadow other than the count (G2 holds the list to the struct).
SHARED = ('ddram', 'cgram', 'blitMs', 'blitted')
ASSIGN = ('=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=')
FENCE_BODY = '__atomic_thread_fence ( __ATOMIC_SEQ_CST ) ;'
OPEN_CALL = ['open', '(', 'probe', ')', ';']
CLOSE_CALL = ['close', '(', 'probe', ')', ';']

TOKEN = re.compile(r"[A-Za-z_]\w*|\d[\w.]*|'(?:\\.|[^'\\\n])*'|\"(?:\\.|[^\"\\\n])*\"|->|::|==|!=|<=|>=|&&|\|\||\+\+|--|<<=|>>=|<<|>>|[-+*/%&|^]=|\S")

# text: the token; line: where it stands; depth: how many braces enclose it (a `}` has the depth of its `{`); start: it opens a statement
# (the one before it ended with `;`, `{` or `}`), which a statement under an `if` or an `else` with no braces does not.
Tok = collections.namedtuple('Tok', 'text line depth start')
Ev = collections.namedtuple('Ev', 'kind tok idx')   # kind: LOAD or STORE of the count, FENCE, DATA


def blank_comments(src):
    """The source with every comment replaced by spaces, newlines kept (the findings quote line numbers). A comment mark inside a string
    or a character literal is not a comment, and a `'` between two characters of a number is a digit separator and not a literal."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
        elif c in '"\'' and not (c == "'" and i and src[i - 1].isalnum()):
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(src[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def tokenize(text):
    src = blank_comments(text)
    starts = [0] + [m.end() for m in re.finditer('\n', src)]
    toks, depth, prev = [], 0, ';'
    for m in TOKEN.finditer(src):
        t = m.group(0)
        if t == '}':
            depth -= 1
        toks.append(Tok(t, bisect.bisect_right(starts, m.start()), depth, prev in (';', '{', '}')))
        if t == '{':
            depth += 1
        prev = t
    return toks


def match(toks, i, opening, closing):
    """The index of the token that closes the one at i, or None."""
    d = 0
    for j in range(i, len(toks)):
        if toks[j].text == opening:
            d += 1
        elif toks[j].text == closing:
            d -= 1
            if d == 0:
                return j
    return None


def struct_body(toks, name):
    """The tokens inside the braces of `struct name { ... }`, or None."""
    for i in range(len(toks) - 1):
        if toks[i].text == 'struct' and toks[i + 1].text == name:
            j = i + 2
            while j < len(toks) and toks[j].text not in ('{', ';'):
                j += 1
            if j < len(toks) and toks[j].text == '{':
                k = match(toks, j, '{', '}')
                if k is not None:
                    return toks[j + 1:k]
    return None


def function_body(toks, name):
    """(the tokens inside the braces of the definition `name( ... ) [const] { ... }`, the line of its name), or None. A call of the same
    name (`open( probe );`) is not a definition and is passed over."""
    for i in range(len(toks) - 1):
        if toks[i].text != name or toks[i + 1].text != '(':
            continue
        j = match(toks, i + 1, '(', ')')
        if j is None:
            continue
        k = j + 1
        if k < len(toks) and toks[k].text == 'const':
            k += 1
        if k < len(toks) and toks[k].text == '{':
            e = match(toks, k, '{', '}')
            if e is not None:
                return toks[k + 1:e], toks[i].line
    return None


def events(body):
    """The count's loads and stores, the fence( ) calls and the reads or writes of the data, in the order the text has them. What follows a
    `.`, `->` or `::` is another object's (`out.seq`, `probe.storeDone`), not the shadow's own."""
    out = []
    for i, t in enumerate(body):
        prev = body[i - 1].text if i else ''
        if prev in ('.', '->', '::'):
            continue
        nxt = body[i + 1].text if i + 1 < len(body) else ''
        nxt2 = body[i + 2].text if i + 2 < len(body) else ''
        if t.text == 'seq':
            store = nxt in ASSIGN or nxt in ('++', '--') or prev in ('++', '--')
            out.append(Ev('STORE' if store else 'LOAD', t, i))
        elif t.text == 'fence' and nxt == '(' and nxt2 == ')':
            out.append(Ev('FENCE', t, i))
        elif t.text in SHARED:
            out.append(Ev('DATA', t, i))
    return out


def members(S):
    """[(name, its words, its line)] for the data members at the top level of the struct: not a function, a constant or a type."""
    base = min((t.depth for t in S), default=0)
    res, cur = [], []
    for t in S:
        if t.depth == base and t.text == ':' and cur and cur[-1].text in ('public', 'private', 'protected'):
            cur = []                                     # an access label
            continue
        cur.append(t)
        if t.depth == base and t.text == '}':
            cur = []                                     # the end of a function definition
        elif t.depth == base and t.text == ';':
            words = [x.text for x in cur]
            line = cur[0].line
            cur = []
            if {'(', 'static', 'using', 'typedef', 'template'} & set(words):
                continue
            j = next((k for k, w in enumerate(words) if w in ('[', '=', '{', ';')), len(words))
            names = [w for w in words[:j] if re.fullmatch(r'[A-Za-z_]\w*', w)]
            if names:
                res.append((names[-1], words, line))
    return res


# ── the rules ──────────────────────────────────────────────────────────────────────────────────────────────────────

def g1_fence(S):
    r = function_body(S, 'fence')
    if r is None:
        return [('G1', 0, 'fence( ) is not defined in LcdGlassShadow: the check has no barrier to hold')]
    body, line = r
    spelled = ' '.join(t.text for t in body)
    if spelled != FENCE_BODY:
        return [('G1', line, f'fence( ) is `{spelled}`, not a sequentially consistent `{FENCE_BODY}`: only a full barrier is `dmb ish`')]
    return []


def g2_members(S):
    got = {name: (words, line) for name, words, line in members(S)}
    out = []
    for name in sorted(set(got) - {'seq'} - set(SHARED)):
        out.append(('G2', got[name][1], f'the shadow has a member `{name}` this check does not know: add it to SHARED in tools/check_glass_seqlock.py '
                                         f'and say where it is written and read'))
    for name in ['seq'] + list(SHARED):
        if name not in got:
            out.append(('G2', 0, f'the member `{name}` the check expects is not in LcdGlassShadow'))
    if 'seq' in got and 'volatile' not in got['seq'][0]:
        out.append(('G2', got['seq'][1], 'the count `seq` is not `volatile`: the fences order the data, and the count is what a loop on the other core re-reads'))
    return out


def g_writer(S, name, gid):
    """open( ): the odd store, the fence, the data. close( ): the fence, the even store."""
    r = function_body(S, name)
    if r is None:
        return [(gid, 0, f'{name}( ) is not defined in LcdGlassShadow: the check cannot find that half of the publish')]
    body, line = r
    ev = events(body)
    base = min((t.depth for t in body), default=0)
    stores = [e for e in ev if e.kind == 'STORE']
    fences = [e for e in ev if e.kind == 'FENCE']
    data = [e for e in ev if e.kind == 'DATA']
    odd = name == 'open'
    if len(stores) != 1:
        return [(gid, line, f'{name}( ) stores to the count {len(stores)} times, not once' +
                 (': it goes odd in ONE store, or a reader can trust data a dead writer left half-copied' if odd else ''))]
    st = stores[0]
    if not fences:
        return [(gid, st.tok.line, f'{name}( ) has no fence( ) ' + ('after the odd store: the data can be seen before the count is odd' if odd
                                                                     else 'before the even store: the data can be seen after the count is even'))]
    if len(fences) > 1:
        return [(gid, fences[1].tok.line, f'{name}( ) has {len(fences)} fence( ) calls, not one')]
    fe = fences[0]
    for e, what in ((st, 'store to the count'), (fe, 'fence( )')):
        if e.tok.depth != base or not e.tok.start:
            return [(gid, e.tok.line, f'the {what} at line {e.tok.line} is under a condition: it must be a statement of {name}( ) of its own')]
    if odd:
        if fe.idx < st.idx:
            return [(gid, fe.tok.line, f'the fence( ) comes before the odd store (line {st.tok.line}): it orders nothing the store did')]
        early = [d for d in data if d.idx < fe.idx]
        if early:
            return [(gid, early[0].tok.line, f'the write of `{early[0].tok.text}` comes before the fence( ) (line {fe.tok.line}): it can be seen before the count is odd')]
    else:
        if st.idx < fe.idx:
            return [(gid, st.tok.line, f'the even store comes before the fence( ) (line {fe.tok.line}): the data can be seen after the count is even')]
        if data:
            return [(gid, data[0].tok.line, f'close( ) touches `{data[0].tok.text}`: the data are written between open( ) and close( )')]
    return []


def g5_puts(S):
    out = []
    for name in ('putBlank', 'putFrame', 'putGlyph'):
        r = function_body(S, name)
        if r is None:
            out.append(('G5', 0, f'{name}( ) is not defined in LcdGlassShadow: the check cannot find that publish'))
            continue
        body, line = r
        words = [t.text for t in body]
        if words[:5] != OPEN_CALL:
            out.append(('G5', line, f'{name}( ): open( probe ) is not the first statement: the count goes odd before the first write of the data'))
        elif words[-5:] != CLOSE_CALL:
            out.append(('G5', body[-1].line, f'{name}( ): close( probe ) is not the last statement: the count goes even after the last write of the data'))
        elif words.count('open') != 1 or words.count('close') != 1:
            out.append(('G5', line, f'{name}( ) calls open( ) {words.count("open")} times and close( ) {words.count("close")}, not once each'))
        else:
            touch = [e for e in events(body) if e.kind in ('STORE', 'LOAD', 'FENCE')]
            if touch:
                what = 'a fence( )' if touch[0].kind == 'FENCE' else 'the count'
                out.append(('G5', touch[0].tok.line, f'{name}( ) touches {what} itself: only open( ) and close( ) do'))
    return out


def g_reader(S):
    r = function_body(S, 'read')
    if r is None:
        return [('G6', 0, 'read( ) is not defined in LcdGlassShadow: the check cannot find the reader')]
    body, line = r
    ev = events(body)
    loads = [e for e in ev if e.kind == 'LOAD']
    fences = [e for e in ev if e.kind == 'FENCE']
    data = [e for e in ev if e.kind == 'DATA']
    if not data:
        return [('G6', line, 'read( ) reads none of the data: there is nothing for the fences to order')]
    first, last = data[0], data[-1]
    base = min((e.tok.depth for e in loads), default=0)     # the level of the loop body: where a fence( ) of the reader must stand

    def free(e):
        return e.tok.depth == base and e.tok.start

    out = []
    firsts = [e for e in loads if e.idx < first.idx]
    if not firsts:
        out.append(('G6', first.tok.line, f'read( ): the count is not looked at before the first read of the data (line {first.tok.line})'))
    else:
        look = firsts[-1]
        between = [f for f in fences if look.idx < f.idx < first.idx]
        if not any(free(f) for f in between):
            note = f' (the fence( ) at line {between[0].tok.line} is under a condition)' if between else ''
            out.append(('G7', look.tok.line, f'read( ): no fence( ) between the first look at the count (line {look.tok.line}) and the first read of the data '
                                              f'(line {first.tok.line}){note}'))
    after = [e for e in loads if e.idx > last.idx]
    if after:
        between = [f for f in fences if last.idx < f.idx < after[0].idx]
        if not any(free(f) for f in between):
            note = f' (the fence( ) at line {between[0].tok.line} is under a condition)' if between else ''
            out.append(('G8', last.tok.line, f'read( ): no fence( ) between the last read of the data (line {last.tok.line}) and the second look at the count '
                                              f'(line {after[0].tok.line}){note}'))
    else:
        out.append(('G9', last.tok.line, f'read( ): the count is not looked at again after the data (line {last.tok.line}): a copy that a publish overtook would be trusted'))
        if not any(free(f) for f in fences if f.idx > last.idx):
            out.append(('G8', last.tok.line, f'read( ): no fence( ) after the last read of the data (line {last.tok.line})'))
    return out


RULES = (('G1', g1_fence), ('G2', g2_members), ('G3', lambda S: g_writer(S, 'open', 'G3')), ('G4', lambda S: g_writer(S, 'close', 'G4')),
         ('G5', g5_puts), ('G6', g_reader))


def check(text):
    """The findings of the header's text: [(id, line, message)], by line. A rule that crashes is a finding too: a gate that cannot read
    the header must not pass it."""
    S = struct_body(tokenize(text), 'LcdGlassShadow')
    if S is None:
        return [('G2', 0, 'struct LcdGlassShadow is not in the header')]
    out = []
    for gid, rule in RULES:
        try:
            out += rule(S)
        except Exception as exc:   # noqa: BLE001
            out.append((gid, 0, f'the check crashed: {type(exc).__name__}: {exc}'))
    return sorted(out, key=lambda f: (f[1], f[0]))


def trace(text):
    """What the check saw, for --verbose: the events of each function it reads, with the line of each."""
    S = struct_body(tokenize(text), 'LcdGlassShadow') or []
    lines = []
    for name in ('open', 'close', 'read'):
        r = function_body(S, name)
        if r:
            lines.append(f'{name}( ): ' + ' '.join(f'{e.kind}@{e.tok.line}' for e in events(r[0])))
    return lines




# ── the self-test ──────────────────────────────────────────────────────────────────────────────────────────────────

def mutate(text, pattern, repl):
    """The text with the ONE match of `pattern` replaced. A pattern that matches nothing, or twice, is an error: a mutant that applies
    nowhere tests nothing, and one that applies twice is not the single fault it names."""
    new, n = re.subn(pattern, repl, text)
    if n != 1:
        raise ValueError(f'the pattern {pattern!r} matches {n} times in the header, not once')
    return new


F = r'fence\( \);'   # the statement, as the header spells it
ODD = r'seq = \(s \+ 1u\) \| 1u; probe\.storeDone\( \);'
EVEN = r'seq = seq \+ 1u; probe\.storeDone\( \);'
FIRST_LOOK = r'const uint32_t before = seq;'
GUARD = r'if \(before & 1u\) \{ clock\.pause\(kReadStepUs\); continue; \}'
FLAG = r'const bool framed = blitted; probe\.copyStep\( \);'
SECOND_LOOK = r'if \(seq == before\) \{'

# (what is done to the header, pattern, replacement, the finding it must give and a phrase of its message). Exactly one finding each.
MUTANTS = (
    # the four fences, one at a time
    ('open( ): the fence( ) after the odd store taken out', '(' + ODD + r')\s*' + F, r'\1', 'G3', 'no fence( )'),
    ('close( ): the fence( ) before the even store taken out', F + r'\s*(' + EVEN + ')', r'\1', 'G4', 'no fence( )'),
    ('read( ): the fence( ) after the first look taken out', '(' + GUARD + r')\s*' + F, r'\1', 'G7', 'no fence( )'),
    ('read( ): the fence( ) before the second look taken out', '(' + FLAG + r')\s*' + F, r'\1', 'G8', 'no fence( )'),
    # a store moved across a fence
    ('open( ): the odd store moved after the fence( )', '(' + ODD + r')(\s*)(' + F + ')', r'\3\2\1', 'G3', 'before the odd store'),
    ('close( ): the even store moved before the fence( )', '(' + F + r')(\s*)(' + EVEN + ')', r'\3\2\1', 'G4', 'before the fence'),
    ('open( ): the write of blitted moved before the fence( )', r'(' + F + r')(\s*)(if \(s & 1u\) \{ blitted = false; probe\.storeDone\( \); \})',
     r'\3\2\1', 'G3', 'before the fence'),
    ('open( ): a second store to the count (an even count first)', '(' + ODD + ')', r'seq = s + 2u; probe.storeDone( ); \1', 'G3', 'not once'),
    # a load moved across a fence
    ('read( ): a read of the data hoisted above the first fence( )', '(' + GUARD + r')(\s*)(' + F + ')', r'\1\2const bool early = blitted; \3', 'G7', 'no fence( )'),
    ('read( ): the read of blitted moved after the second fence( )', '(' + FLAG + r')(\s*)(' + F + ')', r'\3\2\1', 'G8', 'no fence( )'),
    ('read( ): the second look moved above the second fence( )', '(' + FLAG + r')(\s*)(' + F + r')(\s*)(' + SECOND_LOOK + ')',
     r'\1\2const uint32_t again = seq;\4\3\4if (again == before) {', 'G8', 'no fence( )'),
    # the second load
    ('read( ): the second look at the count taken out', SECOND_LOOK, 'if (true) {', 'G9', 'not looked at again'),
    ('read( ): the first look at the count taken out', FIRST_LOOK, 'const uint32_t before = 0;', 'G6', 'not looked at before'),
    # a fence that is not one
    ('close( ): the fence( ) only in a comment', F + r'(\s*' + EVEN + ')', r'/* fence( ); */\1', 'G4', 'no fence( )'),
    ('open( ): the fence( ) under a condition', '(' + ODD + r'\s*)' + F, r'\1if (s & 1u) { fence( ); }', 'G3', 'under a condition'),
    ('read( ): the fence( ) after the first look only in the branch that waits', GUARD + r'\s*' + F,
     'if (before & 1u) { fence( ); clock.pause(kReadStepUs); continue; }', 'G7', 'under a condition'),
    ('open( ): the fence( ) as a statement with no braces under an if', '(' + ODD + r'\s*)' + F, r'\1if (s & 1u) fence( );', 'G3', 'under a condition'),
    # the fence itself
    ('fence( ) relaxed', r'__ATOMIC_SEQ_CST', '__ATOMIC_RELAXED', 'G1', 'sequentially consistent'),
    # the shape the order rests on
    ('putFrame( ): open( probe ) moved after the cells', r'(?s)(void putFrame[^{]*\{\s*)open\(probe\);(.*?)(blitMs = nowMs;)', r'\1\2open(probe); \3', 'G5', 'first statement'),
    ('putGlyph( ): close( probe ) before the data stores', r'(uint8_t \*g = cgram\[slot & 0x07\];)(\s*)(for \(uint8_t i = 0; i < 8; i\+\+\) \{ g\[i\] = bitmap\[i\]; probe\.storeDone\( \); \})(\s*)close\(probe\);',
     r'close(probe);\4\1\2\3', 'G5', 'last statement'),
    ('putBlank( ): a store to the count of its own', r'(void putBlank[^{]*\{\s*open\(probe\);)', r'\1 seq = 0;', 'G5', 'touches the count'),
    ('the count not volatile', r'volatile uint32_t seq;', 'uint32_t seq;', 'G2', 'volatile'),
    ('a member the check does not know', r'(bool\s+blitted;[^\n]*\n)', r'\1\tuint8_t  rssi;\n', 'G2', 'rssi'),
)


def selftest():
    fails, count = [], [0]

    def case(label, ok, detail=''):
        count[0] += 1
        if ok:
            print(f'ok   {label}')
        else:
            fails.append(label)
            print(f'FAIL {label}' + (f': {detail}' if detail else ''))

    clean = read(HEADER)
    findings = check(clean)
    case('the header as it is: no finding', not findings, '; '.join(f'{i} line {n}: {m}' for i, n, m in findings))

    for label, pattern, repl, want_id, phrase in MUTANTS:
        try:
            findings = check(mutate(clean, pattern, repl))
        except ValueError as e:
            case(label, False, str(e))
            continue
        got = [(i, phrase in m) for i, _, m in findings]
        case(f'{label}: exactly one {want_id} finding ("{phrase}")', got == [(want_id, True)],
             '; '.join(f'{i} line {n}: {m}' for i, n, m in findings) or 'no finding')

    # the instrument: a mutant that applies nowhere, or twice, must be refused and not pass
    for what, pattern in (('nowhere', r'this text is not in the header'), ('twice', r'probe\.storeDone\( \);')):
        try:
            mutate(clean, pattern, 'x')
            case(f'a mutant that applies {what} is refused', False, 'mutate( ) accepted it')
        except ValueError:
            case(f'a mutant that applies {what} is refused', True)

    # the command line, as a process: the verdict is the exit status
    with tempfile.TemporaryDirectory(prefix='glass_seqlock_') as tmp:
        def run(*args):
            return subprocess.run([sys.executable, '-I', os.path.abspath(__file__), *args], capture_output=True, text=True)

        good = os.path.join(tmp, 'LcdGlass.h')
        with open(good, 'w', encoding='utf-8') as f:
            f.write(clean)
        r = run('--header', good)
        case('the command line on the header as it is: exit 0', r.returncode == 0, f'exit {r.returncode}: {r.stdout}{r.stderr}')
        bad = os.path.join(tmp, 'LcdGlass_bad.h')
        with open(bad, 'w', encoding='utf-8') as f:
            f.write(mutate(clean, '(' + ODD + r')\s*' + F, r'\1'))
        r = run('--header', bad)
        case('the command line on a header without the first fence: exit 1 and the finding on the output',
             r.returncode == 1 and 'G3' in r.stdout, f'exit {r.returncode}: {r.stdout}{r.stderr}')
        r = run('--header', os.path.join(tmp, 'nowhere.h'))
        case('the command line on a header that is not there: exit 2', r.returncode == 2, f'exit {r.returncode}: {r.stdout}{r.stderr}')

    print(f'{TAG} selftest: {count[0]} cases, {len(fails)} failed')
    return 1 if fails else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description='Where the fences of the alpha glass\'s seqlock stand (src/display/LcdGlass.h).')
    ap.add_argument('--header', default=HEADER, help='the header to read (default src/display/LcdGlass.h)')
    ap.add_argument('--selftest', action='store_true', help='the gate\'s own cases: the clean header, the mutants, the command line')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    try:
        text = read(a.header)
    except (OSError, UnicodeDecodeError) as e:
        print(f'{TAG} cannot read {a.header}: {e}', file=sys.stderr)
        return 2
    rel = os.path.relpath(a.header, ROOT) if a.header.startswith(ROOT) else a.header
    findings = check(text)
    if a.verbose:
        for l in trace(text):
            print(f'{TAG} {l}')
    for gid, line, msg in findings:
        print(f'{TAG} {gid}: {rel}:{line}: {msg}')
    if findings:
        print(f'{TAG} FAIL: {len(findings)} finding(s)')
        return 1
    print(f'{TAG} OK: G1-G9 clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
