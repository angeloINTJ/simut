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
    literal-pool words resolved the same way. A symbol whose name more than one address carries
    (the file-local statics of one name in several files, the clones of one function) is named
    `name@colour` wherever it is referenced, the colour a hash of what the symbol is (its
    instructions, its bytes) and not of where it is: two helpers of one name are told apart, and a
    layout move keeps the colour. The functions of one such name are paired by equal text first, then
    by order. A function whose text still differs gets a second chance, for the numbers that look
    like addresses and the pointers: it is equal when its bytes differ only inside 32-bit words that
    are, on both sides, pointers to the same named thing, or Thumb branches to the same target. The
    second chance never excuses a branch or a call that the text names differently (the bytes of
    a relative branch are the same wherever it lands), nor an unnamed constant whose bytes changed;
  - every sized read-only object, byte for byte, except the words of a word-aligned object that
    are addresses on both sides and point at the same symbol or text (a table of pointers moved
    by the layout; a number that merely looks like an address is compared as a number). Read-only
    is nm's r and R, and the weak objects it calls V and v (vtables, typeinfo, inline variables)
    that lie in a section neither writable nor executable. Objects of one name are matched one to
    one, equal bytes first;
  - the switch tables the compiler numbers per translation unit (CSWTCH.<n>), by content;
  - the text of the data sections: every NUL-terminated printable run of 2 or more bytes in
    .rodata and .data, with its count, so an edited message shows even where no code names it.

It prints how much it read (functions, instructions), how many functions are equal, changed,
only in A and only in B, and the names of the changed ones (the first N with their first
differing line). The callers of a changed function of a shared name are changed too: their call
names another colour. Exit 0 when every function, data object, switch table and data-section text
present in both is equal and none is missing, 1 otherwise, 2 when it read no instruction or found
no code symbol in one of the two (an instrument that read nothing says "equal" about anything).

What "equal" does not cover:
  - an unnamed constant (a local initializer, no symbol) is read as the 64 bytes from its address:
    a layout move that puts other bytes behind it reports the function that loads it (a false
    alarm, held as an expected failure by the test), and a change past the 64th byte is missed;
  - an entry of an unnamed table of pointers made to point at another string that exists in both
    images changes no pointer text and no census;
  - .data and .bss objects are not compared (only the text inside .data is), nor are weak objects
    in a writable section;
  - two symbols of one name with the same content have one colour (equal instructions, equal
    bytes, two .bss variables of one size): a reference moved from one to the other is not seen. For
    code that is no change; for two variables it is another variable;
  - a call through a veneer to a symbol of a shared name carries no colour, and a colour sees three
    levels of shared names (RODADAS); a table of pointers of a shared name has its pointers
    masked in the colour, so two that differ only in where they point share one (a real instance,
    measured 2026-10-15 in the pico_wiretft baseline: BearSSL's HASH_OID, three tables of 20 bytes
    with one colour, 66b3b6c8, and two different contents); and an unnamed
    constant counts in a colour by its first word only, so two helpers whose unnamed tables differ
    after it share one;
  - a function whose bytes are equal and whose text differs only in literal-pool words is
    explained: a number that looks like an address, or a pointer to what is now another symbol at the
    same address. If nothing else differs (a text, an object, another function) it does not show.

The toolchain is the PlatformIO one: $PLATFORMIO_CORE_DIR/packages/toolchain-rp2040-earlephilhower
when that exists, else ~/.platformio/packages/....

