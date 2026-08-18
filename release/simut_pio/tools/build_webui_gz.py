"""
PlatformIO pre-build script — regenerates WebUI_GZ.h from WebUI.h.

Compresses PROGMEM blocks with gzip level 9 and writes the header consumed
by WebManager_Core.cpp, plus the <PAGE>_SERVE macro that binds each route to
the partition its asset landed on.

The output depends on three things — the source, this script, and the env's
page layout — and the stamp on line 2 carries all three, so the "already up to
date" shortcut cannot hand one environment another one's header.

A minificacao roda antes do gzip e e feita por um escaner com estado, nao por
regex — ver o bloco "Minificacao com contexto" abaixo para o porque. Tres
portoes protegem o resultado, do mais exato ao mais grosseiro:
`_assert_only_whitespace_removed` (o conteudo e byte a byte o mesmo),
`_syntax_check_scripts` (ainda compila) e `_assert_not_gutted` (nao encolheu
demais). Os testes do escaner estao em tools/test_webui_minify.py.

Sem dependencia externa de proposito: quem compila a partir do zip da release
precisa que isto funcione com o Python da maquina e mais nada.

Project: SIMUT
License: MIT
"""

import os
import re
import gzip
import hashlib

try:
    Import("env")
    PROJECT_DIR = env.subst("$PROJECT_DIR")
    PIOENV      = env["PIOENV"]
    DIET_RAW    = env.GetProjectOption("custom_fs_pages", "")
except NameError:
    # Running standalone (not inside PlatformIO) — use CWD. No env means no
    # diet: the standalone path is what build_release_pio.sh calls, and a
    # release package carries the complete interface by definition.
    import sys

    PROJECT_DIR = os.getcwd()
    PIOENV      = ""
    DIET_RAW    = ""
INPUT_FILE  = os.path.join(PROJECT_DIR, "WebUI.h")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "src", "WebUI_GZ.h")

# ── Where each page lives: firmware image, or LittleFS ──────────────────────
#
# A page moved to the filesystem is still gzipped — un-gzipping would make it
# ~5x BIGGER, not smaller — it just lives on the 1 MB filesystem partition
# instead of the app slot, and WebManager::serveProtectedFsPage streams it
# from there. The trade is a flash read per request instead of a PROGMEM
# pointer, plus a deploy step, in exchange for app-slot headroom.
#
# THE RULE, from 2026-08-17: a SHIPPING image carries the complete interface.
# Every page, in flash, no deploy step, no route that can answer "Page asset
# missing". The diet is declared PER ENVIRONMENT (custom_fs_pages in
# platformio.ini) and _resolve_diet below refuses it outright for the envs in
# SHIPPING_ENVS.
#
# This used to be one global constant, and that is what made the two images
# fight over one budget. pico_w_test carries the full serial CLI and had 152 B
# of real headroom in August 2026, so LICENSE_PAGE and then ALARMS_PAGE were
# moved out — out of BOTH images, because the constant could not tell them
# apart. The release paid for the test image's problem, and paid twice: the
# pages left the firmware, and data/web/*.gz became a deploy step that the zips
# did not carry (units built from the v2.2.5-beta zip had a broken /license).
#
# Measured on 2026-08-17, at e14170f, with arm-none-eabi-size:
#
#   release, 10 pages embedded + 2 on LittleFS ...... 30892 B free
#   release, all 12 embedded ....................... 18740 B free   <- linked
#   test,    all 12 embedded ....................... overflowed by 856 B
#   test,    all 12 + SIMUT_MDNS=0 ................. 14520 B free
#   test,    all 12 + SIMUT_MDNS=0 + strtol ........ 22044 B free   <- shipped
#
# So the release never needed the diet at all, and the test image ends up with
# twice the headroom it had while carrying MORE pages than before. The
# mechanism stays because the day instrumentation needs 30 KB again, it has to
# come out of the test image and nowhere else.
#
# PICKING A PAGE FOR A DIET (only ever for a test env): big AND rarely opened
# AND not needed to bring the device up. All three, or the cure is worse.
# CFG_PAGE was tried in July 2026 and broke the bootstrap — a freshly
# formatted device answered /config, the page you need to configure it, with
# "Page asset missing". FILE_PAGE is circular: it is how you upload the very
# file that is missing. HIST_PAGE is the biggest at 32 KB and the hottest page
# there is; moving it costs +12.5 ms per load (measured), because the mutex is
# taken once per KB, not once per request. LOGIN_PAGE, FORCE_CHPASS_PAGE,
# STYLE_CSS and LANG_JS are not in DIETABLE at all: the first two lock the
# device out if the file is missing, and the other two are loaded by eight
# pages each and have no FS route (they are served with their own cache
# headers, not through serveProtectedFsPage).
#
# DEPLOYING a diet page: the file must reach /web/ on the device or the route
# answers "Page asset missing". Upload it through the Files page or POST to
# /api/upload. NOT `uploadfs` — that reformats the partition and takes the
# history, logs and calib.csv with it.

