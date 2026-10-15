#!/usr/bin/env python3
"""elf_funcoes.py <a.elf> <b.elf> [--mostrar N] — compares two firmware images function by function.

A refactor that moves code between files changes firmware.bin (the link order moves every
address) while the machine code of each function stays the same. This compares what the
addresses would hide:

  - every function symbol's instructions (arm-none-eabi-objdump -d), each absolute address
    replaced by what it names (the symbol it falls in, "sym+off"; for an unnamed address the
    text or the bytes it holds), the instruction's own address dropped, a branch inside the
    function made relative to its start, a call through a linker veneer (__f_veneer) read as
    a call to f, the compiler's numbered clones (f.constprop.3) by their stable name, and the
    literal-pool words resolved the same way. A function whose text still differs gets a
    second chance: it is equal when its bytes differ only inside 32-bit words that are, on
    both sides, addresses of the same thing or Thumb branches to the same target;
  - every sized read-only object (nm type r or R), byte for byte, except the words of a
    word-aligned object that are addresses on both sides and point at the same symbol or
    text (a table of pointers moved by the layout; a number that merely looks like an address
    is compared as a number);
  - the switch tables the compiler numbers per translation unit (CSWTCH.<n>), by content;
  - the text of the data sections: every NUL-terminated printable run of 2 or more bytes in
    .rodata and .data, with its count, so an edited message shows even where no code names it.

It prints how much it read (functions, instructions), how many functions are equal, changed,
only in A and only in B, and the names of the changed ones (the first N with their first
differing line). Exit 0 when every function, data object, switch table and data-section text
present in both is equal and none is missing, 1 otherwise, 2 when it read no instruction
(an instrument that read nothing says "equal" about anything).

The toolchain is the PlatformIO one: $PLATFORMIO_CORE_DIR/packages/toolchain-rp2040-earlephilhower
when that exists, else ~/.platformio/packages/....

The instrument is proven by tools/test_elf_funcoes.py, which compiles small Cortex-M0+ programs
and checks the verdicts: the same ELF twice and the same objects linked in another order
(the layout moves, the code does not) compare equal; an edited string, a changed constant in
one function, a changed number in a table a function reads through a pointer, and a changed
value in a switch table are each reported, by name; an ELF with no code is refused (exit 2).
"""
import argparse
import bisect
import os
import re
import subprocess
import sys
from pathlib import Path

PACOTE = 'packages/toolchain-rp2040-earlephilhower/bin'


def toolchain_bin():
    """The PlatformIO ARM toolchain's bin directory: the one under PLATFORMIO_CORE_DIR (a private
    core, as the CI-equal builds use) when it exists, else the user's ~/.platformio. If neither
    exists, the user's path, so the error names the usual place."""
    candidatos = []
    if os.environ.get('PLATFORMIO_CORE_DIR'):
        candidatos.append(Path(os.environ['PLATFORMIO_CORE_DIR']) / PACOTE)
    candidatos.append(Path.home() / '.platformio' / PACOTE)
    return next((c for c in candidatos if (c / 'arm-none-eabi-objdump').is_file()), candidatos[-1])


TOOL = toolchain_bin()
OBJDUMP = str(TOOL / 'arm-none-eabi-objdump')
NM = str(TOOL / 'arm-none-eabi-nm')
READELF = str(TOOL / 'arm-none-eabi-readelf')


def simbolos(elf):
    """(sorted starts, [(start, size, name, type)]) of the sized symbols, for address lookup."""
    out = subprocess.run([NM, '-S', '-C', '--defined-only', elf], capture_output=True, text=True, check=True).stdout
    tab = []
    for linha in out.splitlines():
        p = linha.split(None, 3)
        if len(p) == 4 and re.fullmatch(r'[0-9a-f]+', p[1]):
            tab.append((int(p[0], 16), int(p[1], 16), p[3], p[2]))
        else:
            # An assembly routine or a label has no size (the ROM float wrappers, crt0):
            # without it a call to one would be named by the bytes it lands on.
            q = linha.split(None, 2)
            if len(q) == 3 and len(q[1]) == 1 and re.fullmatch(r'[0-9a-f]{8}', q[0]):
                tab.append((int(q[0], 16), 0, q[2], q[1]))
    tab.sort()
    return [t[0] for t in tab], tab


VENEER = re.compile(r'^__(.+)_veneer$')


