#!/usr/bin/env python3
"""test_textos.py -- proves the Angulo's text lookup (src/ui/angulo/Textos.h and Textos.cpp, S10c B2a) on the host.

UA_TXT( "English" ) gives the active language pack's text for that English, or the English itself; the pack carries the texts in a section of
its own, @ANGULO, one line each: 8 hex digits (FNV-1a 32 of the English), a space, the text. ua::textosCarregar( ) builds one allocation from the
section's body and publishes it through one pointer. Nothing in the firmware calls it yet (B2b wires the boot), so the images cannot show whether
it works: this does, with the standard library and g++ only (no Adafruit GFX).

What it runs. tools/tft_ui/host/test_textos_main.cpp, built with Textos.cpp under -fsanitize=address,undefined (a read past the section, a leak
and an undefined shift are failures), -Wall -Wextra -Werror and malloc( ) wrapped so that one case can make it fail: a hit, a miss, no section,
an empty one, 15 shapes of malformed line each with its good twin, a repeated hash, a section over UA_TEXTOS_MAX and one of exactly that size,
UTF-8 byte for byte, line ends, 500 entries shuffled and the edges of the binary search, a second load, and 40,000 random and broken sections.

The hash. ua::fnv1a( ) is compared on 50 strings (UTF-8 among them: the signed-char trap) with the function the device runs, which is cut out
of src/DisplayManager_LangParser.cpp (DisplayManager::fnv1a32, by brace matching, renamed) and compiled in, and with the gate's fnv1a32 in
tools/check_lang_packs.py: three readers, none of them the others' copy. The published vectors of FNV-1a are static_asserts in the program.

The numbers the gate reads. UA_TEXTOS_MAX as the compiler sees it equals what check_lang_packs.textos_max( ) reads from the source text.

What the host cannot prove. The order in which another core sees the stores (Core 0 builds the table, Core 1 reads it) needs two cores; this PC
has one view of memory. The test holds the SHAPE instead: one release store of the pointer, the last statement of textosCarregar( ), after the
fill, the sort and the repeat check; every load of the pointer an acquire; no write to it anywhere else; and that txt( ) allocates nothing.
Whether the barriers are enough on the RP2040 and the RP2350 is for the bench (S10c plan, B7). The kit files also stay free of String, std::,
exceptions and new, and of any include but stddef.h, stdint.h, stdlib.h and string.h.

The controls. Every check is bitten: the same program is built from copies of Textos.cpp and Textos.h with one change each (23 of the lookup
and the loader, 5 of the hash and the macro) and must fail; the shape rules and the other static rules have mutants of their own, and the
hash comparison fails when the device's function is changed. A mutant that does not build is a failure of the test, except the ones that are
meant to be stopped by a static_assert.

Usage: python3 tools/tft_ui/test_textos.py   exit 0 when all hold, 1 when not, 2 when it cannot build.
"""
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ['LANG_GATE_NO_RUN'] = '1'          # check_lang_packs runs its gate on import otherwise (tools/test_lang_gate.py does the same)
import check_lang_packs as gate               # noqa: E402
import check_net_boundary as cnb              # noqa: E402  (its source stripper: one lexer for the gates)

KIT = ROOT / 'src' / 'ui' / 'angulo'
MAIN = HERE / 'host' / 'test_textos_main.cpp'
PARSER = ROOT / 'src' / 'DisplayManager_LangParser.cpp'
CXX = ['g++', '-std=gnu++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
       '-fno-omit-frame-pointer']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=0', UBSAN_OPTIONS='print_stacktrace=1')

CASOS = []


def caso(nome, problemas):
    if isinstance(problemas, bool):
        problemas = [] if problemas else ['false']
    CASOS.append((nome, not problemas))
    print(('ok   ' if not problemas else 'FAIL ') + nome + ('' if not problemas else ': ' + '; '.join(problemas[:4])), flush=True)


class Corte(Exception):
    """The device's function cannot be cut out of its source: the test refuses to go on (a test that cannot look must not pass)."""


# ---- the device's hash, cut out of its source -----------------------------------------------------------------------------------------------