The instrument is proven by tools/test_elf_funcoes.py, which compiles small Cortex-M0+ programs
and checks the verdicts: the same ELF twice and the same objects linked in another order
(the layout moves, the code does not) compare equal; an edited string, a changed constant in
one function, a call retargeted, a changed number in a table a function reads through a pointer,
a changed value in a switch table, a vtable slot retargeted and two same-named helpers swapped
between files are each reported, by name; an ELF with no code, and a path with no file behind it,
are refused (exit 2).
"""
import argparse
import bisect
import hashlib
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
        self.mapas, self.nomes, self.flags = [], [], []
        for linha in secs.splitlines():
            m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', linha)
            if m and int(m.group(2), 16):
                self.mapas.append((int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))
                self.nomes.append(m.group(1))
                mf = re.search(r'\s[0-9a-f]{2}\s+([A-Za-z]*)\s+\d+\s+\d+\s+\d+\s*$', linha)
                self.flags.append(mf.group(1) if mf else '')       # readelf's Flg column: W write, X exec
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
        # Nothing to read: the value is all there is. "<?>" for every such address made a constant
        # that looks like one (0x10120000 past the end of the image, 0x20030000 in SRAM) equal to
        # any other (review of the fixes, 2026-10-15).
        return f'<?{v:#x}>'
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
            partes.append(estavel(n) if n else (texto_em(img, w) or ('?' if img.ler(w, 1) else f'?{w:#x}')))
        else:
            partes.append(f'{w:x}')
    resto = len(b) % 4
    if resto:
        partes.append(b[-resto:].hex())
    return ','.join(partes)


def normalizar(texto, inicios, tab, img, faixa=None, grosso=False):
    """The text of one instruction with every address replaced by what it names. grosso: for the
    colours of colorir( ), an address with no name and no text is "<@" and its first word read, not
    the 64 bytes behind it or the nearest label: a colour must not move with the bytes around an
    unnamed constant."""
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
                if grosso:
                    # No name, no text: the first word read stands for what is there (an unnamed
                    # initializer starts where the address points; what lies after it follows the
                    # layout). Two helpers that differ only in such a table had one colour.
                    b = img.ler(v & ~3, 4)
                    return f'<@{b.hex()}{"@" + str(v & 3) if v & 3 else ""}>' if b else f'<?{v:#x}>'
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


def textos(img):
    """Every NUL-terminated printable run of 2+ bytes in the image's flash sections, with its
    count: an edited message shows here even when no code names it."""
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


# The table that follows a call to a Thumb-1 switch helper (__gnu_thumb1_case_uqi and its kin, or the linker veneer of one) is bytes, and objdump prints it as .word/.short/.byte:
# a word of it that falls in the image's address range (08 0a 0a 10 reads 0x100a0a08) was taken for a pointer and shown by the bytes behind that address, which are another
# function's in the other image, so a function that only moved was called changed (CYW43lwIP::encryptionType in pico_w_test_https, between S8b C5 and C6, 2026-10-16:
# the listings are the same once every address is normalised). The table is collected into one `.casetab` line of its bytes, whatever the way objdump cuts it into directives, and it is
# exactly as long as the code says: GCC emits `cmp rX, #N` and the call, and N + 1 entries of the helper's width follow (a word table is aligned first), padded to a halfword. What follows
# is the literal pool, and the pointer rules read it (a first version took every data directive behind the call for table, so a pool pointer that moved with the layout was called a
# changed table); the bytes of a table are compared as bytes, and no second chance may call a table word a relocated pointer (a word moved by exactly the shift of the layout was excused).
# When the compare cannot be found, the words stay as objdump printed them.
CASO = re.compile(r'^bl [0-9a-f]+ <_*gnu_thumb1_case_([a-z]+?)(?:_veneer)?>')
CMP_N = re.compile(r'^cmp r\d+, #(\d+)$')
LARGURA_CASO = {'uqi': 1, 'sqi': 1, 'uhi': 2, 'shi': 2, 'si': 4}
DADO = re.compile(r'^\.(word|short|hword|byte|2byte|4byte) (0x[0-9a-f]+)\b')
TAM_DADO = {'word': 4, '4byte': 4, 'short': 2, 'hword': 2, '2byte': 2, 'byte': 1}


def fechar_tabela(f):
    tab = f.pop('_tab', None) if f is not None else None
    if f is not None:
        f.pop('_len', None)
    if tab:
        f['ins'].append('.casetab ' + ':'.join(f'{b:02x}' for b in tab))


def resto_de_tabela(b):
    """What a directive that straddles the end of a table leaves: halfwords and a byte, which are no address."""
    out = [f'.short {int.from_bytes(b[i:i + 2], "little"):#06x}' for i in range(0, len(b) - 1, 2)]
    return out + ([f'.byte {b[-1]:#04x}'] if len(b) % 2 else [])


def ler_objdump(elf, tab):
    """The functions as objdump lists them, in address order: [{nome, ini, faixa, ins}], ins being
    each instruction as text, objdump's own guesses removed and nothing else resolved yet."""
    out = subprocess.run([OBJDUMP, '-d', '-C', '--no-show-raw-insn', elf], capture_output=True, text=True,
                         check=True).stdout
    # Each function's range runs to the next function's start (sizes are missing on labels).
    cabecas = [int(m.group(1), 16) for m in re.finditer(r'^([0-9a-f]{8}) <.+>:$', out, re.M)]
    # An ELF with no code has no head at all: main() refuses it (it used to die here on cabecas[-1]).
    proxima = {a: b for a, b in zip(cabecas, cabecas[1:] + ([cabecas[-1] + 0x10000] if cabecas else []))}
    tamanho = {}
    for ini_, tam_, _, _ in tab:
        if tam_:
            tamanho[ini_] = max(tamanho.get(ini_, 0), tam_)
    fns, atual = [], None
    for linha in out.splitlines():
        m = re.match(r'^([0-9a-f]{8}) <(.+)>:$', linha)
        if m:
            fechar_tabela(atual)
            ini = int(m.group(1), 16)
            # The symbol's own size when nm has it (data that objdump disassembles would run on
            # into the next symbol's bytes); the next header otherwise.
            tam = tamanho.get(ini, 0)
            atual = {'nome': estavel(m.group(2)), 'ini': ini, 'faixa': (ini, ini + tam if tam else proxima[ini]),
                     'ins': []}
            fns.append(atual)
            continue
        # This objdump prints instructions at column 0 ("1000d478:\tpush\t{r4, lr}"); the first
        # version wanted leading blanks, matched no line at all and compared empty bodies
        # (found 2026-10-14: two functions that differ came out "equal").
        m = re.match(r'^\s*[0-9a-f]+:\t(.*)$', linha)
        if m and atual is not None:
            ins = re.sub(r'\s+', ' ', m.group(1)).strip()
            d = DADO.match(ins)
            if d and atual.get('_tab') is not None:
                if atual.pop('_alinha', False):          # a word table is aligned to a word: the padding is part of it
                    atual['_len'] += (-int(linha.split(':')[0], 16)) % 4
                b = int(d.group(2), 16).to_bytes(TAM_DADO[d.group(1)], 'little')
                falta = atual['_len'] - len(atual['_tab'])
                atual['_tab'] += b[:falta]
                if len(atual['_tab']) >= atual['_len']:
                    fechar_tabela(atual)
                    atual['ins'].extend(resto_de_tabela(b[falta:]))      # what follows the table is the pool: the pointer rules read it
                continue
            fechar_tabela(atual)
            c = CASO.match(ins)
            atual['ins'].append(re.sub(r'<[^>]*>', '', ins))        # objdump's own guesses: resolved below
            if c and c.group(1) in LARGURA_CASO:
                # GCC: `cmp rX, #N`, `bhi default`, the call, then N + 1 entries of the helper's width, padded to a halfword.
                n = next((int(k.group(1)) for k in map(CMP_N.match, reversed(atual['ins'][-4:-1])) if k), None)
                if n is not None:
                    w = LARGURA_CASO[c.group(1)]
                    atual['_tab'], atual['_len'], atual['_alinha'] = bytearray(), ((n + 1) * w + 1) & ~1, w == 4
    for f in fns:
        fechar_tabela(f)
    return fns


