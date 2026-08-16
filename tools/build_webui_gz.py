"""
PlatformIO pre-build script — regenerates WebUI_GZ.h from WebUI.h.

Compresses PROGMEM blocks with gzip level 9 and writes the header consumed
by WebManager_Core.cpp. Only regenerates if WebUI.h has changed (hash check)
to avoid invalidating the build cache when the asset was not modified.

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

# gzip already compresses whitespace efficiently; minify_html kept OFF.
_HAS_MINIFY_HTML = False

try:
    Import("env")
    PROJECT_DIR = env.subst("$PROJECT_DIR")
except NameError:
    # Running standalone (not inside PlatformIO) — use CWD.
    import sys

    PROJECT_DIR = os.getcwd()
INPUT_FILE  = os.path.join(PROJECT_DIR, "WebUI.h")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "src", "WebUI_GZ.h")

# Pages served from LittleFS instead of being linked into the firmware image.
#
# A page listed here is still gzipped — un-gzipping would make it ~5x BIGGER,
# not smaller — it just lives on the 1 MB filesystem partition instead of the
# app slot, and WebManager::serveProtectedFsPage streams it from there.
#
# The trade is load time (a flash read per request instead of a PROGMEM
# pointer) plus a deploy step, in exchange for app-slot headroom. Only worth
# it for pages that are big AND rarely opened.
#
# Deploying one of these needs the file on the device. Do NOT use
# `pio run -t uploadfs` on a device with data on it: that reformats the
# partition and takes the history, logs and calib.csv with it. Upload the
# single file through the /files page or POST it to /api/upload.
#
# --- Currently empty, and that is deliberate. ---
# CFG_PAGE lived here from when the image had 660 bytes of headroom. Dropping
# the never-used Bluetooth stack from platformio.ini freed 64732 B, so the page
# went back into the firmware on 2026-07-26. Measured cost of bringing it in:
# 11544 B (not the 12152 B of the array — serveProtectedFsPage had /config as
# its only caller, so the helper and its error page get gc-sectioned out too).
# Headroom after: 57928 B.
#
# What that buys is a failure mode removed. On a freshly formatted or
# freshly built device the file is not there yet, and /config — the page you
# need to configure the device — answered with "Page asset missing". Bootstrap
# no longer depends on a manual upload.
#
# HIST_PAGE (32216 B today) is the biggest candidate, but /history is opened
# constantly, so it fails the "rarely opened" half of the criterion above —
# measure the extra latency before ever committing to it.
#
# --- LICENSE_PAGE moved out on 2026-08-16. ---
# The edge-triggered log filter needed 536 B and pico_w_test had 376 B of real
# headroom left (the reported 12 KB omits .ota and the .rodata alignment
# padding — see scratchpad/flashfree.sh and docs/ANALISE_FLASH_RAM.md).
#
# LICENSE_PAGE is the right page to move, for the reasons the criterion above
# names and CFG_PAGE failed: it is static legal text on a route nobody visits
# twice, and a device that never received the file is still fully usable — it
# gets the "Page asset missing" notice on /license and nothing else changes.
# CFG_PAGE broke bootstrap; HIST_PAGE would cost a flash read on the hottest
# page there is. This one costs neither.
#
# Deploy: these files must reach /web/ on the device, or the routes that were
# moved out answer "Page asset missing". Upload through the Files page or POST
# to /api/upload. NOT `uploadfs` — that reformats the partition and takes
# history, logs and calib.csv with it.
#
# The pio zip carries them under data/web/ (build_release_pio.sh). The Arduino
# zips carry no data/ at all, by design — that variant uploads the filesystem
# separately. This comment used to claim "the release zips carry" them while
# data/web/ was gitignored and no script copied it, so no zip had ever carried
# one; a unit built from a zip had a broken /license from the day the page was
# moved out.
#
# Picking what goes here: big AND rarely opened AND not needed to bring the
# device up. All three, or the cure is worse. HIST_PAGE is the biggest at 32 KB
# but is the hottest page there is; CFG_PAGE was tried in July 2026 and broke
# the bootstrap; FILE_PAGE is circular, since it is how you upload these files
# in the first place.
FS_PAGES = {"LICENSE_PAGE": "license.html.gz", "ALARMS_PAGE": "alarms.html.gz"}
FS_OUT_DIR = os.path.join(PROJECT_DIR, "data", "web")

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


# Conservative minification applied BEFORE gzip. Reduces ~10-20% before
# compression, saving a few KB per page post-gzip (gzip already handles
# whitespace but comments and structure still inflate).
# Strategy: tokenize string literals first to avoid touching them, then
# strip comments and collapse whitespace outside strings.
_STR_PLACEHOLDER = "\x00STR{}\x00"
_STRING_RE = re.compile(
    r"""(`(?:\\.|[^`\\])*`        # template literal
        |"(?:\\.|[^"\\])*"        # double-quoted
        |'(?:\\.|[^'\\])*')       # single-quoted
    """,
    re.VERBOSE,
)


def _minify_web_block(src: str) -> str:
    """Minify mixed HTML+CSS+JS preserving string literals.

    If the `minify_html` library (Rust-based, aggressive but correct) is
    available, use it. Otherwise fall back to a conservative regex minifier.

    Conservative: doesn't touch pre/textarea/code blocks and preserves
    anything between quotes/backticks.
    """
    if _HAS_MINIFY_HTML:
        # minify_html applies HTML + JS + CSS minification with syntax
        # awareness. Conservative flags:
        #   keep_closing_tags: True — keeps </p> </li> etc (compat)
        #   keep_html_and_head_opening_tags: True — keeps <html><head>
        #   allow_removing_spaces_between_attributes: True — aggressive OK
        #   minify_css/js: True — minify embedded blocks
        try:
            return minify_html.minify(
                src,
                minify_css=True,   # whitespace + shorthand only, preserves hex/numeric values
                minify_js=False,   # kept OFF: template literals may break with mangling
                keep_closing_tags=True,
                keep_html_and_head_opening_tags=True,
                allow_removing_spaces_between_attributes=True,
                remove_bangs=True,
                remove_processing_instructions=True,
            )
        except Exception as e:
            print(f"build_webui_gz: minify_html failed ({e}), using regex fallback.")

    # Fallback regex (without minify_html installed)
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
    # 4. JS line comments — only inside <script>...</script> to avoid
    #    confusing URLs (`http://`) in HTML href/src attributes.
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

    # 6. Tag adjacency: `> <` -> `><` (only when both are HTML tags;
    #    conservative — only between literal `>` and `<` without other
    #    chars between).
    src = re.sub(r">[ \t]+<", "><", src)
    src = re.sub(r">\n+<", "><", src)

    # 7. Restore strings
    def _restore(m):
        idx = int(m.group(1))
        return saved[idx]

    src = re.sub(r"\x00STR(\d+)\x00", _restore, src)

    return src


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