# Envs that a user's device runs. These carry the whole interface, always.
SHIPPING_ENVS = {"pico_w_release", "pico_w_alpha", "pico_w_asserts"}

# The only pages a diet may name, and the file each becomes on the device.
# The set is exactly the routes that go through serveProtectedPage, which is
# the one server path that has a filesystem twin.
DIETABLE = {
    "DASH_PAGE":    "dash.html.gz",
    "HIST_PAGE":    "history.html.gz",
    "CFG_PAGE":     "config.html.gz",
    "NET_PAGE":     "network.html.gz",
    "USR_PAGE":     "users.html.gz",
    "FILE_PAGE":    "files.html.gz",
    "ALARMS_PAGE":  "alarms.html.gz",
    "LICENSE_PAGE": "license.html.gz",
}

FS_OUT_DIR = os.path.join(PROJECT_DIR, "data", "web")


def _resolve_diet() -> dict:
    """Read custom_fs_pages for this env, and refuse it where it must not be.

    Failing the build is the point. The invariant "a shipping image carries
    the complete interface" is worth exactly as much as the thing that
    enforces it, and the last enforcement was a comment.
    """
    names = [n for n in re.split(r"[,\s]+", DIET_RAW) if n]
    if not names:
        return {}
    if PIOENV in SHIPPING_ENVS:
        raise SystemExit(
            f"build_webui_gz: {PIOENV} e um ambiente de producao e declarou "
            f"custom_fs_pages = {', '.join(names)}.\n"
            f"Imagem de producao carrega a interface INTEIRA no firmware: sem "
            f"passo de implantacao e sem rota que responda 'Page asset "
            f"missing'. A dieta existe so para os ambientes instrumentados."
        )
    unknown = [n for n in names if n not in DIETABLE]
    if unknown:
        raise SystemExit(
            f"build_webui_gz: custom_fs_pages nomeia {', '.join(unknown)}, que "
            f"nao pode sair do firmware.\n"
            f"Elegiveis: {', '.join(sorted(DIETABLE))}.\n"
            f"As demais ou trancam o aparelho se o arquivo faltar (login, "
            f"force_chpass) ou nao tem rota de filesystem (style.css, lang.js)."
        )
    return {n: DIETABLE[n] for n in names}


FS_PAGES = _resolve_diet()

# The layout is part of the build identity, not just the source hash: two envs
# generate different headers from the same WebUI.h, and without this the
# idempotency check below would hand the second env the first one's header.
LAYOUT_TAG = ",".join(sorted(FS_PAGES)) or "all-embedded"

# Languages enabled in firmware.
ENABLED_LANGS = {"en", "pt"}

_LANG_BEGIN = re.compile(
    r'^\s*(?://|<!--)\s*@LANG_BEGIN:(\w+)\s*(?:-->)?\s*$'
)
_LANG_END = re.compile(
    r'^\s*(?://|<!--)\s*@LANG_END:(\w+)\s*(?:-->)?\s*$'
)

# Regex para strings PROGMEM inline no WebUI.h
_PROGMEM_RE = re.compile(
    r'static const char (\w+)\[\] PROGMEM = R"raw\((.*?)\)raw";',
    re.DOTALL,
)


def _strip_disabled_langs(content: str) -> str:
    """Remove @LANG_BEGIN/@LANG_END blocks for disabled languages."""
    out = []
    skip_depth = 0
    for line in content.splitlines(keepends=True):
        mb = _LANG_BEGIN.match(line)
        me = _LANG_END.match(line)
        if mb:
            if mb.group(1) not in ENABLED_LANGS:
                skip_depth += 1
            continue
        if me:
            if skip_depth > 0:
                skip_depth -= 1
            continue
        if skip_depth > 0:
            continue
        out.append(line)
    return "".join(out)