RODADAS = 3


def palavras_cegas(b):
    """The bytes of an object as words for a colour: a word that lies in an address range is "@",
    whatever it points at. A table of bytes (BearSSL's t0_codeblock, one per module, all named alike)
    has words that only look like addresses, and what they would name follows the layout; a colour
    that moved with it would blame every function that loads the table (found on the real study
    comparison, 2026-10-14: ten of them). What this leaves: two same-named tables of pointers that
    differ only in where the pointers go have one colour."""
    partes = []
    for i in range(0, len(b) - 3, 4):
        w = int.from_bytes(b[i:i + 4], 'little')
        partes.append('@' if eh_endereco(w) else f'{w:x}')
    if len(b) % 4:
        partes.append(b[-(len(b) % 4):].hex())
    return ','.join(partes)


def colorir(fns, inicios, tab, img):
    """{start address: 8 hex digits} for every symbol whose name more than one address carries: the
    file-local statics of one name in several files, the compiler's clones of one function
    (foo.constprop.0 and .1 are one name once the numbers are dropped). A reference to such a symbol
    is named `name@colour`, so that two callers of two different `helper`s do not both read
    `bl helper` (review of S2b, 2026-10-14: F and G swapped between two files, each calling the
    helper of its own file, came out "equal").

    The colour is a hash of what the symbol is, not of where it is, so a layout move keeps it: for
    a function its instructions, every address named by a symbol or a text, an unnamed one by the first
    word read there ("<@" and the word; "<?0x..>" and the value when nothing can be read); for
    an object its bytes, the words that lie in an address range masked (palavras_cegas( )). A reference
    to another symbol of a shared
    name is named by that one's colour of the round before: round 0 leaves them "?", round 1 and 2
    use the colour of the round before, so that two helpers alike but for the helper they call differ
    (RODADAS rounds, fixed, because two images must reach the same colour for the same thing).
    CSWTCH tables (compared by content already) and veneers (read as the function they branch to)
    are left out. What it cannot tell: two symbols of one name with the same content."""
    por_nome = {}
    for ini, tam, nome, tipo in tab:
        if tam and not CSWTCH.match(nome) and not VENEER.match(nome):
            por_nome.setdefault(estavel(nome), set()).add(ini)
    dup_nomes = {n for n, enderecos in por_nome.items() if len(enderecos) > 1}
    dup = {ini for n in dup_nomes for ini in por_nome[n]}
    if not dup:
        return {}, set()
    tamanho = {}
    for ini, tam, nome, tipo in tab:
        if ini in dup and tam:
            tamanho[ini] = max(tamanho.get(ini, 0), tam)
    corpos = {f['ini']: f for f in fns}
    cores = {ini: '?' for ini in dup}
    for _ in range(RODADAS):
        tab_r = renomear(tab, cores, dup_nomes)
        novas = {}
        for ini in dup:
            if ini in corpos:
                f = corpos[ini]
                texto = '\n'.join(normalizar(i, inicios, tab_r, img, f['faixa'], grosso=True) for i in f['ins'])
            else:
                b = img.ler(ini, tamanho[ini])
                texto = palavras_cegas(b) if b else f'bss{tamanho[ini]}'
            novas[ini] = hashlib.sha1(texto.encode('utf-8')).hexdigest()[:8]
        cores = novas
    return cores, dup_nomes