def nome_de(endereco, inicios, tab, aproximar=False):
    """The symbol that holds the address (sized), or one that starts exactly there; a code
    pointer's Thumb bit is read as the function it calls. With aproximar, an address with
    neither is named by the nearest unsized label before it (crt0 and assembly targets) —
    only for code: a data pointer named that way would follow the layout."""
    for v, sufixo in ((endereco, ''), (endereco & ~1, '|1') if endereco & 1 else (None, None)):
        if v is None:
            continue
        i = bisect.bisect_right(inicios, v) - 1
        while i >= 0 and inicios[i] == v and tab[i][1] == 0 and i > 0 and inicios[i - 1] == v:
            i -= 1
        if i < 0:
            continue
        ini, tam, nome, _ = tab[i]
        if ini == v or ini <= v < ini + tam:
            # A long call goes through a linker veneer or not depending on the distance, which
            # the layout decides: __foo_veneer and foo are the same call.
            m = VENEER.match(nome)
            if m and v == ini:
                return m.group(1) + sufixo
            return (f'{nome}+{v - ini:#x}' if v != ini else nome) + sufixo
    if aproximar:
        i = bisect.bisect_right(inicios, endereco) - 1
        if i >= 0 and tab[i][1] == 0 and endereco - inicios[i] < 4096:
            return f'{tab[i][2]}+{endereco - inicios[i]:#x}'
    return None


HEX = re.compile(r'\b(?:0x)?([0-9a-f]{8})\b')


class Imagem:
    """The ELF's loadable bytes by address, to read what an unnamed address points at."""
    def __init__(self, elf):
        secs = subprocess.run([READELF, '-S', '-W', elf], capture_output=True, text=True, check=True).stdout
        self.mapas, self.nomes = [], []
        for linha in secs.splitlines():
            m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', linha)
            if m and int(m.group(2), 16):
                self.mapas.append((int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))
                self.nomes.append(m.group(1))
        self.bruto = Path(elf).read_bytes()

    def ler(self, endereco, n):
        for va, off, sz in self.mapas:
            if va <= endereco < va + sz:
                ini = off + endereco - va
                return self.bruto[ini: ini + min(n, va + sz - endereco)]
        return b''


def eh_endereco(v):
    return 0x10000000 <= v < 0x10400000 or 0x20000000 <= v < 0x20082000


def texto_em(img, v):
    b = img.ler(v, 64)
    fim = b.find(b'\0')
    texto = b[:fim] if fim >= 0 else b''
    if fim > 0 and all(32 <= c < 127 or c >= 0x80 for c in texto):
        return '"' + texto.decode('utf-8', 'replace') + '"'
    return None


def conteudo(img, v, inicios, tab):
    """An unnamed address (a string literal, a constant initializer): what is there, not where.
    A table of pointers (a local `const char *x[] = {...}` is copied from one) is rendered word
    by word, each pointer by the symbol or the text it points at: an edited string of the same
    length moves no address, and only its bytes tell."""
    t = texto_em(img, v)
    if t:
        return '<' + t + '>'
    base = v & ~3                                  # pointers in data sit on word boundaries
    b = img.ler(base, 64)
    if not b:
        return '<?>'
    return f'<[{palavras(b, inicios, tab, img)}]{"@" + str(v - base) if v != base else ""}>'


CLONE = re.compile(r'\.(constprop|isra|part|cold|lto_priv)\.\d+')
CSWTCH = re.compile(r'^CSWTCH\.\d+$')


def estavel(nome):
    """A name without the numbers the compiler gives per translation unit (foo.constprop.3)."""
    return CLONE.sub(r'.\1', nome)


def palavras(b, inicios, tab, img):
    """Bytes as 32-bit words, each address by what it points at (one level)."""
    partes = []
    for i in range(0, len(b) - 3, 4):
        w = int.from_bytes(b[i:i + 4], 'little')
        if eh_endereco(w):
            n = nome_de(w, inicios, tab)
            partes.append(estavel(n) if n else (texto_em(img, w) or '?'))
        else:
            partes.append(f'{w:x}')
    resto = len(b) % 4
    if resto:
        partes.append(b[-resto:].hex())
    return ','.join(partes)