def _hash_file(path: str) -> str:
    """Fast SHA256 hash to detect file changes."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# ── Minificacao com contexto ────────────────────────────────────────────────
#
# O que existia aqui era UM regex que casava toda string do documento, guardava
# os trechos casados, tirava os comentarios do resto e devolvia as strings ao
# lugar. A ideia era proteger o conteudo literal; o defeito e que um regex nao
# tem contexto. Para ele toda aspa abre ou fecha uma string, entao uma aspa que
# nao seja delimitador — dentro de um comentario, ou dentro de um literal de
# regex como /[<>&"]/ — desloca o pareamento em um. Dali em diante o casador
# acredita estar dentro de uma string quando esta no codigo, e vice-versa.
#
# Os dois estragos, ambos vistos neste projeto:
#
#   1. Trecho de codigo tratado como "string" de um comentario e DESCARTADO.
#      A ALARMS_PAGE saiu uma vez com 9% do tamanho, todos os handlers
#      perdidos, e o `node --check` nao viu nada porque o que sobrou continuava
#      sendo JavaScript valido.
#   2. O inverso, silencioso e caro: regiao inteira tratada como literal e
#      portanto NAO minificada. Em 2026-08-18 a HIST_PAGE retinha 93% do
#      tamanho cru (92 KB protegidos, 84% da pagina) e a LANG_JS levava 9.990 B
#      so de comentario para dentro do firmware.
#
# A cura nao e um regex melhor, e escanear com estado: quem le byte a byte sabe
# se esta em HTML, em <script>, em <style>, numa string, num comentario ou num
# literal de regex, porque chegou ali passando por tudo que veio antes.
#
# O portao `_assert_only_whitespace_removed` fecha o circulo: ele reescaneia a
# SAIDA e exige que os literais e o codigo (sem espaco) sejam identicos aos da
# entrada. Se algum dia o escaner errar, o build para em vez de embarcar.

# Depois destes, uma barra abre um literal de regex. Depois de identificador,
# numero, string, `)`, `]` ou `}`, ela e divisao.
_JS_REGEX_OK_AFTER = {
    "return", "typeof", "instanceof", "in", "of", "new", "delete", "void",
    "throw", "case", "do", "else", "yield", "await",
}
_JS_IDENT_END = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*$")
_SEP_KINDS = ("ws", "line_comment", "block_comment")


def _scan_string(s: str, i: int, q: str) -> int:
    """Fim (exclusivo) da string que abre em i. Barra invertida escapa."""
    n = len(s)
    j = i + 1
    while j < n:
        c = s[j]
        if c == "\\":
            j += 2
            continue
        if c == q:
            return j + 1
        if c == "\n":       # string sem fechar na linha: nao come o resto
            return j
        j += 1
    return n


def _scan_template(s: str, i: int) -> int:
    """Fim do template a partir de i (que e ` ou }). Para no ${ (inclusive)."""
    n = len(s)
    j = i + 1
    while j < n:
        c = s[j]
        if c == "\\":
            j += 2
            continue
        if c == "`":
            return j + 1
        if c == "$" and j + 1 < n and s[j + 1] == "{":
            return j + 2
        j += 1
    return n


def _scan_regex(s: str, i: int) -> int:
    """Fim do literal de regex, ou i se nao for um. `[...]` suspende o `/`."""
    n = len(s)
    j = i + 1
    in_class = False
    while j < n:
        c = s[j]
        if c == "\\":
            j += 2
            continue
        if c == "\n":
            return i                     # regex nao atravessa linha: era divisao
        if c == "[":
            in_class = True
        elif c == "]":
            in_class = False
        elif c == "/" and not in_class:
            j += 1
            while j < n and s[j].isalpha():
                j += 1
            return j
        j += 1
    return i


def _js_tokens(s: str):
    """Gera (tipo, texto) cobrindo `s` inteiro, sem perder nem duplicar byte.

    tipo: ws | line_comment | block_comment | string | template | regex | code
    """
    i, n = 0, len(s)
    prev = prev_txt = None
    tpl_stack = []          # profundidade de chaves dentro de cada ${ } aberto
    buf = []

    def flush():
        if buf:
            t = "".join(buf)
            del buf[:]
            return ("code", t)
        return None

    while i < n:
        c = s[i]

        # um `}` que fecha o ${ devolve o texto ao template
        if tpl_stack and c == "}" and tpl_stack[-1] == 0:
            tpl_stack.pop()
            tok = flush()
            if tok:
                yield tok
            j = _scan_template(s, i)
            yield ("template", s[i:j])
            # o pedaco pode parar em OUTRO ${ — `a${x}b${y}c` tem dois. Sem
            # empilhar de novo aqui, a pilha esvazia no primeiro, o } do segundo
            # vira codigo comum, e a crase final e lida como ABERTURA de um
            # template novo — que entao engole o resto do arquivo.
            if s[j - 2:j] == "${":
                tpl_stack.append(0)
                prev, prev_txt = "code", "${"
            else:
                prev, prev_txt = "template", s[i:j]
            i = j
            continue
        if tpl_stack and c == "{":
            tpl_stack[-1] += 1
        elif tpl_stack and c == "}":
            tpl_stack[-1] -= 1

        if c in " \t\r\n\f\v":
            j = i
            while j < n and s[j] in " \t\r\n\f\v":
                j += 1
            tok = flush()
            if tok:
                yield tok
            yield ("ws", s[i:j])
            i = j
            continue

        if c == "/" and i + 1 < n:
            nxt = s[i + 1]
            if nxt == "/":
                j = s.find("\n", i)
                j = n if j < 0 else j
                tok = flush()
                if tok:
                    yield tok
                yield ("line_comment", s[i:j])
                i = j
                continue
            if nxt == "*":
                j = s.find("*/", i + 2)
                j = n if j < 0 else j + 2
                tok = flush()
                if tok:
                    yield tok
                yield ("block_comment", s[i:j])
                i = j
                continue
            is_regex = True
            if prev == "code":
                last = prev_txt[-1:] if prev_txt else ""
                if last in ")]}" or last.isalnum() or last in "_$":
                    m = _JS_IDENT_END.search(prev_txt)
                    is_regex = bool(m) and m.group(0) in _JS_REGEX_OK_AFTER
            elif prev in ("string", "template", "regex"):
                is_regex = False
            if is_regex:
                j = _scan_regex(s, i)
                if j > i:
                    tok = flush()
                    if tok:
                        yield tok
                    yield ("regex", s[i:j])
                    prev, prev_txt = "regex", s[i:j]
                    i = j
                    continue
            buf.append(c)
            prev, prev_txt = "code", "".join(buf)
            i += 1
            continue

        if c in "'\"":
            j = _scan_string(s, i, c)
            tok = flush()
            if tok:
                yield tok
            yield ("string", s[i:j])
            prev, prev_txt = "string", s[i:j]
            i = j
            continue

        if c == "`":
            j = _scan_template(s, i)
            tok = flush()
            if tok:
                yield tok
            yield ("template", s[i:j])
            if s[j - 2:j] == "${":
                tpl_stack.append(0)
                prev, prev_txt = "code", "${"
            else:
                prev, prev_txt = "template", s[i:j]
            i = j
            continue

        buf.append(c)
        prev, prev_txt = "code", "".join(buf)
        i += 1

    tok = flush()
    if tok:
        yield tok


def _css_tokens(s: str):
    """Gera (tipo, texto) para CSS: so strings e comentarios /* */ importam."""
    i, n = 0, len(s)
    buf = []

    def flush():
        if buf:
            t = "".join(buf)
            del buf[:]
            return ("code", t)
        return None

    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] == "*":
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            tok = flush()
            if tok:
                yield tok
            yield ("block_comment", s[i:j])
            i = j
            continue
        if c in "'\"":
            j = _scan_string(s, i, c)
            tok = flush()
            if tok:
                yield tok
            yield ("string", s[i:j])
            i = j
            continue
        if c in " \t\r\n\f":
            j = i
            while j < n and s[j] in " \t\r\n\f":
                j += 1
            tok = flush()
            if tok:
                yield tok
            yield ("ws", s[i:j])
            i = j
            continue
        buf.append(c)
        i += 1
    tok = flush()
    if tok:
        yield tok