def corte_da_funcao_fnv(fonte):
    """DisplayManager::fnv1a32 of the parser source, whole, renamed fnv1a32_dispositivo( ): found by its signature, closed by brace matching."""
    m = re.search(r'uint32_t\s+DisplayManager::fnv1a32\s*\(\s*const\s+char\s*\*\s*s\s*\)\s*\{', fonte)
    if not m:
        raise Corte('DisplayManager::fnv1a32 not found in src/DisplayManager_LangParser.cpp')
    i, nivel = m.end(), 1
    while nivel and i < len(fonte):
        nivel += (fonte[i] == '{') - (fonte[i] == '}')
        i += 1
    if nivel:
        raise Corte('DisplayManager::fnv1a32 is not closed')
    return fonte[m.start():i].replace('DisplayManager::fnv1a32', 'fnv1a32_dispositivo', 1) + '\n'


def amostra_de_50():
    """50 distinct strings: 14 by hand (the empty one, the published vectors, accents, an ellipsis, a degree, CJK, an emoji, 255 bytes, blanks and
    a printf format) and 36 lines of the packs' @DICT, evenly spaced: 24 with UTF-8 bytes in them and 12 without (real text)."""
    fixas = ['', 'a', 'foobar', 'é', '…', '°C', '¿Seguro?', '日本語', 'ok \U0001F600', 'x' * 255, 'ab\tc', ' leading', 'trailing ', 'Detalhe · %s']
    linhas = []
    for p in sorted((ROOT / 'data' / 'lang').glob('*.lng')):
        linhas += [ln for ln in gate.dict_lines(p)[0] if ln]
    utf = list(dict.fromkeys(ln for ln in linhas if not ln.isascii()))
    asc = list(dict.fromkeys(ln for ln in linhas if ln.isascii()))

    def espacadas(grupo, quantas):
        passo = max(1, len(grupo) // quantas)
        return [ln for ln in grupo[::passo] if ln not in fixas][:quantas]

    return fixas + espacadas(utf, 24) + espacadas(asc, 12)


# ---- building and running -------------------------------------------------------------------------------------------------------------------

def compilar(pasta, kit, inc, nome='textos_host'):
    """-> (executable or None, the compiler's stderr). `kit` holds the Textos.cpp and Textos.h to build, `inc` holds device_fnv.inc."""
    exe = pasta / nome
    r = subprocess.run([*CXX, f'-I{kit}', f'-I{inc}', str(MAIN), str(kit / 'Textos.cpp'), '-Wl,--wrap=malloc', '-o', str(exe)],
                       capture_output=True, text=True)
    return (exe if r.returncode == 0 else None), r.stderr


def rodar(exe, *args):
    try:
        return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=180, env=ENV)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(args, 124, '', 'timeout')


def hashes(exe, pasta, strings):
    """[(kit, device, gate)] hex for each string: the program's two functions and the gate's, on the same bytes."""
    arq = pasta / 'amostra.hex'
    arq.write_text(''.join(s.encode('utf-8').hex() + '\n' for s in strings))
    r = rodar(exe, 'hashes', str(arq))
    linhas = r.stdout.split('\n')[:len(strings)]
    if r.returncode or len(linhas) != len(strings):
        return None, (r.stderr.strip().splitlines() or ['no output'])[0]
    return [(*ln.split(), f'{gate.fnv1a32(s):08x}') for ln, s in zip(linhas, strings)], ''


def problemas_do_hash(exe, pasta, strings):
    out, erro = hashes(exe, pasta, strings)
    if out is None:
        return [f'the program did not run: {erro}']
    ruins = [f'{s[:20]!r}: kit {k}, device {d}, gate {g}' for (k, d, g), s in zip(out, strings) if not (k == d == g)]
    return [f'{len(ruins)} of {len(strings)} differ, the first {ruins[0]}'] if ruins else []


def compila_trecho(pasta, kit, codigo):
    arq = pasta / 'trecho.cpp'
    arq.write_text('#include "Textos.h"\n' + codigo + '\n')
    r = subprocess.run(['g++', '-std=gnu++17', '-fsyntax-only', f'-I{kit}', str(arq)], capture_output=True, text=True)
    return r.returncode == 0