def renomear(tab, cores, dup_nomes):
    """The symbols with the name of each shared one followed by @colour."""
    return [(ini, tam, f'{estavel(nome)}@{cores[ini]}' if tam and estavel(nome) in dup_nomes and ini in cores else nome,
             tipo) for ini, tam, nome, tipo in tab]


class Elf:
    """What is read from one ELF, once: the symbols as nm lists them, the bytes by address, the
    functions as objdump lists them and the colours of the shared names. ctx is what the naming
    functions take: the starts, the symbols with those names refined by colour, the image."""
    def __init__(self, caminho):
        self.inicios, self.tab = simbolos(caminho)
        self.img = Imagem(caminho)
        self.fns = ler_objdump(caminho, self.tab)
        self.cores, dup_nomes = colorir(self.fns, self.inicios, self.tab, self.img)
        self.tab_ref = renomear(self.tab, self.cores, dup_nomes)
        self.ctx = (self.inicios, self.tab_ref, self.img)


def funcoes(E):
    """[(name, range, normalized instructions)] of the functions of an Elf, in address order."""
    return [(f['nome'], f['faixa'], [normalizar(i, E.inicios, E.tab_ref, E.img, f['faixa']) for i in f['ins']])
            for f in E.fns]


def parear(ra, rb):
    """Keys for the functions of two images, as (bodies A, ranges A, bodies B, ranges B). A name that
    one function carries in each image is its own key. A name that several carry (file-local statics
    of one name, the compiler's clones) is paired by equal body first, so that a layout move that
    reorders them is no change, and what is left by order; the keys are name, name#2, ... in the
    order of A's functions and then B's unpaired ones."""
    ga, gb = {}, {}
    for r in ra:
        ga.setdefault(r[0], []).append(r)
    for r in rb:
        gb.setdefault(r[0], []).append(r)
    fa, xa, fb, xb = {}, {}, {}, {}
    for nome in list(ga) + [n for n in gb if n not in ga]:
        la, lb = ga.get(nome, []), gb.get(nome, [])
        livres = list(range(len(lb)))
        par = {}
        for i, r in enumerate(la):
            j = next((j for j in livres if lb[j][2] == r[2]), None)
            if j is not None:
                par[i] = j
                livres.remove(j)
        for i in range(len(la)):
            if i not in par and livres:
                par[i] = livres.pop(0)
        k = 0
        for i, r in enumerate(la):
            k += 1
            chave = nome if k == 1 else f'{nome}#{k}'
            fa[chave], xa[chave] = r[2], r[1]
            if i in par:
                fb[chave], xb[chave] = lb[par[i]][2], lb[par[i]][1]
        for j in livres:
            k += 1
            chave = nome if k == 1 else f'{nome}#{k}'
            fb[chave], xb[chave] = lb[j][2], lb[j][1]
    return fa, xa, fb, xb


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