def _minify_js(src: str) -> str:
    """Tira comentario e indentacao do JS. NAO junta linhas.

    Juntar linhas mudaria semantica: o JS insere ponto e virgula sozinho no fim
    de linha, entao `return\\nx` e `return x` sao programas diferentes. Um
    comentario que continha quebra vira uma quebra, pelo mesmo motivo.

    A normalizacao acontece TOKEN A TOKEN, nunca com um regex sobre o texto ja
    montado. Um `re.sub(r'[ \\t]{2,}', ' ', ...)` no fim parece inofensivo e nao
    e: ele nao distingue indentacao de conteudo e reescreve o interior das
    strings — `'  <div>'` sai com um espaco so. Foi o portao abaixo que pegou.
    """
    out, pending = [], None
    for kind, txt in _js_tokens(src):
        if kind in _SEP_KINDS:
            if pending != "\n":
                pending = "\n" if "\n" in txt else " "
            continue
        if pending and out:
            out.append(pending)
        pending = None
        out.append(txt)
    return "".join(out)


def _minify_css(src: str) -> str:
    """CSS nao tem ponto e virgula automatico, entao linha pode ser juntada.

    NAO se colapsa espaco em volta de `:` — em seletor ele e significativo:
    `.a :hover` (descendente) e `.a:hover` (mesmo elemento) sao regras
    diferentes, e distinguir uma da outra exigiria saber se o ponto do texto e
    seletor ou declaracao. Medido: colapsar valeria 464 B nos 12 arrays, que o
    gzip ja pega de graca. Pelo mesmo motivo o `;` antes de `}` fica: economiza
    pouco e custaria o invariante que o portao verifica.
    """
    out, pending = [], False
    for kind, txt in _css_tokens(src):
        if kind in ("block_comment", "ws"):
            pending = True
            continue
        if pending and out:
            if txt[:1] not in "{};," and out[-1][-1:] not in "{};,":
                out.append(" ")
        pending = False
        out.append(txt)
    return "".join(out)