def problemas_do_macro(pasta, kit):
    """The macro takes a string literal (or several, joined) and nothing else."""
    ruins = []
    for codigo, deve in (('const char *a() { return UA_TXT( "ok" ); }', True), ('const char *a() { return UA_TXT( "a " "b" ); }', True),
                         ('const char *a( const char *p ) { return UA_TXT( p ); }', False), ('const char *a() { return UA_TXT( 5 ); }', False),
                         ('const char *a() { const char *q = "x"; return UA_TXT( q ); }', False)):
        if compila_trecho(pasta, kit, codigo) != deve:
            ruins.append(f'{codigo!r} {"does not build" if deve else "builds"}')
    return ruins


# ---- the static rules ------------------------------------------------------------------------------------------------------------------------

def corpo_da_funcao(codigo, assinatura):
    """The text between the braces of the function whose signature matches the regex `assinatura` (code with comments and strings blanked)."""
    m = re.search(assinatura, codigo)
    if not m:
        return None
    i, nivel = m.end(), 1
    while nivel and i < len(codigo):
        nivel += (codigo[i] == '{') - (codigo[i] == '}')
        i += 1
    return codigo[m.end():i - 1] if not nivel else None


def forma_da_publicacao(cpp):
    """Problems with how Textos.cpp publishes its table. The host cannot see the order across cores; it sees this shape."""
    codigo, _ = cnb.strip_source(cpp)
    ruins = []
    carregar = corpo_da_funcao(codigo, r'TextosResultado\s+textosCarregar\s*\([^)]*\)\s*\{')
    usar = corpo_da_funcao(codigo, r'const\s+char\s*\*\s*txt\s*\(\s*const\s+char\s*\*\s*en\s*\)\s*\{')
    if carregar is None or usar is None:
        return ['textosCarregar( ) or txt( ) not found']
    lojas = re.findall(r'__atomic_store_n\s*\(\s*&gTabela\s*,[^;]*;', carregar)
    if len(lojas) != 1:
        ruins.append(f'textosCarregar( ) stores the pointer {len(lojas)} times through __atomic_store_n, not once')
    else:
        if not re.search(r',\s*__ATOMIC_RELEASE\s*\)\s*;$', lojas[0]):
            ruins.append('the store of the pointer is not a release store')
        depois = re.sub(r'\s+', ' ', carregar[carregar.index(lojas[0]) + len(lojas[0]):]).strip()
        if depois != 'return TEXTOS_OK;':
            ruins.append(f'the store is not the last thing before the return: {depois[:50]!r}')
        antes = carregar[:carregar.index(lojas[0])]
        for marca in ('malloc(', 'memcpy(', 'ordenar(', 'TEXTOS_DUPLICADA'):
            if marca not in antes:
                ruins.append(f'the store comes before {marca}')
    if len(re.findall(r'\bgTabela\s*=(?!=)', codigo)) != 1:
        ruins.append('the pointer is assigned somewhere besides its initialiser (it is written by the one atomic store)')
    cargas = re.findall(r'__atomic_load_n\s*\(\s*&gTabela\s*,\s*(__ATOMIC_\w+)\s*\)', codigo)
    if len(cargas) < 4 or any(c != '__ATOMIC_ACQUIRE' for c in cargas):
        ruins.append(f'a load of the pointer is not an acquire load: {cargas}')
    if not re.search(r'__atomic_load_n\s*\(\s*&gTabela\s*,\s*__ATOMIC_ACQUIRE\s*\)', usar):
        ruins.append('txt( ) does not read the pointer with an acquire load')
    if re.search(r'\bgTabela\b', usar.replace('&gTabela', '')):
        ruins.append('txt( ) reads the pointer other than through the acquire load')
    return ruins