def normalizar(texto, inicios, tab, img, faixa=None):
    def troca(m):
        v = int(m.group(1), 16)
        if faixa and faixa[0] <= v < faixa[1]:
            return f'<.+{v - faixa[0]:#x}>'           # a branch or a literal inside the function
        if eh_endereco(v):
            n = nome_de(v, inicios, tab)
            if n is None:
                t = texto_em(img, v)
                if t:
                    return '<' + t + '>'
                n = nome_de(v, inicios, tab, aproximar=True)
            if n is None:
                return conteudo(img, v, inicios, tab)
            base, _, off = n.partition('+')
            if CSWTCH.match(base):
                # A switch table is numbered per translation unit: name it by its content.
                i = bisect.bisect_right(inicios, v) - 1
                ini, tam = tab[i][0], tab[i][1]
                return f'<CSWTCH[{palavras(img.ler(ini, tam), inicios, tab, img)}]{"+" + off if off else ""}>'
            return f'<{estavel(n)}>'
        return m.group(0)
    return HEX.sub(troca, texto)


def textos(elf):
    """Every NUL-terminated printable run of 2+ bytes in the image's flash sections, with its
    count: an edited message shows here even when no code names it."""
    img = Imagem(elf)
    from collections import Counter
    c = Counter()
    # Data sections only: machine code has printable runs too, and its bytes follow the layout.
    for nome, (va, off, sz) in zip(img.nomes, img.mapas):
        if nome not in ('.rodata', '.data'):
            continue
        for m in re.finditer(rb'[\x20-\x7e\x80-\xff]{2,}(?=\x00)', img.bruto[off:off + sz]):
            try:
                m.group(0).decode('utf-8')
            except UnicodeDecodeError:
                continue
            c[m.group(0)] += 1
    return c


def funcoes(elf):
    inicios, tab = simbolos(elf)
    img = Imagem(elf)
    out = subprocess.run([OBJDUMP, '-d', '-C', '--no-show-raw-insn', elf], capture_output=True, text=True,
                         check=True).stdout
    # Each function's range runs to the next function's start (sizes are missing on labels).
    cabecas = [int(m.group(1), 16) for m in re.finditer(r'^([0-9a-f]{8}) <.+>:$', out, re.M)]
    proxima = {a: b for a, b in zip(cabecas, cabecas[1:] + [cabecas[-1] + 0x10000])}
    tamanho = {}
    for ini_, tam_, _, _ in tab:
        if tam_:
            tamanho[ini_] = max(tamanho.get(ini_, 0), tam_)
    corpo, atual, faixa, faixas = {}, None, None, {}
    for linha in out.splitlines():
        m = re.match(r'^([0-9a-f]{8}) <(.+)>:$', linha)
        if m:
            ini = int(m.group(1), 16)
            # The symbol's own size when nm has it (data that objdump disassembles would run on
            # into the next symbol's bytes); the next header otherwise.
            tam = tamanho.get(ini, 0)
            faixa = (ini, ini + tam if tam else proxima[ini])
            atual = estavel(m.group(2))
            if atual in corpo:                      # two clones of one function: keep both
                k = 2
                while f'{atual}#{k}' in corpo:
                    k += 1
                atual = f'{atual}#{k}'
            corpo[atual] = []
            faixas[atual] = faixa
            continue
        # This objdump prints instructions at column 0 ("1000d478:\tpush\t{r4, lr}"); the first
        # version wanted leading blanks, matched no line at all and compared empty bodies
        # (found 2026-10-14: two functions that differ came out "equal").
        m = re.match(r'^\s*[0-9a-f]+:\t(.*)$', linha)
        if m and atual is not None:
            ins = re.sub(r'\s+', ' ', m.group(1)).strip()
            ins = re.sub(r'<[^>]*>', '', ins)               # objdump's own guesses: resolved below
            corpo[atual].append(normalizar(ins, inicios, tab, img, faixa))
    return corpo, faixas, (inicios, tab, img)


def alvo_relativo(v, faixa, inicios, tab, img):
    if faixa[0] <= v < faixa[1]:
        return f'.+{v - faixa[0]:#x}'
    return alvo(v, inicios, tab, img)