def mesmo_alvo(x, y):
    """Two targets (alvo_relativo( ) strings) are the same thing when a symbol, a text or a place in
    the function names them alike. What lies behind an address that has no name ("<[...]>", "<?>")
    is not a name: two unnamed addresses are not the same target because nothing can be read at
    either (found 2026-10-14 by the review of S2b: the BL of a call retargeted from foo to bar
    followed by `movs r0, #1`, the halfwords 0xF8xx and 0x2001, read as the address 0x2001F8xx
    on both sides, "<?>" equal to "<?>", and the change was called a relocation)."""
    return x == y and not x.startswith('<')


def eh_flash(v):
    return 0x10000000 <= v < 0x10400000


def mesmo_conteudo(x, y, va, vb, pa, pb):
    """The one unnamed target that is still the same: bytes in flash that read alike on both sides,
    behind a word-aligned pointer (pa, pb: its address in each image). A pointer to the empty
    string has no text to be named by, and the C library's __global_locale (newlib, 13 words of
    them) moves with the string pool while what lies there does not. An address in SRAM that has
    no name, a pair of instructions that only looks like one, is never this: the review of S2b
    broke the tool with it, and a halfword-aligned pointer is accepted only when it is named
    (the SDK's float shims keep a pointer at a halfword address, behind a table byte)."""
    return (x == y and x.startswith('<') and not x.startswith('<?') and eh_flash(va) and eh_flash(vb)
            and pa % 4 == 0 and pb % 4 == 0)


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
                xa_, xb_ = alvo_relativo(va, ra, ia, ta, imga), alvo_relativo(vb, rb, ib, tb, imgb)
                ok = mesmo_alvo(xa_, xb_) or mesmo_conteudo(xa_, xb_, va, vb, ra[0] + w, rb[0] + w)
            if not ok:
                da = desvio_thumb(int.from_bytes(pa[:2], 'little'), int.from_bytes(pa[2:], 'little'), ra[0] + w)
                db = desvio_thumb(int.from_bytes(pb[:2], 'little'), int.from_bytes(pb[2:], 'little'), rb[0] + w)
                ok = da is not None and db is not None and \
                    mesmo_alvo(alvo_relativo(da, ra, ia, ta, imga), alvo_relativo(db, rb, ib, tb, imgb))
            if ok:
                for k in range(w, w + 4):
                    explicado[k] = True
                break
        if not explicado[i]:
            return False
    return True


TEXTOS_ENTRE_ASPAS = re.compile(r'"[^"]*"')
CONTEUDO = re.compile(r'^\.word <(?:CSWTCH)?\[')          # a pool word shown as the bytes behind it


def constante_mudou(x, y):
    """True when a literal-pool word that renders as the content of an unnamed constant (a local
    `const T t[] = {...}` has no symbol, so the word is shown as the bytes behind it) or of a
    switch table (CSWTCH, which has a number per translation unit and is shown by its content) differs
    between the two sides of one function, other than in the text of the strings it points at.
    so_relocacao( ) reads the function's own bytes and would excuse it: the bytes are equal when
    the pool word sits at the same address on both sides, and only the table behind it changed
    (found 2026-10-14 with tools/test_elf_funcoes.py: one number edited in such a table came out
    "equal"). The text of a string is not this case, it is textos( )'s: an edited month name in
    a local table of names is reported as that text pair and not as the functions that load the
    table. What this leaves hidden: an entry of such a table made to point at another string
    that exists in both images (the pointer changes, no text does)."""
    return any(p != q and (CONTEUDO.match(p) or CONTEUDO.match(q))
               and TEXTOS_ENTRE_ASPAS.sub('""', p) != TEXTOS_ENTRE_ASPAS.sub('""', q)
               for p, q in zip(x, y))


RAMO = re.compile(r'^b[a-z]{0,2}(?:\.[nw])? <')