_RAW_TEXT = ("script", "style", "pre", "textarea")
_TAG_OPEN = re.compile(r"<(/?)([A-Za-z][A-Za-z0-9-]*)")


def _scan_tag(s: str, i: int) -> int:
    """Fim da tag que abre em i, respeitando aspas de atributo."""
    n = len(s)
    j = i + 1
    while j < n:
        c = s[j]
        if c in "'\"":
            j = _scan_string(s, j, c)
            continue
        if c == ">":
            return j + 1
        j += 1
    return n


def _squeeze_tag(tag: str) -> str:
    """Colapsa espaco ENTRE atributos, nunca dentro de um valor."""
    out, i, n = [], 0, len(tag)
    while i < n:
        c = tag[i]
        if c in "'\"":
            j = _scan_string(tag, i, c)
            out.append(tag[i:j])
            i = j
            continue
        if c in " \t\r\n":
            j = i
            while j < n and tag[j] in " \t\r\n":
                j += 1
            out.append("" if (j < n and tag[j] == ">") else " ")
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _minify_html(src: str) -> str:
    """Colapsa espaco POR REGIAO, nunca com um regex sobre o documento pronto.

    <pre> e <textarea> saem intactos: neles o espaco e o que aparece na tela. O
    minificador anterior nao os conhecia e comia as linhas em branco do texto
    da licenca MIT, que ia para a /license como um paragrafo corrido.
    """
    segs = []                                   # (tipo, texto)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "<":
            if src.startswith("<!--", i):
                j = src.find("-->", i + 4)
                i = n if j < 0 else j + 3
                continue
            m = _TAG_OPEN.match(src, i)
            if m:
                tag = m.group(2).lower()
                j = _scan_tag(src, i)
                raw_tag = src[i:j]
                segs.append(("tag", _squeeze_tag(raw_tag)))
                i = j
                if not m.group(1) and tag in _RAW_TEXT and not raw_tag.endswith("/>"):
                    end = re.compile(r"</%s\b" % tag, re.I).search(src, i)
                    k = end.start() if end else n
                    body = src[i:k]
                    if body.strip():
                        if tag == "script":
                            body = _minify_js(body)
                        elif tag == "style":
                            body = _minify_css(body)
                    segs.append(("raw", body))
                    i = k
                continue
            segs.append(("text", c))
            i += 1
            continue
        j = src.find("<", i)
        j = n if j < 0 else j
        segs.append(("text", src[i:j]))
        i = j

    out = []
    for idx, (kind, txt) in enumerate(segs):
        if kind != "text":
            out.append(txt)
            continue
        if not txt.strip():
            # espaco entre duas tags nao renderiza nada e some
            prev_tag = idx > 0 and segs[idx - 1][0] == "tag"
            next_tag = idx + 1 < len(segs) and segs[idx + 1][0] == "tag"
            out.append("" if (prev_tag and next_tag) else " ")
            continue
        out.append(re.sub(r"\s+", " ", txt))
    return "".join(out).strip()


def _block_kind(name: str) -> str:
    """Sufixo do simbolo decide a linguagem. _CSS -> css, _JS -> js, resto html."""
    if name.endswith("_CSS"):
        return "css"
    if name.endswith("_JS"):
        return "js"
    return "html"


def _minify_web_block(src: str, kind: str = "html") -> str:
    return {"js": _minify_js, "css": _minify_css, "html": _minify_html}[kind](src)