def regras_de_estilo(nome, texto, permitidos):
    """No String, std::, exceptions, new, delete; includes only from `permitidos`; txt( ) allocates nothing."""
    codigo, inclusoes = cnb.strip_source(texto)
    ruins = []
    for proibido in (r'\bString\b', r'\bstd\b', r'\bthrow\b', r'\btry\b', r'\bcatch\b', r'\bnew\b', r'\bdelete\b', r'\bprintf\b'):
        if re.search(proibido, codigo):
            ruins.append(f'{nome} holds {proibido[2:-2]}')
    for _, inclui, _ in inclusoes:
        if inclui not in permitidos:
            ruins.append(f'{nome} includes {inclui}')
    usar = corpo_da_funcao(codigo, r'const\s+char\s*\*\s*txt\s*\(\s*const\s+char\s*\*\s*en\s*\)\s*\{') if nome.endswith('.cpp') else ''
    if usar is None:
        ruins.append('txt( ) not found')
    elif re.search(r'\b(malloc|calloc|realloc|free|memcpy)\b', usar):
        ruins.append('txt( ) allocates or copies')
    return ruins


# ---- mutants --------------------------------------------------------------------------------------------------------------------------------

# (name, file, text it replaces (once), what replaces it, what must stop it: 'run' the program fails, 'build' the compiler or a static_assert,
#  'hashes' the comparison with the device's function, 'macro' the compile checks of the macro)
MUTANTES = [
    ('the search compares with <= and passes the hit', 'Textos.cpp', 'if ( v.h[meio] < h ) lo = meio + 1;', 'if ( v.h[meio] <= h ) lo = meio + 1;', 'run'),
    ('a miss gives nullptr and not the English', 'Textos.cpp', 'if ( lo == t->n || v.h[lo] != h ) return en;', 'if ( lo == t->n || v.h[lo] != h ) return nullptr;', 'run'),
    ('the final comparison is dropped: a miss inside the table answers another text', 'Textos.cpp', 'if ( lo == t->n || v.h[lo] != h ) return en;', 'if ( lo == t->n ) return en;', 'run'),
    ('the search leaves out the first entry', 'Textos.cpp', 'uint32_t lo = 0, hi = t->n;', 'uint32_t lo = 1, hi = t->n;', 'run'),
    ('the table is not sorted', 'Textos.cpp', '  ordenar( v.h, v.d, quantas );\n', '  (void)ordenar;\n', 'run'),
    ('the offsets do not move with the hashes', 'Textos.cpp', '        d[j] = d[j - folga];\n', '', 'run'),
    ('the sort stops before the gap of 1', 'Textos.cpp', '{ 701, 301, 132, 57, 23, 10, 4, 1 }', '{ 701, 301, 132, 57, 23, 10, 4 }', 'run'),
    ('a repeated hash is accepted', 'Textos.cpp', 'if ( v.h[k] == v.h[k - 1] ) {', 'if ( false && v.h[k] == v.h[k - 1] ) {', 'run'),
    ('a repeated hash leaks the allocation', 'Textos.cpp', '      free( t );\n      return TEXTOS_DUPLICADA;', '      return TEXTOS_DUPLICADA;', 'run'),
    ('the size bound is not checked', 'Textos.cpp', '  if ( n > UA_TEXTOS_MAX ) return TEXTOS_GRANDE;\n', '', 'run'),
    ('the size bound is exclusive', 'Textos.cpp', 'if ( n > UA_TEXTOS_MAX ) return TEXTOS_GRANDE;', 'if ( n >= UA_TEXTOS_MAX ) return TEXTOS_GRANDE;', 'run'),
    ('a bad hex digit is skipped', 'Textos.cpp', 'if ( d < 0 ) return LINHA_MA;', 'if ( d < 0 ) continue;', 'run'),
    ('the space after the hash is not checked', 'Textos.cpp', 'if ( len < 10 || ini[8] != \' \' ) return LINHA_MA;', 'if ( len < 10 ) return LINHA_MA;', 'run'),
    ('a text of blanks is accepted', 'Textos.cpp', 'if ( !visivel ) return LINHA_MA;', 'if ( false && !visivel ) return LINHA_MA;', 'run'),
    ('a NUL in the text is accepted', 'Textos.cpp', "if ( ini[k] == '\\0' ) return LINHA_MA;", "if ( false && ini[k] == '\\0' ) return LINHA_MA;", 'run'),
    ('the CR of a CRLF stays in the text', 'Textos.cpp', "  if ( len && ini[len - 1] == '\\r' ) len--;\n", '', 'run'),
    ('a blank line is malformed', 'Textos.cpp', 'if ( len == 0 ) return LINHA_VAZIA;', 'if ( len == 0 ) return LINHA_MA;', 'run'),
    ('the texts are cut to ASCII', 'Textos.cpp', 'memcpy( v.t + fim, tx, tam );', 'for ( size_t q = 0; q < tam; q++ ) v.t[fim + q] = tx[q] & 0x7f;', 'run'),
    ('a section with no line publishes an empty table', 'Textos.cpp', '  if ( quantas == 0 ) return TEXTOS_VAZIO;\n', '', 'run'),
    ('a second load replaces the first', 'Textos.cpp', '  if ( __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE ) ) return TEXTOS_JA_CARREGADO;\n', '', 'run'),
    ('malloc( ) failing is not checked', 'Textos.cpp', '  if ( !t ) return TEXTOS_SEM_MEMORIA;\n', '', 'run'),
    ('the recorded size is one byte short', 'Textos.cpp', 't->bytes = bytes;', 't->bytes = bytes - 1;', 'run'),
    ('textosLimpar( ) does not give the table back', 'Textos.cpp', '  free( t );\n}\n\nsize_t textosContagem', '  (void)t;\n}\n\nsize_t textosContagem', 'run'),
    ('a null pointer hashes as zero', 'Textos.h', 'if ( !s ) return h;', 'if ( !s ) return 0;', 'build'),
    ('FNV-1 and not FNV-1a (multiply, then xor)', 'Textos.h', '    h ^= (uint8_t)*s++;\n    h *= 0x01000193u;', '    h *= 0x01000193u;\n    h ^= (uint8_t)*s++;', 'build'),
    ('a prime that is not the FNV prime', 'Textos.h', 'h *= 0x01000193u;', 'h *= 0x01000197u;', 'build'),
    ('the bytes are taken signed (a UTF-8 byte sign-extends)', 'Textos.h', 'h ^= (uint8_t)*s++;', 'h ^= *s++;', 'hashes'),
    ('the macro takes any expression', 'Textos.h', '( ::ua::txt( "" en ) )', '( ::ua::txt( en ) )', 'macro'),
]