def desvio_thumb(h1, h2, pc):
    """Target of a 32-bit Thumb BL or B.W at pc (h1, h2 its two halfwords), or None."""
    if (h1 & 0xF800) != 0xF000 or (h2 & 0xD000) not in (0xD000, 0x9000):
        return None
    s_ = (h1 >> 10) & 1
    j1, j2 = (h2 >> 13) & 1, (h2 >> 11) & 1
    i1, i2 = 1 - (j1 ^ s_), 1 - (j2 ^ s_)
    off = (s_ << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
    if s_:
        off -= 1 << 25
    return pc + 4 + off


def so_relocacao(nome, fa, fb, ctx_a, ctx_b):
    """Second chance for a function whose normalized text differs: its bytes are equal except
    inside 32-bit words (at any halfword alignment) that are, on both sides, addresses of the
    same thing or Thumb branches to the same target. Hand-written assembly hides pointers in
    data (the SDK's float shims put a table byte and a pointer in one word) and constants in
    the address range (an ENC28J60 command 0x1000a000), which text normalization misreads."""
    (ia, ta, imga), (ib, tb, imgb) = ctx_a, ctx_b
    ra, rb = fa[nome], fb[nome]
    ba, bb = imga.ler(ra[0], ra[1] - ra[0]), imgb.ler(rb[0], rb[1] - rb[0])
    if not ba or len(ba) != len(bb):
        return False
    explicado = [x == y for x, y in zip(ba, bb)]
    for i in range(len(ba)):
        if explicado[i]:
            continue
        # Only windows that hold byte i; word-aligned first (data), then halfword (Thumb code).
        janelas = [w for w in range(max(0, i - 3), i + 1) if w % 2 == 0 and w + 4 <= len(ba)]
        for w in sorted(janelas, key=lambda w: w % 4):
            pa, pb = ba[w:w + 4], bb[w:w + 4]
            va, vb = int.from_bytes(pa, 'little'), int.from_bytes(pb, 'little')
            ok = False
            if eh_endereco(va) and eh_endereco(vb):
                ok = alvo_relativo(va, ra, ia, ta, imga) == alvo_relativo(vb, rb, ib, tb, imgb)
            if not ok:
                da = desvio_thumb(int.from_bytes(pa[:2], 'little'), int.from_bytes(pa[2:], 'little'), ra[0] + w)
                db = desvio_thumb(int.from_bytes(pb[:2], 'little'), int.from_bytes(pb[2:], 'little'), rb[0] + w)
                ok = da is not None and db is not None and \
                    alvo_relativo(da, ra, ia, ta, imga) == alvo_relativo(db, rb, ib, tb, imgb)
            if ok:
                for k in range(w, w + 4):
                    explicado[k] = True
                break
        if not explicado[i]:
            return False
    return True


def dados(elf):
    """Read-only objects (type r/R) by name -> their content, from the ELF sections. A table of
    pointers (a dictionary, a font's glyph table, a vtable) holds addresses that follow the
    layout: a word-aligned object is rendered word by word, each address by the symbol or the
    text it points at, so only a real change of content shows."""
    inicios, tab = simbolos(elf)
    img = Imagem(elf)
    secs = subprocess.run([READELF, '-S', '-W', elf], capture_output=True, text=True, check=True).stdout
    mapas = []
    for linha in secs.splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', linha)
        if m:
            mapas.append((int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))
    bruto = Path(elf).read_bytes()
    res = {}
    for ini, tam, nome, tipo in tab:
        if tipo not in 'rR' or tam == 0:
            continue
        for va, off, sz in mapas:
            if va <= ini and ini + tam <= va + sz:
                res[nome] = (bruto[off + ini - va: off + ini - va + tam], ini % 4 == 0)
                break
    return res, inicios, tab, img


def alvo(w, inicios, tab, img):
    n = nome_de(w, inicios, tab)
    if n:
        base, _, off = n.partition('+')
        return estavel(base) + ('+' + off if off else '') if not CSWTCH.match(base) else 'CSWTCH+' + off
    # Unnamed and not a text: what it holds, so a pointer to an edited table is not "the same".
    return texto_em(img, w) or conteudo(img, w, inicios, tab)


def mesmo_dado(xa, xb, ctx_a, ctx_b):
    """Equal bytes, or bytes that differ only in words that are addresses on both sides and
    point at the same symbol or text: a table of pointers moved by the layout. A data word that
    merely looks like an address is compared as data, so a glyph table cannot hide a change."""
    (ba, alinhado), (bb, _) = xa, xb
    if ba == bb:
        return True
    if len(ba) != len(bb) or not alinhado or len(ba) % 4:
        return False
    for k in range(0, len(ba), 4):
        if ba[k:k + 4] == bb[k:k + 4]:
            continue
        wa, wb = int.from_bytes(ba[k:k + 4], 'little'), int.from_bytes(bb[k:k + 4], 'little')
        if not (eh_endereco(wa) and eh_endereco(wb)):
            return False
        if alvo(wa, *ctx_a) != alvo(wb, *ctx_b):
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--mostrar', type=int, default=10)
    a = ap.parse_args()
    (fa, xa, cfa), (fb, xb, cfb) = funcoes(a.a), funcoes(a.b)
    for d in (fa, fb):
        for n in [n for n in d if VENEER.match(n)]:
            del d[n]
    iguais = [n for n in fa if n in fb and fa[n] == fb[n]]
    diferem = [n for n in fa if n in fb and fa[n] != fb[n]]
    mudaram = [n for n in diferem if not so_relocacao(n, xa, xb, cfa, cfb)]
    relocadas = len(diferem) - len(mudaram)
    so_a = sorted(set(fa) - set(fb))
    so_b = sorted(set(fb) - set(fa))
    da, *ctx_a = dados(a.a)
    db, *ctx_b = dados(a.b)
    # Switch tables the compiler names CSWTCH.<n>: the number follows the translation unit's
    # count, so a function moved to another file renumbers them. They are compared by content.
    anon = re.compile(r'^CSWTCH\.\d+$')
    ta = sorted(palavras(v[0], *ctx_a) for n, v in da.items() if anon.match(n))
    tb = sorted(palavras(v[0], *ctx_b) for n, v in db.items() if anon.match(n))
    da = {n: v for n, v in da.items() if not anon.match(n)}
    db = {n: v for n, v in db.items() if not anon.match(n)}
    dmud = [n for n in da if n in db and not mesmo_dado(da[n], db[n], ctx_a, ctx_b)]
    dso_a = sorted(set(da) - set(db))
    dso_b = sorted(set(db) - set(da))
    tabelas_iguais = ta == tb
    ca, cb = textos(a.a), textos(a.b)
    txt_a = sorted((ca - cb).items())
    txt_b = sorted((cb - ca).items())
    lidas_a, lidas_b = sum(len(v) for v in fa.values()), sum(len(v) for v in fb.values())
    print(f'lido: {len(fa)} funções e {lidas_a} instruções em A, {len(fb)} e {lidas_b} em B')
    if not lidas_a or not lidas_b:
        # An instrument that read nothing says "all equal" about anything (2026-10-14, twice).
        print('nenhuma instrução lida: o formato do objdump mudou? veredito recusado', file=sys.stderr)
        return 2
    print(f'funções: {len(iguais) + relocadas} iguais ({relocadas} só por relocação de ponteiro ou desvio), '
          f'{len(mudaram)} mudaram, {len(so_a)} só em A, {len(so_b)} só em B')
    print(f'dados só-leitura: {len(da) - len(dmud) - len(dso_a)} iguais, {len(dmud)} mudaram, '
          f'{len(dso_a)} só em A, {len(dso_b)} só em B; tabelas de switch: {len(ta)} em A, {len(tb)} em B, '
          f'{"mesmo conteúdo" if tabelas_iguais else "CONTEÚDO DIFERENTE"}')
    print(f'textos na flash: {sum(ca.values())} em A, {sum(cb.values())} em B; {len(txt_a)} só em A, {len(txt_b)} só em B')
    for rot, lista in (('texto só em A', txt_a), ('texto só em B', txt_b)):
        for t, n in lista[:a.mostrar]:
            print(f'  {rot}: {t[:60]!r} x{n}')
    for n in mudaram[:a.mostrar]:
        x, y = fa[n], fb[n]
        k = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
        print(f'  mudou: {n} ({len(x)} -> {len(y)} instruções); 1ª diferença na {k}: '
              f'{x[k] if k < len(x) else "-"!r} | {y[k] if k < len(y) else "-"!r}')
    for rotulo, lista in (('só em A', so_a), ('só em B', so_b), ('dado mudou', dmud), ('dado só em A', dso_a),
                          ('dado só em B', dso_b)):
        for n in lista[:a.mostrar]:
            print(f'  {rotulo}: {n}')
    return 0 if not (mudaram or so_a or so_b or dmud or dso_a or dso_b or txt_a or txt_b) and tabelas_iguais else 1


if __name__ == '__main__':
    sys.exit(main())