def _essence(src: str, kind: str):
    """A essencia do texto: os literais em ordem, e o codigo sem espaco algum.

    Os trechos de codigo entram concatenados e sem espaco de proposito — onde
    cai a fronteira entre dois deles depende do espaco que foi removido, entao
    comparar a LISTA daria falso positivo. Os literais entram inteiros, byte a
    byte, porque dentro deles o espaco e conteudo.
    """
    toks = _js_tokens(src) if kind == "js" else _css_tokens(src)
    lits, code = [], []
    for k, t in toks:
        if k in _SEP_KINDS:
            continue
        if k in ("string", "template", "regex"):
            lits.append(t)
            code.append("\x00")
        else:
            code.append(re.sub(r"\s+", "", t))
    return lits, "".join(code)


def _inline_regions(html: str, tag: str):
    """Corpos de cada <script>/<style> embutido do documento."""
    out = []
    for m in re.finditer(r"<%s\b([^>]*)>(.*?)</%s>" % (tag, tag), html, re.S | re.I):
        if tag == "script" and "src=" in m.group(1):
            continue
        out.append(m.group(2))
    return out


def _assert_only_whitespace_removed(name: str, raw: str, mini: str, kind: str) -> None:
    """Exige que a minificacao so tenha tirado espaco e comentario.

    Este e o portao que faltava. O modo de falha antigo era silencioso: codigo
    sumia, o que restava continuava sendo JavaScript valido, o `node --check`
    passava e o build dizia SUCCESS. Aqui a saida e reescaneada e comparada com
    a entrada literal por literal; qualquer byte de conteudo que mude para o
    build alto.
    """
    def cmp(a: str, b: str, label: str) -> None:
        la, ca = _essence(a, "js" if label.startswith("script") or kind == "js" else "css")
        lb, cb = _essence(b, "js" if label.startswith("script") or kind == "js" else "css")
        if la == lb and ca == cb:
            return
        if ca != cb:
            i = next((i for i in range(min(len(ca), len(cb))) if ca[i] != cb[i]),
                     min(len(ca), len(cb)))
            det = (f"codigo divergiu em {i}:\n"
                   f"  antes : ...{ca[max(0, i-60):i+60]}...\n"
                   f"  depois: ...{cb[max(0, i-60):i+60]}...")
        else:
            i = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]),
                     min(len(la), len(lb)))
            det = (f"literal #{i} divergiu:\n"
                   f"  antes : {la[i][:120] if i < len(la) else '(nao existe)'!r}\n"
                   f"  depois: {lb[i][:120] if i < len(lb) else '(nao existe)'!r}")
        raise SystemExit(
            f"build_webui_gz: {name} / {label} — a minificacao mudou CONTEUDO, "
            f"nao so espaco e comentario.\n{det}\n"
            f"O escaner de tokens errou o contexto. NAO embarcar."
        )

    if kind in ("js", "css"):
        cmp(raw, mini, kind)
        return
    for tag in ("script", "style"):
        a, b = _inline_regions(raw, tag), _inline_regions(mini, tag)
        if len(a) != len(b):
            raise SystemExit(
                f"build_webui_gz: {name} — {len(a)} blocos <{tag}> na entrada e "
                f"{len(b)} na saida. A minificacao perdeu ou criou um bloco."
            )
        for i, (x, y) in enumerate(zip(a, b)):
            cmp(x, y, f"{tag}[{i}]")


# /style.css e /lang.js sao servidos com Cache-Control: public, max-age=604800
# (WebManager_Util.cpp). Sem versionar a URL, gravar um firmware novo NAO troca
# o que o navegador ja tem: ele nem pergunta durante 7 dias, e a interface
# atualizada so aparecia limpando os dados do site na mao. Carimbar a URL com um
# tag derivado do hash do WebUI.h mantem o cache longo (o Pico e lento e reservir
# esses arquivos a cada navegacao custa) e mesmo assim invalida sozinho quando os
# assets mudam. O servidor casa a rota pelo caminho e ignora a query — verificado
# contra o dispositivo antes de adotar isto.
_ASSET_URLS = ("/style.css", "/lang.js")


def _stamp_assets(src: str, tag: str) -> str:
    """Carimba ?v=<tag> nas URLs dos assets compartilhados."""
    for url in _ASSET_URLS:
        src = src.replace('"%s"' % url, '"%s?v=%s"' % (url, tag))
    return src


# Com o escaner com contexto, as taxas reais dos 12 arrays ficam entre 56% e
# 97% e sao consistentes entre si. Antes iam de 60% a 97% — e essa dispersao ERA
# o sintoma: a HIST_PAGE retinha 93% e a ALARMS 98% nao por serem densas, mas
# porque o casador de strings dessincronizava e a minificacao nunca as
# alcancava. Um numero perto do teto merece a mesma desconfianca que um perto
# do piso.
MIN_RETAINED_FRACTION = 0.40