# Mutants of the publication's shape (static: the program cannot see an order across cores). Each is a list of edits and must give at least one problem.
_LOJA = '__atomic_store_n( &gTabela, t, __ATOMIC_RELEASE );'
_CARGA = 'Tabela *t = __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE );\n  if ( !t ) return en;'
MUTANTES_DA_FORMA = [
    ('a plain store', [(_LOJA, 'gTabela = t;')]),
    ('a relaxed store', [(_LOJA, '__atomic_store_n( &gTabela, t, __ATOMIC_RELAXED );')]),
    ('published before it is filled', [(_LOJA, ''), ('  t->bytes = bytes;\n', '  t->bytes = bytes;\n  ' + _LOJA + '\n')]),
    ('a relaxed load in txt( )', [(_CARGA, _CARGA.replace('__ATOMIC_ACQUIRE', '__ATOMIC_RELAXED'))]),
    ('txt( ) reads the pointer with a plain load', [(_CARGA, 'Tabela *t = gTabela;\n  if ( !t ) return en;')]),
    ('a second store after the first', [('  return TEXTOS_OK;\n}', '  ' + _LOJA + '\n  return TEXTOS_OK;\n}')]),
]


def aplicar(texto, antigo, novo):
    if texto.count(antigo) != 1:
        raise Corte(f'a mutant\'s text occurs {texto.count(antigo)} times, not once: {antigo[:60]!r}')
    return texto.replace(antigo, novo)


