"""
PlatformIO pre:script — regenera WebUI_GZ.h a partir de WebUI.h.

Executado automaticamente antes de cada build. Comprime blocos PROGMEM
com gzip nivel 9 e escreve o header consumido por WebManager_Core.cpp.

Apenas regera se WebUI.h mudou (timestamps) para nao invalidar cache de
build quando o asset nao foi alterado.

Project: SIMUT
License: MIT
"""

import os
import re
import gzip
import hashlib

try:
    import minify_html
    _HAS_MINIFY_HTML = True
except ImportError:
    _HAS_MINIFY_HTML = False

# F-FLASH-DIET 2026-05-10: testado com e sem minify_html — gzip já comprime
# whitespace eficientemente e o ganho líquido é zero. Mantido OFF.
_HAS_MINIFY_HTML = False

try:
    Import("env")
    PROJECT_DIR = env.subst("$PROJECT_DIR")
except NameError:
    # Running standalone (not inside PlatformIO) — use CWD.
    import sys

    PROJECT_DIR = os.getcwd()
INPUT_FILE  = os.path.join(PROJECT_DIR, "WebUI.h")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "WebUI_GZ.h")

# Idiomas habilitados no firmware (consistente com F-I18N-TRIM.1).
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
    """Remove blocos @LANG_BEGIN/@LANG_END de idiomas nao habilitados."""
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
    """SHA256 rapido do arquivo para detectar mudancas."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# Minificacao conservadora aplicada ANTES do gzip. Reduz ~10-20% antes da
# compressao, o que economiza alguns KB pos-gzip por pagina (gzip ja
# desconta whitespace mas comentarios e estrutura ainda inflam).
# Estrategia: tokenizar string literals primeiro para nao tocar neles, depois
# strip comentarios e colapsar whitespace fora de strings.
_STR_PLACEHOLDER = "\x00STR{}\x00"
_STRING_RE = re.compile(
    r"""(`(?:\\.|[^`\\])*`        # template literal
        |"(?:\\.|[^"\\])*"        # double-quoted
        |'(?:\\.|[^'\\])*')       # single-quoted
    """,
    re.VERBOSE,
)


def _minify_web_block(src: str) -> str:
    """Minifica HTML+CSS+JS misto preservando string literals.

    Se a lib `minify_html` (Rust-based, agressiva mas correta) estiver
    disponivel, usa ela. Senao cai num minify regex conservador.

    Conservador: nao mexe em pre/textarea/code (raros nesses assets) e
    preserva qualquer coisa entre aspas/backticks.
    """
    if _HAS_MINIFY_HTML:
        # minify_html aplica minify de HTML + JS + CSS embutido com
        # awareness de sintaxe. Flags conservadoras:
        #   keep_closing_tags: True — mantem </p> </li> etc (compat)
        #   keep_html_and_head_opening_tags: True — mantem <html><head>
        #   allow_removing_spaces_between_attributes: True — agressivo OK
        #   minify_css/js: True — minifica embutido
        try:
            return minify_html.minify(
                src,
                minify_css=True,   # F-FLASH-DIET: preserva valores hex/numéricos, só whitespace+shorthand
                minify_js=False,   # mantido OFF: template literals podem quebrar com mangling
                keep_closing_tags=True,
                keep_html_and_head_opening_tags=True,
                allow_removing_spaces_between_attributes=True,
                remove_bangs=True,
                remove_processing_instructions=True,
            )
        except Exception as e:
            print(f"build_webui_gz: minify_html falhou ({e}), usando fallback regex.")

    # Fallback regex (sem minify_html instalado)
    # 1. Save strings
    saved = []

    def _save(m):
        saved.append(m.group(0))
        return _STR_PLACEHOLDER.format(len(saved) - 1)

    src = _STRING_RE.sub(_save, src)

    # 2. HTML comments
    src = re.sub(r"<!--.*?-->", "", src, flags=re.DOTALL)
    # 3. CSS/JS block comments
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.DOTALL)
    # 4. JS line comments — apenas dentro de <script>...</script> para nao
    #    confundir com URLs (`http://`) em href/src de HTML.
    def _strip_js_lines(m):
        body = m.group(2)
        body = re.sub(r"^[ \t]*//[^\n]*$", "", body, flags=re.MULTILINE)
        return m.group(1) + body + m.group(3)

    src = re.sub(
        r"(<script\b[^>]*>)(.*?)(</script>)",
        _strip_js_lines,
        src,
        flags=re.DOTALL,
    )

    # 5. Collapse whitespace
    src = re.sub(r"[ \t]+", " ", src)
    src = re.sub(r" *\n+ *", "\n", src)
    src = re.sub(r"\n{2,}", "\n", src)

    # 6. Tag adjacency: `> <` -> `><` (so quando ambos sao tags HTML; na
    #    duvida, conservador — somente entre `>` e `<` literal sem outros
    #    chars).
    src = re.sub(r">[ \t]+<", "><", src)
    src = re.sub(r">\n+<", "><", src)

    # 7. Restore strings
    def _restore(m):
        idx = int(m.group(1))
        return saved[idx]

    src = re.sub(r"\x00STR(\d+)\x00", _restore, src)

    return src


def generate() -> None:
    if not os.path.isfile(INPUT_FILE):
        print("build_webui_gz: WebUI.h ausente — pulando geracao.")
        return

    # Idempotencia: pula se nada mudou.
    input_hash = _hash_file(INPUT_FILE)
    if os.path.isfile(OUTPUT_FILE):
        output_hash = _hash_file(OUTPUT_FILE)
        # O .h gerado contem um comentario com o hash do input na primeira linha.
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            first_line = f.readline()
        if input_hash in first_line:
            print("build_webui_gz: WebUI_GZ.h esta atualizado — pulando.")
            return

    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    content = _strip_disabled_langs(content)
    matches = _PROGMEM_RE.findall(content)

    out_lines = [
        "// Auto-generated by tools/build_webui_gz.py — do not edit.",
        f"// Source hash: {input_hash}",
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        "",
    ]

    total_in = 0
    total_min = 0
    total_gz = 0
    for name, html_content in matches:
        original_len = len(html_content)
        minified = _minify_web_block(html_content)
        compressed = gzip.compress(minified.encode("utf-8"), compresslevel=9)
        hex_parts = [f"0x{b:02x}" for b in compressed]

        array_lines = []
        for i in range(0, len(hex_parts), 16):
            array_lines.append("    " + ", ".join(hex_parts[i : i + 16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed)

        total_in  += original_len
        total_min += len(minified)
        total_gz  += length

        gz_name = name + "_GZ"
        ratio = length / max(original_len, 1) * 100
        out_lines.append(
            f"// {name}: {original_len} -> {len(minified)} (min) -> {length} bytes (gz, {ratio:.1f}%)"
        )
        out_lines.append(
            f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};"
        )
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")

    out_lines.append("}")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")

    print(
        f"build_webui_gz: {len(matches)} arrays | "
        f"input {total_in} -> minified {total_min} ({100*total_min/max(total_in,1):.1f}%) "
        f"-> gzipped {total_gz} ({100*total_gz/max(total_in,1):.1f}%)"
    )


generate()