def _assert_not_gutted(name: str, original: int, minified: int) -> None:
    """Fail the build when minification silently drops most of a page.

    Terceiro portao, e o mais grosseiro dos tres. O
    `_assert_only_whitespace_removed` ja provaria isto de forma exata para
    JS e CSS; esta razao continua aqui porque tambem cobre o HTML, onde nao ha
    invariante de token para comparar, e porque custa nada.

    O que ele pega: a ALARMS_PAGE saiu uma vez com 9% do fonte — todos os
    handlers perdidos, a pagina ainda analisando — porque um comentario JS
    continha uma aspa dupla e um apostrofo.
    """
    if original <= 0:
        return
    retained = minified / original
    if retained >= MIN_RETAINED_FRACTION:
        return
    raise SystemExit(
        f"build_webui_gz: {name} ENCOLHEU DEMAIS na minificacao — "
        f"{original} -> {minified} B ({retained:.0%} do original, "
        f"minimo {MIN_RETAINED_FRACTION:.0%}).\n"
        f"Causa comum: aspas ou apostrofo dentro de comentario JS no WebUI.h. "
        f"O `node --check` nao detecta: o que sobra continua sintaticamente valido."
    )


def _syntax_check_scripts(name: str, text: str, kind: str = "html") -> None:
    """Valida a sintaxe do JS minificado via `node --check`.

    Segunda linha de defesa depois do `_assert_only_whitespace_removed`: aquele
    prova que o conteudo nao mudou, este prova que o resultado ainda compila.
    Sao checagens diferentes — o modo de falha historico passava neste aqui,
    porque o que sobrava depois de o minificador comer meia pagina continuava
    sendo JavaScript sintaticamente valido.

    A LANG_JS ficou anos FORA deste portao: ela nao e HTML, nao tem tag
    <script>, e a busca so olhava para dentro de uma. E o maior bloco de JS do
    projeto. Agora entra pelo parametro `kind`.

    No-op se o node nao estiver no PATH.
    """
    import re as _re
    import shutil
    import subprocess
    import tempfile

    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        return
    if kind == "css":
        return
    if kind == "js":
        blocks = [(name, text)]
    else:
        blocks = [
            (f"{name} <script>[{i}]", s)
            for i, s in enumerate(_re.findall(r"<script>(.*?)</script>", text, _re.DOTALL))
        ]
    for label, script in blocks:
        if not script.strip():
            continue
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as tf:
            tf.write(script)
            path = tf.name
        try:
            r = subprocess.run([node, "--check", path], capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(
                    f"build_webui_gz: SINTAXE JS INVALIDA apos minificacao em "
                    f"{label} — build abortado.\n{r.stderr.strip()[:400]}"
                )
        finally:
            os.unlink(path)


def _prune_stale_fs_pages() -> None:
    """Delete the data/web outputs this layout does not produce.

    A page brought back into the firmware leaves its .gz behind, and a leftover
    is worse than clutter: it still looks like a deploy artifact, so the next
    reader takes it as evidence that the route is served from the filesystem.
    Only names this script itself can emit are ever removed — nothing else in
    data/web is touched.
    """
    if not os.path.isdir(FS_OUT_DIR):
        return
    for fn in DIETABLE.values():
        if fn in FS_PAGES.values():
            continue
        path = os.path.join(FS_OUT_DIR, fn)
        if os.path.isfile(path):
            os.remove(path)
            print(
                f"build_webui_gz: removed stale data/web/{fn} — that page is in "
                f"the firmware image now."
            )


def generate() -> None:
    if not os.path.isfile(INPUT_FILE):
        print("build_webui_gz: WebUI.h not found — skipping generation.")
        return

    # Idempotency: skip if nothing changed.
    #
    # The stamp covers all three things the output depends on: the source, the
    # generator itself, and the layout. The generator half matters because the
    # skip only became real today — the check read line 1 while the hash was
    # written on line 2, so it never once matched and every build regenerated.
    # Turning it on without hashing this file would mean editing the minifier
    # and getting yesterday's pages.
    input_hash = _hash_file(INPUT_FILE)
    gen_hash = _hash_file(os.path.join(PROJECT_DIR, "tools", "build_webui_gz.py"))
    stamp = f"{input_hash} gen={gen_hash[:12]} layout={LAYOUT_TAG}"
    if os.path.isfile(OUTPUT_FILE):
        # The generated .h carries the input hash AND the layout in the first
        # comment line. The layout half is not cosmetic: `pio run -e pico_w_test
        # && pio run -e pico_w_release` compiles the same WebUI.h into two
        # different headers, and matching on the hash alone would hand the
        # second env whatever the first one left behind — a release image with
        # pages missing, or a test image over the ceiling, from a build that
        # printed "up to date".
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            head = f.readline() + f.readline()
        # The FS_PAGES outputs are not covered by the header hash: deleting one
        # would otherwise be invisible here and the build would quietly ship a
        # firmware whose /config has no page to serve.
        fs_ok = all(
            os.path.isfile(os.path.join(FS_OUT_DIR, fn)) for fn in FS_PAGES.values()
        )
        if stamp in head and fs_ok:
            print(
                f"build_webui_gz: WebUI_GZ.h is up to date (layout={LAYOUT_TAG}) "
                f"— skipping."
            )
            return

    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    content = _strip_disabled_langs(content)
    matches = _PROGMEM_RE.findall(content)

    out_lines = [
        "// Auto-generated by tools/build_webui_gz.py — do not edit.",
        f"// Source hash: {stamp}",
        f"// Env: {PIOENV or '(standalone)'}",
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        "",
    ]

    # One SERVE macro per dietable page, emitted next to the asset it serves.
    # The handler in WebManager_Auth.cpp is then one line that reads the same
    # whichever partition the page landed on. Before this, moving a page meant
    # editing the diet here AND the handler there, two files that could
    # disagree in silence: the header would stop emitting the array and the
    # build would die, or worse, the handler kept streaming a filesystem path
    # for a page that was back in flash.
    serve_macros = []

    total_in = 0
    total_min = 0
    total_gz = 0
    asset_tag = input_hash[:8]
    for name, html_content in matches:
        original_len = len(html_content)
        kind = _block_kind(name)
        stamped = _stamp_assets(html_content, asset_tag)
        minified = _minify_web_block(stamped, kind)
        _assert_only_whitespace_removed(name, stamped, minified, kind)
        _syntax_check_scripts(name, minified, kind)
        _assert_not_gutted(name, original_len, len(minified))
        # mtime=0, or the gzip header carries the build clock and firmware.bin
        # differs on every build from identical sources. That makes a released
        # image impossible to reproduce, and makes "is this the binary we
        # tested?" unanswerable by hash — which is exactly the question a
        # firmware release has to answer.
        compressed = gzip.compress(minified.encode("utf-8"), compresslevel=9, mtime=0)
        hex_parts = [f"0x{b:02x}" for b in compressed]

        array_lines = []
        for i in range(0, len(hex_parts), 16):
            array_lines.append("    " + ", ".join(hex_parts[i : i + 16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed)

        total_in  += original_len
        total_min += len(minified)
        total_gz  += length

        ratio = length / max(original_len, 1) * 100

        if name in FS_PAGES:
            os.makedirs(FS_OUT_DIR, exist_ok=True)
            fs_path = os.path.join(FS_OUT_DIR, FS_PAGES[name])
            with open(fs_path, "wb") as fo:
                fo.write(compressed)
            serve_macros.append(
                f"// {name}: {length} bytes gz on LittleFS — NOT in this image.\n"
                f"#define {name}_SERVE(perm) "
                f'serveProtectedFsPage((perm), "/web/{FS_PAGES[name]}")'
            )
            print(
                f"build_webui_gz: {name} -> data/web/{FS_PAGES[name]} "
                f"({length} bytes gz) — served from LittleFS, not linked."
            )
            continue

        gz_name = name + "_GZ"
        out_lines.append(
            f"// {name}: {original_len} -> {len(minified)} (min) -> {length} bytes (gz, {ratio:.1f}%)"
        )
        out_lines.append(
            f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};"
        )
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")
        if name in DIETABLE:
            serve_macros.append(
                f"#define {name}_SERVE(perm) serveProtectedPage((perm), "
                f"WebUI_GZ::{gz_name}, WebUI_GZ::{gz_name}_LEN)"
            )

    out_lines.append("}")
    out_lines.append("")
    out_lines.append("// Route bindings — see the SERVE macro note in build_webui_gz.py.")
    out_lines.extend(serve_macros)

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")

    _prune_stale_fs_pages()

    print(
        f"build_webui_gz: {len(matches)} arrays | layout={LAYOUT_TAG} | "
        f"input {total_in} -> minified {total_min} ({100*total_min/max(total_in,1):.1f}%) "
        f"-> gzipped {total_gz} ({100*total_gz/max(total_in,1):.1f}%)"
    )


generate()