# Real ratios across the ten pages sit between 61% and 97%. A page that comes
# out far below that has not been minified, it has been eaten.
MIN_RETAINED_FRACTION = 0.40


def _assert_not_gutted(name: str, original: int, minified: int) -> None:
    """Fail the build when minification silently drops most of a page.

    The syntax check above cannot see this: what survives is valid JavaScript,
    just missing the half of the page that mattered. ALARMS_PAGE shipped once
    at 9% of its source — every handler gone, the page still parsing — because
    a JS comment contained a double quote and an apostrophe, which is the same
    trigger that cost a release before.

    A ratio is a blunt instrument, but the failure it catches is not subtle.
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


def _syntax_check_scripts(name: str, html: str) -> None:
    """Valida a sintaxe dos <script> inline do HTML minificado via `node --check`.

    O minificador preserva strings ANTES de remover comentarios: aspas dentro
    de um comentario JS confundem essa ordem e ja produziram paginas com 13KB
    de JS devorado (pagina /history quebrada em producao). Falha o BUILD alto
    e claro em vez de embarcar JS invalido. No-op se node nao estiver no PATH.
    """
    import re as _re
    import shutil
    import subprocess
    import tempfile

    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        return
    for i, script in enumerate(_re.findall(r"<script>(.*?)</script>", html, _re.DOTALL)):
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
                    f"{name} <script>[{i}] — build abortado.\n"
                    f"Causa comum: aspas dentro de comentarios JS no WebUI.h.\n"
                    f"{r.stderr.strip()[:400]}"
                )
        finally:
            os.unlink(path)


def generate() -> None:
    if not os.path.isfile(INPUT_FILE):
        print("build_webui_gz: WebUI.h not found — skipping generation.")
        return

    # Idempotency: skip if nothing changed.
    input_hash = _hash_file(INPUT_FILE)
    if os.path.isfile(OUTPUT_FILE):
        output_hash = _hash_file(OUTPUT_FILE)
        # The generated .h contains the input hash in the first comment line.
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            first_line = f.readline()
        # The FS_PAGES outputs are not covered by the header hash: deleting one
        # would otherwise be invisible here and the build would quietly ship a
        # firmware whose /config has no page to serve.
        fs_ok = all(
            os.path.isfile(os.path.join(FS_OUT_DIR, fn)) for fn in FS_PAGES.values()
        )
        if input_hash in first_line and fs_ok:
            print("build_webui_gz: WebUI_GZ.h is up to date — skipping.")
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
    asset_tag = input_hash[:8]
    for name, html_content in matches:
        original_len = len(html_content)
        minified = _minify_web_block(_stamp_assets(html_content, asset_tag))
        _syntax_check_scripts(name, minified)
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

    out_lines.append("}")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")

    print(
        f"build_webui_gz: {len(matches)} arrays | "
        f"input {total_in} -> minified {total_min} ({100*total_min/max(total_in,1):.1f}%) "
        f"-> gzipped {total_gz} ({100*total_gz/max(total_in,1):.1f}%)"
    )


generate()