def chamada_mudou(x, y):
    """True when a branch or a call (`bl <foo>`, `b.n <.+0x1c>`) reads differently in the two sides
    of one function. so_relocacao( ) compares bytes, and the bytes of a relative branch are the same
    wherever the target lies: a function moved so that the displacement is unchanged calls whatever
    is there now, and `bl <foo>` against `bl <bar>` came out "equal by relocation" when the bytes
    were the same (found 2026-10-14 with the two same-named helpers of tools/test_elf_funcoes.py).
    A branch the text names differently is a different target; the second chance is for numbers
    that look like addresses and for pointers, not for this."""
    return any(p != q and (RAMO.match(p) or RAMO.match(q)) for p, q in zip(x, y))


def mudou_de_fato(n, fa, fb, xa, xb, cfa, cfb):
    """A function whose text differs is changed, unless the second chance explains the difference."""
    if [i for i in fa[n] if i.startswith('.casetab')] != [i for i in fb[n] if i.startswith('.casetab')]:
        return True                    # a switch helper's table is bytes: no word of it is a pointer that moved
    return (constante_mudou(fa[n], fb[n]) or chamada_mudou(fa[n], fb[n])
            or not so_relocacao(n, xa, xb, cfa, cfb))


def dados(E):
    """Read-only objects by name -> a list of their contents (bytes, word-aligned), from the ELF
    sections, in address order. Read-only is nm's r/R, and the weak objects nm calls V/v (the
    vtables, the typeinfo, an inline variable) that lie in a section neither writable nor
    executable: nm marks a weak object V wherever it is, and a dictionary of vtables whose slots
    were retargeted to another existing function came out "equal" while they were skipped
    (review of S2b, 2026-10-14). A name can hold more than one object: a file-local
    `static const` table has the same name in every translation unit that declares it, and nm
    lists them all (found 2026-10-14: a dictionary keyed by name kept the one at the highest
    address, and an edit in the other came out "equal"). A table of pointers (a dictionary, a
    font's glyph table, a vtable) holds addresses that follow the layout: comparar_dados( )
    matches them by what they point at, so only a real change of content shows."""
    img = E.img
    res = {}
    for ini, tam, nome, tipo in E.tab:
        if tipo not in 'rRVv' or tam == 0:
            continue
        for (va, off, sz), flg in zip(img.mapas, img.flags):
            if va <= ini and ini + tam <= va + sz:
                if tipo in 'Vv' and ('W' in flg or 'X' in flg):
                    break
                res.setdefault(nome, []).append((img.bruto[off + ini - va: off + ini - va + tam], ini % 4 == 0))
                break
    return res


def alvo(w, inicios, tab, img):
    n = nome_de(w, inicios, tab)
    if n:
        base, _, off = n.partition('+')
        if CSWTCH.match(base):
            # By content, as normalizar( ) names it: "CSWTCH+off" made two tables of any content one
            # target (review of the fixes, 2026-10-15).
            i = bisect.bisect_right(inicios, w) - 1
            return f'CSWTCH[{palavras(img.ler(tab[i][0], tab[i][1]), inicios, tab, img)}]{"+" + off if off else ""}'
        return estavel(base) + ('+' + off if off else '')
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


