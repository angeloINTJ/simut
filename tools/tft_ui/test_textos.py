#!/usr/bin/env python3
"""test_textos.py -- proves the Angulo's text lookup (src/ui/angulo/Textos.h and Textos.cpp, S10c B2a) on the host.

UA_TXT( "English" ) gives the active language pack's text for that English, or the English itself; the pack carries the texts in a section of
its own, @ANGULO, one line each: 8 hex digits (FNV-1a 32 of the English), a space, the text. ua::textosCarregar( ) builds one allocation from the
section's body and publishes it through one pointer. Since S10c B2b the Angulo images call it at boot, and since B3 the Painel and Base screens and the kit's two texts look theirs up (B4 and B5 convert the rest); a wrong
table on a board shows only on a board (B7), so this proves the lookup itself, with the standard library and g++ only (no Adafruit GFX).

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

What the review of B2a added (S10c, B2a-b). textosLimpar( ) exists only under UA_HOST: the object of Textos.cpp built without it (g++, and the
project's arm-none-eabi-g++ for the Cortex-M0+ where it is installed) holds no such symbol and a call does not compile, and the object built with it holds
the symbol (the control; another control takes the guard off and the check must fail). The gate's reading of an English is the compiler's: ua::fnv1a( )
of the literals in a program g++ builds equals the gate's hash for each literal that is UTF-8 (an escape split across adjacent pieces included), and the
literals that are not UTF-8 are refused, not read (a control decodes each piece by itself, as the gate did, and the check must fail). One CR before a
newline is a line end and a second one is the text's ("abc\r\r\n"). The publication also runs under ThreadSanitizer (tools/tft_ui/host/
test_textos_tsan_main.cpp: three reader threads and the loader): silent on the original, with the readers having seen the table, and a data race reported
for a relaxed store and for a store before the fill. ThreadSanitizer needs libtsan and does not start on every kernel (here it needs `setarch <arch> -R`):
where it cannot run, the check prints "NOT RUN <name>: <reason>" and the summary counts it, never a silent pass. The gates job does not depend on it.

What B2b added (S10c, B2b). The chain from a pack's bytes to UA_TXT( ), on the device's own text. tools/tft_ui/host/test_textos_carga_main.cpp is built with the real
src/LangPackIndex.h (SIMUT_UI_ANGULO=1) and the real Textos.cpp, and with three cuts of the device's sources made by brace matching, nothing copied: DisplayManager::uiLangLoaded( )
of src/ui/angulo/DisplayManager_UiA_Textos.cpp, the statement of loadLangFile( ) that keeps the @ANGULO range (DisplayManager_LangParser.cpp) and the core's call site of the hook and
the log of its result (AppManager_Boot.cpp). What is stubbed is what the host lacks: LittleFS (a read that can come short, an open that can fail), LOG_CODE (it records), a
DisplayManager reduced to the members the cuts touch (the test checks them against DisplayManager.h) and malloc( ) wrapped to count, to track the peak and to fail on demand. The
planted packs go through the scanner, the loader's store, the hook and UA_TXT( ): a section; none; a header with nothing under it; blank lines only; over UA_TEXTOS_MAX (nothing
opened, nothing allocated) and exactly at it; the section B2a projected for pt-BR (the boot's peak, buffer plus table, is asserted); a directive after @WEBDICT; six spellings of the
directive name (the scanner and the gate agree on each); a section twice (the last wins); a malformed line; a repeated hash; a read that comes short; a file that does not open; no
memory for the buffer and for the table; a range kept with no pack active; a second call. Where the gate has a rule (check_lang_packs.pack_index and check_angulo) it must say the
same. The products' scanner, built with SIMUT_UI_ANGULO=0, prints for seven packs the index (its sizeof included) of the stage tip's src/LangPackIndex.h (git show e2adcc27) and knows
no @ANGULO. The format rule: Textos.h's two pragmas make a looked-up text as a printf format an error on g++ and on the cross-compiler, and the same plants compile without them.
Controls: 15 mutants of the three cuts and of the scanner, each stopped by the case it names, and 3 of the scanner and the capability on the products' side.

Usage: python3 tools/tft_ui/test_textos.py   exit 0 when all hold, 1 when not, 2 when it cannot build.
"""
import concurrent.futures
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import threading

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ['LANG_GATE_NO_RUN'] = '1'          # check_lang_packs runs its gate on import otherwise (tools/test_lang_gate.py does the same)
import check_lang_packs as gate               # noqa: E402
import check_ui_texts as cut                  # noqa: E402  (the gate's reader of literals; a control replaces one of its functions)
import check_net_boundary as cnb              # noqa: E402  (its source stripper: one lexer for the gates)