def construir_mutante(raiz, n, arquivo, antigo, novo):
    pasta = raiz / f'mut{n}'
    pasta.mkdir()
    for f in ('Textos.cpp', 'Textos.h'):
        texto = (KIT / f).read_text(encoding='utf-8')
        (pasta / f).write_text(aplicar(texto, antigo, novo) if f == arquivo else texto, encoding='utf-8')
    return pasta


def matar(raiz, inc, n, mut, amostra):
    """-> (killed, how). 'run' wants a program that builds and fails; 'build' one the compiler refuses; 'hashes' one that builds and disagrees with the
    device's function; 'macro' one that accepts what the macro must refuse."""
    nome, arquivo, antigo, novo, quem = mut
    pasta = construir_mutante(raiz, n, arquivo, antigo, novo)
    exe, erro = compilar(pasta, pasta, inc, 'mut')
    if quem == 'build':
        parou = exe is None and 'static assertion failed' in erro
        return parou, ('static_assert' if parou else 'it built, or failed otherwise: ' + (erro.strip().splitlines() or ['it built'])[0][:80])
    if exe is None:
        return False, 'does not build: ' + (erro.strip().splitlines() or ['?'])[0][:100]
    if quem == 'hashes':
        p = problemas_do_hash(exe, pasta, amostra)
        return bool(p), p[0][:80] if p else 'the 50 hashes agree'
    if quem == 'macro':
        p = problemas_do_macro(pasta, pasta)
        return bool(p), p[0][:80] if p else 'the macro refuses what it must'
    r = rodar(exe, 'run')
    quais = [ln for ln in r.stdout.splitlines() if ln.startswith('FAIL')]
    if r.returncode == 0:
        return False, 'passes every case'
    return True, (quais[0][5:70] if quais else 'the sanitizer: ' + (r.stderr.strip().splitlines() or ['?'])[0][:60])