def comparar_dados(da, db, ctx_a, ctx_b):
    """(objects equal, names whose objects differ, names only in A, names only in B). The objects
    of one name are matched one to one: equal bytes first, then mesmo_dado( ) for the tables of
    pointers the layout moved. An object left without a partner makes its name differ; when the number of objects of a name changes, the report says whether the ones left over have the same bytes (conteúdo igual) or not (conteúdo diferente)."""
    iguais, mudaram = 0, []
    for nome in sorted(set(da) & set(db)):
        la, lb = da[nome], db[nome]
        livres = list(range(len(lb)))
        sem_par = []
        for i, x in enumerate(la):
            j = next((j for j in livres if lb[j][0] == x[0]), None)
            if j is None:
                sem_par.append(i)
            else:
                livres.remove(j)
        resto = []
        for i in sem_par:
            j = next((j for j in livres if mesmo_dado(la[i], lb[j], ctx_a, ctx_b)), None)
            if j is None:
                resto.append(i)
            else:
                livres.remove(j)
        iguais += len(la) - len(resto)
        if resto or livres:
            if len(la) == len(lb):
                mudaram.append(nome)
            else:
                # The number of copies changed. It says "conteúdo igual" only when every copy left over has a twin with the same bytes on the other side
                # and nothing else is unpaired; any other change of the count says "conteúdo diferente". The label used to be the same for both, and
                # an acceptance script took it as proof that only a copy had been dropped (review of S8a C1 to C6, 2026-10-15).
                sobras, outros, sem_par = ([la[i] for i in resto], lb, livres) if len(la) > len(lb) else ([lb[j] for j in livres], la, resto)
                igual = not sem_par and all(any(x[0] == y[0] for y in outros) for x in sobras)
                mudaram.append(f'{nome} ({len(la)} objetos em A, {len(lb)} em B, conteúdo {"igual" if igual else "diferente"})')
    return iguais, mudaram, sorted(set(da) - set(db)), sorted(set(db) - set(da))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--mostrar', type=int, default=10)
    a = ap.parse_args()
    try:
        Ea, Eb = Elf(a.a), Elf(a.b)
    except (OSError, subprocess.CalledProcessError) as exc:
        # A path with no file behind it, or one nm, readelf or objdump will not read, is a refusal,
        # not a difference: uncaught, the traceback exits 1, which reads as "changed" (2026-10-15,
        # a size mutation whose build had failed was compared as if its ELF existed).
        motivo = (getattr(exc, 'stderr', None) or str(exc)).strip()
        print(f'não consegui ler as imagens ({motivo}): veredito recusado', file=sys.stderr)
        return 2
    cfa, cfb = Ea.ctx, Eb.ctx
    ra = [r for r in funcoes(Ea) if not VENEER.match(r[0])]
    rb = [r for r in funcoes(Eb) if not VENEER.match(r[0])]
    fa, xa, fb, xb = parear(ra, rb)
    iguais = [n for n in fa if n in fb and fa[n] == fb[n]]
    diferem = [n for n in fa if n in fb and fa[n] != fb[n]]
    mudaram = [n for n in diferem if mudou_de_fato(n, fa, fb, xa, xb, cfa, cfb)]
    relocadas = len(diferem) - len(mudaram)
    so_a = sorted(set(fa) - set(fb))
    so_b = sorted(set(fb) - set(fa))
    da, db = dados(Ea), dados(Eb)
    ctx_a, ctx_b = cfa, cfb
    # Switch tables the compiler names CSWTCH.<n>: the number follows the translation unit's
    # count, so a function moved to another file renumbers them. They are compared by content.
    anon = re.compile(r'^CSWTCH\.\d+$')
    ta = sorted(palavras(v[0], *ctx_a) for n, lista in da.items() if anon.match(n) for v in lista)
    tb = sorted(palavras(v[0], *ctx_b) for n, lista in db.items() if anon.match(n) for v in lista)
    da = {n: v for n, v in da.items() if not anon.match(n)}
    db = {n: v for n, v in db.items() if not anon.match(n)}
    d_iguais, dmud, dso_a, dso_b = comparar_dados(da, db, ctx_a, ctx_b)
    tabelas_iguais = ta == tb
    ca, cb = textos(Ea.img), textos(Eb.img)
    txt_a = sorted((ca - cb).items())
    txt_b = sorted((cb - ca).items())
    lidas_a, lidas_b = sum(len(v) for v in fa.values()), sum(len(v) for v in fb.values())
    print(f'lido: {len(fa)} funções e {lidas_a} instruções em A, {len(fb)} e {lidas_b} em B')
    if not lidas_a or not lidas_b:
        # An instrument that read nothing says "all equal" about anything (2026-10-14, twice).
        print('nenhuma instrução lida: o formato do objdump mudou? veredito recusado', file=sys.stderr)
        return 2
    for lado, (_, tab_, _) in (('A', cfa), ('B', cfb)):
        if not any(tam and tipo in 'TtWw' for _, tam, _, tipo in tab_):
            # Stripped, objdump still prints the code, under the names of its sections: two "functions"
            # that compare equal about nothing, and no address can be named.
            print(f'nenhum símbolo de código em {lado}: o ELF foi stripped? veredito recusado', file=sys.stderr)
            return 2
    print(f'funções: {len(iguais) + relocadas} iguais ({relocadas} só por relocação de ponteiro ou desvio), '
          f'{len(mudaram)} mudaram, {len(so_a)} só em A, {len(so_b)} só em B')
    print(f'dados só-leitura: {d_iguais} iguais, {len(dmud)} mudaram, '
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