KIT = ROOT / 'src' / 'ui' / 'angulo'
MAIN = HERE / 'host' / 'test_textos_main.cpp'
PARSER = ROOT / 'src' / 'DisplayManager_LangParser.cpp'
CXX = ['g++', '-std=gnu++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DUA_HOST', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
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
    ('every trailing CR is stripped, not one', 'Textos.cpp', "if ( len && ini[len - 1] == '\\r' ) len--;", "while ( len && ini[len - 1] == '\\r' ) len--;", 'run'),
    ('a blank line is malformed', 'Textos.cpp', 'if ( len == 0 ) return LINHA_VAZIA;', 'if ( len == 0 ) return LINHA_MA;', 'run'),
    ('the texts are cut to ASCII', 'Textos.cpp', 'memcpy( v.t + fim, tx, tam );', 'for ( size_t q = 0; q < tam; q++ ) v.t[fim + q] = tx[q] & 0x7f;', 'run'),
    ('a section with no line publishes an empty table', 'Textos.cpp', '  if ( quantas == 0 ) return TEXTOS_VAZIO;\n', '', 'run'),
    ('a second load replaces the first', 'Textos.cpp', '  if ( __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE ) ) return TEXTOS_JA_CARREGADO;\n', '', 'run'),
    ('malloc( ) failing is not checked', 'Textos.cpp', '  if ( !t ) return TEXTOS_SEM_MEMORIA;\n', '', 'run'),
    ('the recorded size is one byte short', 'Textos.cpp', 't->bytes = bytes;', 't->bytes = bytes - 1;', 'run'),
    ('textosLimpar( ) does not give the table back', 'Textos.cpp', '  free( t );\n}\n#endif\n\nsize_t textosContagem', '  (void)t;\n}\n#endif\n\nsize_t textosContagem', 'run'),
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


# ---- the device build, the English as the compiler's bytes, and the publication under ThreadSanitizer (S10c, B2a-b) -------------------------------

NAO_RODOU = []


def nao_rodou(nome, motivo):
    """A check that cannot run here says so in so many words and the summary counts it: never a pass that says nothing."""
    NAO_RODOU.append((nome, motivo))
    print(f'NOT RUN {nome}: {motivo}', flush=True)


def ferramenta(nome):
    """The path of a tool on the PATH or in a toolchain PlatformIO installed (~/.platformio/packages/toolchain-*/bin), or None."""
    achado = shutil.which(nome)
    for pasta in sorted((pathlib.Path.home() / '.platformio' / 'packages').glob('toolchain-*/bin')):
        if not achado and (pasta / nome).is_file():
            achado = str(pasta / nome)
    return achado


def objeto(pasta, kit, cxx, flags, nome):
    """Textos.cpp of `kit` compiled to an object the way the project compiles it (-Os, one section a function). -> (path or None, first line of stderr)."""
    obj = pasta / nome
    r = subprocess.run([cxx, '-std=gnu++17', '-Os', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror', *flags, f'-I{kit}',
                        '-c', str(kit / 'Textos.cpp'), '-o', str(obj)], capture_output=True, text=True)
    return (obj if r.returncode == 0 else None), (r.stderr.strip().splitlines() or ['?'])[0][:100]


def compila_com(pasta, cxx, flags, codigo, kit):
    arq = pasta / 'trecho_flags.cpp'
    arq.write_text('#include "Textos.h"\n' + codigo + '\n')
    return subprocess.run([cxx, '-std=gnu++17', '-fsyntax-only', *flags, f'-I{kit}', str(arq)], capture_output=True, text=True).returncode == 0


def problemas_do_limpar(pasta, kit, avisar=True):
    """textosLimpar( ) frees the table that Core 1 may be reading, so it exists only under UA_HOST, the define the host harness passes. The object of
    Textos.cpp built the way a device builds it (no UA_HOST) must hold no such symbol, and a call to it must not compile; the same source built with
    UA_HOST must hold it (that one is the control: an empty answer from nm could be a tool that reads nothing). The host's g++ always; the project's
    cross-compiler (Cortex-M0+) where it is installed, and said plainly where it is not."""
    ruins = []
    pernas = [('g++', 'nm', [], 'g++')]
    cross, nm = ferramenta('arm-none-eabi-g++'), ferramenta('arm-none-eabi-nm')
    if cross and nm:
        pernas.append((cross, nm, ['-mthumb', '-mcpu=cortex-m0plus'], 'arm-none-eabi-g++ (Cortex-M0+)'))
    elif avisar:
        nao_rodou('device build: textosLimpar( ) absent from the cross-compiled object',
                  'no arm-none-eabi-g++ and arm-none-eabi-nm on the PATH or in ~/.platformio/packages; only the host compiler ran this check')
    for k, (cxx, nmx, flags, quem) in enumerate(pernas):
        sem, e1 = objeto(pasta, kit, cxx, flags, f'sem_host{k}.o')
        com, e2 = objeto(pasta, kit, cxx, [*flags, '-DUA_HOST'], f'com_host{k}.o')
        if sem is None or com is None:
            ruins.append(f'{quem}: Textos.cpp does not build {"without" if sem is None else "with"} UA_HOST: {e1 if sem is None else e2}')
            continue
        lidos = [subprocess.run([nmx, '-C', str(o)], capture_output=True, text=True) for o in (sem, com)]
        if any(r.returncode for r in lidos):
            ruins.append(f'{quem}: nm cannot read the objects')
        else:
            if 'textosLimpar' not in lidos[1].stdout:
                ruins.append(f'{quem}: the object built WITH UA_HOST has no textosLimpar, so nm could not show its absence from the other')
            if 'textosLimpar' in lidos[0].stdout:
                ruins.append(f'{quem}: textosLimpar( ) is in the object built without UA_HOST, where Core 1 may be reading the table it frees')
        if compila_com(pasta, cxx, flags, 'void f( ) { ua::textosLimpar( ); }', kit):
            ruins.append(f'{quem}: a call to ua::textosLimpar( ) compiles without UA_HOST')
        if not compila_com(pasta, cxx, [*flags, '-DUA_HOST'], 'void f( ) { ua::textosLimpar( ); }', kit):
            ruins.append(f'{quem}: a call to ua::textosLimpar( ) does not compile with UA_HOST, which the host harness needs')
    return ruins


# (a literal as it is written in the source, True when its bytes are valid UTF-8 once the compiler has joined and decoded the pieces). Each is planted
# as UA_TXT( "Z" <literal> ): the leading piece gives every English a letter (the gate lists only texts with one) and keeps the join in the test.
LITERAIS = [
    (r'"a\xE2\x80" "\xA6"', True),          # the review's case: an ellipsis split across two pieces
    (r'"\xF0\x9F" "\x98\x80"', True),       # a 4-byte character split in the middle
    (r'"caf\xC3\xA9"', True), (r'"\t"', True), (r'"\u00e9\u2026"', True), (r'"\303\251"', True), (r'"\x41\x42"', True), (r'"line\n"', True),
    (r'R"(raw "q" \n)"', True), (r'"Two " "pieces"', True), ('"é… ok"', True),
    (r'"\xFF"', False), (r'"\xC3"', False), (r'"\xE2\x80" "x"', False), (r'"\xC0\x80"', False),
]


def problemas_da_leitura(pasta):
    """The gate's reading of an English is the compiler's: the bytes ua::fnv1a( ) hashes in a program the compiler built from the same literals equal the
    gate's (check_lang_packs.angulo_literals( ) and its fnv1a32) for every literal that is UTF-8, and the others are refused, not read."""
    prog = pasta / 'bytes_do_compilador.cpp'
    prog.write_text('#include "Textos.h"\n#include <stdio.h>\nint main( ) {\n'
                    + ''.join(f'  printf( "%08x\\n", (unsigned)ua::fnv1a( "" "Z" {lit} ) );\n' for lit, _ in LITERAIS) + '  return 0;\n}\n')
    exe = pasta / 'bytes_do_compilador'
    r = subprocess.run(['g++', '-std=gnu++17', f'-I{KIT}', str(prog), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        return ['the compiler does not build the literals: ' + (r.stderr.strip().splitlines() or ['?'])[0][:120]]
    hs = [int(x, 16) for x in subprocess.run([str(exe)], capture_output=True, text=True).stdout.split()]
    angulo = pasta / 'angulo_lido'
    angulo.mkdir()
    (angulo / 'plant.cpp').write_text(''.join(f'f( UA_TXT( "Z" {lit} ) );\n' for lit, _ in LITERAIS), encoding='utf-8')
    antes = gate.ANGULO_DIR
    gate.ANGULO_DIR = angulo
    try:
        vivos = {int(w.rsplit(':', 1)[1]): t for t, w in gate.angulo_literals().items()}
        ruins = []
        try:
            recusados = {int(w.rsplit(':', 1)[1]) for w, _ in gate.angulo_bad_literals()}
        except AttributeError as e:
            ruins.append(f'the gate cannot name an English it refuses: {e}')
            recusados = set()
    finally:
        gate.ANGULO_DIR = antes
    for i, (lit, valido) in enumerate(LITERAIS, 1):
        if valido:
            t = vivos.get(i)
            if t is None:
                ruins.append(f'{lit}: the gate does not read it')
            elif gate.fnv1a32(t) != hs[i - 1]:
                ruins.append(f'{lit}: the gate hashes {gate.fnv1a32(t):08x}, the compiler\'s bytes {hs[i - 1]:08x}')
        else:
            if i in vivos:
                ruins.append(f'{lit}: not UTF-8, and the gate reads it ({vivos[i]!r})')
            if i not in recusados:
                ruins.append(f'{lit}: not UTF-8, and the gate does not name it as refused')
    return ruins[:6]


TSAN_MAIN = HERE / 'host' / 'test_textos_tsan_main.cpp'
CXX_TSAN = ['g++', '-std=gnu++17', '-O1', '-g', '-fsanitize=thread', '-fno-omit-frame-pointer', '-pthread', '-DUA_HOST']


def rodar_tsan(lanc, exe, limite):
    """One run of a ThreadSanitizer program. Under a randomised address space it dies at start-up with 'unexpected memory mapping' now and then
    (the layout is drawn at each start): that start is tried again, up to 5 times, and is never read as a run that found nothing. -> (the run,
    whether the program started)."""
    for _ in range(5):
        r = subprocess.run([*lanc, str(exe)], capture_output=True, text=True, timeout=limite, env=dict(os.environ, TSAN_OPTIONS='halt_on_error=0:exitcode=66'))
        if 'FATAL: ThreadSanitizer' not in r.stderr:
            return r, True
    return r, False


def lancador_tsan(pasta):
    """-> (the command prefix that starts a ThreadSanitizer program here, '') or (None, why not). It builds an empty program: on some kernels the first
    start dies with 'unexpected memory mapping' (a randomised address space the shadow memory does not fit); the same program under `setarch <arch> -R`
    (no randomisation) starts."""
    fonte = pasta / 'tsan_vazio.cpp'
    fonte.write_text('int main( ) { return 0; }\n')
    exe = pasta / 'tsan_vazio'
    r = subprocess.run([*CXX_TSAN, str(fonte), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        return None, 'the compiler cannot build with -fsanitize=thread: ' + (r.stderr.strip().splitlines() or ['?'])[0][:120]
    ultimo = ''
    for lanc in ([['setarch', platform.machine(), '-R']] if shutil.which('setarch') else []) + [[]]:
        try:
            tentativas = [subprocess.run([*lanc, str(exe)], capture_output=True, text=True, timeout=60) for _ in range(5)]
        except subprocess.TimeoutExpired:
            ultimo = 'timeout'
            continue
        ruim = next((r for r in tentativas if r.returncode), None)
        if ruim is None:
            return lanc, ''
        ultimo = (ruim.stderr.strip().splitlines() or [f'exit {ruim.returncode}'])[0][:120]
    return None, 'a ThreadSanitizer program does not start here (5 starts in a row), with or without setarch -R: ' + ultimo


def problemas_tsan(pasta, cpp, lanc):
    """The publication under ThreadSanitizer: three reader threads call txt( ) while the loader runs. The original is silent (exit 0, no report) AND the
    readers saw the table (hits > 0: a silence from readers that never saw it proves nothing); a relaxed store and a store before the fill each give a
    data race report, which is what shows the instrument bites. Each run is repeated: ThreadSanitizer sees the races of the run it watched.
    -> (problems, '') or ([], why it could not run: ThreadSanitizer died at start-up)."""
    ruins = []
    _loja = '__atomic_store_n( &gTabela, t, __ATOMIC_RELEASE );'
    casos = [('original', [], False),
             ('a relaxed store', [(_loja, '__atomic_store_n( &gTabela, t, __ATOMIC_RELAXED );')], True),
             ('published before it is filled', [(_loja, ''), ('  t->bytes = bytes;\n', '  t->bytes = bytes;\n  ' + _loja + '\n')], True)]
    for n, (nome, edicoes, quer_corrida) in enumerate(casos):
        pasta_n = pasta / f'tsan{n}'
        pasta_n.mkdir()
        texto = cpp
        for antigo, novo in edicoes:
            texto = aplicar(texto, antigo, novo)
        (pasta_n / 'Textos.cpp').write_text(texto, encoding='utf-8')
        shutil.copy(KIT / 'Textos.h', pasta_n / 'Textos.h')
        exe = pasta_n / 'tsan'
        r = subprocess.run([*CXX_TSAN, f'-I{pasta_n}', str(TSAN_MAIN), str(pasta_n / 'Textos.cpp'), '-o', str(exe)], capture_output=True, text=True)
        if r.returncode:
            ruins.append(f'{nome}: does not build: ' + (r.stderr.strip().splitlines() or ['?'])[0][:100])
            continue
        corridas, vistas = 0, []
        for _ in range(5):
            try:
                r, iniciou = rodar_tsan(lanc, exe, 120)
                if not iniciou:
                    return [], f'{nome}: ThreadSanitizer died at start-up in 5 tries in a row: ' + (r.stderr.strip().splitlines() or ['?'])[0][:100]
            except subprocess.TimeoutExpired:
                ruins.append(f'{nome}: timeout')
                break
            m = re.search(r'tsan run: load=(\d+) hits=(\d+) bad=(\d+)', r.stdout)
            corrida = 'WARNING: ThreadSanitizer: data race' in r.stderr
            corridas += corrida
            vistas.append((r.returncode, corrida, m.groups() if m else None))
            if not quer_corrida and (r.returncode or corrida or not m or m.group(1) != '0' or int(m.group(2)) < 3000 or int(m.group(3))):
                ruins.append(f'{nome}: exit {r.returncode}, race report {corrida}, {m.group(0) if m else "no result line"}; '
                             + (next((ln for ln in r.stderr.splitlines() if 'ThreadSanitizer' in ln), '')[:80]))
                break
            if quer_corrida and corrida:
                break
        if quer_corrida and not corridas:
            ruins.append(f'control "{nome}": ThreadSanitizer reported no race in 5 runs of a broken publication, so its silence on the original proves nothing: {vistas[:2]}')
    return ruins, ''


# ---- the chain of S10c B2b: a pack's bytes -> the scanner -> the loader's store -> uiLangLoaded( ) -> UA_TXT( ) --------------------------------------
# tools/tft_ui/host/test_textos_carga_main.cpp is built with the REAL src/LangPackIndex.h (SIMUT_UI_ANGULO=1) and the REAL Textos.cpp, and with two cuts
# of the device's own text (brace matching, nothing copied): DisplayManager::uiLangLoaded( ) from the Angulo's glue file and the statement of
# loadLangFile( ) that keeps the section's range. What is stubbed is what the host lacks (LittleFS, LOG_CODE, the class reduced to the members the cuts
# touch, and malloc( ) wrapped to count, to track the peak and to fail on demand).

CARGA = HERE / 'host' / 'test_textos_carga_main.cpp'
GATE_LOCK = threading.Lock()
SRC = ROOT / 'src'
GLUE = KIT / 'DisplayManager_UiA_Textos.cpp'
INDICE_H = SRC / 'LangPackIndex.h'
CABECALHO = SRC / 'DisplayManager.h'
BOOT = SRC / 'AppManager_Boot.cpp'
PONTA = 'e2adcc27'   # the stage tip B2b started from: the products' scanner is held to its LangPackIndex.h

# TextosResultado, by number: the context of a refusal in the log is this number (the binary log keeps no text)
R_OK, R_VAZIO, R_GRANDE, R_MALFORMADA, R_DUPLICADA, R_SEM_MEMORIA, R_JA_CARREGADO, R_LEITURA = range(8)
LOG_INFO, LOG_WARN = 1, 2
DETALHE = 'ui_texts'   # the detail of the core's record: the reason is the number
PARES = [('Cancel', 'Cancelar'), ('Save changes', 'Guardar cambios'), ('Naïve café', 'Ingenuo café …'), ('Unused', 'Sin usar')]
CAB = b'@NAME Planted\n@CODE tst\n@DICT\none\n'
RAB = b'@WEBDICT\n{"k":"v"}\n'


def fim_do_bloco_c(texto, abre):
    """The index after the '}' that closes the '{' at `abre`, skipping strings, characters and comments."""
    nivel, i, n = 0, abre, len(texto)
    while i < n:
        c = texto[i]
        if texto.startswith('//', i):
            j = texto.find('\n', i)
            i = n if j < 0 else j
            continue
        if texto.startswith('/*', i):
            j = texto.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in '"\'':
            j = i + 1
            while j < n and texto[j] != c:
                j += 2 if texto[j] == '\\' else 1
            i = j + 1
            continue
        if c == '{':
            nivel += 1
        elif c == '}':
            nivel -= 1
            if nivel == 0:
                return i + 1
        i += 1
    raise Corte('unbalanced braces')


def corte_do_glue(fonte):
    """DisplayManager::uiLangLoaded( ) of the glue file, whole."""
    ms = list(re.finditer(r'int\s+DisplayManager::uiLangLoaded\s*\(\s*\)\s*\{', fonte))
    if len(ms) != 1:
        raise Corte(f'DisplayManager::uiLangLoaded( ) is defined {len(ms)} times in {GLUE.name}, not once')
    return fonte[ms[0].start():fim_do_bloco_c(fonte, ms[0].end() - 1)] + '\n'


def corte_do_carregador(fonte):
    """The statement of loadLangFile( ) that keeps the @ANGULO range, whole."""
    ms = list(re.finditer(r'if\s*\(\s*ix\.end\[LANG_SEC_ANGULO\]\s*>\s*ix\.start\[LANG_SEC_ANGULO\]\s*\)\s*\{', fonte))
    if len(ms) != 1:
        raise Corte(f'the statement that keeps the @ANGULO range appears {len(ms)} times in {PARSER.name}, not once')
    return fonte[ms[0].start():fim_do_bloco_c(fonte, ms[0].end() - 1)] + '\n'


def corte_da_chamada(fonte):
    """The core's call site of the hook in AppManager::setup( ): `if (const int x = _displayMgr->uiLangLoaded( )) { LOG_CODE( ... ); }`, whole."""
    ms = list(re.finditer(r'if\s*\(\s*const\s+int\s+\w+\s*=\s*_displayMgr->uiLangLoaded\(\s*\)\s*\)\s*\{', fonte))
    if len(ms) != 1:
        raise Corte(f'the call site of uiLangLoaded( ) appears {len(ms)} times in {BOOT.name}, not once')
    return fonte[ms[0].start():fim_do_bloco_c(fonte, ms[0].end() - 1)] + '\n'


def codigo_do_log():
    for ln in (ROOT / 'tools' / 'logcodes.tsv').read_text(encoding='utf-8').splitlines():
        c = ln.split('\t')
        if len(c) >= 2 and c[1] == 'I18N_PACK_FAILED':
            return int(c[0])
    raise Corte('I18N_PACK_FAILED is not in tools/logcodes.tsv')


def espelho(pasta, indice_h=None, caps_h=None):
    """A copy of the headers LangPackIndex.h reaches (net/NetCaps.h, ui/UiCaps.h), with `indice_h` / `caps_h` as the scanner / the capabilities when
    given: the mutants and the stage tip's scanner are built from here. -> the directory to pass as -I."""
    (pasta / 'net').mkdir(parents=True, exist_ok=True)
    (pasta / 'ui').mkdir(parents=True, exist_ok=True)
    (pasta / 'LangPackIndex.h').write_text(indice_h if indice_h is not None else INDICE_H.read_text(encoding='utf-8'), encoding='utf-8')
    shutil.copy(SRC / 'net' / 'NetCaps.h', pasta / 'net' / 'NetCaps.h')
    (pasta / 'ui' / 'UiCaps.h').write_text(caps_h if caps_h is not None else (SRC / 'ui' / 'UiCaps.h').read_text(encoding='utf-8'), encoding='utf-8')
    return pasta


def compilar_carga(pasta, glue, carregador, chamada, angulo=True, indice_h=None, caps_h=None, nome='carga'):
    """-> (executable or None, the compiler's stderr)."""
    inc = pasta / 'inc'
    inc.mkdir(parents=True, exist_ok=True)
    (inc / 'glue.inc').write_text(glue, encoding='utf-8')
    (inc / 'carregador.inc').write_text(carregador, encoding='utf-8')
    (inc / 'chamada.inc').write_text(chamada, encoding='utf-8')
    exe = pasta / nome
    r = subprocess.run([*CXX, f'-DSIMUT_UI_ANGULO={1 if angulo else 0}', f'-DI18N_PACK_FAILED_VALUE={codigo_do_log()}', f'-I{espelho(pasta / "src", indice_h, caps_h)}',
                        f'-I{KIT}', f'-I{inc}', str(CARGA), str(KIT / 'Textos.cpp'), *(['-Wl,--wrap=malloc', '-Wl,--wrap=free'] if angulo else []), '-o', str(exe)],
                       capture_output=True, text=True)
    return (exe if r.returncode == 0 else None), r.stderr


def corpo_da_secao(pares):
    return ''.join(f'{gate.fnv1a32(en):08x} {tr}\n' for en, tr in pares).encode('utf-8')


def linha_cheia(tamanho):
    """One valid line of exactly `tamanho` bytes (8 hex digits, a space, the text, the newline)."""
    return b'00000001 ' + b'x' * (tamanho - 10) + b'\n'


def secao_projetada():
    """408 distinct lines, 6,333 B of text: the section B2a projected for pt-BR (10,413 B)."""
    return b''.join(f'{i + 1:08x} '.encode() + b'y' * (16 if i < 213 else 15) + b'\n' for i in range(408))


def rodar_carga(exe, pasta, dados, *args, textos=()):
    """Runs the program on the pack `dados` -> a dict of what it printed, plus 'rc' and 'err'."""
    pasta.mkdir(parents=True, exist_ok=True)
    (pasta / 'p.lng').write_bytes(dados)
    cmd = [str(exe), *args, 'p.lng']
    if textos:
        (pasta / 'textos.hex').write_text(''.join(t.encode('utf-8').hex() + '\n' for t in textos))
        cmd = [str(exe), '--texts', 'textos.hex', *args, 'p.lng']
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=pasta, timeout=120, env=ENV)
    except subprocess.TimeoutExpired:
        return {'rc': 124, 'err': 'timeout'}
    out = {'rc': r.returncode, 'err': r.stderr, 'logs': [], 'txt': {}, 'lit': {}}
    for ln in r.stdout.splitlines():
        k, _, resto = ln.partition(' ')
        if k == 'INDEX':
            campos = resto.split()
            out['sizeof'] = int(campos[0].split('=')[1])
            out['tail'] = campos[1].split('=')[1] == '1'
            out['idx'] = {c.split('=')[0]: tuple(int(x) for x in c.split('=')[1].split(':')) for c in campos[2:]}
        elif k in ('KEEP', 'CALL', 'TABLE'):
            out[k.lower()] = {c.split('=')[0]: int(c.split('=')[1]) for c in resto.split()}
        elif k == 'LOG':
            p = resto.split(' ', 4)
            out['logs'].append((int(p[0]), p[1], int(p[2]), int(p[3]), p[4] if len(p) > 4 else ''))
        elif k == 'TXT':
            h, _, v = resto.partition(' ')
            out['txt'][bytes.fromhex(h).decode('utf-8')] = bytes.fromhex(v).decode('utf-8')
        elif k == 'LIT':
            n, _, v = resto.partition(' ')
            out['lit'][int(n)] = bytes.fromhex(v).decode('utf-8')
    return out


def veredito_do_portao(dados, vivos):
    """check_lang_packs.check_angulo on the same bytes -> True when the gate passes the pack. `vivos` are the English texts the gate is told the sources hold
    (the planted pack's own, so that a section with the right entries has no missing text and no orphan)."""
    import contextlib
    import io
    idx, tail = gate.pack_index(dados)
    if tail:
        return False
    live = {en: f'planted:{k}' for k, en in enumerate(vivos, 1)}
    with GATE_LOCK, contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):   # the redirection is the process's, so one thread at a time
        return bool(gate.check_angulo(pathlib.Path('planted.lng'), dados, idx, live, gate.textos_max(), {}))


def pacote_com(corpo, *, antes=CAB, nome=b'@ANGULO\n', depois=RAB):
    return antes + nome + corpo + depois


def problemas_da_cadeia(exe, pasta):
    """The whole battery on a program built from the (real or mutated) sources -> [(case, what went wrong)]. The real sources give []."""
    ruins, vistos = [], set()
    MAXB = gate.textos_max()
    codigo = codigo_do_log()
    ingles = [en for en, _ in PARES] + ['Not in the pack']
    traduz = dict(PARES)

    def v(caso, cond, texto):
        vistos.add(caso)
        if not cond:
            ruins.append((caso, texto))

    def sem_nada(caso, o, extra=''):
        v(caso, o.get('rc') == 0, f'the program died or a sanitizer spoke: rc {o.get("rc")} {str(o.get("err", ""))[:90]}')
        if o.get('rc') != 0:
            return False
        v(caso, o['call']['live'] == o['table']['bytes'], f'{o["call"]["live"]} B still alive after the call, the table holds {o["table"]["bytes"]}: a buffer was not freed' + extra)
        return True

    # 1. a pack with a section: the range, the call, the record, the texts found by UA_TXT( )
    dados = pacote_com(corpo_da_secao(PARES))
    gi, gt = gate.pack_index(dados)
    o = rodar_carga(exe, pasta / 'secao', dados, textos=ingles)
    if sem_nada('section', o):
        a, b = gi['ANGULO']
        v('section', o['idx'].get('ANGULO') == (1, a, b), f'the scanner found {o["idx"].get("ANGULO")}, the gate {(a, b)}')
        v('section', o['keep'] == {'loaded': 1, 'offset': a, 'len': b - a}, f'the loader kept {o["keep"]}, the section is {a}..{b}')
        v('section', o['call']['mallocs'] == 2 and o['call']['opens'] == 1, f'{o["call"]["mallocs"]} allocations and {o["call"]["opens"]} opens: the buffer and the table, one open')
        v('section', o['logs'] == [], f'a section that loaded left records: {o["logs"]}')
        v('section', o['table']['count'] == len(PARES), f'{o["table"]["count"]} texts in the table')
        v('section', all(o['txt'].get(en) == traduz.get(en, en) for en in ingles), f'ua::txt( ) gave {o["txt"]}')
        v('section', [o['lit'].get(k) for k in range(3)] == [PARES[0][1], PARES[1][1], PARES[2][1]], f'UA_TXT( literal ) gave {o["lit"]}')
        v('section', o['call']['peak'] == (b - a) + o['table']['bytes'], f'the peak is {o["call"]["peak"]} B, the section {b - a} plus the table {o["table"]["bytes"]}')
        v('section', veredito_do_portao(dados, [en for en, _ in PARES]), 'the gate refuses a pack the device loads')

    # 2. no section, and 3. a section header with nothing under it: English everywhere, nothing allocated, nothing opened, nothing said
    for caso, dados in (('no section', CAB + RAB), ('empty section', pacote_com(b''))):
        o = rodar_carga(exe, pasta / caso.replace(' ', '_'), dados, textos=ingles)
        if sem_nada(caso, o):
            v(caso, o['keep']['len'] == 0 and (o['idx']['ANGULO'][0] == 1) == (caso == 'empty section'), f'the loader kept {o["keep"]}, the scanner {o["idx"]["ANGULO"]}')
            v(caso, o['call']['mallocs'] == 0 and o['call']['opens'] == 0, f'{o["call"]["mallocs"]} allocations, {o["call"]["opens"]} opens')
            v(caso, o['logs'] == [] and o['table']['count'] == 0, f'records {o["logs"]}, {o["table"]["count"]} texts')
            v(caso, all(o['txt'].get(en) == en for en in ingles) and o['lit'] == {0: 'Cancel', 1: 'Save changes', 2: 'Naïve café'}, f'not the English: {o["txt"]} {o["lit"]}')

    # 4. blank lines only: the section is read, there is nothing to keep, and it says so
    dados = pacote_com(b'\n\r\n\n')
    o = rodar_carga(exe, pasta / 'blank', dados, textos=ingles)
    if sem_nada('blank lines only', o):
        v('blank lines only', o['logs'] == [(LOG_WARN, 'I18N', codigo, 100 + R_VAZIO, DETALHE)], f'the records are {o["logs"]}')
        v('blank lines only', o['call']['mallocs'] == 1 and o['table']['count'] == 0 and all(o['txt'][en] == en for en in ingles), 'English was not kept everywhere')

    # 5. over the bound: refused before anything is opened or allocated; and exactly at the bound it loads
    dados = pacote_com(linha_cheia(MAXB + 1))
    o = rodar_carga(exe, pasta / 'over', dados, textos=ingles)
    if sem_nada('over the bound', o):
        v('over the bound', o['call']['mallocs'] == 0 and o['call']['opens'] == 0, f'{o["call"]["mallocs"]} allocations and {o["call"]["opens"]} opens before the refusal')
        v('over the bound', o['logs'] == [(LOG_WARN, 'I18N', codigo, 100 + R_GRANDE, DETALHE)], f'the records are {o["logs"]}')
        v('over the bound', o['table']['count'] == 0 and all(o['txt'][en] == en for en in ingles), 'English was not kept everywhere')
        v('over the bound', not veredito_do_portao(dados, ['x' * (MAXB - 10)]), 'the gate passes a section over UA_TEXTOS_MAX')
    dados = pacote_com(linha_cheia(MAXB))
    o = rodar_carga(exe, pasta / 'at_bound', dados)
    if sem_nada('at the bound', o):
        v('at the bound', o['logs'] == [] and o['call']['peak'] == MAXB + o['table']['bytes'],
          f'records {o["logs"]}, peak {o["call"]["peak"]}')
        v('at the bound', o['table']['bytes'] == MAXB + 5 and o['call']['peak'] == 2 * MAXB + 5,
          f'the worst case is {o["call"]["peak"]} B with a table of {o["table"]["bytes"]} B; the glue file says 2 * UA_TEXTOS_MAX + 5 and a table of UA_TEXTOS_MAX + 5')

    # 6. the section B2a projected for pt-BR: the peak the boot costs, buffer plus table
    dados = pacote_com(secao_projetada())
    o = rodar_carga(exe, pasta / 'projected', dados)
    if sem_nada('projected section', o):
        v('projected section', o['keep']['len'] == 10413 and o['table']['bytes'] == 9197 and o['call']['peak'] == 10413 + 9197,
          f'section {o["keep"]["len"]}, table {o["table"]["bytes"]}, peak {o["call"]["peak"]}')

    # 7. a directive after @WEBDICT's marker: the loader refuses the pack, the Angulo hears nothing; the gate refuses it too
    dados = CAB + RAB + b'@ANGULO\n' + corpo_da_secao(PARES)
    o = rodar_carga(exe, pasta / 'after', dados, textos=ingles)
    if sem_nada('after @WEBDICT', o):
        v('after @WEBDICT', o['tail'] and gate.pack_index(dados)[1] and not o['keep']['loaded'], 'the scanner and the gate disagree on a directive after @WEBDICT')
        v('after @WEBDICT', o['call']['mallocs'] == 0 and o['logs'] == [] and all(o['txt'][en] == en for en in ingles), 'the Angulo acted on a refused pack')
        v('after @WEBDICT', not veredito_do_portao(dados, [en for en, _ in PARES]), 'the gate passes a section after @WEBDICT')

    # 8. other names: only the exact name is the section, and the scanner and the gate say the same about each
    corpo = corpo_da_secao(PARES)
    variantes = [(b'@ANGULOX\n', False), (b'@ANGUL\n', False), (b'@angulo\n', False), (b'@ANGULO extra words\n', True), (b'@ANGULO\r\n', True), (b'@ANGULO\tx\n', True)]
    for k, (nome, esperado) in enumerate(variantes):
        dados = pacote_com(corpo, nome=nome)
        gi, _ = gate.pack_index(dados)
        o = rodar_carga(exe, pasta / f'name_{k}', dados, textos=ingles)
        caso = f'directive {nome.strip().decode()!r}'
        if sem_nada(caso, o):
            v(caso, bool(o['idx']['ANGULO'][0]) == esperado and ('ANGULO' in gi) == esperado, f'scanner {o["idx"]["ANGULO"]}, gate {gi.get("ANGULO")}, expected present={esperado}')
            if esperado:
                v(caso, (o['idx']['ANGULO'][1], o['idx']['ANGULO'][2]) == gi['ANGULO'], f'range {o["idx"]["ANGULO"]} against the gate\'s {gi["ANGULO"]}')
                v(caso, o['table']['count'] == len(PARES) and o['txt']['Cancel'] == 'Cancelar', 'the section was not loaded')
            else:
                v(caso, o['call']['mallocs'] == 0 and o['logs'] == [] and all(o['txt'][en] == en for en in ingles), 'a name that is not the section was read as one')
    # a section twice: the last one wins, as in the gate's index (which refuses the pack)
    dados = CAB + b'@ANGULO\n' + corpo_da_secao(PARES[:1]) + b'@ANGULO\n' + corpo_da_secao(PARES[1:3]) + RAB
    o = rodar_carga(exe, pasta / 'twice_in_file', dados, textos=ingles)
    if sem_nada('section twice', o):
        v('section twice', (o['idx']['ANGULO'][1], o['idx']['ANGULO'][2]) == gate.pack_index(dados)[0]['ANGULO'] and o['table']['count'] == 2
          and o['txt']['Cancel'] == 'Cancel' and o['txt']['Save changes'] == 'Guardar cambios', f'the last section did not win: {o["idx"]["ANGULO"]} {o["table"]}')

    # 9. a refused section keeps nothing, not even its good lines; each leaves its reason in the record and frees what it took
    ruim = corpo_da_secao(PARES[:2]) + b'zzzzzzzz not a hash\n'
    duplo = corpo_da_secao(PARES[:2]) + corpo_da_secao(PARES[:1])
    for caso, corpo, res, vivos in (('malformed line', ruim, R_MALFORMADA, [en for en, _ in PARES[:2]]), ('repeated hash', duplo, R_DUPLICADA, [en for en, _ in PARES[:2]])):
        dados = pacote_com(corpo)
        o = rodar_carga(exe, pasta / caso.replace(' ', '_'), dados, textos=ingles)
        if sem_nada(caso, o):
            v(caso, o['logs'] == [(LOG_WARN, 'I18N', codigo, 100 + res, DETALHE)], f'the records are {o["logs"]}')
            v(caso, o['table']['count'] == 0 and all(o['txt'][en] == en for en in ingles), 'a refused section kept something')
            v(caso, not veredito_do_portao(dados, vivos), 'the gate passes what the device refuses')

    # 10. the file: a read that comes short, a file that does not open, and an allocation that fails (the buffer, then the table)
    dados = pacote_com(corpo_da_secao(PARES))
    for caso, args, res, esperado in (('read comes short', ('--short', '5'), R_LEITURA, (1, 1)), ('file does not open', ('--noopen',), R_LEITURA, (0, 0)),
                                      ('no memory for the buffer', ('--nomem', '1'), R_SEM_MEMORIA, (1, 1)), ('no memory for the table', ('--nomem', '2'), R_SEM_MEMORIA, (2, 1))):
        o = rodar_carga(exe, pasta / caso.replace(' ', '_'), dados, *args, textos=ingles)
        if sem_nada(caso, o):
            v(caso, o['logs'] == [(LOG_WARN, 'I18N', codigo, 100 + res, DETALHE)], f'the records are {o["logs"]}')
            v(caso, (o['call']['mallocs'], o['call']['opens']) == esperado, f'{o["call"]["mallocs"]} allocations and {o["call"]["opens"]} opens, expected {esperado}')
            v(caso, o['table']['count'] == 0 and all(o['txt'][en] == en for en in ingles), 'English was not kept everywhere')

    # 11. a stale range with no pack active (the board in English, or the pack refused): the hook does nothing at all
    o = rodar_carga(exe, pasta / 'inactive', dados, '--inactive')
    if sem_nada('no pack active', o):
        v('no pack active', o['keep']['loaded'] == 0 and o['keep']['len'] > 0 and o['call']['mallocs'] == 0 and o['call']['opens'] == 0 and o['logs'] == [],
          f'with the range kept and no pack active: {o["call"]} {o["logs"]}')
    # 12. a second call: the first table stays, the second says so
    o = rodar_carga(exe, pasta / 'twice_called', dados, '--twice', textos=ingles)
    if sem_nada('second call', o):
        v('second call', o['logs'] == [(LOG_WARN, 'I18N', codigo, 100 + R_JA_CARREGADO, DETALHE)]
          and o['txt']['Cancel'] == 'Cancelar', f'the records are {o["logs"]}')
    return ruins, vistos


def problemas_do_original(pasta, indice_atual=None, caps_atual=None):
    """The products' scanner: built with SIMUT_UI_ANGULO=0 from the current LangPackIndex.h and from the stage tip's, it prints the same index (the sizeof of the
    index included) for every planted pack, and knows no @ANGULO. -> [problem]"""
    r = subprocess.run(['git', 'show', f'{PONTA}:src/LangPackIndex.h'], capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        return [f'cannot read src/LangPackIndex.h at {PONTA}: {r.stderr.strip()[:80]}']
    ruins = []
    novo, e1 = compilar_carga(pasta / 'orig_novo', 'int DisplayManager::uiLangLoaded( ) { return 0; }\n', '', '', angulo=False, indice_h=indice_atual, caps_h=caps_atual, nome='orig')
    velho, e2 = compilar_carga(pasta / 'orig_velho', 'int DisplayManager::uiLangLoaded( ) { return 0; }\n', '', '', angulo=False, indice_h=r.stdout, nome='orig')
    if not novo or not velho:
        return [f'the original build of the harness does not compile: {(e1 if not novo else e2).strip().splitlines()[:1]}']
    pacotes = [CAB + RAB, pacote_com(corpo_da_secao(PARES)), pacote_com(b''), CAB + RAB + b'@ANGULO\n' + corpo_da_secao(PARES), pacote_com(corpo_da_secao(PARES), nome=b'@ANGULOX\n'),
               pacote_com(corpo_da_secao(PARES), nome=b'@ANGULO\r\n'), b'@NAME x\n@CODE y\n@DICT\nd\n@HELP\nh\n@LOGCODES\n1\tx\n@TRL\n00000001 t\n@LICENSE\nl\n' + RAB]
    for k, dados in enumerate(pacotes):
        a = rodar_carga(novo, pasta / f'orig_n{k}', dados)
        b = rodar_carga(velho, pasta / f'orig_v{k}', dados)
        if a.get('rc') != 0 or b.get('rc') != 0:
            ruins.append(f'pack {k}: the program failed ({a.get("err", "")[:60]} {b.get("err", "")[:60]})')
        elif (a['sizeof'], a['tail'], a['idx']) != (b['sizeof'], b['tail'], b['idx']):
            ruins.append(f'pack {k}: the products\' index differs from the stage tip\'s: {(a["sizeof"], a["tail"], a["idx"])} against {(b["sizeof"], b["tail"], b["idx"])}')
        elif 'ANGULO' in a['idx']:
            ruins.append(f'pack {k}: the products\' scanner knows @ANGULO')
    return ruins


def problemas_de_forma_da_carga():
    """The places the chain is wired at, read from the sources: nothing here runs, so each is a pattern that fails when the wiring moves."""
    ruins = []
    try:
        glue = GLUE.read_text(encoding='utf-8')
        cab = CABECALHO.read_text(encoding='utf-8')
        boot = BOOT.read_text(encoding='utf-8')
        membros = (SRC / 'ui' / 'UiDriverMembers.h').read_text(encoding='utf-8')
        kit_membros = (KIT / 'Members.h').read_text(encoding='utf-8')
    except OSError as e:
        return [f'cannot read {e.filename}: {e.strerror}']
    m = re.search(r'struct\s+ActiveLang\s*\{', cab)
    if not m:
        return ['struct ActiveLang not found in src/DisplayManager.h']
    est = cab[m.start():fim_do_bloco_c(cab, m.end() - 1)]
    if not re.search(r'char\s+path\[64\]\s*;', est):
        ruins.append('ActiveLang::path is not char[64]: the stub of the harness holds 64')
    if not re.search(r'#if\s+SIMUT_UI_PACK_TEXTS\b[^\n]*\n(?:[^\n#]*\n)*?[^\n]*uint32_t\s+anguloOffset\s*;[^\n]*\n[^\n]*uint32_t\s+anguloLen\s*;', est):
        ruins.append('ActiveLang does not hold uint32_t anguloOffset and anguloLen under #if SIMUT_UI_PACK_TEXTS')
    chamadas = re.findall(r'(?:->|\.)\s*uiLangLoaded\s*\(', ''.join(p.read_text(encoding='utf-8', errors='ignore') for p in SRC.rglob('*') if p.suffix in ('.cpp', '.h', '.c')))
    if len(chamadas) != 1:
        ruins.append(f'uiLangLoaded( ) is called {len(chamadas)} times in src/, not once')
    if not re.search(r'findAndLoadLangFile\(\s*\);\s*\}\s*(?:/\*(?:(?!\*/).)*\*/\s*)?if\s*\(\s*const\s+int\s+\w+\s*=\s*_displayMgr->uiLangLoaded\(\s*\)\s*\)', boot, re.S):
        ruins.append('the call is not right after the block that loads the pack in AppManager::setup( )')
    if not re.search(r'UI_HOOK\s+int\s+uiLangLoaded\(\s*\)\s*\{\s*return\s+0\s*;\s*\}', membros):
        ruins.append("the original's empty always_inline hook is not in src/ui/UiDriverMembers.h")
    if not re.search(r'int\s+uiLangLoaded\(\s*\)\s*;', kit_membros):
        ruins.append("the Angulo's declaration of uiLangLoaded( ) is not in src/ui/angulo/Members.h")
    enum = (re.search(r'enum\s+TextosResultado[^{]*\{([^}]*)\}', (KIT / 'Textos.h').read_text(encoding='utf-8'), re.S) or [None, ''])[1]
    n_enum = len(re.findall(r'^\s*TEXTOS_[A-Z_]+', enum, re.M))
    log_h = re.sub(r'\s*\n\s*\*?\s*', ' ', (SRC / 'SystemDefs_Logging.h').read_text(encoding='utf-8'))
    if n_enum != 8 or not all(x in log_h for x in ('context 100 + ua::TextosResultado', '101 empty', '102 over UA_TEXTOS_MAX', '103 a malformed line', '104 a repeated hash', '105 no memory',
                                                 '106 a table already published', '107 the pack could not be read')):
        ruins.append(f'{n_enum} results of textosCarregar( ) (8 expected: 0 to 7), or the context convention 100 + the result is not written above the I18N codes of SystemDefs_Logging.h')
    for linha in ('#include <stdlib.h>', '#include "Textos.h"'):
        if linha not in glue:
            ruins.append(f'the glue does not include {linha}')
    return ruins


MUTANTES_DA_CARGA = [
    # (name, target, edit, the case that must say so)
    ('the bound is not checked before the open', 'glue', lambda t: aplicar(t, 'if ( len > ua::UA_TEXTOS_MAX ) {', 'if ( false ) {'), 'over the bound'),
    ('the buffer is not freed', 'glue', lambda t: aplicar(t, '        free( secao );\n', ''), 'section'),
    ('the read is not checked', 'glue', lambda t: aplicar(t, 'f.readBytes( secao, len ) == len', '( f.readBytes( secao, len ), true )'), 'read comes short'),
    ('a pack that is not active is read', 'glue', lambda t: aplicar(t, '!_activeLangLoaded || ', ''), 'no pack active'),
    ('a pack without the section allocates', 'glue', lambda t: aplicar(t, '_activeLang.anguloLen == 0', 'false'), 'no section'),
    ('the hook reports no reason', 'glue', lambda t: aplicar(t, 'return (int)r;', 'return (int)r * 0;'), 'blank lines only'),
    ('the core logs the bare result as the context', 'chamada', lambda t: aplicar(t, '100 + langWhy', 'langWhy'), 'blank lines only'),
    ('the core logs at the pack loader\'s own level', 'chamada', lambda t: aplicar(t, 'LOG_WARN', 'LOG_ERROR'), 'blank lines only'),
    ('the core logs a success too', 'chamada', lambda t: aplicar(t, '_displayMgr->uiLangLoaded( )', '_displayMgr->uiLangLoaded( ) + 1'), 'section'),
    ('the loader keeps the end for the offset', 'carregador', lambda t: aplicar(t, 'anguloOffset = ix.start[LANG_SEC_ANGULO]', 'anguloOffset = ix.end[LANG_SEC_ANGULO]'), 'section'),
    ('the loader keeps the end for the length', 'carregador', lambda t: aplicar(t, 'ix.end[LANG_SEC_ANGULO] - ix.start[LANG_SEC_ANGULO]', 'ix.end[LANG_SEC_ANGULO]'), 'section'),
    ('the scanner does not know @ANGULO', 'indice', lambda t: re.sub(r'[ \t]*\{ "ANGULO", 6, LANG_SEC_ANGULO \},[^\n]*\n', '', t, count=1), 'section'),
    ('the scanner has the wrong length for the name', 'indice', lambda t: aplicar(t, '{ "ANGULO", 6,', '{ "ANGULO", 5,'), 'section'),
    ('the scanner takes a longer name for the section', 'indice', lambda t: aplicar(t, '_dirLen == d.len &&', '_dirLen >= d.len &&'), "directive '@ANGULOX'"),
]
MUTANTES_DO_ORIGINAL = [
    ('the scanner\'s entry is outside the capability', 'indice', lambda t: re.sub(r'#if SIMUT_UI_PACK_TEXTS[^\n]*\n(\s*\{ "ANGULO"[^\n]*\n)#endif\n', r'\1', t, count=1)),
    ('the enum entry is outside the capability', 'indice', lambda t: re.sub(r'#if SIMUT_UI_PACK_TEXTS[^\n]*\n(\s*LANG_SEC_ANGULO,[^\n]*\n)#endif\n', r'\1', t, count=1)),
    ('the capability is 1 on the original too', 'caps', lambda t: aplicar(t, '#define SIMUT_UI_PACK_TEXTS (SIMUT_UI_ANGULO)', '#define SIMUT_UI_PACK_TEXTS (1)')),
]


def matar_carga(raiz, n, mut, glue, carregador, chamada, indice):
    """-> (killed by the case it names, how). A mutant that does not build is a failure of the test."""
    nome, alvo, edicao, quem = mut
    pasta = raiz / f'mutante_carga_{n}'
    g, c, ch, i = glue, carregador, chamada, indice
    try:
        if alvo == 'glue':
            g = edicao(glue)
        elif alvo == 'carregador':
            c = edicao(carregador)
        elif alvo == 'chamada':
            ch = edicao(chamada)
        else:
            i = edicao(indice)
    except Corte as e:
        return False, str(e)
    if (g, c, ch, i) == (glue, carregador, chamada, indice):
        return False, 'the edit changed nothing: the text it looks for is not there'
    exe, erro = compilar_carga(pasta, g, c, ch, indice_h=i, nome='mut')
    if not exe:
        return False, 'does not build: ' + (erro.strip().splitlines() or ['?'])[0][:100]
    ruins, _ = problemas_da_cadeia(exe, pasta / 'run')
    if not ruins:
        return False, 'passes every check'
    return any(caso == quem for caso, _ in ruins), f'{len(ruins)} problems, first: {ruins[0][0]}: {ruins[0][1][:60]}' if not any(caso == quem for caso, _ in ruins) else ruins[0][0]


def problemas_do_formato(pasta):
    """Textos.h turns -Wformat-nonliteral and -Wformat-security into errors (S10c, B2b): a looked-up text cannot be a printf format. With the project's
    -Wall -Wextra -Werror, on the host's g++ and on the cross-compiler where it is installed: UA_TXT( ) as a format with arguments fails, as a format alone
    fails, a literal format with the text as an argument compiles, and the same two plants compile once the pragmas are taken out and the two warnings are
    named off (the control: it is the pragma that stops them; Ubuntu's g++ has -Wformat-security on by default, hence the names)."""
    ruins = []
    hdr = (KIT / 'Textos.h').read_text(encoding='utf-8')
    sem = pasta / 'sem_pragma'
    sem.mkdir(parents=True, exist_ok=True)
    (sem / 'Textos.h').write_text(re.sub(r'#pragma GCC diagnostic error "-Wformat-(nonliteral|security)"\n', '', hdr), encoding='utf-8')
    if sem.joinpath('Textos.h').read_text() == hdr:
        return ['Textos.h has no #pragma GCC diagnostic error for -Wformat-nonliteral and -Wformat-security']
    com_args = '#include <stdio.h>\nvoid f( char *b ) { snprintf( b, 16, UA_TXT( "x %d" ), 1 ); }\n'
    sozinho = '#include <stdio.h>\nvoid g( ) { printf( UA_TXT( "hello" ) ); }\n'
    bom = '#include <stdio.h>\nvoid h( char *b ) { snprintf( b, 16, "%d %s", 1, UA_TXT( "min" ) ); }\n'
    pernas = [('g++', [])]
    cross = ferramenta('arm-none-eabi-g++')
    if cross:
        pernas.append((cross, ['-mthumb', '-mcpu=cortex-m0plus']))
    else:
        nao_rodou('format rule on the cross-compiler (arm-none-eabi-g++)', 'not on the PATH or in ~/.platformio/packages; only the host compiler ran this check')
    for cxx, extra in pernas:
        base = ['-Wall', '-Wextra', '-Werror', *extra]
        for nome, codigo, deve_falhar in (('a looked-up text as a format with arguments', com_args, True), ('a looked-up text as the whole format', sozinho, True),
                                          ('a literal format with the text as an argument', bom, False)):
            if compila_com(pasta, cxx, base, codigo, KIT) == deve_falhar:
                ruins.append(f'{cxx}: {nome} {"compiles" if deve_falhar else "does not compile"}')
        for nome, codigo in (('a text as a format with arguments', com_args), ('a text as the whole format', sozinho)):
            if not compila_com(pasta, cxx, [*base, '-Wno-format-nonliteral', '-Wno-format-security'], codigo, sem):
                ruins.append(f'{cxx}: the control fails: {nome} does not compile even without the pragma')
    return ruins



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

        # the device build, the English as the compiler's bytes, the publication under ThreadSanitizer (S10c, B2a-b)
        caso('device build: textosLimpar( ) is there only under UA_HOST (no such symbol in Textos.cpp built without it, and a call does not compile)',
             problemas_do_limpar(tmp, KIT))
        caso(f"English: the gate reads the compiler's bytes on {len([1 for _, v in LITERAIS if v])} literals (a split escape included) and refuses the "
             f"{len([1 for _, v in LITERAIS if not v])} that are not UTF-8", problemas_da_leitura(tmp))
        lanc, porque = lancador_tsan(tmp)
        if lanc is None:
            nao_rodou('publication under ThreadSanitizer (3 reader threads and the loader; a race on a relaxed store and on a store before the fill)', porque)
        else:
            ruins_tsan, porque = problemas_tsan(tmp, cpp, lanc)
            if porque:
                nao_rodou('publication under ThreadSanitizer (3 reader threads and the loader; a race on a relaxed store and on a store before the fill)', porque)
            else:
                caso('publication under ThreadSanitizer' + (f' (started with {" ".join(lanc)})' if lanc else '')
                     + ': silent on the original with the readers seeing the table, a race reported for a relaxed store and for a store before the fill',
                     ruins_tsan)

        # the controls of the first two checks (the ThreadSanitizer one carries its own: the two broken publications)
        sem_guarda = tmp / 'sem_guarda'
        sem_guarda.mkdir()
        (sem_guarda / 'Textos.h').write_text(aplicar(aplicar(hdr, '#ifdef UA_HOST\n', ''), '#endif\n', ''), encoding='utf-8')
        (sem_guarda / 'Textos.cpp').write_text(aplicar(aplicar(cpp, '#ifdef UA_HOST\n', ''), '#endif\n', ''), encoding='utf-8')
        (tmp / 'ctl_limpar').mkdir()
        caso('control: the device-build check fails when the guard is taken off textosLimpar( )',
             [] if problemas_do_limpar(tmp / 'ctl_limpar', sem_guarda, avisar=False) else ['the check passes with textosLimpar( ) in the device build'])
        velho = cut._piece_bytes
        cut._piece_bytes = lambda code, text, a, c: velho(code, text, a, c).decode('utf-8', 'replace').encode('utf-8')
        try:
            (tmp / 'ctl_leitura').mkdir()
            reprovou = problemas_da_leitura(tmp / 'ctl_leitura')
        finally:
            cut._piece_bytes = velho
        caso("control: the English check fails when the gate decodes each piece by itself, as it did before B2a-b",
             [] if reprovou else ['the check passes with the old decoding'])

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

        # the chain of S10c B2b: a pack's bytes -> the scanner -> the loader's store -> uiLangLoaded( ) -> UA_TXT( ) (the device's sources, cut; the host harness)
        try:
            glue_c = corte_do_glue(GLUE.read_text(encoding='utf-8'))
            carregador_c = corte_do_carregador(PARSER.read_text(encoding='utf-8'))
            chamada_c = corte_da_chamada(BOOT.read_text(encoding='utf-8'))
            indice_c = INDICE_H.read_text(encoding='utf-8')
            caps_c = (SRC / 'ui' / 'UiCaps.h').read_text(encoding='utf-8')
            cortou = []
        except (Corte, OSError) as e:
            cortou = [str(e)]
        caso("chain: the device's hook, the loader's store and the core's call site can be cut out of src/ (uiLangLoaded( ) in the Angulo's glue, the @ANGULO range in loadLangFile( ), the log of its result in AppManager::setup( ))", cortou)
        if not cortou:
            pasta_c = tmp / 'cadeia'
            exe_c, erro_c = compilar_carga(pasta_c, glue_c, carregador_c, chamada_c)
            caso("chain: the harness builds with the real LangPackIndex.h (SIMUT_UI_ANGULO=1), the real Textos.cpp and the three cuts",
                 [] if exe_c else ['does not build: ' + (erro_c.strip().splitlines() or ['?'])[0][:160]])
            if exe_c:
                ruins_c, vistos_c = problemas_da_cadeia(exe_c, pasta_c / 'run')
                for nome_c in sorted(vistos_c):
                    caso(f'chain, {nome_c}', [p for c, p in ruins_c if c == nome_c])
            caso('chain: the places it is wired at (the fields of ActiveLang, the one call after the pack load, the hook in both drivers, a reason for every result)',
                 problemas_de_forma_da_carga())
            caso("chain: the products' scanner (SIMUT_UI_ANGULO=0) prints the stage tip's index, sizeof included, for 7 planted packs and knows no @ANGULO",
                 problemas_do_original(tmp / 'original'))
            caso('format rule: a looked-up text as a printf format is an error after Textos.h (-Wformat-nonliteral, -Wformat-security), a literal format with the text as an argument is not',
                 problemas_do_formato(tmp / 'formato'))
            if exe_c:
                alvos_c = list(enumerate(MUTANTES_DA_CARGA))
                with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as pool:
                    feitos_c = list(pool.map(lambda nm: matar_carga(tmp, nm[0], nm[1], glue_c, carregador_c, chamada_c, indice_c), alvos_c))
                for (n, m), (morto, como) in zip(alvos_c, feitos_c):
                    caso(f'control: "{m[0]}" is stopped by "{m[3]}" ({como})', [] if morto else [como])
            for n, (nome_m, alvo_m, edicao_m) in enumerate(MUTANTES_DO_ORIGINAL):
                try:
                    pi, pc = (edicao_m(indice_c), None) if alvo_m == 'indice' else (None, edicao_m(caps_c))
                    achados = problemas_do_original(tmp / f'orig_mut{n}', pi, pc) if (pi, pc) != (None, None) else []
                except Corte as e:
                    achados = []
                    nome_m += f' (the edit does not apply: {e})'
                caso(f'control: "{nome_m}" is seen in the products\' scanner', [] if achados else ['the products\' check passes with the mutant'])
    ruins = [n for n, ok in CASOS if not ok]
    print(f'\n{len(CASOS)} checks, {len(ruins)} failed, {len(NAO_RODOU)} not run' + ''.join(f'\n  NOT RUN {n}: {m}' for n, m in NAO_RODOU))
    return 1 if ruins else 0


if __name__ == '__main__':
    sys.exit(main())