def main():
    try:
        fonte = PARSER.read_text(encoding='utf-8')
        fnv = corte_da_funcao_fnv(fonte)
    except Corte as e:
        print(f'cannot cut the device\'s hash: {e}', file=sys.stderr)
        return 2
    amostra = amostra_de_50()
    with tempfile.TemporaryDirectory(prefix='textos_host_') as tmp:
        tmp = pathlib.Path(tmp)
        inc = tmp / 'inc'
        inc.mkdir()
        (inc / 'device_fnv.inc').write_text(fnv)
        exe, erro = compilar(tmp, KIT, inc)
        if not exe:
            print(erro, file=sys.stderr)
            return 2

        # the program's own cases
        r = rodar(exe, 'run')
        vistos = 0
        for ln in r.stdout.splitlines():
            if ln.startswith('ok   '):
                caso(ln[5:], True)
                vistos += 1
            elif ln.startswith('FAIL '):
                nome, _, porque = ln[5:].partition(': ')
                caso(nome, [porque])
                vistos += 1
        caso('the program ran to its end: no sanitizer report, no crash, every case printed',
             [] if r.returncode == 0 and 'cases, 0 failed' in r.stdout and vistos >= 17 else
             [f'exit {r.returncode}, {vistos} cases, {(r.stderr.strip().splitlines() or ["no stderr"])[0][:100]}'])

        # the numbers the gate reads, against the compiler's
        m = re.search(r'^UA_TEXTOS_MAX (\d+)$', r.stdout, re.M)
        lido = gate.textos_max()
        caso('UA_TEXTOS_MAX: the gate reads from the source what the compiler holds', [] if m and int(m.group(1)) == lido else [f'compiler {m and m.group(1)}, gate {lido}'])

        # the hash against the device's function and the gate's
        ruins = problemas_do_hash(exe, tmp, amostra)
        altos = sum(any(b >= 0x80 for b in s.encode('utf-8')) for s in amostra)
        caso(f'hash: ua::fnv1a( ) equals the device\'s fnv1a32 and the gate\'s on {len(amostra)} strings ({altos} with UTF-8 bytes)',
             ruins + ([] if len(amostra) == 50 and altos >= 20 else [f'the sample is {len(amostra)} strings with {altos} of UTF-8, not 50 with 20']))
        caso('the macro takes a string literal (or several) and refuses a variable, a number and a pointer', problemas_do_macro(tmp, KIT))

        # the publication's shape, and the kit's style rules
        cpp = (KIT / 'Textos.cpp').read_text(encoding='utf-8')
        hdr = (KIT / 'Textos.h').read_text(encoding='utf-8')
        caso('publication: one release store, last, after the fill; every load an acquire; no other write (the host cannot prove the order across cores)',
             forma_da_publicacao(cpp))
        caso('style: no String, std::, exceptions, new or printf, includes only stddef.h stdint.h stdlib.h string.h, and txt( ) allocates nothing',
             regras_de_estilo('Textos.cpp', cpp, {'Textos.h', 'stdlib.h', 'string.h'}) + regras_de_estilo('Textos.h', hdr, {'stddef.h', 'stdint.h'}))

        # the controls
        ctl = []
        for nome, edicoes in MUTANTES_DA_FORMA:
            mutante = cpp
            for antigo, novo in edicoes:
                mutante = aplicar(mutante, antigo, novo)
            if not forma_da_publicacao(mutante):
                ctl.append(f'the shape rule passes "{nome}"')
        caso(f'control: the publication rule fails each of its {len(MUTANTES_DA_FORMA)} mutants', ctl)
        ctl = []
        for texto, nome, alvo in ((cpp + '\n#include <vector>\n', 'an include of <vector>', 'Textos.cpp'),
                                  (aplicar(cpp, 'malloc( bytes )', 'new char[bytes]'), 'new', 'Textos.cpp'),
                                  (aplicar(cpp, 'return TEXTOS_DUPLICADA;', 'throw 1; return TEXTOS_DUPLICADA;'), 'throw', 'Textos.cpp'),
                                  (aplicar(cpp, 'if ( !t ) return en;', 'String s; if ( !t ) return en;'), 'String', 'Textos.cpp'),
                                  (aplicar(cpp, 'const uint32_t h = fnv1a( en );', 'const uint32_t h = fnv1a( en ); void *q = malloc( 4 ); free( q );'), 'malloc in txt( )', 'Textos.cpp'),
                                  (aplicar(hdr, '#include <stdint.h>', '#include <stdint.h>\n#include <iostream>'), 'an include of <iostream>', 'Textos.h')):
            if not regras_de_estilo(alvo, texto, {'Textos.h', 'stdlib.h', 'string.h'} if alvo.endswith('.cpp') else {'stddef.h', 'stdint.h'}):
                ctl.append(f'the style rule passes {nome}')
        caso('control: the style rules fail each of their 6 mutants', ctl)

        # the mutants of the program
        alvos = [(n, m) for n, m in enumerate(MUTANTES)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as pool:
            feitos = list(pool.map(lambda nm: matar(tmp, inc, nm[0], nm[1], amostra), alvos))
        for (n, m), (morto, como) in zip(alvos, feitos):
            caso(f'control: "{m[0]}" is stopped ({m[4]})', [] if morto else [como])
        # and the hash comparison fails when the DEVICE's function changes (the cut is the device's, not a copy of the kit's)
        try:
            outro = corte_da_funcao_fnv(aplicar(fonte, 'h *= 0x01000193u;', 'h *= 0x01000197u;'))
        except Corte as e:
            print(f'cannot mutate the device\'s hash: {e}', file=sys.stderr)
            return 2
        inc2 = tmp / 'inc2'
        inc2.mkdir()
        (inc2 / 'device_fnv.inc').write_text(outro)
        exe2, erro2 = compilar(tmp, KIT, inc2, 'dispositivo_mudado')
        caso('control: the comparison fails when the device\'s function changes', ['does not build: ' + erro2[:80]] if not exe2 else
             [] if problemas_do_hash(exe2, tmp, amostra) else ['the 50 hashes still agree'])
    ruins = [n for n, ok in CASOS if not ok]
    print(f'\n{len(CASOS)} checks, {len(ruins)} failed')
    return 1 if ruins else 0


if __name__ == '__main__':
    sys.exit(main())
