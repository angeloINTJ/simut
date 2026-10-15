#!/usr/bin/env python3
"""test_elf_funcoes.py — the proof of tools/elf_funcoes.py, the tool every "the refactor changed no
machine code" claim of the UI-driver stages leans on.

A comparison tool that is wrong in one direction says "equal" about anything, so it is proven on
programs whose difference is known. This compiles small C programs with the PlatformIO ARM
toolchain for the Cortex-M0+ (-mcpu=cortex-m0plus -mthumb -O2 -ffunction-sections
-fdata-sections), links them with -nostdlib and a minimal script (code and read-only data at
0x10000000, .data and .bss at 0x20000000, one function in RAM so that the linker has to put a
veneer in front of its caller), and runs elf_funcoes.py as a subprocess on pairs of images.

The six verdicts, and what each case checks:

  (1) the same ELF twice                          exit 0
  (2) the same objects linked in another order    exit 0; the layout really moved (the two .bin
      differ, a function sits elsewhere, both carry the veneer); two variants of it: a call
      through the veneer against a direct call, a function moved to another file (its
      CSWTCH switch table is renumbered), and a hand-written shim in the manner of the SDK's
      float wrappers whose pointer sits at a halfword address (only the second chance can say
      it is the same), two static helpers of one name whose files are linked in the other order
      (they swap places; they are paired by equal text), and two vtables moved with the layout
  (3) one string edited, same length              exit 1, naming exactly that text and nothing
      else; the string reached through a table of pointers, used directly by a function, in
      .data, and in a local table of names that has no symbol (the functions that load the
      table are not blamed: the text is the census's)
  (4) one constant changed in one function        exit 1, naming exactly that function; also
      when the change sits beside a `movs r0, #1`, so that a pair of halfwords reads as an
      address in SRAM that has no name (a call retargeted from foo to bar, then `return 1`; an
      argument changed from 7 to 8, then `+ 1`): the review of S2b found both called "equal".
      Also a call that goes elsewhere: two static helpers of one name with F and G swapped between
      their files (F and G named, the helpers, whose bodies are the same, not), the body of one
      helper changed (that helper and its caller named, not the other caller), and a call retargeted
      to the function that took its callee's place, so that the bytes of the caller are the same
  (5) a number changed in a static const table    exit 1, naming that table, with the same link
      order and with another; the function that reads it through a pointer is not blamed. Its
      two companions, the cases the second chance of the tool must not hide: the same edit in a
      local initializer that has no symbol (the function that loads it is named), and two
      tables of one name in two files (an edit in either one is named)
  (6) an ELF with no code symbols                 exit 2: data only ("nenhuma instrução lida"),
      stripped ("nenhum símbolo de código"), and one side data only

and, beside them, a value changed in a switch table (exit 1, "CONTEÚDO DIFERENTE"), a slot of a
vtable (a weak read-only object, nm type V) retargeted to another existing function, with the
vtables alone to show it (exit 1 naming both), and the same vtables with the layout moved (exit 0,
the weak objects counted among the read-only ones), and one known
limit, held as an expected failure: an unnamed constant is read as the 64 bytes from its address,
so a layout move that puts other bytes behind it reports the function that loads it (a false
alarm, never a false "equal"). If that stops happening the test says so and asks for the mark to
come off, as for any xfail.

Then the instrument is bitten: each of the mutants below is elf_funcoes.py with one change (an
instruction regex that wants leading blanks, which once made it compare empty bodies; a text
census that skips a section; data compared loosely; the second chance excusing everything;
the correction of 2026-10-14 taken out; and so on), and the cases that fail must be exactly the
ones listed for it. A mutant whose text is no longer in the tool is an error.

Written on 2026-10-14 with GCC 14.3.0 of the PlatformIO toolchain. Three defects of the tool
came out of writing it, and are fixed in tools/elf_funcoes.py: an ELF with no code crashed it
(and a stripped one was read as two "functions" named after its sections), one number edited in
a local initializer with no symbol came out "equal" (the second chance read the function's own
bytes, which had not changed), and two read-only objects of one name were collapsed into one
(30 of 293 in the pico_wiretft image, so an edit in the other passed). The review of S2b found
four more, each with its case (4b, 4c, 4d, 4f, 7v): two addresses with no name counted as the same
target, a vtable and the other weak read-only objects were not compared, two functions of one name
were not told apart, and a call to another function passed as a relocation when the bytes of the
caller were the same.

Needs arm-none-eabi-gcc from the PlatformIO toolchain package (tools/elf_funcoes.py says where it
looks). Without it the test skips with a message, and exits 1 instead when SIMUT_ELF_TOOLCHAIN is
`require`: set it in a job that has the toolchain, so a missing package is a failure there and
not a skip nobody reads. The `gates` job of build.yml has no ARM toolchain (it installs the
native platform only), so this test is not wired into CI yet; the `firmware` jobs have it.

Usage: python3 tools/test_elf_funcoes.py [--keep DIR]   exit 0 all cases and all mutants, 1 not
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import elf_funcoes  # noqa: E402  (the tool and the test look for the toolchain in one place)

BIN = elf_funcoes.TOOL
GCC = BIN / 'arm-none-eabi-gcc'
CFLAGS = ['-mcpu=cortex-m0plus', '-mthumb', '-O2', '-ffunction-sections', '-fdata-sections', '-fno-ident']
LDFLAGS = ['-mcpu=cortex-m0plus', '-mthumb', '-nostdlib', '-nostartfiles', '-Wl,--entry=0x10000000',
           '-Wl,--no-warn-rwx-segments']

SCRIPT = '''MEMORY {
  FLASH ( rx )  : ORIGIN = 0x10000000, LENGTH = 2M
  RAM   ( rwx ) : ORIGIN = 0x20000000, LENGTH = 256K
}
SECTIONS {
  .text : { *( .text .text.* ) } > FLASH
  .rodata : { *( .rodata .rodata.* ) } > FLASH
  .data : { *( .data .data.* ) } > RAM AT> FLASH
  .ramtext : { *( .ramtext .ramtext.* ) } > RAM AT> FLASH
  .bss : { *( .bss .bss.* COMMON ) } > RAM
  /DISCARD/ : { *( .ARM.exidx* .ARM.attributes .comment ) }
}
'''

# a.c: strings. A table of pointers, a literal that a function loads directly, text in .data, and
# a switch (its table is CSWTCH.<n>, numbered per translation unit).
A_C = '''const char *const g_names[] = { "alpha-sensor", "bravo-sensor", "charlie-probe", "delta-gauge" };
char g_tag[] = "data-section-tag";
const char *name_of( int i ) { return g_names[i & 3]; }
const char *banner( void ) { return "SIMUT-TOY-BANNER"; }
char tag_at( int i ) { return g_tag[i & 15]; }
int pre_map( int c ) {
  switch ( c ) {
    case 0: return 41; case 1: return 7; case 2: return 88; case 3: return 15;
    case 4: return 52; case 5: return 36; case 6: return 19; case 7: return 63;
    default: return -2;
  }
}
@MOVED@'''

MAP_CODE = '''int map_code( int c ) {
  switch ( c ) {
    case 0: return 17; case 1: return 4; case 2: return 93; case 3: return 8;
    case 4: return 61; case 5: return 29; case 6: return 12; case 7: return 70;
    default: return -1;
  }
}
'''

# b.c: a constant in a function, a switch, and a static const table of numbers that a function
# reads through a pointer.
B_C = '''int scale( int x ) { return x * 37 + 11; }
@MAP@static const unsigned short k_cal[8] = { 100, 220, 340, 460, 580, 700, 820, 940 };
const unsigned short *cal_table( void ) { return k_cal; }
unsigned cal_at( int i ) { return cal_table( )[i & 7]; }
'''

# c.c: a function in RAM, 0x10000000 bytes from its caller: out of the reach of BL, so the linker
# puts a veneer (__ram_add_veneer) in flash and the caller branches to that.
C_C = '''__attribute__(( @SECTION@noinline )) int ram_add( int a, int b ) { return a + b + 5; }
int call_ram( int x ) { return ram_add( x, x ) ^ 3; }
void entry( void ) { for ( ;; ) { } }
'''
RAM_SECTION = 'section( ".ramtext" ), '

# e.c: local initializers, which have no symbol: the function holds the address of the bytes. One
# of numbers, one of pointers to strings (a local table of month names is the real example).
E_C = '''unsigned local_cal( int i ) {
  const unsigned short t[8] = { 301, 302, 303, 304, 305, 306, 307, 308 };
  return t[i & 7];
}
const char *month_at( int i ) {
  const char *const t[4] = { "uno-month", "dos-month", "tres-month", "cuatro-month" };
  return t[i & 3];
}
'''

# u.c and v.c: a file-local static const table each, with the same name.
U_C = '''static const unsigned short k_tab[4] = { 1, 2, 3, 4 };
unsigned u_tab( int i ) { return k_tab[i & 3]; }
'''
V_C = '''static const unsigned short k_tab[4] = { 5, 6, 7, 8 };
unsigned v_tab( int i ) { return k_tab[i & 3]; }
'''

# p.c and q.c: a call, then `return 1`. `movs r0, #1` is the halfword 0x2001, and the second half of
# the BL before it makes with it the 32-bit window 0x2001xxxx, which reads as an address in SRAM
# that has no name. r.c and s.c: the same with an argument, `movs r1, #7` then `movs r0, #1`.
P_C = '''void foo( void );
void bar( void );
int f( void ) { foo( ); return 1; }
'''
Q_C = '''volatile int g_a, g_b;
__attribute__(( noinline )) void foo( void ) { g_a = 1; }
__attribute__(( noinline )) void bar( void ) { g_b = 2; }
void entry( void ) { for ( ;; ) { } }
'''
Q2_C = '''volatile int g_a, g_b;
__attribute__(( noinline )) void bar( void ) { g_b = 2; }
__attribute__(( noinline )) void foo( void ) { g_a = 1; }
void entry( void ) { for ( ;; ) { } }
'''
R_C = '''int ext( int, int );
int f( void ) { return ext( 1, 7 ) + 1; }
'''
S_C = '''int ext( int a, int b ) { return a * b; }
void entry( void ) { for ( ;; ) { } }
'''

# m.s and n.c: a hand-written shim in the manner of the SDK's float wrappers: after `bx lr` a table
# byte pair and then a POINTER (to the end of the shim) at a halfword address, 2 mod 4. objdump
# prints it as two .word that are not addresses, so the text differs when the shim moves, and only
# the second chance, reading the pointer at its halfword address, can tell it is the same.
M_S = '''    .syntax unified
    .cpu cortex-m0plus
    .thumb
    .section .text.shim, "ax"
    .balign 4
    .global shim_dadd
    .thumb_func
    .type shim_dadd, %function
shim_dadd:
    movs r0, #0
    bx lr
    .byte 0x1c, 0xdf
    .word shim_dadd_end + 1
shim_dadd_end:
    nop
    nop
    .size shim_dadd, . - shim_dadd
'''
N_C = '''int pad( int x ) { return x * 77 + 3; }
const char *pick( int i ) { static const char *const t[] = { "aa", "bb" }; return t[i & 1]; }
void entry( void ) { for ( ;; ) { } }
'''

# vt.cpp and k.c: two classes with inline virtual functions have no key function, so their
# vtables (`vtable for C1`) are weak objects, nm type V, in .rodata. C1 and C2 swap their bases
# between the two images: every function is the same, only a slot of each vtable names another,
# existing, function. The asm barrier hides the dynamic type, so that the call really goes
# through the vtable; k.c is the padding that moves the layout.
VT_CPP = '''struct Base1 { virtual int f( ) { return 1; } virtual int h( ) { return 10; } };
struct Base2 { virtual int f( ) { return 2; } virtual int h( ) { return 20; } };
struct C1 : @BA@ { int h( ) override { return 3; } };
struct C2 : @BB@ { int h( ) override { return 4; } };
int run1( void ) { C1 c; @BA@ *p = &c; __asm__ volatile( "" : "+r"( p ) ); return p->f( ) + p->h( ); }
int run2( void ) { C2 c; @BB@ *p = &c; __asm__ volatile( "" : "+r"( p ) ); return p->f( ) + p->h( ); }
'''
K_C = '''int padk( int x ) { return x * 91 + 9; }
void entry( void ) { for ( ;; ) { } }
'''

# h1.c and h2.c: a file-local helper of the same name in each, with different bodies, and a
# caller of it. F and G swap files between the images: each now calls the other helper, and
# every symbol still reads `bl helper`.
H1_C = '''static __attribute__(( noinline )) int helper( int x ) { return x * 3 + 1; }
int @CALLER@( int x ) { return helper( x ); }
'''
H2_C = '''static __attribute__(( noinline )) int helper( int x ) { return x * 5 + 2; }
int @CALLER@( int x ) { return helper( x ); }
void entry( void ) { for ( ;; ) { } }
'''

# d.c: no code at all.
D_C = '''const int g_only_data[4] = { 1, 2, 3, 4 };
const char g_note[] = "just data, no code";
'''


ASSEMBLY = {'m'}          # the sources that are assembly, not C
CPLUSPLUS = {'vt'}       # and C++, compiled without RTTI or devirtualization
CXXFLAGS = CFLAGS + ['-fno-rtti', '-fno-exceptions', '-fno-devirtualize', '-fno-devirtualize-speculatively']


def fontes(moved=False, ram=True):
    """The sources of the main program. moved: map_code lives in a.c (after pre_map) and not in b.c."""
    return {
        'a': A_C.replace('@MOVED@', MAP_CODE if moved else ''),
        'b': B_C.replace('@MAP@', '' if moved else MAP_CODE),
        'c': C_C.replace('@SECTION@', RAM_SECTION if ram else ''),
        'e': E_C, 'u': U_C, 'v': V_C, 'd': D_C, 'p': P_C, 'q': Q_C, 'q2': Q2_C, 'r': R_C, 's': S_C,
        'm': M_S, 'n': N_C, 'vt': VT_CPP.replace('@BA@', 'Base1').replace('@BB@', 'Base2'), 'k': K_C,
        'h1': H1_C.replace('@CALLER@', 'F'), 'h2': H2_C.replace('@CALLER@', 'G'),
    }


def editar(src, arquivo, antigo, novo):
    """A copy of the sources with one text replaced; the text must be there exactly once."""
    assert src[arquivo].count(antigo) == 1, (arquivo, antigo)
    return dict(src, **{arquivo: src[arquivo].replace(antigo, novo)})


def rodar(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding='utf-8', **kw)
    if r.returncode:
        raise RuntimeError(f'{cmd[0]} failed ({r.returncode}):\n{r.stderr}')
    return r.stdout


class Oficina:
    """Compiles each distinct source once and links the images."""
    def __init__(self, tmp):
        self.tmp = tmp
        self.ld = tmp / 'link.ld'
        self.ld.write_text(SCRIPT)

    def objeto(self, nome, texto):
        h = hashlib.sha1(texto.encode()).hexdigest()[:10]
        o = self.tmp / f'{nome}_{h}.o'
        if not o.exists():
            c = self.tmp / f'{nome}_{h}.{"s" if nome in ASSEMBLY else "cpp" if nome in CPLUSPLUS else "c"}'
            c.write_text(texto)
            rodar([GCC, *(CXXFLAGS if nome in CPLUSPLUS else CFLAGS), '-c', c, '-o', o])
        return o

    def elf(self, rotulo, src, ordem):
        elf = self.tmp / f'{rotulo}.elf'
        rodar([GCC, *LDFLAGS, f'-Wl,-T,{self.ld}', *[self.objeto(n, src[n]) for n in ordem], '-o', elf])
        return elf


def simbolos(elf):
    out = rodar([BIN / 'arm-none-eabi-nm', '-S', elf])
    return {l.split()[-1]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) >= 3}


def binario(elf):
    b = elf.with_suffix('.bin')
    rodar([BIN / 'arm-none-eabi-objcopy', '-O', 'binary', elf, b])
    return b.read_bytes()


class Rel:
    """What the tool printed, as numbers and names."""
    def __init__(self, code, out, err):
        self.code, self.out, self.err = code, out, err
        busca = lambda rx: re.search(rx, out, re.M)
        m = busca(r'^lido: (\d+) funções e (\d+) instruções em A, (\d+) e (\d+) em B')
        self.lido = tuple(map(int, m.groups())) if m else None
        m = busca(r'^funções: (\d+) iguais \((\d+) só por relocação de ponteiro ou desvio\), (\d+) mudaram, '
                  r'(\d+) só em A, (\d+) só em B')
        self.func = tuple(map(int, m.groups())) if m else None
        m = busca(r'^dados só-leitura: (\d+) iguais, (\d+) mudaram, (\d+) só em A, (\d+) só em B; '
                  r'tabelas de switch: (\d+) em A, (\d+) em B, (mesmo conteúdo|CONTEÚDO DIFERENTE)')
        self.dados = tuple(map(int, m.groups()[:6])) if m else None
        self.tabelas_iguais = (m.group(7) == 'mesmo conteúdo') if m else None
        m = busca(r'^textos na flash: (\d+) em A, (\d+) em B; (\d+) só em A, (\d+) só em B')
        self.textos = tuple(map(int, m.groups())) if m else None
        self.mudou = re.findall(r'^  mudou: (.+?) \(\d+ -> \d+ instruções\)', out, re.M)
        self.so_a = re.findall(r'^  só em A: (.+)$', out, re.M)
        self.so_b = re.findall(r'^  só em B: (.+)$', out, re.M)
        self.dado_mudou = re.findall(r'^  dado mudou: (.+?)(?: \(\d+ objetos em A, \d+ em B\))?$', out, re.M)
        self.dado_so = re.findall(r'^  dado só em [AB]: (.+)$', out, re.M)
        self.texto_a = re.findall(r"^  texto só em A: b'(.*)' x\d+$", out, re.M)
        self.texto_b = re.findall(r"^  texto só em B: b'(.*)' x\d+$", out, re.M)

    def so(self, **kw):
        """True when the report names exactly these (and no other) functions, tables, texts."""
        want = dict(mudou=[], so_a=[], so_b=[], dado_mudou=[], dado_so=[], texto_a=[], texto_b=[])
        want.update(kw)
        return all(getattr(self, k) == v for k, v in want.items())

    def nomes(self):
        partes = []
        for k in ('mudou', 'dado_mudou', 'texto_a', 'texto_b', 'so_a', 'so_b', 'dado_so'):
            v = getattr(self, k)
            if v:
                partes.append(f'{k}={v}')
        return ', '.join(partes) or 'nothing named'


def executar(tool, a, b, env):
    r = subprocess.run([sys.executable, str(tool), str(a), str(b), '--mostrar', '50'], capture_output=True,
                       text=True, encoding='utf-8', env=env)
    return Rel(r.returncode, r.stdout, r.stderr)


# ---------------------------------------------------------------------------------------------
# The cases. A case: id, title, the two images, the check on the report, and (optionally) a
# precondition on the images that proves the scenario happened (it does not depend on the tool).

def montar(tmp):
    of = Oficina(tmp)
    s = fontes()
    ordem, volta = ['a', 'b', 'c'], ['c', 'b', 'a']
    e = {}
    e['base'] = of.elf('base', s, ordem)
    e['movida'] = of.elf('movida', s, volta)
    e['str'] = of.elf('str', editar(s, 'a', '"alpha-sensor"', '"alpha-sensoR"'), ordem)
    e['lit'] = of.elf('lit', editar(s, 'a', '"SIMUT-TOY-BANNER"', '"SIMUT-TOY-BANNEr"'), ordem)
    e['tag'] = of.elf('tag', editar(s, 'a', '"data-section-tag"', '"data-section-taG"'), ordem)
    e['const'] = of.elf('const', editar(s, 'b', 'x * 37 + 11', 'x * 37 + 12'), ordem)
    e['tab'] = of.elf('tab', editar(s, 'b', '340, 460', '341, 460'), ordem)
    e['tab_movida'] = of.elf('tab_movida', editar(s, 'b', '340, 460', '341, 460'), volta)
    e['sw'] = of.elf('sw', editar(s, 'b', 'case 4: return 61;', 'case 4: return 62;'), ordem)
    e['sem_veneer'] = of.elf('sem_veneer', fontes(ram=False), ordem)
    e['fn_movida'] = of.elf('fn_movida', fontes(moved=True), ordem)
    anon = ['a', 'b', 'c', 'e']
    e['anon'] = of.elf('anon', s, anon)
    e['anon_edit'] = of.elf('anon_edit', editar(s, 'e', '303,', '399,'), anon)
    e['anon_txt'] = of.elf('anon_txt', editar(s, 'e', '"dos-month"', '"dos-montH"'), anon)
    e['anon_ord'] = of.elf('anon_ord', s, ['e', 'c', 'b', 'a'])
    e['dup'] = of.elf('dup', s, ['u', 'v'])
    e['dup_u'] = of.elf('dup_u', editar(s, 'u', '3, 4', '3, 9'), ['u', 'v'])
    e['dup_v'] = of.elf('dup_v', editar(s, 'v', '7, 8', '7, 9'), ['u', 'v'])
    e['dados'] = of.elf('dados', s, ['d'])
    e['hlp'] = of.elf('hlp', s, ['h1', 'h2'])
    e['hlp_movida'] = of.elf('hlp_movida', s, ['h2', 'h1'])
    e['hlp_troca'] = of.elf('hlp_troca', dict(s, h1=H1_C.replace('@CALLER@', 'G'), h2=H2_C.replace('@CALLER@', 'F')), ['h1', 'h2'])
    e['hlp_mudou'] = of.elf('hlp_mudou', editar(s, 'h1', 'x * 3 + 1', 'x * 3 + 2'), ['h1', 'h2'])
    e['vt'] = of.elf('vt', s, ['vt', 'k'])
    e['vt_movida'] = of.elf('vt_movida', s, ['k', 'vt'])
    sv = dict(s, vt=VT_CPP.replace('@BA@', 'Base2').replace('@BB@', 'Base1'))
    e['vt_troca'] = of.elf('vt_troca', sv, ['vt', 'k'])
    e['shim'] = of.elf('shim', s, ['m', 'n'])
    e['shim_movida'] = of.elf('shim_movida', s, ['n', 'm'])
    e['call_foo'] = of.elf('call_foo', s, ['p', 'q'])
    e['call_bar_trocadas'] = of.elf('call_bar_trocadas', editar(dict(s, q=Q2_C), 'p', 'foo( ); return 1', 'bar( ); return 1'), ['p', 'q'])
    e['call_bar'] = of.elf('call_bar', editar(s, 'p', 'foo( ); return 1', 'bar( ); return 1'), ['p', 'q'])
    e['arg7'] = of.elf('arg7', s, ['r', 's'])
    e['arg8'] = of.elf('arg8', editar(s, 'r', 'ext( 1, 7 )', 'ext( 1, 8 )'), ['r', 's'])
    strip = tmp / 'strip.elf'
    shutil.copy(e['base'], strip)
    rodar([BIN / 'arm-none-eabi-strip', '-s', strip])
    e['strip'] = strip
    return e


def pre_movida(e):
    sa, sb = simbolos(e['base']), simbolos(e['movida'])
    return (binario(e['base']) != binario(e['movida']) and sa['name_of'] != sb['name_of']
            and '__ram_add_veneer' in sa and '__ram_add_veneer' in sb and sa['ram_add'] >= 0x20000000,
            f'name_of at {sa["name_of"]:#x} then {sb["name_of"]:#x}, .bin differ, veneer in both, ram_add at {sa["ram_add"]:#x}')


def pre_sem_veneer(e):
    sa, sb = simbolos(e['base']), simbolos(e['sem_veneer'])
    return ('__ram_add_veneer' in sa and '__ram_add_veneer' not in sb and sb['ram_add'] < 0x20000000,
            'a call through __ram_add_veneer against a direct call to ram_add in flash')


def pre_fn_movida(e):
    na = sorted(n for n in simbolos(e['base']) if n.startswith('CSWTCH'))
    nb = sorted(n for n in simbolos(e['fn_movida']) if n.startswith('CSWTCH'))
    return (na != nb and len(na) == len(nb) == 2 and binario(e['base']) != binario(e['fn_movida']),
            f'switch tables {na} then {nb}')


def pre_anon_ord(e):
    return (binario(e['anon']) != binario(e['anon_ord']), 'the same sources, the unnamed table behind other bytes')


def pre_tab_movida(e):
    return simbolos(e['base'])['k_cal'] != simbolos(e['tab_movida'])['k_cal'], 'k_cal moved'


def cb_igual(r):
    ok = (r.code == 0 and r.so() and r.tabelas_iguais and r.func[2:] == (0, 0, 0))
    return ok, f'exit {r.code}, {r.func[0]} functions equal, {r.dados[0]} read-only objects, ' \
               f'{r.dados[4]} switch tables, {r.textos[0]} texts'


def c1(r):
    ok, d = cb_igual(r)
    # An instrument that reads nothing says "equal": the program has 11 functions (68 instructions
    # with GCC 14.3.0), 2 read-only objects, 2 switch tables and 6 texts, and the report must show
    # it saw them. Floors, not the exact counts, for a compiler that inlines a little more.
    ok = ok and r.lido[0] >= 10 and r.lido[1] >= 50 and r.dados[0] >= 2 and r.dados[4] == 2 and r.textos[0] >= 6
    return ok, d + f', {r.lido[1]} instructions read'


def c2(r):
    return cb_igual(r)


def c2v(r):
    ok, d = cb_igual(r)
    return ok, d


def c3(r):
    return r.code == 1 and r.so(texto_a=['alpha-sensor'], texto_b=['alpha-sensoR']) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c3b(r):
    return r.code == 1 and r.so(texto_a=['SIMUT-TOY-BANNER'], texto_b=['SIMUT-TOY-BANNEr']) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c3c(r):
    return r.code == 1 and r.so(texto_a=['data-section-tag'], texto_b=['data-section-taG']) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c4(r):
    return r.code == 1 and r.so(mudou=['scale']) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def c5(r):
    return r.code == 1 and r.so(dado_mudou=['k_cal']) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def c5b(r):
    return c5(r)


def c5c(r):
    return r.code == 1 and r.so(mudou=['local_cal']) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def c3d(r):
    return r.code == 1 and r.so(texto_a=['dos-month'], texto_b=['dos-montH']) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c2s(r):
    ok, d = cb_igual(r)
    return ok and r.func[1] >= 1, d + f'; {r.func[1]} excused by the second chance'


def c7v(r):
    return r.code == 1 and r.so(dado_mudou=['vtable for C1', 'vtable for C2']) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c2w(r):
    ok, d = cb_igual(r)
    return ok and r.dados[0] >= 2, d + f'; {r.dados[0]} read-only objects compared, the weak ones included'


def c9(r):
    return r.code == 1 and sorted(r.mudou) == ['F', 'G'] and r.so(mudou=r.mudou) and r.tabelas_iguais, \
        f'exit {r.code}: {r.nomes()}'


def c9b(r):
    # the helper that changed, and the one caller that now calls something else; not G
    helpers = [n for n in r.mudou if n.startswith('helper')]
    return r.code == 1 and len(helpers) == 1 and sorted(r.mudou) == sorted(helpers + ['F']) \
        and r.so(mudou=r.mudou) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def corpo_de(elf, nome):
    """The bytes of a function, read as the tool reads them."""
    for linha in rodar([BIN / 'arm-none-eabi-nm', '-S', elf]).splitlines():
        p = linha.split()
        if len(p) == 4 and p[3] == nome:
            return elf_funcoes.Imagem(str(elf)).ler(int(p[0], 16), int(p[1], 16))
    raise KeyError(nome)


def pre_mesmos_bytes(e):
    a, b = corpo_de(e['call_foo'], 'f'), corpo_de(e['call_bar_trocadas'], 'f')
    return a == b and len(a) > 0, f'the {len(a)} bytes of f are the same in both images, and it calls another function'


def pre_hlp_movida(e):
    sa, sb = simbolos(e['hlp']), simbolos(e['hlp_movida'])
    return sa['F'] != sb['F'] and sa['G'] != sb['G'], f'F at {sa["F"]:#x} then {sb["F"]:#x}, the two helpers swapped places'


def c8(r):
    return r.code == 1 and r.so(mudou=['f']) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def c5d(r):
    return r.code == 1 and r.so(dado_mudou=['k_tab']) and r.tabelas_iguais, f'exit {r.code}: {r.nomes()}'


def c5e(r):
    return r.code == 0 and r.so() and r.dados[0] == 2, f'exit {r.code}, {r.dados[0]} objects equal (two tables named k_tab)'


def c6(r):
    return r.code == 2 and 'nenhuma instrução lida' in r.err, f'exit {r.code}: {r.err.strip().splitlines()[0] if r.err.strip() else "-"}'


def c6b(r):
    return r.code == 2 and 'nenhum símbolo de código em A' in r.err, \
        f'exit {r.code}: {r.err.strip().splitlines()[0] if r.err.strip() else "-"}'


def c7(r):
    return r.code == 1 and r.so() and r.tabelas_iguais is False, f'exit {r.code}, switch tables differ: {not r.tabelas_iguais}'


def cx(r):
    # The known limit: reported although the sources are the same. XPASS (exit 0) is a failure.
    if r.code == 0:
        return False, 'XPASS: the unnamed constant is no longer reported when only the layout moved; take the ' \
                      'mark off this case and the note off elf_funcoes.py'
    return r.code == 1 and r.mudou and set(r.mudou) <= {'local_cal', 'month_at'} and r.so(mudou=r.mudou), \
        f'xfail, as recorded: exit {r.code}, {r.nomes()}'


CASOS = [
    # id, title, A, B, check, precondition
    ('1', 'the same ELF twice', 'base', 'base', c1, None),
    ('2', 'the same objects linked in another order', 'base', 'movida', c2, pre_movida),
    ('2v', 'a call through a veneer against a direct call', 'base', 'sem_veneer', c2v, pre_sem_veneer),
    ('2m', 'a function moved to another file, its switch table renumbered', 'base', 'fn_movida', c2v, pre_fn_movida),
    ('2s', 'an assembly shim with a pointer at a halfword address, moved', 'shim', 'shim_movida', c2s, None),
    ('2w', 'two vtables (weak read-only objects), the layout moved', 'vt', 'vt_movida', c2w, None),
    ('2h', 'two static helpers of one name, the files linked in the other order', 'hlp', 'hlp_movida', c2v, pre_hlp_movida),
    ('3', 'one string edited, same length, in a table of pointers', 'base', 'str', c3, None),
    ('3b', 'one string edited, loaded directly by a function', 'base', 'lit', c3b, None),
    ('3c', 'one string edited in .data', 'base', 'tag', c3c, None),
    ('3d', 'one string edited in a local table of names with no symbol', 'anon', 'anon_txt', c3d, None),
    ('4', 'one constant changed in one function', 'base', 'const', c4, None),
    ('5', 'a number changed in a static const table, same layout', 'base', 'tab', c5, None),
    ('5b', 'the same, and the layout moved too', 'base', 'tab_movida', c5b, pre_tab_movida),
    ('5c', 'a number changed in a local initializer with no symbol', 'anon', 'anon_edit', c5c, None),
    ('4d', 'two static helpers of one name, F and G swapped between their files', 'hlp', 'hlp_troca', c9, None),
    ('4e', 'the body of one of the two helpers changed', 'hlp', 'hlp_mudou', c9b, None),
    ('4b', 'a call retargeted, then return 1 (BL and movs r0,#1 read as an address)', 'call_foo', 'call_bar', c8, None),
    ('4f', 'a call retargeted to the function that took its callee place (same displacement, same bytes)', 'call_foo', 'call_bar_trocadas', c8, pre_mesmos_bytes),
    ('4c', 'an argument changed before movs r0,#1 (two movs read as an address)', 'arg7', 'arg8', c8, None),
    ('5d', 'two tables of one name, the first edited', 'dup', 'dup_u', c5d, None),
    ('5e', 'two tables of one name, the second edited', 'dup', 'dup_v', c5d, None),
    ('5f', 'two tables of one name, none edited', 'dup', 'dup', c5e, None),
    ('6', 'an ELF with no code at all', 'dados', 'dados', c6, None),
    ('6b', 'a stripped ELF, no code symbols', 'strip', 'strip', c6b, None),
    ('6c', 'code against an ELF with no code', 'base', 'dados', c6, None),
    ('7', 'a value changed in a switch table', 'base', 'sw', c7, None),
    ('7v', 'a vtable slot retargeted to another existing function (a weak object)', 'vt', 'vt_troca', c7v, None),
    ('x', 'known limit: an unnamed constant, the layout moved', 'anon', 'anon_ord', cx, pre_anon_ord),
]

# Each mutant is the tool with one change: (id, what it does, text in the tool, replacement, the
# cases that must fail).
MUTANTES = [
    ('m1', 'instruction lines must start with blanks (the bug that read empty bodies)',
     "m = re.match(r'^\\s*[0-9a-f]+:\\t(.*)$', linha)", "m = re.match(r'^\\s+[0-9a-f]+:\\t(.*)$', linha)",
     None),
    ('m2', 'the text census skips every section',
     "if nome not in ('.rodata', '.data'):", 'if True:', {'1', '3', '3b', '3c', '3d'}),
    ('m3', 'the text census skips .data', "if nome not in ('.rodata', '.data'):", "if nome not in ('.rodata',):", {'1', '3c'}),
    ('m4', 'read-only objects are never compared', "d_iguais, dmud, dso_a, dso_b = comparar_dados(da, db, ctx_a, ctx_b)",
     "d_iguais, dmud, dso_a, dso_b = len(da), [], [], []", {'5', '5b', '5d', '5e', '5f', '7v'}),
    ('m5', 'same-named objects are matched by position of the first only',
     "la, lb = da[nome], db[nome]", "la, lb = da[nome][-1:], db[nome][-1:]", {'5d', '5f'}),
    ('m6', 'every function the text normalization sees as changed is excused',
     "mudaram = [n for n in diferem if mudou_de_fato(n, fa, fb, xa, xb, cfa, cfb)]",
     "mudaram = []", {'4', '4b', '4c', '4d', '4e', '4f', '5c', 'x'}),
    ('m7', 'a changed unnamed constant is excused (the fix of 2026-10-14 taken out)',
     "constante_mudou(fa[n], fb[n]) or ", "", {'5c'}),
    ('m8', 'switch tables are never different', "tabelas_iguais = ta == tb", "tabelas_iguais = True", {'7'}),
    ('m9', 'an empty reading is accepted', "if not lidas_a or not lidas_b:", "if False:", {'6', '6c'}),
    ('m10', 'a missing code symbol is accepted', "if not any(tam and tipo in 'TtWw' for _, tam, _, tipo in tab_):",
     "if False:", {'6b'}),
    ('m11', 'veneers are not read as the function they branch to',
     "VENEER = re.compile(r'^__(.+)_veneer$')", "VENEER = re.compile(r'^__(.+)_veneer_x$')", {'2v'}),
    ('m12', 'switch tables are named by number, not by content',
     "CSWTCH = re.compile(r'^CSWTCH\\.\\d+$')", "CSWTCH = re.compile(r'^CSWTCHX\\.\\d+$')", {'2m'}),
    ('m13', 'the second chance is never given (a literal or a table edited under a function blames it)',
     "or not so_relocacao(n, xa, xb, cfa, cfb))", "or True)", {'2s', '3b', '3d', '5d', '5e', '7'}),
    ('m15', 'two unnamed addresses count as the same target (the pair BL + movs r0,#1 is excused)',
     "return x == y and not x.startswith('<')", "return x == y", {'4c'}),
    ('m16', 'a pointer at a halfword address is never accepted (the first fix proposed by the review)',
     "ok = mesmo_alvo(xa_, xb_) or mesmo_conteudo(xa_, xb_, va, vb, ra[0] + w, rb[0] + w)",
     "ok = (ra[0] + w) % 4 == 0 and (mesmo_alvo(xa_, xb_) or mesmo_conteudo(xa_, xb_, va, vb, ra[0] + w, rb[0] + w))",
     {'2s'}),
    ('m17', 'weak read-only objects (vtables, typeinfo, inline variables) are skipped',
     "if tipo not in 'rRVv' or tam == 0:", "if tipo not in 'rR' or tam == 0:", {'2w', '7v'}),
    ('m18', 'a branch the text names differently is excused when the bytes are the same',
     "return (constante_mudou(fa[n], fb[n]) or chamada_mudou(fa[n], fb[n])", "return (constante_mudou(fa[n], fb[n])", {'4d', '4e', '4f'}),
    ('m19', 'same-named functions are paired by address order only',
     "j = next((j for j in livres if lb[j][2] == r[2]), None)", "j = None", {'2h'}),
    ('m20', 'a reference to a shared name carries no colour',
     "self.tab_ref = renomear(self.tab, self.cores, dup_nomes)", "self.tab_ref = self.tab", {'4d', '4e'}),
    ('m14', 'a changed string in an unnamed table blames the functions that load it',
     "and TEXTOS_ENTRE_ASPAS.sub('\"\"', p) != TEXTOS_ENTRE_ASPAS.sub('\"\"', q)", "", {'3d'}),
]


def avaliar(tool, imagens, env, pool):
    """Runs every case with a tool; {id: (ok, detail)}."""
    futuros = {c[0]: pool.submit(executar, tool, imagens[c[2]], imagens[c[3]], env) for c in CASOS}
    out = {}
    for cid, titulo, _, _, chk, _ in CASOS:
        r = futuros[cid].result()
        try:
            out[cid] = chk(r) if r.lido is not None or r.code == 2 else (False, f'no report (exit {r.code}): {r.err.strip()[-200:]}')
        except (TypeError, IndexError) as exc:       # a report that lacks a line
            out[cid] = (False, f'unreadable report (exit {r.code}): {exc!r}')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--keep', help='build in this folder and leave the images there')
    args = ap.parse_args()

    if not GCC.is_file():
        msg = f'arm-none-eabi-gcc not found at {GCC} (the PlatformIO toolchain-rp2040-earlephilhower package)'
        if os.environ.get('SIMUT_ELF_TOOLCHAIN') == 'require':
            print('FAIL:', msg, '(SIMUT_ELF_TOOLCHAIN=require)')
            return 1
        print('SKIP:', msg, '- nothing was tested')
        return 0

    env = dict(os.environ, PYTHONIOENCODING='utf-8')
    versao = rodar([GCC, '--version']).splitlines()[0]
    print(f'toolchain: {versao}')
    falhas = []
    with (tempfile.TemporaryDirectory(prefix='elf_funcoes_test_') if not args.keep else _Pasta(args.keep)) as tmpdir:
        tmp = Path(tmpdir)
        imagens = montar(tmp)
        with ThreadPoolExecutor(max_workers=8) as pool:
            real = avaliar(HERE / 'elf_funcoes.py', imagens, env, pool)
            print(f'\n{len(CASOS)} cases against tools/elf_funcoes.py:')
            for cid, titulo, _, _, _, pre in CASOS:
                ok, detalhe = real[cid]
                if pre:
                    pok, pdet = pre(imagens)
                    if not pok:
                        ok, detalhe = False, f'the scenario did not happen ({pdet}); {detalhe}'
                    else:
                        detalhe += f' [{pdet}]'
                print(f'  {"ok  " if ok else "FAIL"} ({cid}) {titulo}: {detalhe}')
                if not ok:
                    falhas.append(cid)

            print(f'\n{len(MUTANTES)} mutants of the tool (the cases that fail must be exactly the listed ones):')
            fonte = (HERE / 'elf_funcoes.py').read_text(encoding='utf-8')
            for mid, titulo, antigo, novo, esperado in MUTANTES:
                if fonte.count(antigo) != 1:
                    print(f'  FAIL {mid}: its text is {fonte.count(antigo)} times in elf_funcoes.py, not once: {antigo[:60]!r}')
                    falhas.append(mid)
                    continue
                mut = tmp / f'mutante_{mid}.py'
                mut.write_text(fonte.replace(antigo, novo), encoding='utf-8')
                res = avaliar(mut, imagens, env, pool)
                caem = {cid for cid, (ok, _) in res.items() if not ok}
                # m1 reads no instruction at all: every case that expects a report must fail.
                alvo = esperado if esperado is not None else {c[0] for c in CASOS if c[0] not in ('6', '6c')}
                ok = caem == alvo
                print(f'  {"ok  " if ok else "FAIL"} {mid} {titulo}: fails {sorted(caem, key=_ordem)}'
                      + ('' if ok else f', expected {sorted(alvo, key=_ordem)}'))
                if not ok:
                    falhas.append(mid)
    print()
    if falhas:
        print(f'FAIL: {len(falhas)} of {len(CASOS) + len(MUTANTES)}: {", ".join(falhas)}')
        return 1
    print(f'OK: {len(CASOS)} cases and {len(MUTANTES)} mutants')
    return 0


def _ordem(cid):
    m = re.match(r'(\d*)(\D*)', cid)
    return (int(m.group(1)) if m.group(1) else 99, m.group(2))


class _Pasta:
    def __init__(self, caminho):
        self.caminho = caminho

    def __enter__(self):
        Path(self.caminho).mkdir(parents=True, exist_ok=True)
        return self.caminho

    def __exit__(self, *a):
        return False


if __name__ == '__main__':
    sys.exit(main())
